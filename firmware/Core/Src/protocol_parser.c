/**
  ******************************************************************************
  * @file    protocol_parser.c
  * @brief   协议解析、命令分发与响应帧构造
  *
  * 所有命令处理都在主循环上下文执行(由 uart_poll() 回调进入), 因此可以直接
  * 操作外设寄存器, 无需额外加锁。
  ******************************************************************************
  */

#include "protocol_parser.h"

#include "adc_sampler.h"
#include "dac_output.h"
#include "gpio_driver.h"
#include "main.h"
#include "pwm_output.h"
#include "uart_handler.h"

static proto_stats_t g_stats;
static uint32_t      g_last_rx_tick;

/* ========================================================================== */
/* 小端序读写辅助                                                             */
/* ========================================================================== */

/** @brief 从缓冲读取小端 16 位 */
static uint16_t proto_read_u16(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8U));
}

/** @brief 从缓冲读取小端 32 位 */
static uint32_t proto_read_u32(const uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8U) |
           ((uint32_t)buf[2] << 16U) |
           ((uint32_t)buf[3] << 24U);
}

/** @brief 写入小端 16 位 */
static void proto_write_u16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

/* ========================================================================== */
/* 命令处理                                                                   */
/* ========================================================================== */

/**
 * @brief  SET_ANALOG: 设置 DAC 输出
 * @param  payload [通道1B][值2B]
 * @return ACK 状态码
 */
static uint8_t handle_set_analog(const uint8_t *payload, uint8_t len)
{
    if (len != PAYLOAD_LEN_SET_ANALOG) {
        return ACK_ERR_LENGTH;
    }

    uint8_t  channel = payload[0];
    uint16_t value   = proto_read_u16(&payload[1]);

    if (channel >= (uint8_t)DAC_CH_COUNT) {
        return ACK_ERR_PARAM;
    }
    if (value > CFG_DAC_MAX_VALUE) {
        return ACK_ERR_PARAM;
    }

    return dac_set_value((dac_channel_t)channel, value) ? ACK_OK : ACK_ERR_PARAM;
}

/**
 * @brief  SET_DIGITAL: 设置数字输出
 * @param  payload [端口1B][状态1B], 端口 0xFF 表示按位图整体写入
 */
static uint8_t handle_set_digital(const uint8_t *payload, uint8_t len)
{
    if (len != PAYLOAD_LEN_SET_DIGITAL) {
        return ACK_ERR_LENGTH;
    }

    uint8_t port  = payload[0];
    uint8_t state = payload[1];

    if (port == DIGITAL_PORT_ALL) {
        gpio_write_all_outputs((uint8_t)(state & 0x0FU));
        return ACK_OK;
    }

    if (port >= (uint8_t)GPIO_OUT_COUNT) {
        return ACK_ERR_PARAM;
    }

    return gpio_write((gpio_out_id_t)port, (state != 0U)) ? ACK_OK : ACK_ERR_PARAM;
}

/**
 * @brief  SET_FREQUENCY: 设置 PWM 频率
 * @param  payload [通道1B][频率4B]
 */
static uint8_t handle_set_frequency(const uint8_t *payload, uint8_t len)
{
    if (len != PAYLOAD_LEN_SET_FREQUENCY) {
        return ACK_ERR_LENGTH;
    }

    uint8_t  channel = payload[0];
    uint32_t freq_hz = proto_read_u32(&payload[1]);

    if (channel >= (uint8_t)PWM_CH_COUNT) {
        return ACK_ERR_PARAM;
    }

    return pwm_set_frequency((pwm_channel_t)channel, freq_hz) ? ACK_OK : ACK_ERR_PARAM;
}

/**
 * @brief  BATCH_SET: 一次设置所有输出量
 * @param  payload 16B:
 *         [0-3]  水位传感器频率(Hz, 小端32)
 *         [4-5]  NTC DAC 原始值(12 位)
 *         [6-9]  Hall 传感器频率(Hz, 小端32)
 *         [10]   门锁
 *         [11]   进水阀
 *         [12]   排水泵
 *         [13]   加热器
 *         [14-15]保留
 * @note   数字量 4 路先合成位图再一次写入, 保证同步切换
 */
static uint8_t handle_batch_set(const uint8_t *payload, uint8_t len)
{
    if (len != PAYLOAD_LEN_BATCH_SET) {
        return ACK_ERR_LENGTH;
    }

    uint32_t water_freq = proto_read_u32(&payload[0]);
    uint16_t ntc_value  = proto_read_u16(&payload[4]);
    uint32_t hall_freq  = proto_read_u32(&payload[6]);

    uint8_t status = ACK_OK;

    if (!pwm_set_frequency(PWM_CH_WATER_LEVEL, water_freq)) {
        status = ACK_ERR_PARAM;
    }
    if (!pwm_set_frequency(PWM_CH_HALL, hall_freq)) {
        status = ACK_ERR_PARAM;
    }
    if (ntc_value > CFG_DAC_MAX_VALUE) {
        status = ACK_ERR_PARAM;
    } else {
        (void)dac_set_value(DAC_CH_NTC, ntc_value);
    }

    uint8_t bitmap = 0U;
    if (payload[10] != 0U) { bitmap |= (uint8_t)(1U << GPIO_OUT_DOOR_LOCK); }
    if (payload[11] != 0U) { bitmap |= (uint8_t)(1U << GPIO_OUT_INLET_VALVE); }
    if (payload[12] != 0U) { bitmap |= (uint8_t)(1U << GPIO_OUT_DRAIN_PUMP); }
    if (payload[13] != 0U) { bitmap |= (uint8_t)(1U << GPIO_OUT_HEATER); }
    gpio_write_all_outputs(bitmap);

    return status;
}

/**
 * @brief  组装 REPORT_INPUT 载荷(16B)
 *         [0-1]  电机 PWM 占空比(0.1% 单位, 0-1000)
 *         [2]    继电器(数字输出)状态位图, 供上位机回读确认
 *         [3-4]  ADC1 = PA0 原始值
 *         [5-6]  ADC2 = PA2 原始值
 *         [7-8]  ADC3 = PA3 原始值
 *         [9-10] ADC4 = PA6 原始值
 *         [11-12]GPIO 输入位图(bit0-3: 电机使能/加热使能/进水阀/排水泵)
 *         [13-15]保留
 */
static void protocol_build_report(uint8_t *payload)
{
    uint16_t adc_values[ADC_IDX_COUNT];

    adc_get_all_values(adc_values);

    proto_write_u16(&payload[0], adc_get_motor_duty_permille());
    payload[2] = gpio_get_output_bitmap();

    for (uint32_t i = 0U; i < ADC_IDX_COUNT; i++) {
        proto_write_u16(&payload[3U + (i * 2U)], adc_values[i]);
    }

    proto_write_u16(&payload[11], gpio_read_all_inputs());

    payload[13] = 0U;
    payload[14] = 0U;
    payload[15] = 0U;
}

/**
 * @brief  READ_ALL: 上报当前所有输入量
 * @note   使用 100Hz 采集任务缓存的最新值, 不阻塞等待 ADC
 */
static uint8_t handle_read_all(uint8_t len)
{
    if (len != 0U) {
        return ACK_ERR_LENGTH;
    }

    return protocol_send_report() ? ACK_OK : ACK_ERR_BUSY;
}

/**
 * @brief  HEARTBEAT: 原样回复心跳, 供上位机判定链路存活
 */
static uint8_t handle_heartbeat(uint8_t len)
{
    if (len != 0U) {
        return ACK_ERR_LENGTH;
    }

    return protocol_send_heartbeat() ? ACK_OK : ACK_ERR_BUSY;
}

/* ========================================================================== */
/* 对外接口                                                                   */
/* ========================================================================== */

/** @brief 初始化协议层, 注册 UART 帧回调 */
void protocol_init(void)
{
    for (uint32_t i = 0U; i < sizeof(g_stats) / sizeof(uint32_t); i++) {
        ((uint32_t *)&g_stats)[i] = 0U;
    }

    /* 初始视为无连接: 回溯一个超时窗口, 避免上电后尚未连接就开始上报 */
    g_last_rx_tick = sys_get_tick() - CFG_LINK_TIMEOUT_MS;
    uart_set_frame_callback(protocol_handle_frame);
}

/**
 * @brief  分发一个完整帧
 * @note   除 READ_ALL(直接回 REPORT_INPUT) 与 HEARTBEAT(回心跳) 外,
 *         其余命令统一回 ACK
 */
void protocol_handle_frame(const proto_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    g_last_rx_tick = sys_get_tick();
    g_stats.rx_frames++;

    uint8_t status = ACK_OK;

    switch (frame->cmd) {
    case CMD_SET_ANALOG:
        status = handle_set_analog(frame->payload, frame->len);
        (void)protocol_send_ack(frame->cmd, status);
        break;

    case CMD_SET_DIGITAL:
        status = handle_set_digital(frame->payload, frame->len);
        (void)protocol_send_ack(frame->cmd, status);
        break;

    case CMD_SET_FREQUENCY:
        status = handle_set_frequency(frame->payload, frame->len);
        (void)protocol_send_ack(frame->cmd, status);
        break;

    case CMD_READ_ALL:
        status = handle_read_all(frame->len);
        if (status != ACK_OK) {
            (void)protocol_send_ack(frame->cmd, status);
        }
        break;

    case CMD_BATCH_SET:
        status = handle_batch_set(frame->payload, frame->len);
        (void)protocol_send_ack(frame->cmd, status);
        break;

    case CMD_HEARTBEAT:
        status = handle_heartbeat(frame->len);
        break;

    default:
        status = ACK_ERR_UNKNOWN_CMD;
        (void)protocol_send_ack(frame->cmd, status);
        break;
    }

    /* 统计错误类型 */
    switch (status) {
    case ACK_ERR_LENGTH:
        g_stats.err_length++;
        break;
    case ACK_ERR_PARAM:
        g_stats.err_param++;
        break;
    case ACK_ERR_UNKNOWN_CMD:
        g_stats.err_unknown++;
        break;
    default:
        break;
    }
}

/** @brief 采集当前输入量并上报 */
bool protocol_send_report(void)
{
    uint8_t payload[PAYLOAD_LEN_REPORT_INPUT];

    protocol_build_report(payload);

    if (!uart_send_frame(CMD_REPORT_INPUT, payload, PAYLOAD_LEN_REPORT_INPUT)) {
        return false;
    }

    g_stats.tx_frames++;
    return true;
}

/** @brief 发送 ACK */
bool protocol_send_ack(uint8_t cmd, uint8_t status)
{
    uint8_t payload[PAYLOAD_LEN_ACK];

    payload[0] = cmd;
    payload[1] = status;

    if (!uart_send_frame(CMD_ACK, payload, PAYLOAD_LEN_ACK)) {
        return false;
    }

    g_stats.tx_frames++;
    return true;
}

/** @brief 发送心跳(无载荷) */
bool protocol_send_heartbeat(void)
{
    if (!uart_send_frame(CMD_HEARTBEAT, NULL, 0U)) {
        return false;
    }

    g_stats.tx_frames++;
    return true;
}

/** @brief 最近一次收到有效帧的时间戳 */
uint32_t protocol_get_last_rx_tick(void)
{
    return g_last_rx_tick;
}

/** @brief 获取协议层统计 */
const proto_stats_t *protocol_get_stats(void)
{
    return &g_stats;
}

/**
 * @brief  进入安全状态
 * @note   上位机断连后调用: 数字输出全部关断, 频率输出停止, DAC 归零,
 *         避免被测主控收到"卡死"的传感器信号
 */
void protocol_enter_safe_state(void)
{
    gpio_reset_all_outputs();
    pwm_stop_all();
    dac_reset_all();
}
