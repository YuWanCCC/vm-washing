/**
  ******************************************************************************
  * @file    main.h
  * @brief   公共类型/系统时基/全局声明
  ******************************************************************************
  */

#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "stm32f103_reg.h"
#include "config.h"
#include "pin_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 当前系统内核频率(Hz), 由 SystemCoreClockUpdate() 维护 */
extern volatile uint32_t SystemCoreClock;

/** @brief 启动文件在跳转 main() 前调用, 完成最小系统初始化 */
void SystemInit(void);

/** @brief 依据 RCC 寄存器实际配置重新计算并刷新 SystemCoreClock */
void SystemCoreClockUpdate(void);

/* ========================================================================== */
/* 系统时基 (SysTick, 1ms)                                                    */
/* ========================================================================== */

/** @brief 初始化 1ms 系统滴答 */
void systick_init(void);

/** @brief 获取自复位以来的毫秒数(约 49.7 天回绕) */
uint32_t sys_get_tick(void);

/** @brief 计算距离 start 时刻经过的毫秒数(回绕安全) */
uint32_t sys_elapsed(uint32_t start);

/** @brief 毫秒级忙等延时 */
void sys_delay_ms(uint32_t ms);

/** @brief SysTick 中断内部调用, 递增毫秒计数 */
void sys_tick_inc(void);

/* ========================================================================== */
/* 周期性采集 tick (TIM3, CFG_TICK_FREQ_HZ)                                   */
/* ========================================================================== */

/** @brief 中断内置位采集标志 */
void app_set_sample_tick(void);

/** @brief 致命错误处理: 关中断并快闪 LED */
void error_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
