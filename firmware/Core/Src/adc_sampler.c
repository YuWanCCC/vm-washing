/**
  ******************************************************************************
  * @file    adc_sampler.c
  * @brief   ADC1 四通道扫描采集实现
  *
  * 采集方式: 规则组序列长度 = 4 通道 x 4 次过采样 = 16 次转换。
  * 一次软件触发(SWSTART)完成整个序列, DMA1_Channel1 循环搬运到内部缓冲,
  * adc_poll() 检测传输完成后按通道求算术平均, 抑制随机噪声。
  *
  * 序列排布: [CH0,CH2,CH3,CH6] x 4, 同一通道的 4 次采样在时间上分散,
  * 相比连续 4 次采样对周期性干扰的抑制效果更好。
  ******************************************************************************
  */

#include "adc_sampler.h"
#include "main.h"

/** @brief 序列总长度 */
#define ADC_SEQ_LENGTH      (ADC_IDX_COUNT * CFG_ADC_OVERSAMPLE)

/* 规则组序列最多 16 次转换, 超出则无法用单次触发完成整轮过采样 */
#if ((CFG_ADC_CHANNEL_COUNT * CFG_ADC_OVERSAMPLE) > 16)
#error "CFG_ADC_CHANNEL_COUNT * CFG_ADC_OVERSAMPLE 不得超过 16"
#endif

/** @brief 阻塞采集超时循环次数(约数十微秒量级, 留足余量) */
#define ADC_TIMEOUT_LOOPS   1000000UL

/** @brief 逻辑索引 -> 物理 ADC 通道号 */
static const uint8_t g_adc_channels[ADC_IDX_COUNT] = {
    ADC_CH_MOTOR_PWM,
    ADC_CH_SPARE1,
    ADC_CH_SPARE2,
    ADC_CH_SPARE3
};

static volatile uint16_t g_adc_raw[ADC_SEQ_LENGTH];  /*!< DMA 目标缓冲       */
static uint16_t g_adc_value[ADC_IDX_COUNT];          /*!< 平均后的采样值     */
static uint32_t g_sample_count;                      /*!< 完成的扫描轮数     */
static bool     g_busy;                              /*!< 本轮转换进行中     */

/**
 * @brief  短延时(ADC 上电稳定/校准等待)
 */
static void adc_delay_cycles(uint32_t cycles)
{
    while (cycles-- != 0U) {
        __nop();
    }
}

/**
 * @brief  配置规则组序列: [CH0,CH2,CH3,CH6] 重复 CFG_ADC_OVERSAMPLE 次
 */
static void adc_config_sequence(void)
{
    uint32_t sqr1 = 0U;
    uint32_t sqr2 = 0U;
    uint32_t sqr3 = 0U;

    for (uint32_t i = 0U; i < ADC_SEQ_LENGTH; i++) {
        uint32_t channel = g_adc_channels[i % ADC_IDX_COUNT];

        if (i < 6U) {                 /* SQ1-SQ6   -> SQR3 */
            sqr3 |= channel << (5U * i);
        } else if (i < 12U) {         /* SQ7-SQ12  -> SQR2 */
            sqr2 |= channel << (5U * (i - 6U));
        } else {                      /* SQ13-SQ16 -> SQR1 */
            sqr1 |= channel << (5U * (i - 12U));
        }
    }

    /* L[3:0] = 转换次数 - 1 */
    sqr1 |= (uint32_t)(ADC_SEQ_LENGTH - 1U) << 20U;

    ADC1->SQR1 = sqr1;
    ADC1->SQR2 = sqr2;
    ADC1->SQR3 = sqr3;
}

/**
 * @brief  配置 DMA1_Channel1 从 ADC1->DR 搬运到 g_adc_raw
 */
static void adc_config_dma(void)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    DMA1_CH1->CCR   = 0U;                       /* 先关闭通道再配置        */
    DMA1_CH1->CPAR  = (uint32_t)&ADC1->DR;
    DMA1_CH1->CMAR  = (uint32_t)g_adc_raw;
    DMA1_CH1->CNDTR = ADC_SEQ_LENGTH;
    DMA1_CH1->CCR   = DMA_CCR_MINC | DMA_CCR_CIRC |
                      DMA_CCR_PSIZE_16 | DMA_CCR_MSIZE_16 |
                      DMA_CCR_PL_HIGH;

    DMA1->IFCR = DMA_FLAG_GIF(1);
    DMA1_CH1->CCR |= DMA_CCR_EN;
}

/** @brief 初始化 ADC1(12MHz 时钟) 与采集 DMA */
void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;

    /* 模拟输入引脚: 关闭数字输入与上下拉 */
    gpio_pin_mode(PIN_ADC_PORT, PIN_ADC_MOTOR_PWM, GPIO_MODE_IN_ANALOG);
    gpio_pin_mode(PIN_ADC_PORT, PIN_ADC_SPARE1,    GPIO_MODE_IN_ANALOG);
    gpio_pin_mode(PIN_ADC_PORT, PIN_ADC_SPARE2,    GPIO_MODE_IN_ANALOG);
    gpio_pin_mode(PIN_ADC_PORT, PIN_ADC_SPARE3,    GPIO_MODE_IN_ANALOG);

    /* 复位 ADC1 到确定状态 */
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1RST;

    /* 采样时间: 4 路全部取 55.5 周期(0b101), 兼顾速度与源阻抗匹配 */
    uint32_t smpr2 = 0U;
    for (uint32_t i = 0U; i < ADC_IDX_COUNT; i++) {
        smpr2 |= 0x5UL << (3U * g_adc_channels[i]);
    }
    ADC1->SMPR2 = smpr2;
    ADC1->SMPR1 = 0U;

    ADC1->CR1 = ADC_CR1_SCAN;               /* 扫描模式, 单次转换整个序列 */
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_EXTSEL_SWSTART | ADC_CR2_EXTTRIG;

    adc_config_sequence();
    adc_config_dma();

    /* 上电并等待稳定(>=1us) */
    ADC1->CR2 |= ADC_CR2_ADON;
    adc_delay_cycles(1000U);

    /* 复位校准 -> 校准, 两步均需等待硬件自动清零 */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {
    }
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }

    for (uint32_t i = 0U; i < ADC_IDX_COUNT; i++) {
        g_adc_value[i] = 0U;
    }
    g_sample_count = 0U;
    g_busy = false;
}

/** @brief 软件触发一轮扫描 */
void adc_start_sample(void)
{
    if (g_busy) {
        return;     /* 上一轮未完成, 避免 DMA 写指针错位 */
    }

    DMA1->IFCR = DMA_FLAG_GIF(1);
    g_busy = true;
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

/**
 * @brief  检查扫描完成并更新滤波值
 * @return true 表示已更新
 */
bool adc_poll(void)
{
    if (!g_busy) {
        return false;
    }

    if ((DMA1->ISR & DMA_FLAG_TCIF(1)) == 0U) {
        return false;
    }

    DMA1->IFCR = DMA_FLAG_GIF(1);

    /* 按通道累加同一逻辑索引的 CFG_ADC_OVERSAMPLE 个采样点 */
    for (uint32_t ch = 0U; ch < ADC_IDX_COUNT; ch++) {
        uint32_t sum = 0U;
        for (uint32_t n = 0U; n < CFG_ADC_OVERSAMPLE; n++) {
            sum += g_adc_raw[(n * ADC_IDX_COUNT) + ch];
        }
        g_adc_value[ch] = (uint16_t)(sum / CFG_ADC_OVERSAMPLE);
    }

    g_sample_count++;
    g_busy = false;

    return true;
}

/** @brief 阻塞采集一轮 */
bool adc_sample_blocking(void)
{
    g_busy = false;
    adc_start_sample();

    for (uint32_t i = 0U; i < ADC_TIMEOUT_LOOPS; i++) {
        if (adc_poll()) {
            return true;
        }
    }

    g_busy = false;
    return false;
}

/** @brief 获取指定通道滤波值 */
uint16_t adc_get_value(adc_index_t index)
{
    if ((uint32_t)index >= (uint32_t)ADC_IDX_COUNT) {
        return 0U;
    }

    return g_adc_value[index];
}

/** @brief 获取所有通道滤波值 */
void adc_get_all_values(uint16_t *values)
{
    if (values == NULL) {
        return;
    }

    for (uint32_t i = 0U; i < ADC_IDX_COUNT; i++) {
        values[i] = g_adc_value[i];
    }
}

/** @brief 采样值转电压(mV) */
uint16_t adc_get_voltage_mv(adc_index_t index)
{
    uint32_t value = adc_get_value(index);

    return (uint16_t)((value * CFG_ADC_VREF_MV) / CFG_ADC_FULL_SCALE);
}

/** @brief 电机 PWM 采样值折算为占空比(0.1%) */
uint16_t adc_get_motor_duty_permille(void)
{
    uint32_t value = adc_get_value(ADC_IDX_MOTOR_PWM);
    uint32_t duty  = (value * 1000U) / CFG_ADC_FULL_SCALE;

    if (duty > 1000U) {
        duty = 1000U;
    }

    return (uint16_t)duty;
}

/** @brief 累计扫描轮数 */
uint32_t adc_get_sample_count(void)
{
    return g_sample_count;
}
