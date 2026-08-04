/**
  ******************************************************************************
  * @file    config.h
  * @brief   全局配置(时钟/通信/采样/缓冲区)
  ******************************************************************************
  */

#ifndef CONFIG_H
#define CONFIG_H

/* ========================================================================== */
/* 时钟配置                                                                   */
/* ========================================================================== */

#define CFG_HSE_FREQ_HZ         8000000UL   /*!< 外部晶振 8MHz                */
#define CFG_SYSCLK_FREQ_HZ      72000000UL  /*!< 系统时钟 72MHz (HSE*9)       */
#define CFG_HCLK_FREQ_HZ        72000000UL  /*!< AHB  72MHz                   */
#define CFG_PCLK1_FREQ_HZ       36000000UL  /*!< APB1 36MHz (最大值)          */
#define CFG_PCLK2_FREQ_HZ       72000000UL  /*!< APB2 72MHz                   */
#define CFG_TIMCLK_FREQ_HZ      72000000UL  /*!< 定时器时钟(APB1 分频2时倍频) */
#define CFG_ADCCLK_FREQ_HZ      12000000UL  /*!< ADC 时钟 = PCLK2/6, <=14MHz  */

/* ========================================================================== */
/* 串口通信配置                                                               */
/* ========================================================================== */

#define CFG_UART_BAUDRATE       115200UL    /*!< 与上位机约定 115200 8N1      */
#define CFG_UART_RX_BUF_SIZE    256U        /*!< DMA 环形接收缓冲(必须2的幂)  */
#define CFG_UART_TX_BUF_SIZE    256U        /*!< 发送缓冲                     */

/* ========================================================================== */
/* 采集与上报配置                                                             */
/* ========================================================================== */

#define CFG_ADC_SAMPLE_RATE_HZ  100U        /*!< ADC/GPIO 采集频率 100Hz      */
#define CFG_ADC_OVERSAMPLE      4U          /*!< 每通道过采样次数(取平均)     */
#define CFG_ADC_CHANNEL_COUNT   4U          /*!< 扫描通道数                   */
#define CFG_ADC_VREF_MV         3300U       /*!< ADC/DAC 参考电压 3.3V        */
#define CFG_ADC_FULL_SCALE      4095U       /*!< 12 位满量程                  */

#define CFG_TICK_FREQ_HZ        CFG_ADC_SAMPLE_RATE_HZ /*!< TIM3 周期性 tick  */
#define CFG_AUTO_REPORT_ENABLE  1           /*!< 1: 每个 tick 主动上报输入量  */

/* ========================================================================== */
/* 链路保活配置                                                               */
/* ========================================================================== */

#define CFG_HEARTBEAT_MS        500U        /*!< 心跳间隔 500ms               */
#define CFG_LINK_TIMEOUT_MS     2000U       /*!< 超时无帧则判定断连           */
#define CFG_SAFE_STATE_ON_LOST  1           /*!< 断连后输出复位到安全状态     */

/* ========================================================================== */
/* 输出量限制                                                                 */
/* ========================================================================== */

#define CFG_PWM_FREQ_MIN_HZ     1UL         /*!< PWM 最低频率                 */
#define CFG_PWM_FREQ_MAX_HZ     100000UL    /*!< PWM 最高频率                 */
#define CFG_DAC_MAX_VALUE       4095U       /*!< DAC 12 位最大值              */

/*
 * 说明: DAC 外设仅存在于带 DAC 的 STM32F103 型号(如 RB/RC/VC/ZE 等)。
 * 若实际使用 STM32F103C8 (无 DAC), 需改用 PWM + RC 低通滤波方案,
 * 只需替换 dac_output.c 的底层实现, 上层接口保持不变。
 */

#endif /* CONFIG_H */
