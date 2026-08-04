/**
  ******************************************************************************
  * @file    adc_sampler.h
  * @brief   ADC1 四通道扫描采集(软件触发 + DMA + 过采样平均)
  ******************************************************************************
  */

#ifndef ADC_SAMPLER_H
#define ADC_SAMPLER_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 逻辑 ADC 通道索引(与上报顺序一致) */
typedef enum {
    ADC_IDX_MOTOR_PWM = 0,  /*!< PA0 ADC_IN0 电机 PWM 占空比               */
    ADC_IDX_SPARE1    = 1,  /*!< PA2 ADC_IN2 备用                          */
    ADC_IDX_SPARE2    = 2,  /*!< PA3 ADC_IN3 备用                          */
    ADC_IDX_SPARE3    = 3,  /*!< PA6 ADC_IN6 备用                          */
    ADC_IDX_COUNT     = 4
} adc_index_t;

/**
 * @brief  初始化 ADC1 与 DMA1_Channel1
 * @note   规则组序列长度 = 4 通道 x CFG_ADC_OVERSAMPLE 次, 一次软件触发完成
 *         全部转换, DMA 搬运到内部缓冲, 由 adc_poll() 求平均
 */
void adc_init(void);

/** @brief 软件触发启动一轮扫描(非阻塞) */
void adc_start_sample(void);

/**
 * @brief  检查扫描是否完成并更新滤波结果
 * @return true 表示本次调用更新了采样值
 */
bool adc_poll(void);

/** @brief 阻塞采集一轮(初始化自检/调试用), 超时返回 false */
bool adc_sample_blocking(void);

/**
 * @brief  获取指定逻辑通道的 12 位滤波值
 * @param  index ADC_IDX_xxx
 */
uint16_t adc_get_value(adc_index_t index);

/**
 * @brief  获取所有通道滤波值
 * @param  values 输出数组, 至少 ADC_IDX_COUNT 个元素
 */
void adc_get_all_values(uint16_t *values);

/** @brief 获取指定通道电压(mV) */
uint16_t adc_get_voltage_mv(adc_index_t index);

/**
 * @brief  把电机 PWM 采样值折算为占空比
 * @return 占空比, 单位 0.1% (0-1000)
 * @note   PA0 经 RC 低通滤波后的平均电压正比于占空比
 */
uint16_t adc_get_motor_duty_permille(void);

/** @brief 累计完成的扫描轮数(用于诊断) */
uint32_t adc_get_sample_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_SAMPLER_H */
