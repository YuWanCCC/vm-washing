/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   中断服务程序
  *
  * 原则: 中断内只做标志处理与数据搬运, 业务逻辑放到主循环执行, 保证
  * 100Hz 采集节拍与 UART 接收的实时性。
  ******************************************************************************
  */

#include "stm32f1xx_it.h"

#include "main.h"
#include "uart_handler.h"

/* ========================================================================== */
/* 内核异常                                                                   */
/* ========================================================================== */

/** @brief 不可屏蔽中断 */
void NMI_Handler(void)
{
    for (;;) {
    }
}

/** @brief 硬件错误: 停在此处便于调试器抓现场 */
void HardFault_Handler(void)
{
    error_handler();
}

void MemManage_Handler(void)
{
    error_handler();
}

void BusFault_Handler(void)
{
    error_handler();
}

void UsageFault_Handler(void)
{
    error_handler();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/** @brief 1ms 系统滴答 */
void SysTick_Handler(void)
{
    sys_tick_inc();
}

/* ========================================================================== */
/* 外设中断                                                                   */
/* ========================================================================== */

/**
 * @brief  USART1 中断
 * @note   处理空闲检测与溢出等错误标志, 数据由 DMA 搬运
 */
void USART1_IRQHandler(void)
{
    uart_irq_handler();
}

/**
 * @brief  USART1 RX DMA(DMA1 通道5) 中断
 * @note   循环模式回绕时触发, 仅清标志
 */
void DMA1_Channel5_IRQHandler(void)
{
    uart_rx_dma_irq_handler();
}

/**
 * @brief  USART1 TX DMA(DMA1 通道4) 中断
 * @note   一段发送完成后自动接续环形缓冲的下一段
 */
void DMA1_Channel4_IRQHandler(void)
{
    uart_tx_dma_irq_handler();
}

/**
 * @brief  TIM3 更新中断: 100Hz 采集节拍
 */
void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_UIF) != 0U) {
        TIM3->SR &= ~TIM_SR_UIF;
        app_set_sample_tick();
    }
}
