/**
  ******************************************************************************
  * @file    stm32f103_reg.h
  * @brief   STM32F103ZET6 轻量级寄存器定义(替代 CMSIS/HAL)
  *
  * 目标器件: STM32F103ZET6, 144 脚大容量(High-density)型号,
  *           512K FLASH / 64K RAM, 含 GPIOA-GPIOG、ADC1-3、TIM1-8、
  *           USART1-3 + UART4/5、DMA1/DMA2、双通道 DAC。
  *
  * 本工程不依赖 ST 官方 HAL 库, 仅通过本文件提供的寄存器映射直接操作外设。
  * 结构体成员顺序严格按照 RM0008 参考手册的寄存器偏移排列, 不可随意增删。
  ******************************************************************************
  */

#ifndef STM32F103_REG_H
#define STM32F103_REG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __IO volatile

/* ========================================================================== */
/* 外设结构体定义                                                             */
/* ========================================================================== */

/** @brief 复位与时钟控制 */
typedef struct {
    __IO uint32_t CR;        /*!< 0x00 时钟控制         */
    __IO uint32_t CFGR;      /*!< 0x04 时钟配置         */
    __IO uint32_t CIR;       /*!< 0x08 时钟中断         */
    __IO uint32_t APB2RSTR;  /*!< 0x0C APB2 复位        */
    __IO uint32_t APB1RSTR;  /*!< 0x10 APB1 复位        */
    __IO uint32_t AHBENR;    /*!< 0x14 AHB  时钟使能    */
    __IO uint32_t APB2ENR;   /*!< 0x18 APB2 时钟使能    */
    __IO uint32_t APB1ENR;   /*!< 0x1C APB1 时钟使能    */
    __IO uint32_t BDCR;      /*!< 0x20 备份域控制       */
    __IO uint32_t CSR;       /*!< 0x24 控制/状态        */
} RCC_TypeDef;

/** @brief FLASH 接口(仅用到等待周期与预取) */
typedef struct {
    __IO uint32_t ACR;
    __IO uint32_t KEYR;
    __IO uint32_t OPTKEYR;
    __IO uint32_t SR;
    __IO uint32_t CR;
    __IO uint32_t AR;
    __IO uint32_t RESERVED;
    __IO uint32_t OBR;
    __IO uint32_t WRPR;
} FLASH_TypeDef;

/** @brief 通用 IO 端口 */
typedef struct {
    __IO uint32_t CRL;   /*!< 0x00 端口配置低(pin0-7)  */
    __IO uint32_t CRH;   /*!< 0x04 端口配置高(pin8-15) */
    __IO uint32_t IDR;   /*!< 0x08 输入数据            */
    __IO uint32_t ODR;   /*!< 0x0C 输出数据            */
    __IO uint32_t BSRR;  /*!< 0x10 置位/复位           */
    __IO uint32_t BRR;   /*!< 0x14 复位                */
    __IO uint32_t LCKR;  /*!< 0x18 配置锁定            */
} GPIO_TypeDef;

/** @brief 复用功能与事件控制 */
typedef struct {
    __IO uint32_t EVCR;
    __IO uint32_t MAPR;
    __IO uint32_t EXTICR[4];
    __IO uint32_t RESERVED;
    __IO uint32_t MAPR2;
} AFIO_TypeDef;

/** @brief 通用同步异步收发器 */
typedef struct {
    __IO uint32_t SR;
    __IO uint32_t DR;
    __IO uint32_t BRR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t CR3;
    __IO uint32_t GTPR;
} USART_TypeDef;

/** @brief DMA 单通道寄存器组(每通道 20 字节) */
typedef struct {
    __IO uint32_t CCR;
    __IO uint32_t CNDTR;
    __IO uint32_t CPAR;
    __IO uint32_t CMAR;
    __IO uint32_t RESERVED;
} DMA_Channel_TypeDef;

/** @brief DMA 控制器公共寄存器 */
typedef struct {
    __IO uint32_t ISR;
    __IO uint32_t IFCR;
} DMA_TypeDef;

/** @brief 定时器(通用/高级通用共用布局) */
typedef struct {
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SMCR;
    __IO uint32_t DIER;
    __IO uint32_t SR;
    __IO uint32_t EGR;
    __IO uint32_t CCMR1;
    __IO uint32_t CCMR2;
    __IO uint32_t CCER;
    __IO uint32_t CNT;
    __IO uint32_t PSC;
    __IO uint32_t ARR;
    __IO uint32_t RCR;
    __IO uint32_t CCR1;
    __IO uint32_t CCR2;
    __IO uint32_t CCR3;
    __IO uint32_t CCR4;
    __IO uint32_t BDTR;
    __IO uint32_t DCR;
    __IO uint32_t DMAR;
} TIM_TypeDef;

/** @brief 模数转换器 */
typedef struct {
    __IO uint32_t SR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SMPR1;
    __IO uint32_t SMPR2;
    __IO uint32_t JOFR1;
    __IO uint32_t JOFR2;
    __IO uint32_t JOFR3;
    __IO uint32_t JOFR4;
    __IO uint32_t HTR;
    __IO uint32_t LTR;
    __IO uint32_t SQR1;
    __IO uint32_t SQR2;
    __IO uint32_t SQR3;
    __IO uint32_t JSQR;
    __IO uint32_t JDR1;
    __IO uint32_t JDR2;
    __IO uint32_t JDR3;
    __IO uint32_t JDR4;
    __IO uint32_t DR;
} ADC_TypeDef;

/** @brief 数模转换器 */
typedef struct {
    __IO uint32_t CR;
    __IO uint32_t SWTRIGR;
    __IO uint32_t DHR12R1;
    __IO uint32_t DHR12L1;
    __IO uint32_t DHR8R1;
    __IO uint32_t DHR12R2;
    __IO uint32_t DHR12L2;
    __IO uint32_t DHR8R2;
    __IO uint32_t DHR12RD;
    __IO uint32_t DHR12LD;
    __IO uint32_t DHR8RD;
    __IO uint32_t DOR1;
    __IO uint32_t DOR2;
} DAC_TypeDef;

/** @brief 系统滴答定时器 */
typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __IO uint32_t CALIB;
} SysTick_TypeDef;

/** @brief 嵌套向量中断控制器 */
typedef struct {
    __IO uint32_t ISER[8];
    uint32_t      RESERVED0[24];
    __IO uint32_t ICER[8];
    uint32_t      RESERVED1[24];
    __IO uint32_t ISPR[8];
    uint32_t      RESERVED2[24];
    __IO uint32_t ICPR[8];
    uint32_t      RESERVED3[24];
    __IO uint32_t IABR[8];
    uint32_t      RESERVED4[56];
    __IO uint8_t  IP[240];
} NVIC_TypeDef;

/** @brief 系统控制块(仅用到 VTOR/AIRCR) */
typedef struct {
    __IO uint32_t CPUID;
    __IO uint32_t ICSR;
    __IO uint32_t VTOR;
    __IO uint32_t AIRCR;
    __IO uint32_t SCR;
    __IO uint32_t CCR;
    __IO uint8_t  SHP[12];
    __IO uint32_t SHCSR;
} SCB_TypeDef;

/* ========================================================================== */
/* 外设基地址                                                                 */
/* ========================================================================== */

#define FLASH_BASE_ADDR   0x40022000UL
#define RCC_BASE_ADDR     0x40021000UL
#define AFIO_BASE_ADDR    0x40010000UL
#define GPIOA_BASE_ADDR   0x40010800UL
#define GPIOB_BASE_ADDR   0x40010C00UL
#define GPIOC_BASE_ADDR   0x40011000UL
#define GPIOD_BASE_ADDR   0x40011400UL
#define GPIOE_BASE_ADDR   0x40011800UL
#define GPIOF_BASE_ADDR   0x40011C00UL   /*!< 大容量器件(144 脚)才有  */
#define GPIOG_BASE_ADDR   0x40012000UL   /*!< 大容量器件(144 脚)才有  */
#define ADC1_BASE_ADDR    0x40012400UL
#define ADC2_BASE_ADDR    0x40012800UL
#define ADC3_BASE_ADDR    0x40013C00UL   /*!< 大容量器件专有          */
#define TIM1_BASE_ADDR    0x40012C00UL
#define TIM8_BASE_ADDR    0x40013400UL   /*!< 大容量器件专有          */
#define USART1_BASE_ADDR  0x40013800UL
#define TIM2_BASE_ADDR    0x40000000UL
#define TIM3_BASE_ADDR    0x40000400UL
#define TIM4_BASE_ADDR    0x40000800UL
#define TIM5_BASE_ADDR    0x40000C00UL   /*!< 大容量器件专有          */
#define TIM6_BASE_ADDR    0x40001000UL   /*!< 大容量器件专有          */
#define TIM7_BASE_ADDR    0x40001400UL   /*!< 大容量器件专有          */
#define USART2_BASE_ADDR  0x40004400UL
#define USART3_BASE_ADDR  0x40004800UL
#define UART4_BASE_ADDR   0x40004C00UL   /*!< 大容量器件专有          */
#define UART5_BASE_ADDR   0x40005000UL   /*!< 大容量器件专有          */
#define DAC_BASE_ADDR     0x40007400UL
#define DMA1_BASE_ADDR    0x40020000UL
#define DMA2_BASE_ADDR    0x40020400UL   /*!< 大容量器件专有          */

#define FLASH_R           ((FLASH_TypeDef *)FLASH_BASE_ADDR)
#define RCC               ((RCC_TypeDef   *)RCC_BASE_ADDR)
#define AFIO              ((AFIO_TypeDef  *)AFIO_BASE_ADDR)
#define GPIOA             ((GPIO_TypeDef  *)GPIOA_BASE_ADDR)
#define GPIOB             ((GPIO_TypeDef  *)GPIOB_BASE_ADDR)
#define GPIOC             ((GPIO_TypeDef  *)GPIOC_BASE_ADDR)
#define GPIOD             ((GPIO_TypeDef  *)GPIOD_BASE_ADDR)
#define GPIOE             ((GPIO_TypeDef  *)GPIOE_BASE_ADDR)
#define GPIOF             ((GPIO_TypeDef  *)GPIOF_BASE_ADDR)
#define GPIOG             ((GPIO_TypeDef  *)GPIOG_BASE_ADDR)
#define ADC1              ((ADC_TypeDef   *)ADC1_BASE_ADDR)
#define ADC2              ((ADC_TypeDef   *)ADC2_BASE_ADDR)
#define ADC3              ((ADC_TypeDef   *)ADC3_BASE_ADDR)
#define TIM1              ((TIM_TypeDef   *)TIM1_BASE_ADDR)
#define TIM2              ((TIM_TypeDef   *)TIM2_BASE_ADDR)
#define TIM3              ((TIM_TypeDef   *)TIM3_BASE_ADDR)
#define TIM4              ((TIM_TypeDef   *)TIM4_BASE_ADDR)
#define TIM5              ((TIM_TypeDef   *)TIM5_BASE_ADDR)
#define TIM6              ((TIM_TypeDef   *)TIM6_BASE_ADDR)
#define TIM7              ((TIM_TypeDef   *)TIM7_BASE_ADDR)
#define TIM8              ((TIM_TypeDef   *)TIM8_BASE_ADDR)
#define USART1            ((USART_TypeDef *)USART1_BASE_ADDR)
#define USART2            ((USART_TypeDef *)USART2_BASE_ADDR)
#define USART3            ((USART_TypeDef *)USART3_BASE_ADDR)
#define UART4             ((USART_TypeDef *)UART4_BASE_ADDR)
#define UART5             ((USART_TypeDef *)UART5_BASE_ADDR)
#define DAC               ((DAC_TypeDef   *)DAC_BASE_ADDR)
#define DMA1              ((DMA_TypeDef   *)DMA1_BASE_ADDR)
#define DMA2              ((DMA_TypeDef   *)DMA2_BASE_ADDR)

/* DMA1 通道 1-7, 通道 1 起始偏移 0x08, 步长 0x14 */
#define DMA1_CHANNEL(n)   ((DMA_Channel_TypeDef *)(DMA1_BASE_ADDR + 0x08UL + 0x14UL * ((n) - 1U)))
#define DMA1_CH1          DMA1_CHANNEL(1)
#define DMA1_CH4          DMA1_CHANNEL(4)
#define DMA1_CH5          DMA1_CHANNEL(5)

/* DMA2 通道 1-5(大容量器件), 偏移规则与 DMA1 相同 */
#define DMA2_CHANNEL(n)   ((DMA_Channel_TypeDef *)(DMA2_BASE_ADDR + 0x08UL + 0x14UL * ((n) - 1U)))

#define SYSTICK           ((SysTick_TypeDef *)0xE000E010UL)
#define NVIC              ((NVIC_TypeDef    *)0xE000E100UL)
#define SCB               ((SCB_TypeDef     *)0xE000ED00UL)

/* ========================================================================== */
/* 位定义(仅列出本工程使用的位)                                               */
/* ========================================================================== */

/* --- RCC --- */
#define RCC_CR_HSION            (1UL << 0)
#define RCC_CR_HSIRDY           (1UL << 1)
#define RCC_CR_HSEON            (1UL << 16)
#define RCC_CR_HSERDY           (1UL << 17)
#define RCC_CR_PLLON            (1UL << 24)
#define RCC_CR_PLLRDY           (1UL << 25)

#define RCC_CFGR_SW_HSI         (0UL << 0)
#define RCC_CFGR_SW_PLL         (2UL << 0)
#define RCC_CFGR_SW_MASK        (3UL << 0)
#define RCC_CFGR_SWS_MASK       (3UL << 2)
#define RCC_CFGR_SWS_PLL        (2UL << 2)
#define RCC_CFGR_HPRE_DIV1      (0UL << 4)
#define RCC_CFGR_PPRE1_DIV2     (4UL << 8)
#define RCC_CFGR_PPRE2_DIV1     (0UL << 11)
#define RCC_CFGR_ADCPRE_DIV6    (2UL << 14)
#define RCC_CFGR_PLLSRC_HSE     (1UL << 16)
#define RCC_CFGR_PLLXTPRE_DIV1  (0UL << 17)
#define RCC_CFGR_PLLMULL9       (7UL << 18)

#define RCC_APB2ENR_AFIOEN      (1UL << 0)
#define RCC_APB2ENR_IOPAEN      (1UL << 2)
#define RCC_APB2ENR_IOPBEN      (1UL << 3)
#define RCC_APB2ENR_IOPCEN      (1UL << 4)
#define RCC_APB2ENR_IOPDEN      (1UL << 5)
#define RCC_APB2ENR_IOPEEN      (1UL << 6)
#define RCC_APB2ENR_IOPFEN      (1UL << 7)
#define RCC_APB2ENR_IOPGEN      (1UL << 8)
#define RCC_APB2ENR_ADC1EN      (1UL << 9)
#define RCC_APB2ENR_ADC2EN      (1UL << 10)
#define RCC_APB2ENR_TIM1EN      (1UL << 11)
#define RCC_APB2ENR_SPI1EN      (1UL << 12)
#define RCC_APB2ENR_TIM8EN      (1UL << 13)
#define RCC_APB2ENR_USART1EN    (1UL << 14)
#define RCC_APB2ENR_ADC3EN      (1UL << 15)

#define RCC_APB1ENR_TIM2EN      (1UL << 0)
#define RCC_APB1ENR_TIM3EN      (1UL << 1)
#define RCC_APB1ENR_TIM4EN      (1UL << 2)
#define RCC_APB1ENR_TIM5EN      (1UL << 3)
#define RCC_APB1ENR_TIM6EN      (1UL << 4)
#define RCC_APB1ENR_TIM7EN      (1UL << 5)
#define RCC_APB1ENR_USART2EN    (1UL << 17)
#define RCC_APB1ENR_USART3EN    (1UL << 18)
#define RCC_APB1ENR_UART4EN     (1UL << 19)
#define RCC_APB1ENR_UART5EN     (1UL << 20)
#define RCC_APB1ENR_DACEN       (1UL << 29)

#define RCC_AHBENR_DMA1EN       (1UL << 0)
#define RCC_AHBENR_DMA2EN       (1UL << 1)

#define RCC_APB2RSTR_ADC1RST    (1UL << 9)

/* --- FLASH ACR --- */
#define FLASH_ACR_LATENCY_2     (2UL << 0)
#define FLASH_ACR_PRFTBE        (1UL << 4)

/* --- AFIO MAPR --- */
#define AFIO_MAPR_SWJ_CFG_MASK       (7UL << 24)
#define AFIO_MAPR_SWJ_CFG_JTAGDISABLE (2UL << 24)  /*!< 关闭 JTAG 保留 SWD, 释放 PB3/PB4 */

/* --- USART --- */
#define USART_SR_PE             (1UL << 0)
#define USART_SR_FE             (1UL << 1)
#define USART_SR_NE             (1UL << 2)
#define USART_SR_ORE            (1UL << 3)
#define USART_SR_IDLE           (1UL << 4)
#define USART_SR_RXNE           (1UL << 5)
#define USART_SR_TC             (1UL << 6)
#define USART_SR_TXE            (1UL << 7)

#define USART_CR1_RE            (1UL << 2)
#define USART_CR1_TE            (1UL << 3)
#define USART_CR1_IDLEIE        (1UL << 4)
#define USART_CR1_RXNEIE        (1UL << 5)
#define USART_CR1_TCIE          (1UL << 6)
#define USART_CR1_UE            (1UL << 13)

#define USART_CR3_DMAR          (1UL << 6)
#define USART_CR3_DMAT          (1UL << 7)

/* --- DMA CCR --- */
#define DMA_CCR_EN              (1UL << 0)
#define DMA_CCR_TCIE            (1UL << 1)
#define DMA_CCR_HTIE            (1UL << 2)
#define DMA_CCR_TEIE            (1UL << 3)
#define DMA_CCR_DIR_M2P         (1UL << 4)
#define DMA_CCR_CIRC            (1UL << 5)
#define DMA_CCR_PINC            (1UL << 6)
#define DMA_CCR_MINC            (1UL << 7)
#define DMA_CCR_PSIZE_16        (1UL << 8)
#define DMA_CCR_MSIZE_16        (1UL << 10)
#define DMA_CCR_PL_HIGH         (2UL << 12)

/* DMA1 ISR/IFCR: 每通道 4 位, 通道 n 的 GIF 位于 bit (4*(n-1)) */
#define DMA_FLAG_GIF(n)         (1UL << (4U * ((n) - 1U)))
#define DMA_FLAG_TCIF(n)        (1UL << (4U * ((n) - 1U) + 1U))
#define DMA_FLAG_HTIF(n)        (1UL << (4U * ((n) - 1U) + 2U))
#define DMA_FLAG_TEIF(n)        (1UL << (4U * ((n) - 1U) + 3U))

/* --- TIM --- */
#define TIM_CR1_CEN             (1UL << 0)
#define TIM_CR1_URS             (1UL << 2)
#define TIM_CR1_ARPE            (1UL << 7)
#define TIM_DIER_UIE            (1UL << 0)
#define TIM_SR_UIF              (1UL << 0)
#define TIM_EGR_UG              (1UL << 0)
#define TIM_CCMR_OCM_PWM1_CH1   (6UL << 4)   /*!< CCMR1 OC1M = 110 */
#define TIM_CCMR_OCPE_CH1       (1UL << 3)   /*!< CCMR1 OC1PE      */
#define TIM_CCMR_OCM_PWM1_CH2   (6UL << 12)  /*!< CCMR1 OC2M = 110 */
#define TIM_CCMR_OCPE_CH2       (1UL << 11)  /*!< CCMR1 OC2PE      */
#define TIM_CCER_CC1E           (1UL << 0)
#define TIM_CCER_CC2E           (1UL << 4)
#define TIM_BDTR_MOE            (1UL << 15)

/* --- ADC --- */
#define ADC_SR_EOC              (1UL << 1)
#define ADC_CR1_SCAN            (1UL << 8)
#define ADC_CR2_ADON            (1UL << 0)
#define ADC_CR2_DMA             (1UL << 8)
#define ADC_CR2_EXTSEL_SWSTART  (7UL << 17)
#define ADC_CR2_EXTTRIG         (1UL << 20)
#define ADC_CR2_SWSTART         (1UL << 22)
#define ADC_CR2_TSVREFE         (1UL << 23)
#define ADC_CR2_RSTCAL          (1UL << 3)
#define ADC_CR2_CAL             (1UL << 2)

/* --- DAC --- */
#define DAC_CR_EN1              (1UL << 0)
#define DAC_CR_BOFF1            (1UL << 1)
#define DAC_CR_EN2              (1UL << 16)
#define DAC_CR_BOFF2            (1UL << 17)

/* --- SysTick --- */
#define SYSTICK_CTRL_ENABLE     (1UL << 0)
#define SYSTICK_CTRL_TICKINT    (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE  (1UL << 2)

/* ========================================================================== */
/* 中断号                                                                     */
/* ========================================================================== */

typedef enum {
    DMA1_Channel1_IRQn     = 11,
    DMA1_Channel4_IRQn     = 14,
    DMA1_Channel5_IRQn     = 15,
    ADC1_2_IRQn            = 18,
    TIM2_IRQn              = 28,
    TIM3_IRQn              = 29,
    TIM4_IRQn              = 30,
    USART1_IRQn            = 37,
    USART2_IRQn            = 38,
    USART3_IRQn            = 39,
    /* --- 以下为大容量器件专有中断 --- */
    ADC3_IRQn              = 47,
    TIM5_IRQn              = 50,
    UART4_IRQn             = 52,
    UART5_IRQn             = 53,
    TIM6_IRQn              = 54,
    TIM7_IRQn              = 55,
    DMA2_Channel1_IRQn     = 56,
    DMA2_Channel2_IRQn     = 57,
    DMA2_Channel3_IRQn     = 58,
    DMA2_Channel4_5_IRQn   = 59
} IRQn_Type;

/* ========================================================================== */
/* 内核辅助函数                                                               */
/* ========================================================================== */

/** @brief 使能指定外部中断 */
static inline void nvic_enable_irq(IRQn_Type irqn)
{
    NVIC->ISER[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
}

/** @brief 关闭指定外部中断 */
static inline void nvic_disable_irq(IRQn_Type irqn)
{
    NVIC->ICER[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
}

/** @brief 设置中断优先级(0-15, 数值越小优先级越高) */
static inline void nvic_set_priority(IRQn_Type irqn, uint32_t priority)
{
    NVIC->IP[(uint32_t)irqn] = (uint8_t)((priority & 0x0FU) << 4U);
}

static inline void __enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }

/** @brief 读取 PRIMASK(1 表示当前已关中断) */
static inline uint32_t __get_primask(void)
{
    uint32_t result;
    __asm volatile ("mrs %0, primask" : "=r" (result) :: "memory");
    return result;
}

/** @brief 恢复 PRIMASK */
static inline void __set_primask(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

static inline void __dsb(void)         { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void __nop(void)         { __asm volatile ("nop"); }
static inline void __wfi(void)         { __asm volatile ("wfi"); }

#ifdef __cplusplus
}
#endif

#endif /* STM32F103_REG_H */
