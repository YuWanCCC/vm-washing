/**
  ******************************************************************************
  * @file    pwm_output.c
  * @brief   PWM 频率输出实现
  *
  * 通道映射:
  *   PWM_CH_WATER_LEVEL -> TIM1_CH1 -> PA8  (水位传感器频率信号)
  *   PWM_CH_HALL        -> TIM2_CH2 -> PA1  (Hall 转速传感器频率信号)
  *
  * 频率公式: f = TIMCLK / ((PSC+1) * (ARR+1)), TIMCLK = 72MHz
  * 为保证占空比分辨率, 优先取最小 PSC, 使 ARR 尽量大。
  ******************************************************************************
  */

#include "pwm_output.h"
#include "main.h"

#define PWM_DEFAULT_DUTY_PERMILLE   500U    /*!< 默认 50% 占空比              */
#define PWM_TIMER_MAX_RELOAD        65536UL /*!< ARR+1 / PSC+1 的最大值       */

/** @brief 单个 PWM 通道的运行上下文 */
typedef struct {
    TIM_TypeDef   *tim;        /*!< 所属定时器                              */
    __IO uint32_t *ccr;        /*!< 比较寄存器                              */
    uint32_t       reload;     /*!< 当前 ARR+1                              */
    uint32_t       freq_hz;    /*!< 当前实际频率, 0 表示停止                */
    uint16_t       duty;       /*!< 占空比, 0.1% 单位                       */
} pwm_ctx_t;

static pwm_ctx_t g_pwm[PWM_CH_COUNT] = {
    { TIM1, &TIM1->CCR1, 0U, 0U, PWM_DEFAULT_DUTY_PERMILLE },
    { TIM2, &TIM2->CCR2, 0U, 0U, PWM_DEFAULT_DUTY_PERMILLE }
};

/**
 * @brief  根据当前 reload 与 duty 刷新比较值
 */
static void pwm_apply_duty(pwm_ctx_t *ctx)
{
    uint32_t ccr = (ctx->reload * (uint32_t)ctx->duty) / 1000U;

    /* 保证 ARR 有效时占空比不越界 */
    if (ccr >= ctx->reload) {
        ccr = ctx->reload - 1U;
    }

    *(ctx->ccr) = ccr;
}

/** @brief 初始化 TIM1_CH1 与 TIM2_CH2 的 PWM 输出 */
void pwm_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_TIM1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    gpio_pin_mode(PIN_PWM_WATER_PORT, PIN_PWM_WATER, GPIO_MODE_AF_PP_50M);
    gpio_pin_mode(PIN_PWM_HALL_PORT,  PIN_PWM_HALL,  GPIO_MODE_AF_PP_50M);

    /* --- TIM1_CH1: PWM 模式1 + 预装载, 高级定时器需额外使能 MOE --- */
    TIM1->CR1   = TIM_CR1_ARPE;
    TIM1->PSC   = 0U;
    TIM1->ARR   = 0U;
    TIM1->CCR1  = 0U;
    TIM1->CCMR1 = TIM_CCMR_OCM_PWM1_CH1 | TIM_CCMR_OCPE_CH1;
    TIM1->CCER  = TIM_CCER_CC1E;
    TIM1->BDTR  = TIM_BDTR_MOE;
    TIM1->EGR   = TIM_EGR_UG;

    /* --- TIM2_CH2: PWM 模式1 + 预装载 --- */
    TIM2->CR1   = TIM_CR1_ARPE;
    TIM2->PSC   = 0U;
    TIM2->ARR   = 0U;
    TIM2->CCR2  = 0U;
    TIM2->CCMR1 = TIM_CCMR_OCM_PWM1_CH2 | TIM_CCMR_OCPE_CH2;
    TIM2->CCER  = TIM_CCER_CC2E;
    TIM2->EGR   = TIM_EGR_UG;

    /* 初始为停止状态, 输出低电平 */
    pwm_stop_all();
}

/**
 * @brief  设置输出频率
 * @note   freq_hz = 0 时停止输出; 其余先算最小 PSC 再算 ARR, 写入后用 UG
 *         事件立即生效
 */
bool pwm_set_frequency(pwm_channel_t channel, uint32_t freq_hz)
{
    if ((uint32_t)channel >= (uint32_t)PWM_CH_COUNT) {
        return false;
    }

    if (freq_hz == 0U) {
        pwm_stop(channel);
        return true;
    }

    if ((freq_hz < CFG_PWM_FREQ_MIN_HZ) || (freq_hz > CFG_PWM_FREQ_MAX_HZ)) {
        return false;
    }

    pwm_ctx_t *ctx = &g_pwm[channel];

    /* 总分频比 = TIMCLK / freq */
    uint32_t total = (CFG_TIMCLK_FREQ_HZ + (freq_hz / 2U)) / freq_hz;
    if (total == 0U) {
        total = 1U;
    }

    /* PSC+1 取满足 ARR+1 <= 65536 的最小值 */
    uint32_t psc_plus1 = (total + (PWM_TIMER_MAX_RELOAD - 1U)) / PWM_TIMER_MAX_RELOAD;
    if (psc_plus1 == 0U) {
        psc_plus1 = 1U;
    }
    if (psc_plus1 > PWM_TIMER_MAX_RELOAD) {
        return false;   /* 频率过低, 无法实现 */
    }

    uint32_t reload = total / psc_plus1;
    if (reload < 2U) {
        reload = 2U;    /* 至少 2 个计数才能形成方波 */
    }
    if (reload > PWM_TIMER_MAX_RELOAD) {
        reload = PWM_TIMER_MAX_RELOAD;
    }

    ctx->reload  = reload;
    ctx->freq_hz = CFG_TIMCLK_FREQ_HZ / (psc_plus1 * reload);

    ctx->tim->PSC = (uint16_t)(psc_plus1 - 1U);
    ctx->tim->ARR = (uint16_t)(reload - 1U);
    pwm_apply_duty(ctx);

    /* 产生更新事件, 立即装载 PSC/ARR/CCR, 然后启动计数 */
    ctx->tim->EGR  = TIM_EGR_UG;
    ctx->tim->SR  &= ~TIM_SR_UIF;
    ctx->tim->CR1 |= TIM_CR1_CEN;

    return true;
}

/** @brief 设置占空比(0.1% 单位) */
bool pwm_set_duty(pwm_channel_t channel, uint16_t duty_permille)
{
    if (((uint32_t)channel >= (uint32_t)PWM_CH_COUNT) || (duty_permille > 1000U)) {
        return false;
    }

    pwm_ctx_t *ctx = &g_pwm[channel];
    ctx->duty = duty_permille;

    if (ctx->reload != 0U) {
        pwm_apply_duty(ctx);
    }

    return true;
}

/**
 * @brief  停止指定通道
 * @note   比较值清 0 后 PWM 模式1 输出恒为低电平, 再停止计数器保持该电平
 */
void pwm_stop(pwm_channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)PWM_CH_COUNT) {
        return;
    }

    pwm_ctx_t *ctx = &g_pwm[channel];

    *(ctx->ccr)    = 0U;
    ctx->tim->EGR  = TIM_EGR_UG;
    ctx->tim->CR1 &= ~TIM_CR1_CEN;
    ctx->tim->CNT  = 0U;

    ctx->freq_hz = 0U;
    ctx->reload  = 0U;
}

/** @brief 停止所有通道 */
void pwm_stop_all(void)
{
    for (uint32_t i = 0U; i < (uint32_t)PWM_CH_COUNT; i++) {
        pwm_stop((pwm_channel_t)i);
    }
}

/** @brief 读取通道实际输出频率 */
uint32_t pwm_get_frequency(pwm_channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)PWM_CH_COUNT) {
        return 0U;
    }

    return g_pwm[channel].freq_hz;
}
