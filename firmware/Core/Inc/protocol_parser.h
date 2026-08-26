/**
  ******************************************************************************
  * @file    protocol_parser.h
  * @brief   HIL 上位机通信协议定义与解析分发
  *
  * 帧格式:
  *   +------+-----+-----+-----------+---------+------+
  *   | 0xAA | CMD | LEN | PAYLOAD   | CRC16   | 0x55 |
  *   |  1B  | 1B  | 1B  | 0-250B    |  2B     |  1B  |
  *   +------+-----+-----+-----------+---------+------+
  *
  * - CRC16 计算范围: CMD + LEN + PAYLOAD (不含帧头/帧尾), CRC16-CCITT
  * - CRC16 传输顺序: 低字节先(小端)
  * - 载荷内多字节数值统一使用小端序
  ******************************************************************************
  */

#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* 帧结构常量                                                                 */
/* ========================================================================== */

#define PROTO_FRAME_HEADER      0xAAU
#define PROTO_FRAME_TAIL        0x55U
#define PROTO_MAX_PAYLOAD       250U
#define PROTO_OVERHEAD          6U      /*!< 头+CMD+LEN+CRC(2)+尾            */
#define PROTO_MAX_FRAME         (PROTO_MAX_PAYLOAD + PROTO_OVERHEAD)

/* ========================================================================== */
/* 命令码                                                                     */
/* ========================================================================== */

#define CMD_SET_ANALOG          0x01U   /*!< PC->MCU [通道1B][值2B]          */
#define CMD_SET_DIGITAL         0x02U   /*!< PC->MCU [端口1B][状态1B]        */
#define CMD_SET_FREQUENCY       0x03U   /*!< PC->MCU [通道1B][频率4B]        */
#define CMD_READ_ALL            0x04U   /*!< PC->MCU 无载荷                  */
#define CMD_BATCH_SET           0x05U   /*!< PC->MCU 16B 批量数据(含门开关) */
#define CMD_REPORT_INPUT        0x81U   /*!< MCU->PC 16B 输入数据            */
#define CMD_ACK                 0x82U   /*!< MCU->PC [对应CMD1B][状态1B]     */
#define CMD_HEARTBEAT           0xFEU   /*!< 双向, 无载荷                    */

/* ========================================================================== */
/* 载荷长度                                                                   */
/* ========================================================================== */

#define PAYLOAD_LEN_SET_ANALOG      3U
#define PAYLOAD_LEN_SET_DIGITAL     2U
#define PAYLOAD_LEN_SET_FREQUENCY   5U
#define PAYLOAD_LEN_BATCH_SET       16U
#define PAYLOAD_LEN_REPORT_INPUT    16U
#define PAYLOAD_LEN_ACK             2U

/* ========================================================================== */
/* ACK 状态码                                                                 */
/* ========================================================================== */

#define ACK_OK                  0x00U   /*!< 执行成功                        */
#define ACK_ERR_LENGTH          0x01U   /*!< 载荷长度错误                    */
#define ACK_ERR_PARAM           0x02U   /*!< 参数越界(通道号/数值)           */
#define ACK_ERR_UNKNOWN_CMD     0x03U   /*!< 未知命令                        */
#define ACK_ERR_BUSY            0x04U   /*!< 发送忙, 未能执行                */

/* ========================================================================== */
/* SET_DIGITAL 端口编号                                                       */
/* ========================================================================== */

#define DIGITAL_PORT_DOOR_LOCK      0x00U
#define DIGITAL_PORT_INLET_VALVE    0x01U
#define DIGITAL_PORT_DRAIN_PUMP     0x02U
#define DIGITAL_PORT_HEATER         0x03U
#define DIGITAL_PORT_ALL            0xFFU /*!< 状态低 4 位作为位图整体写入   */

/* ========================================================================== */
/* 帧数据结构                                                                 */
/* ========================================================================== */

/** @brief 一个解析完成、CRC 已校验的接收帧 */
typedef struct {
    uint8_t cmd;                          /*!< 命令码                        */
    uint8_t len;                          /*!< 载荷长度                      */
    uint8_t payload[PROTO_MAX_PAYLOAD];   /*!< 载荷内容                      */
} proto_frame_t;

/** @brief 协议层运行统计 */
typedef struct {
    uint32_t rx_frames;      /*!< 成功处理的帧数                            */
    uint32_t tx_frames;      /*!< 发送的帧数                                */
    uint32_t err_length;     /*!< 长度错误次数                              */
    uint32_t err_param;      /*!< 参数错误次数                              */
    uint32_t err_unknown;    /*!< 未知命令次数                              */
} proto_stats_t;

/* ========================================================================== */
/* 接口                                                                       */
/* ========================================================================== */

/** @brief 初始化协议层并向 UART 注册帧回调 */
void protocol_init(void);

/**
 * @brief  处理一个完整帧(UART 层帧回调入口)
 * @note   在主循环上下文调用, 不在中断中执行业务逻辑
 */
void protocol_handle_frame(const proto_frame_t *frame);

/** @brief 采集当前所有输入量并发送 REPORT_INPUT 帧 */
bool protocol_send_report(void);

/** @brief 发送 ACK 帧 */
bool protocol_send_ack(uint8_t cmd, uint8_t status);

/** @brief 发送心跳帧 */
bool protocol_send_heartbeat(void);

/** @brief 最近一次收到有效帧的时间戳(ms), 用于断连判定 */
uint32_t protocol_get_last_rx_tick(void);

/** @brief 获取协议层统计信息 */
const proto_stats_t *protocol_get_stats(void);

/**
 * @brief  把所有输出复位到安全状态(断连保护)
 * @note   关闭 4 路数字输出、停止 PWM、DAC 归零
 */
void protocol_enter_safe_state(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_PARSER_H */
