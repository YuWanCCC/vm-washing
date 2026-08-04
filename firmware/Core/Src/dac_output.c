/**
  ******************************************************************************
  * @file    dac_output.c
  * @brief   DAC 模拟量输出实现
  *
  * DAC 通道1 -> PA4 (NTC 温度传感器等效电压)
  * DAC 通道2 -> PA5 (备用模拟量)
  * 未使能触发时, 写入 DHR12Rx 后 1 个 APB1 周期即刷新到 DOR 输出。
  ******************************************************************************
  */

#include "dac_output.h"
#include "main.h"

static uint16_t g_dac_value[DAC_CH_COUNT];   /*!< 各通道当前设定值影子       */

/** @brief 初始化 DAC 双通道 */
void dac_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;

    /* DAC 输出引脚必须配置为模拟输入模式(关闭数字输入避免额外功耗) */
    gpio_pin_mode(PIN_DAC1_PORT, PIN_DAC1, GPIO_MODE_IN_ANALOG);
    gpio_pin_mode(PIN_DAC2_PORT, PIN_DAC2, GPIO_MODE_IN_ANALOG);

    /* 先写 0 再使能, 保证上电输出 0V; BOFFx = 0 表示使能输出缓冲 */
    DAC->DHR12R1 = 0U;
    DAC->DHR12R2 = 0U;
    DAC->CR = DAC_CR_EN1 | DAC_CR_EN2;

    g_dac_value[DAC_CH_NTC]   = 0U;
    g_dac_value[DAC_CH_SPARE] = 0U;
}

/** @brief 设置 DAC 原始值(12 位) */
bool dac_set_value(dac_channel_t channel, uint16_t value_12bit)
{
    if ((uint32_t)channel >= (uint32_t)DAC_CH_COUNT) {
        return false;
    }

    if (value_12bit > CFG_DAC_MAX_VALUE) {
        value_12bit = CFG_DAC_MAX_VALUE;
    }

    if (channel == DAC_CH_NTC) {
        DAC->DHR12R1 = value_12bit;
    } else {
        DAC->DHR12R2 = value_12bit;
    }

    g_dac_value[channel] = value_12bit;
    return true;
}

/**
 * @brief  按电压设置 DAC 输出
 * @note   value = mv * 4095 / 3300, 四舍五入
 */
bool dac_set_voltage(dac_channel_t channel, uint16_t voltage_mv)
{
    if (voltage_mv > CFG_ADC_VREF_MV) {
        voltage_mv = CFG_ADC_VREF_MV;
    }

    uint32_t value = ((uint32_t)voltage_mv * CFG_DAC_MAX_VALUE + (CFG_ADC_VREF_MV / 2U))
                     / CFG_ADC_VREF_MV;

    return dac_set_value(channel, (uint16_t)value);
}

/** @brief 读取通道当前设定值 */
uint16_t dac_get_value(dac_channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)DAC_CH_COUNT) {
        return 0U;
    }

    return g_dac_value[channel];
}

/** @brief 读取通道当前设定电压(mV) */
uint16_t dac_get_voltage(dac_channel_t channel)
{
    uint32_t value = dac_get_value(channel);

    return (uint16_t)((value * CFG_ADC_VREF_MV) / CFG_DAC_MAX_VALUE);
}

/** @brief 所有通道归零 */
void dac_reset_all(void)
{
    DAC->DHR12R1 = 0U;
    DAC->DHR12R2 = 0U;
    g_dac_value[DAC_CH_NTC]   = 0U;
    g_dac_value[DAC_CH_SPARE] = 0U;
}
