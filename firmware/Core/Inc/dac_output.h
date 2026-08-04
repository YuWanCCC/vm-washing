/**
  ******************************************************************************
  * @file    dac_output.h
  * @brief   DAC 模拟量输出(NTC 阻值模拟等)
  ******************************************************************************
  */

#ifndef DAC_OUTPUT_H
#define DAC_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief DAC 通道编号 */
typedef enum {
    DAC_CH_NTC   = 0,   /*!< PA4 DAC_OUT1 NTC 阻值模拟                      */
    DAC_CH_SPARE = 1,   /*!< PA5 DAC_OUT2 备用模拟量                        */
    DAC_CH_COUNT = 2
} dac_channel_t;

/** @brief 初始化 DAC 双通道, 输出缓冲使能, 初值 0 */
void dac_init(void);

/**
 * @brief  设置 DAC 输出原始值
 * @param  channel     DAC 通道
 * @param  value_12bit 0-4095, 超出则被裁剪
 * @return true 设置成功; false 通道非法
 */
bool dac_set_value(dac_channel_t channel, uint16_t value_12bit);

/**
 * @brief  按电压设置 DAC 输出
 * @param  voltage_mv 目标电压, 单位 mV, 0-3300
 */
bool dac_set_voltage(dac_channel_t channel, uint16_t voltage_mv);

/** @brief 读取通道当前设定的原始值 */
uint16_t dac_get_value(dac_channel_t channel);

/** @brief 读取通道当前设定电压(mV) */
uint16_t dac_get_voltage(dac_channel_t channel);

/** @brief 所有通道输出归零 */
void dac_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DAC_OUTPUT_H */
