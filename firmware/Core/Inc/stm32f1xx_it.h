/**
  ******************************************************************************
  * @file    stm32f1xx_it.h
  * @brief   中断服务程序声明
  ******************************************************************************
  */

#ifndef STM32F1XX_IT_H
#define STM32F1XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M3 内核异常 */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* 外设中断 */
void USART1_IRQHandler(void);        /*!< USART1 IDLE / 错误标志            */
void DMA1_Channel5_IRQHandler(void); /*!< USART1 RX DMA                     */
void DMA1_Channel4_IRQHandler(void); /*!< USART1 TX DMA                     */
void TIM3_IRQHandler(void);          /*!< 100Hz 采集 tick                   */

#ifdef __cplusplus
}
#endif

#endif /* STM32F1XX_IT_H */
