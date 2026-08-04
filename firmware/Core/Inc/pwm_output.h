/**
  ******************************************************************************
  * @file    pwm_output.h
  * @brief   PWM 频率输出(模拟水位/Hall 传感器方波)
  ******************************************************************************
  */

#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief PWM 通道编号 */
typedef enum {
    PWM_CH_WATER_LEVEL = 0,  /*!< PA8  TIM1_CH1 水位传感器频率              */
    PWM_CH_HALL        = 1,  /*!< PA1  TIM2_CH2 Hall 传感器频率             */
    PWM_CH_COUNT       = 2
} pwm_channel_t;

/** @brief 初始化 TIM1_CH1 / TIM2_CH2 为 PWM 输出, 初始停止 */
void pwm_init(void);

/**
 * @brief  设置指定通道输出频率(50% 占空比)
 * @param  channel PWM 通道
 * @param  freq_hz 目标频率, 0 表示停止输出(引脚保持低电平)
 * @return true 设置成功; false 通道非法或频率超出 1Hz-100kHz
 * @note   freq = 72MHz / (PSC+1) / (ARR+1), 自动搜索最小 PSC 以获得最高分辨率
 */
bool pwm_set_frequency(pwm_channel_t channel, uint32_t freq_hz);

/**
 * @brief  设置占空比(默认 50%, 特殊测试场景可调)
 * @param  duty_permille 占空比, 单位 0.1% (0-1000)
 */
bool pwm_set_duty(pwm_channel_t channel, uint16_t duty_permille);

/** @brief 停止通道输出 */
void pwm_stop(pwm_channel_t channel);

/** @brief 停止所有通道输出 */
void pwm_stop_all(void);

/** @brief 读取通道当前实际输出频率(Hz), 0 表示停止 */
uint32_t pwm_get_frequency(pwm_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif /* PWM_OUTPUT_H */
