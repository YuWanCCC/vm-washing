/**
  ******************************************************************************
  * @file    system_stm32f1xx.c
  * @brief   最小系统初始化(替代 CMSIS 版本)
  *
  * SystemInit() 由启动文件在跳转 main 之前调用, 只把时钟系统恢复到确定的
  * 复位状态, 真正的 72MHz 配置由 main.c 的 SystemClock_Config() 完成。
  ******************************************************************************
  */

#include "main.h"

/** @brief 中断向量表在 FLASH 中的偏移(需与链接脚本一致) */
#define VECT_TAB_OFFSET     0x00000000UL

/** @brief 当前内核频率, 供延时/波特率等计算使用 */
volatile uint32_t SystemCoreClock = CFG_HSE_FREQ_HZ;

/**
 * @brief  复位时钟系统到已知状态
 * @note   使用 ISP/IAP 或从 bootloader 跳转时, RCC 可能残留上一次配置,
 *         因此这里显式回到 HSI 并关闭 PLL
 */
void SystemInit(void)
{
    /* 打开 HSI 并切回 HSI 作为系统时钟 */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW_MASK;

    /* 关闭 HSE/PLL 及其旁路配置 */
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_PLLON);
    RCC->CR &= ~(1UL << 18);    /* HSEBYP */

    /* 清零时钟配置与所有时钟中断标志 */
    RCC->CFGR = 0U;
    RCC->CIR  = 0x009F0000UL;

    /* 设置向量表位置 */
    SCB->VTOR = 0x08000000UL | (VECT_TAB_OFFSET & 0x1FFFFF80UL);

    SystemCoreClock = 8000000UL;   /* 此时运行在 HSI 8MHz */
}

/**
 * @brief  根据 RCC 寄存器回算当前内核频率
 * @note   仅支持本工程用到的 HSI / HSE / PLL(HSE 源) 三种情况
 */
void SystemCoreClockUpdate(void)
{
    uint32_t cfgr = RCC->CFGR;
    uint32_t sysclk;

    switch (cfgr & RCC_CFGR_SWS_MASK) {
    case 0U:    /* HSI */
        sysclk = 8000000UL;
        break;

    case (1UL << 2):    /* HSE */
        sysclk = CFG_HSE_FREQ_HZ;
        break;

    case RCC_CFGR_SWS_PLL: {
        uint32_t pllmul = ((cfgr >> 18U) & 0xFU) + 2U;   /* PLLMUL: 0->x2 ... */
        if (pllmul > 16U) {
            pllmul = 16U;
        }

        uint32_t pllsource;
        if ((cfgr & RCC_CFGR_PLLSRC_HSE) != 0U) {
            pllsource = CFG_HSE_FREQ_HZ;
            if ((cfgr & (1UL << 17U)) != 0U) {   /* PLLXTPRE: HSE/2 */
                pllsource /= 2U;
            }
        } else {
            pllsource = 8000000UL / 2U;          /* HSI/2 */
        }

        sysclk = pllsource * pllmul;
        break;
    }

    default:
        sysclk = 8000000UL;
        break;
    }

    /* AHB 预分频 */
    static const uint8_t ahb_shift[16] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U
    };

    SystemCoreClock = sysclk >> ahb_shift[(cfgr >> 4U) & 0xFU];
}
