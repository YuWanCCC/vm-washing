/**
  ******************************************************************************
  * @file    uart_handler.h
  * @brief   USART1 + DMA 收发与帧同步状态机
  ******************************************************************************
  */

#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#include "protocol_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 帧同步状态机状态 */
typedef enum {
    UART_STATE_WAIT_HEADER = 0, /*!< 等待 0xAA                              */
    UART_STATE_RECV_CMD,        /*!< 接收命令字                              */
    UART_STATE_RECV_LEN,        /*!< 接收长度字                              */
    UART_STATE_RECV_PAYLOAD,    /*!< 接收载荷                                */
    UART_STATE_RECV_CRC,        /*!< 接收 CRC16(低字节先)                    */
    UART_STATE_VERIFY_TAIL      /*!< 校验 0x55 帧尾                          */
} uart_rx_state_t;

/** @brief UART 层统计 */
typedef struct {
    uint32_t rx_bytes;        /*!< 累计接收字节数                           */
    uint32_t rx_frames;       /*!< 成功组帧数                               */
    uint32_t err_crc;         /*!< CRC 错误帧数                             */
    uint32_t err_tail;        /*!< 帧尾错误帧数                             */
    uint32_t err_length;      /*!< 长度字段非法帧数                         */
    uint32_t err_overrun;     /*!< DMA 环形缓冲溢出次数                     */
    uint32_t tx_frames;       /*!< 发送帧数                                 */
    uint32_t tx_dropped;      /*!< 因发送忙丢弃的帧数                       */
} uart_stats_t;

/** @brief 完整帧回调原型 */
typedef void (*uart_frame_cb_t)(const proto_frame_t *frame);

/**
 * @brief  初始化 USART1(115200 8N1) 与收发 DMA
 * @note   RX 使用 DMA1_Channel5 循环模式, TX 使用 DMA1_Channel4 单次模式
 */
void uart_init(void);

/** @brief 注册完整帧回调(收到并通过 CRC 校验的帧) */
void uart_set_frame_callback(uart_frame_cb_t callback);

/**
 * @brief  轮询处理 DMA 环形缓冲中的新数据, 驱动帧同步状态机
 * @note   必须在主循环中周期调用; 组好帧后同步调用帧回调
 */
void uart_poll(void);

/**
 * @brief  按协议格式打包并发送一帧
 * @param  cmd     命令码
 * @param  payload 载荷指针, 可为 NULL
 * @param  len     载荷长度(<= PROTO_MAX_PAYLOAD)
 * @return true 表示已提交给 DMA 发送
 */
bool uart_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len);

/**
 * @brief  发送裸数据(调试用)
 * @return true 表示已提交发送
 */
bool uart_send(const uint8_t *data, uint16_t len);

/** @brief 发送通道是否空闲 */
bool uart_tx_is_idle(void);

/** @brief 获取 UART 层统计信息 */
const uart_stats_t *uart_get_stats(void);

/** @brief 复位帧同步状态机(用于错误恢复) */
void uart_reset_parser(void);

/* --- 供中断服务程序调用 --- */

/** @brief USART1 中断处理体(IDLE/错误标志清理) */
void uart_irq_handler(void);

/** @brief RX DMA 中断处理体(半传输/传输完成) */
void uart_rx_dma_irq_handler(void);

/** @brief TX DMA 中断处理体(传输完成) */
void uart_tx_dma_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_HANDLER_H */
