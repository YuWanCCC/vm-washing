/**
  ******************************************************************************
  * @file    uart_handler.c
  * @brief   USART1 + DMA 收发与帧同步状态机实现
  *
  * 接收: DMA1_Channel5 循环模式写入 g_rx_buf(256B 环形缓冲), 主循环调用
  *       uart_poll() 按 DMA 写指针取出新字节, 逐字节驱动帧同步状态机。
  *       整帧校验通过后在主循环上下文回调协议层, 中断里不做业务处理。
  *
  * 发送: 软件环形缓冲 + DMA1_Channel4 分段搬运。上层可连续提交多帧,
  *       DMA 传输完成中断自动接续下一段, 避免忙等与丢帧。
  ******************************************************************************
  */

#include "uart_handler.h"
#include "crc16.h"
#include "main.h"

#if ((CFG_UART_RX_BUF_SIZE & (CFG_UART_RX_BUF_SIZE - 1U)) != 0U)
#error "CFG_UART_RX_BUF_SIZE 必须是 2 的幂"
#endif
#if ((CFG_UART_TX_BUF_SIZE & (CFG_UART_TX_BUF_SIZE - 1U)) != 0U)
#error "CFG_UART_TX_BUF_SIZE 必须是 2 的幂"
#endif

#define RX_BUF_MASK     (CFG_UART_RX_BUF_SIZE - 1U)
#define TX_BUF_MASK     (CFG_UART_TX_BUF_SIZE - 1U)

/* --- 接收 --- */
static uint8_t  g_rx_buf[CFG_UART_RX_BUF_SIZE];   /*!< DMA 环形接收缓冲     */
static uint16_t g_rx_tail;                        /*!< 软件读指针           */

/* --- 发送 --- */
static uint8_t           g_tx_buf[CFG_UART_TX_BUF_SIZE]; /*!< 环形发送缓冲  */
static volatile uint16_t g_tx_head;               /*!< 写指针(主循环)       */
static volatile uint16_t g_tx_tail;               /*!< 读指针(中断)         */
static volatile uint16_t g_tx_dma_len;            /*!< 本次 DMA 传输长度    */
static volatile bool     g_tx_active;             /*!< DMA 传输进行中       */

/* --- 帧同步状态机 --- */
static uart_rx_state_t g_state;
static proto_frame_t   g_frame;      /*!< 正在组装的帧                     */
static uint16_t        g_payload_idx;
static uint16_t        g_crc_calc;   /*!< 边收边算的 CRC                   */
static uint16_t        g_crc_recv;   /*!< 收到的 CRC                       */
static uint8_t         g_crc_idx;

static uart_stats_t     g_stats;
static uart_frame_cb_t  g_frame_cb;

/**
 * @brief  启动一段 TX DMA 传输(仅搬运环形缓冲中连续的一段)
 * @note   必须在关中断或中断上下文中调用
 */
static void uart_tx_dma_start(void)
{
    uint16_t used = (uint16_t)((g_tx_head - g_tx_tail) & TX_BUF_MASK);

    if (used == 0U) {
        g_tx_active = false;
        return;
    }

    /* 一次只发送到缓冲末尾, 回绕部分留给下一次传输完成中断 */
    uint16_t chunk = (uint16_t)(CFG_UART_TX_BUF_SIZE - g_tx_tail);
    if (chunk > used) {
        chunk = used;
    }

    g_tx_dma_len = chunk;
    g_tx_active  = true;

    DMA1_CH4->CCR  &= ~DMA_CCR_EN;
    DMA1->IFCR      = DMA_FLAG_GIF(4);
    DMA1_CH4->CMAR  = (uint32_t)&g_tx_buf[g_tx_tail];
    DMA1_CH4->CNDTR = chunk;
    DMA1_CH4->CCR  |= DMA_CCR_EN;
}

/** @brief 配置 USART1 引脚与波特率 */
static void uart_config_usart(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN | RCC_APB2ENR_USART1EN;

    gpio_pin_mode(PIN_UART_TX_PORT, PIN_UART_TX, GPIO_MODE_AF_PP_50M);
    gpio_pin_mode(PIN_UART_RX_PORT, PIN_UART_RX, GPIO_MODE_IN_PULL);
    gpio_pin_set(PIN_UART_RX_PORT, PIN_UART_RX);   /* RX 上拉, 空闲为高 */

    /* BRR = PCLK2 / 波特率 (整数+4 位小数格式, 四舍五入) */
    USART1->BRR = (CFG_PCLK2_FREQ_HZ + (CFG_UART_BAUDRATE / 2U)) / CFG_UART_BAUDRATE;

    /* 8 位数据 无校验 1 停止位 */
    USART1->CR2 = 0U;
    USART1->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_IDLEIE;
}

/** @brief 配置收发 DMA 通道 */
static void uart_config_dma(void)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    /* --- RX: DMA1_Channel5, 外设到内存, 循环模式 --- */
    DMA1_CH5->CCR   = 0U;
    DMA1_CH5->CPAR  = (uint32_t)&USART1->DR;
    DMA1_CH5->CMAR  = (uint32_t)g_rx_buf;
    DMA1_CH5->CNDTR = CFG_UART_RX_BUF_SIZE;
    DMA1->IFCR      = DMA_FLAG_GIF(5);
    DMA1_CH5->CCR   = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_HIGH | DMA_CCR_TCIE;
    DMA1_CH5->CCR  |= DMA_CCR_EN;

    /* --- TX: DMA1_Channel4, 内存到外设, 单次模式 --- */
    DMA1_CH4->CCR   = 0U;
    DMA1_CH4->CPAR  = (uint32_t)&USART1->DR;
    DMA1_CH4->CMAR  = (uint32_t)g_tx_buf;
    DMA1_CH4->CNDTR = 0U;
    DMA1->IFCR      = DMA_FLAG_GIF(4);
    DMA1_CH4->CCR   = DMA_CCR_MINC | DMA_CCR_DIR_M2P | DMA_CCR_TCIE;

    nvic_set_priority(DMA1_Channel5_IRQn, 1U);
    nvic_set_priority(DMA1_Channel4_IRQn, 2U);
    nvic_set_priority(USART1_IRQn, 3U);
    nvic_enable_irq(DMA1_Channel5_IRQn);
    nvic_enable_irq(DMA1_Channel4_IRQn);
    nvic_enable_irq(USART1_IRQn);
}

/** @brief 初始化 UART 收发 */
void uart_init(void)
{
    g_rx_tail     = 0U;
    g_tx_head     = 0U;
    g_tx_tail     = 0U;
    g_tx_dma_len  = 0U;
    g_tx_active   = false;
    g_frame_cb    = NULL;

    uart_reset_parser();

    for (uint32_t i = 0U; i < sizeof(g_stats) / sizeof(uint32_t); i++) {
        ((uint32_t *)&g_stats)[i] = 0U;
    }

    uart_config_usart();
    uart_config_dma();
}

/** @brief 注册整帧回调 */
void uart_set_frame_callback(uart_frame_cb_t callback)
{
    g_frame_cb = callback;
}

/** @brief 复位帧同步状态机 */
void uart_reset_parser(void)
{
    g_state       = UART_STATE_WAIT_HEADER;
    g_payload_idx = 0U;
    g_crc_calc    = CRC16_INIT;
    g_crc_recv    = 0U;
    g_crc_idx     = 0U;
}

/**
 * @brief  帧同步状态机: 逐字节推进
 *         WAIT_HEADER -> RECV_CMD -> RECV_LEN -> RECV_PAYLOAD
 *                     -> RECV_CRC -> VERIFY_TAIL
 * @note   CRC 边收边算, 无需额外拷贝缓冲
 */
static void uart_parse_byte(uint8_t byte)
{
    switch (g_state) {
    case UART_STATE_WAIT_HEADER:
        if (byte == PROTO_FRAME_HEADER) {
            g_crc_calc = CRC16_INIT;
            g_state    = UART_STATE_RECV_CMD;
        }
        break;

    case UART_STATE_RECV_CMD:
        g_frame.cmd = byte;
        g_crc_calc  = crc16_update(g_crc_calc, &byte, 1U);
        g_state     = UART_STATE_RECV_LEN;
        break;

    case UART_STATE_RECV_LEN:
        if (byte > PROTO_MAX_PAYLOAD) {
            /* 长度非法, 丢弃并重新同步 */
            g_stats.err_length++;
            g_state = UART_STATE_WAIT_HEADER;
            break;
        }
        g_frame.len   = byte;
        g_crc_calc    = crc16_update(g_crc_calc, &byte, 1U);
        g_payload_idx = 0U;
        g_crc_idx     = 0U;
        g_crc_recv    = 0U;
        g_state = (byte == 0U) ? UART_STATE_RECV_CRC : UART_STATE_RECV_PAYLOAD;
        break;

    case UART_STATE_RECV_PAYLOAD:
        g_frame.payload[g_payload_idx] = byte;
        g_crc_calc = crc16_update(g_crc_calc, &byte, 1U);
        g_payload_idx++;
        if (g_payload_idx >= g_frame.len) {
            g_state = UART_STATE_RECV_CRC;
        }
        break;

    case UART_STATE_RECV_CRC:
        /* CRC 小端: 先低字节后高字节 */
        g_crc_recv |= (uint16_t)((uint16_t)byte << (8U * g_crc_idx));
        g_crc_idx++;
        if (g_crc_idx >= 2U) {
            g_state = UART_STATE_VERIFY_TAIL;
        }
        break;

    case UART_STATE_VERIFY_TAIL:
        if (byte != PROTO_FRAME_TAIL) {
            g_stats.err_tail++;
        } else if (g_crc_recv != g_crc_calc) {
            g_stats.err_crc++;
        } else {
            g_stats.rx_frames++;
            if (g_frame_cb != NULL) {
                g_frame_cb(&g_frame);
            }
        }
        g_state = UART_STATE_WAIT_HEADER;
        break;

    default:
        g_state = UART_STATE_WAIT_HEADER;
        break;
    }
}

/**
 * @brief  从 DMA 环形缓冲取出新数据并解析
 * @note   DMA 写指针 = 缓冲长度 - CNDTR
 */
void uart_poll(void)
{
    uint16_t head = (uint16_t)(CFG_UART_RX_BUF_SIZE - (DMA1_CH5->CNDTR & RX_BUF_MASK));
    head &= RX_BUF_MASK;

    while (g_rx_tail != head) {
        uint8_t byte = g_rx_buf[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1U) & RX_BUF_MASK);
        g_stats.rx_bytes++;
        uart_parse_byte(byte);
    }
}

/** @brief 提交裸数据到发送环形缓冲 */
bool uart_send(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return false;
    }

    /* 与 TX DMA 中断共享环形缓冲指针, 需要临界区保护 */
    uint32_t primask = __get_primask();

    __disable_irq();

    uint16_t used = (uint16_t)((g_tx_head - g_tx_tail) & TX_BUF_MASK);
    uint16_t free_space = (uint16_t)(TX_BUF_MASK - used);

    if (len > free_space) {
        g_stats.tx_dropped++;
        __set_primask(primask);
        return false;
    }

    for (uint16_t i = 0U; i < len; i++) {
        g_tx_buf[g_tx_head] = data[i];
        g_tx_head = (uint16_t)((g_tx_head + 1U) & TX_BUF_MASK);
    }

    if (!g_tx_active) {
        uart_tx_dma_start();
    }

    __set_primask(primask);
    return true;
}

/**
 * @brief  打包并发送一帧
 * @note   帧结构 [0xAA][CMD][LEN][PAYLOAD][CRC_L][CRC_H][0x55],
 *         CRC 覆盖 CMD+LEN+PAYLOAD
 */
bool uart_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    static uint8_t frame[PROTO_MAX_FRAME];

    if (len > PROTO_MAX_PAYLOAD) {
        return false;
    }
    if ((len > 0U) && (payload == NULL)) {
        return false;
    }

    uint16_t idx = 0U;
    frame[idx++] = PROTO_FRAME_HEADER;
    frame[idx++] = cmd;
    frame[idx++] = len;

    for (uint8_t i = 0U; i < len; i++) {
        frame[idx++] = payload[i];
    }

    uint16_t crc = crc16_calc(&frame[1], (uint16_t)(len + 2U));
    frame[idx++] = (uint8_t)(crc & 0xFFU);
    frame[idx++] = (uint8_t)((crc >> 8U) & 0xFFU);
    frame[idx++] = PROTO_FRAME_TAIL;

    if (!uart_send(frame, idx)) {
        return false;
    }

    g_stats.tx_frames++;
    return true;
}

/** @brief 发送通道是否空闲 */
bool uart_tx_is_idle(void)
{
    return (!g_tx_active) && (g_tx_head == g_tx_tail);
}

/** @brief 获取统计信息 */
const uart_stats_t *uart_get_stats(void)
{
    return &g_stats;
}

/* ========================================================================== */
/* 中断处理体                                                                 */
/* ========================================================================== */

/**
 * @brief  USART1 中断: 处理空闲帧间隔与溢出错误
 * @note   DMA 循环接收时若发生 ORE 必须读 SR+DR 清除, 否则接收会停滞
 */
void uart_irq_handler(void)
{
    uint32_t sr = USART1->SR;

    if ((sr & (USART_SR_IDLE | USART_SR_ORE | USART_SR_NE | USART_SR_FE)) != 0U) {
        if ((sr & USART_SR_ORE) != 0U) {
            g_stats.err_overrun++;
        }
        (void)USART1->DR;   /* 读 DR 清除 IDLE/ORE/NE/FE 标志 */
    }
}

/**
 * @brief  RX DMA 中断: 环形缓冲回绕
 * @note   循环模式下 DMA 自动重装, 此处仅清标志; 数据提取在 uart_poll()
 */
void uart_rx_dma_irq_handler(void)
{
    if ((DMA1->ISR & DMA_FLAG_TCIF(5)) != 0U) {
        DMA1->IFCR = DMA_FLAG_GIF(5);
    }
}

/**
 * @brief  TX DMA 中断: 推进读指针并接续下一段
 */
void uart_tx_dma_irq_handler(void)
{
    if ((DMA1->ISR & DMA_FLAG_TCIF(4)) == 0U) {
        DMA1->IFCR = DMA_FLAG_GIF(4);
        return;
    }

    DMA1->IFCR     = DMA_FLAG_GIF(4);
    DMA1_CH4->CCR &= ~DMA_CCR_EN;

    g_tx_tail    = (uint16_t)((g_tx_tail + g_tx_dma_len) & TX_BUF_MASK);
    g_tx_dma_len = 0U;

    uart_tx_dma_start();
}
