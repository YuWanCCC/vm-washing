/**
  ******************************************************************************
  * @file    gpio_driver.c
  * @brief   数字量输出/输入驱动实现
  *
  * 输出: PB0-PB3 推挽输出, 复位后默认低电平(安全状态)
  * 输入: PB4-PB7 带下拉输入, 采集主控驱动信号, 软件消抖
  ******************************************************************************
  */

#include "gpio_driver.h"
#include "main.h"

/** @brief 消抖次数, 100Hz tick 下约 30ms */
#define GPIO_DEBOUNCE_COUNT     3U

/** @brief 4 路输出对应的物理引脚号(PB) */
static const uint8_t g_out_pins[GPIO_OUT_COUNT] = {
    PIN_OUT_DOOR_LOCK,
    PIN_OUT_INLET_VALVE,
    PIN_OUT_DRAIN_PUMP,
    PIN_OUT_HEATER
};

/** @brief 4 路输入对应的物理引脚号(PB) */
static const uint8_t g_in_pins[GPIO_IN_COUNT] = {
    PIN_IN_MOTOR_EN,
    PIN_IN_HEAT_EN,
    PIN_IN_INLET_VALVE,
    PIN_IN_DRAIN_PUMP
};

static uint8_t g_out_bitmap;      /*!< 输出状态影子寄存器                   */
static uint8_t g_in_stable;       /*!< 消抖后的稳定输入位图                 */
static uint8_t g_in_last;         /*!< 上一次原始采样位图                   */
static uint8_t g_in_counter;      /*!< 连续一致计数                         */

/**
 * @brief  读取 PB4-PB7 原始电平并压缩为 bit0-bit3
 */
static uint8_t gpio_read_raw_inputs(void)
{
    uint32_t idr = PIN_IN_PORT->IDR;
    uint8_t  bitmap = 0U;

    for (uint8_t i = 0U; i < GPIO_IN_COUNT; i++) {
        if ((idr & (1UL << g_in_pins[i])) != 0U) {
            bitmap |= (uint8_t)(1U << i);
        }
    }

    return bitmap;
}

/**
 * @brief  初始化数字 IO
 * @note   PB3(JTDO)/PB4(NJTRST) 默认被 JTAG 占用, 必须关闭 JTAG 只保留 SWD,
 *         否则加热器输出与电机使能输入无法工作
 */
void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;

    /* 关闭 JTAG 保留 SWD, 释放 PB3/PB4 */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG_MASK) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    /* 输出引脚: 先拉低再切换为推挽输出, 避免上电瞬间误动作 */
    PIN_OUT_PORT->BRR = PIN_OUT_MASK;
    for (uint8_t i = 0U; i < GPIO_OUT_COUNT; i++) {
        gpio_pin_mode(PIN_OUT_PORT, g_out_pins[i], GPIO_MODE_OUT_PP_50M);
    }
    g_out_bitmap = 0U;

    /* 输入引脚: 下拉输入, 无外部驱动时读到 0 */
    for (uint8_t i = 0U; i < GPIO_IN_COUNT; i++) {
        gpio_pin_mode(PIN_IN_PORT, g_in_pins[i], GPIO_MODE_IN_PULL);
    }
    PIN_IN_PORT->BRR = PIN_IN_MASK;   /* ODR = 0 -> 下拉 */

    /* 状态指示灯 PC13, 低电平点亮, 初始熄灭 */
    gpio_pin_mode(PIN_LED_PORT, PIN_LED, GPIO_MODE_OUT_PP_50M);
    gpio_pin_set(PIN_LED_PORT, PIN_LED);

    g_in_stable  = gpio_read_raw_inputs();
    g_in_last    = g_in_stable;
    g_in_counter = 0U;
}

/** @brief 设置单路数字输出 */
bool gpio_write(gpio_out_id_t pin_id, bool state)
{
    if ((uint32_t)pin_id >= (uint32_t)GPIO_OUT_COUNT) {
        return false;
    }

    if (state) {
        gpio_pin_set(PIN_OUT_PORT, g_out_pins[pin_id]);
        g_out_bitmap |= (uint8_t)(1U << (uint8_t)pin_id);
    } else {
        gpio_pin_reset(PIN_OUT_PORT, g_out_pins[pin_id]);
        g_out_bitmap &= (uint8_t)~(1U << (uint8_t)pin_id);
    }

    return true;
}

/**
 * @brief  按位图整体写入 4 路输出
 * @note   使用 BSRR 一次完成置位与复位, 保证 4 路同步切换
 */
void gpio_write_all_outputs(uint8_t bitmap)
{
    uint32_t set_mask   = 0U;
    uint32_t reset_mask = 0U;

    for (uint8_t i = 0U; i < GPIO_OUT_COUNT; i++) {
        if ((bitmap & (1U << i)) != 0U) {
            set_mask |= 1UL << g_out_pins[i];
        } else {
            reset_mask |= 1UL << g_out_pins[i];
        }
    }

    /* BSRR 高 16 位为复位域, 低 16 位为置位域 */
    PIN_OUT_PORT->BSRR = set_mask | (reset_mask << 16U);
    g_out_bitmap = (uint8_t)(bitmap & 0x0FU);
}

/** @brief 读取单路数字输入(消抖后的稳定值) */
bool gpio_read(gpio_in_id_t pin_id)
{
    if ((uint32_t)pin_id >= (uint32_t)GPIO_IN_COUNT) {
        return false;
    }

    return ((g_in_stable & (1U << (uint8_t)pin_id)) != 0U);
}

/** @brief 读取全部数字输入位图 */
uint16_t gpio_read_all_inputs(void)
{
    return (uint16_t)g_in_stable;
}

/** @brief 读取当前输出状态位图 */
uint8_t gpio_get_output_bitmap(void)
{
    return g_out_bitmap;
}

/**
 * @brief  输入消抖处理, 在 100Hz tick 中调用
 * @note   采样值连续 GPIO_DEBOUNCE_COUNT 次不变才认为稳定
 */
void gpio_poll_inputs(void)
{
    uint8_t raw = gpio_read_raw_inputs();

    if (raw == g_in_last) {
        if (g_in_counter < GPIO_DEBOUNCE_COUNT) {
            g_in_counter++;
            if (g_in_counter >= GPIO_DEBOUNCE_COUNT) {
                g_in_stable = raw;
            }
        }
    } else {
        g_in_last    = raw;
        g_in_counter = 0U;
    }
}

/** @brief 所有输出复位为低电平 */
void gpio_reset_all_outputs(void)
{
    PIN_OUT_PORT->BRR = PIN_OUT_MASK;
    g_out_bitmap = 0U;
}

/** @brief 状态灯控制(PC13 低电平点亮) */
void gpio_set_led(bool on)
{
    if (on) {
        gpio_pin_reset(PIN_LED_PORT, PIN_LED);
    } else {
        gpio_pin_set(PIN_LED_PORT, PIN_LED);
    }
}

/** @brief 状态灯翻转 */
void gpio_toggle_led(void)
{
    gpio_pin_toggle(PIN_LED_PORT, PIN_LED);
}
