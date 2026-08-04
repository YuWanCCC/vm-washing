/**
  ******************************************************************************
  * @file      startup_stm32f103c8tx.s
  * @brief     STM32F103C8 启动文件(精简版, GNU AS 语法)
  *
  * 上电流程:
  *   1. 设置栈指针(由链接脚本给出的 _estack)
  *   2. 拷贝 .data 段从 FLASH 到 RAM
  *   3. 清零 .bss 段
  *   4. 调用 SystemInit() 复位时钟系统
  *   5. 跳转 main(), 返回则死循环
  *
  * 未使用 C 库初始化(__libc_init_array), 本工程为纯 C 且无静态构造需求。
  ******************************************************************************
  */

  .syntax unified
  .cpu cortex-m3
  .fpu softvfp
  .thumb

.global  g_pfnVectors
.global  Default_Handler

/* 链接脚本导出的符号 */
.word  _sidata      /* .data 在 FLASH 中的加载地址 */
.word  _sdata       /* .data 在 RAM 中的起始地址   */
.word  _edata       /* .data 在 RAM 中的结束地址   */
.word  _sbss        /* .bss 起始                   */
.word  _ebss        /* .bss 结束                   */

/**
 * @brief 复位入口
 */
  .section  .text.Reset_Handler
  .weak  Reset_Handler
  .type  Reset_Handler, %function
Reset_Handler:
  ldr   r0, =_estack
  mov   sp, r0            /* 设置栈顶 */

/* 拷贝已初始化数据段 .data: FLASH -> RAM */
  ldr   r0, =_sdata
  ldr   r1, =_edata
  ldr   r2, =_sidata
  movs  r3, #0
  b     LoopCopyDataInit

CopyDataInit:
  ldr   r4, [r2, r3]
  str   r4, [r0, r3]
  adds  r3, r3, #4

LoopCopyDataInit:
  adds  r4, r0, r3
  cmp   r4, r1
  bcc   CopyDataInit

/* 清零未初始化数据段 .bss */
  ldr   r2, =_sbss
  ldr   r4, =_ebss
  movs  r3, #0
  b     LoopFillZerobss

FillZerobss:
  str   r3, [r2]
  adds  r2, r2, #4

LoopFillZerobss:
  cmp   r2, r4
  bcc   FillZerobss

/* 系统初始化并进入主程序 */
  bl    SystemInit
  bl    main
  bx    lr

.size  Reset_Handler, .-Reset_Handler

/**
 * @brief 未实现中断的缺省处理: 死循环, 便于调试器定位异常中断源
 */
  .section  .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b  Infinite_Loop
  .size  Default_Handler, .-Default_Handler

/******************************************************************************
* 中断向量表
******************************************************************************/
  .section  .isr_vector,"a",%progbits
  .type  g_pfnVectors, %object
  .size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
  .word  _estack
  .word  Reset_Handler
  .word  NMI_Handler
  .word  HardFault_Handler
  .word  MemManage_Handler
  .word  BusFault_Handler
  .word  UsageFault_Handler
  .word  0
  .word  0
  .word  0
  .word  0
  .word  SVC_Handler
  .word  DebugMon_Handler
  .word  0
  .word  PendSV_Handler
  .word  SysTick_Handler

  .word  WWDG_IRQHandler              /* IRQ0  窗口看门狗          */
  .word  PVD_IRQHandler               /* IRQ1  电压检测            */
  .word  TAMPER_IRQHandler            /* IRQ2  侵入检测            */
  .word  RTC_IRQHandler               /* IRQ3  RTC                 */
  .word  FLASH_IRQHandler             /* IRQ4  FLASH               */
  .word  RCC_IRQHandler               /* IRQ5  RCC                 */
  .word  EXTI0_IRQHandler             /* IRQ6  EXTI0               */
  .word  EXTI1_IRQHandler             /* IRQ7  EXTI1               */
  .word  EXTI2_IRQHandler             /* IRQ8  EXTI2               */
  .word  EXTI3_IRQHandler             /* IRQ9  EXTI3               */
  .word  EXTI4_IRQHandler             /* IRQ10 EXTI4               */
  .word  DMA1_Channel1_IRQHandler     /* IRQ11 DMA1 通道1 (ADC)    */
  .word  DMA1_Channel2_IRQHandler     /* IRQ12 DMA1 通道2          */
  .word  DMA1_Channel3_IRQHandler     /* IRQ13 DMA1 通道3          */
  .word  DMA1_Channel4_IRQHandler     /* IRQ14 DMA1 通道4 (UART TX)*/
  .word  DMA1_Channel5_IRQHandler     /* IRQ15 DMA1 通道5 (UART RX)*/
  .word  DMA1_Channel6_IRQHandler     /* IRQ16 DMA1 通道6          */
  .word  DMA1_Channel7_IRQHandler     /* IRQ17 DMA1 通道7          */
  .word  ADC1_2_IRQHandler            /* IRQ18 ADC1/ADC2           */
  .word  USB_HP_CAN1_TX_IRQHandler    /* IRQ19                     */
  .word  USB_LP_CAN1_RX0_IRQHandler   /* IRQ20                     */
  .word  CAN1_RX1_IRQHandler          /* IRQ21                     */
  .word  CAN1_SCE_IRQHandler          /* IRQ22                     */
  .word  EXTI9_5_IRQHandler           /* IRQ23 EXTI9-5             */
  .word  TIM1_BRK_IRQHandler          /* IRQ24 TIM1 刹车           */
  .word  TIM1_UP_IRQHandler           /* IRQ25 TIM1 更新           */
  .word  TIM1_TRG_COM_IRQHandler      /* IRQ26 TIM1 触发           */
  .word  TIM1_CC_IRQHandler           /* IRQ27 TIM1 捕获比较       */
  .word  TIM2_IRQHandler              /* IRQ28 TIM2 (Hall PWM)     */
  .word  TIM3_IRQHandler              /* IRQ29 TIM3 (100Hz tick)   */
  .word  TIM4_IRQHandler              /* IRQ30 TIM4                */
  .word  I2C1_EV_IRQHandler           /* IRQ31                     */
  .word  I2C1_ER_IRQHandler           /* IRQ32                     */
  .word  I2C2_EV_IRQHandler           /* IRQ33                     */
  .word  I2C2_ER_IRQHandler           /* IRQ34                     */
  .word  SPI1_IRQHandler              /* IRQ35                     */
  .word  SPI2_IRQHandler              /* IRQ36                     */
  .word  USART1_IRQHandler            /* IRQ37 上位机通信          */
  .word  USART2_IRQHandler            /* IRQ38                     */
  .word  USART3_IRQHandler            /* IRQ39                     */
  .word  EXTI15_10_IRQHandler         /* IRQ40 EXTI15-10           */
  .word  RTC_Alarm_IRQHandler         /* IRQ41 RTC 闹钟            */
  .word  USBWakeUp_IRQHandler         /* IRQ42 USB 唤醒            */

/******************************************************************************
* 弱符号定义: 未在 C 代码中实现的中断都指向 Default_Handler
******************************************************************************/

  .weak  NMI_Handler
  .thumb_set NMI_Handler,Default_Handler

  .weak  HardFault_Handler
  .thumb_set HardFault_Handler,Default_Handler

  .weak  MemManage_Handler
  .thumb_set MemManage_Handler,Default_Handler

  .weak  BusFault_Handler
  .thumb_set BusFault_Handler,Default_Handler

  .weak  UsageFault_Handler
  .thumb_set UsageFault_Handler,Default_Handler

  .weak  SVC_Handler
  .thumb_set SVC_Handler,Default_Handler

  .weak  DebugMon_Handler
  .thumb_set DebugMon_Handler,Default_Handler

  .weak  PendSV_Handler
  .thumb_set PendSV_Handler,Default_Handler

  .weak  SysTick_Handler
  .thumb_set SysTick_Handler,Default_Handler

  .weak  WWDG_IRQHandler
  .thumb_set WWDG_IRQHandler,Default_Handler

  .weak  PVD_IRQHandler
  .thumb_set PVD_IRQHandler,Default_Handler

  .weak  TAMPER_IRQHandler
  .thumb_set TAMPER_IRQHandler,Default_Handler

  .weak  RTC_IRQHandler
  .thumb_set RTC_IRQHandler,Default_Handler

  .weak  FLASH_IRQHandler
  .thumb_set FLASH_IRQHandler,Default_Handler

  .weak  RCC_IRQHandler
  .thumb_set RCC_IRQHandler,Default_Handler

  .weak  EXTI0_IRQHandler
  .thumb_set EXTI0_IRQHandler,Default_Handler

  .weak  EXTI1_IRQHandler
  .thumb_set EXTI1_IRQHandler,Default_Handler

  .weak  EXTI2_IRQHandler
  .thumb_set EXTI2_IRQHandler,Default_Handler

  .weak  EXTI3_IRQHandler
  .thumb_set EXTI3_IRQHandler,Default_Handler

  .weak  EXTI4_IRQHandler
  .thumb_set EXTI4_IRQHandler,Default_Handler

  .weak  DMA1_Channel1_IRQHandler
  .thumb_set DMA1_Channel1_IRQHandler,Default_Handler

  .weak  DMA1_Channel2_IRQHandler
  .thumb_set DMA1_Channel2_IRQHandler,Default_Handler

  .weak  DMA1_Channel3_IRQHandler
  .thumb_set DMA1_Channel3_IRQHandler,Default_Handler

  .weak  DMA1_Channel4_IRQHandler
  .thumb_set DMA1_Channel4_IRQHandler,Default_Handler

  .weak  DMA1_Channel5_IRQHandler
  .thumb_set DMA1_Channel5_IRQHandler,Default_Handler

  .weak  DMA1_Channel6_IRQHandler
  .thumb_set DMA1_Channel6_IRQHandler,Default_Handler

  .weak  DMA1_Channel7_IRQHandler
  .thumb_set DMA1_Channel7_IRQHandler,Default_Handler

  .weak  ADC1_2_IRQHandler
  .thumb_set ADC1_2_IRQHandler,Default_Handler

  .weak  USB_HP_CAN1_TX_IRQHandler
  .thumb_set USB_HP_CAN1_TX_IRQHandler,Default_Handler

  .weak  USB_LP_CAN1_RX0_IRQHandler
  .thumb_set USB_LP_CAN1_RX0_IRQHandler,Default_Handler

  .weak  CAN1_RX1_IRQHandler
  .thumb_set CAN1_RX1_IRQHandler,Default_Handler

  .weak  CAN1_SCE_IRQHandler
  .thumb_set CAN1_SCE_IRQHandler,Default_Handler

  .weak  EXTI9_5_IRQHandler
  .thumb_set EXTI9_5_IRQHandler,Default_Handler

  .weak  TIM1_BRK_IRQHandler
  .thumb_set TIM1_BRK_IRQHandler,Default_Handler

  .weak  TIM1_UP_IRQHandler
  .thumb_set TIM1_UP_IRQHandler,Default_Handler

  .weak  TIM1_TRG_COM_IRQHandler
  .thumb_set TIM1_TRG_COM_IRQHandler,Default_Handler

  .weak  TIM1_CC_IRQHandler
  .thumb_set TIM1_CC_IRQHandler,Default_Handler

  .weak  TIM2_IRQHandler
  .thumb_set TIM2_IRQHandler,Default_Handler

  .weak  TIM3_IRQHandler
  .thumb_set TIM3_IRQHandler,Default_Handler

  .weak  TIM4_IRQHandler
  .thumb_set TIM4_IRQHandler,Default_Handler

  .weak  I2C1_EV_IRQHandler
  .thumb_set I2C1_EV_IRQHandler,Default_Handler

  .weak  I2C1_ER_IRQHandler
  .thumb_set I2C1_ER_IRQHandler,Default_Handler

  .weak  I2C2_EV_IRQHandler
  .thumb_set I2C2_EV_IRQHandler,Default_Handler

  .weak  I2C2_ER_IRQHandler
  .thumb_set I2C2_ER_IRQHandler,Default_Handler

  .weak  SPI1_IRQHandler
  .thumb_set SPI1_IRQHandler,Default_Handler

  .weak  SPI2_IRQHandler
  .thumb_set SPI2_IRQHandler,Default_Handler

  .weak  USART1_IRQHandler
  .thumb_set USART1_IRQHandler,Default_Handler

  .weak  USART2_IRQHandler
  .thumb_set USART2_IRQHandler,Default_Handler

  .weak  USART3_IRQHandler
  .thumb_set USART3_IRQHandler,Default_Handler

  .weak  EXTI15_10_IRQHandler
  .thumb_set EXTI15_10_IRQHandler,Default_Handler

  .weak  RTC_Alarm_IRQHandler
  .thumb_set RTC_Alarm_IRQHandler,Default_Handler

  .weak  USBWakeUp_IRQHandler
  .thumb_set USBWakeUp_IRQHandler,Default_Handler
