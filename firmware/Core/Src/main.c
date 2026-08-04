/**
  ******************************************************************************
  * @file    main.c
  * @brief   洗衣机 HIL 仿真板固件主程序
  *
  * 职责:
  *   1. 配置 72MHz 系统时钟并初始化各外设模块
  *   2. 主循环解析上位机命令(UART DMA + 帧状态机)
  *   3. 100Hz 周期采集 ADC/GPIO 并上报 REPORT_INPUT
  *   4. 500ms 心跳, 链路超时进入安全状态
  ******************************************************************************
  */

#include "main.h"

#include "adc_sampler.h"
#include "dac_output.h"
#include "gpio_driver.h"
#include "protocol_parser.h"
#include "pwm_output.h"
#include "uart_handler.h"

/** @brief 链路断开时 LED 快闪周期(ms) */
#define LED_FAST_BLINK_MS       100U

static volatile uint32_t g_sys_tick_ms;     /*!< SysTick 毫秒计数           */
static volatile bool     g_sample_tick;     /*!< TIM3 采集节拍标志          */
static bool              g_link_ok;         /*!< 上位机链路状态             */

/* ========================================================================== */
/* 系统时基                                                                   */
/* ========================================================================== */

/** @brief 初始化 1ms SysTick, 使用 HCLK 作为时钟源 */
void systick_init(void)
{
    g_sys_tick_ms = 0U;

    SYSTICK->LOAD = (CFG_HCLK_FREQ_HZ / 1000U) - 1U;
    SYSTICK->VAL  = 0U;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

/** @brief SysTick 中断内递增毫秒计数 */
void sys_tick_inc(void)
{
    g_sys_tick_ms++;
}

/** @brief 获取毫秒时间戳 */
uint32_t sys_get_tick(void)
{
    return g_sys_tick_ms;
}

/** @brief 计算经过的毫秒数, 无符号减法天然处理回绕 */
uint32_t sys_elapsed(uint32_t start)
{
    return (uint32_t)(g_sys_tick_ms - start);
}

/** @brief 毫秒忙等延时 */
void sys_delay_ms(uint32_t ms)
{
    uint32_t start = g_sys_tick_ms;

    while (sys_elapsed(start) < ms) {
    }
}

/** @brief TIM3 中断内置位采集节拍 */
void app_set_sample_tick(void)
{
    g_sample_tick = true;
}

/* ========================================================================== */
/* 时钟与定时器配置                                                           */
/* ========================================================================== */

/**
 * @brief  配置系统时钟到 72MHz
 * @note   HSE 8MHz -> PLL x9 -> SYSCLK 72MHz
 *         AHB /1 = 72MHz, APB1 /2 = 36MHz, APB2 /1 = 72MHz, ADC /6 = 12MHz
 *         72MHz 下 FLASH 需要 2 个等待周期
 */
static void SystemClock_Config(void)
{
    /* 1. 使能 HSE 并等待就绪 */
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t timeout = 0U; (RCC->CR & RCC_CR_HSERDY) == 0U; timeout++) {
        if (timeout > 0x00100000UL) {
            error_handler();    /* 晶振未起振 */
        }
    }

    /* 2. FLASH 预取 + 2 等待周期(必须在提高频率前设置) */
    FLASH_R->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* 3. 总线分频与 PLL 配置: PLLSRC = HSE, PLLMUL = x9 */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1 |
                RCC_CFGR_ADCPRE_DIV6 | RCC_CFGR_PLLSRC_HSE |
                RCC_CFGR_PLLXTPRE_DIV1 | RCC_CFGR_PLLMULL9;

    /* 4. 启动 PLL 并等待锁定 */
    RCC->CR |= RCC_CR_PLLON;
    for (uint32_t timeout = 0U; (RCC->CR & RCC_CR_PLLRDY) == 0U; timeout++) {
        if (timeout > 0x00100000UL) {
            error_handler();
        }
    }

    /* 5. 切换系统时钟到 PLL 并确认切换完成 */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {
    }

    SystemCoreClock = CFG_SYSCLK_FREQ_HZ;
}

/**
 * @brief  初始化 TIM3 产生 100Hz 采集节拍
 * @note   72MHz / 7200 = 10kHz 计数, 再 100 分频得到 100Hz
 */
static void tim3_tick_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    TIM3->CR1  = 0U;
    TIM3->PSC  = (uint16_t)((CFG_TIMCLK_FREQ_HZ / 10000UL) - 1U);
    TIM3->ARR  = (uint16_t)((10000UL / CFG_TICK_FREQ_HZ) - 1U);
    TIM3->EGR  = TIM_EGR_UG;
    TIM3->SR   = 0U;
    TIM3->DIER = TIM_DIER_UIE;

    nvic_set_priority(TIM3_IRQn, 4U);
    nvic_enable_irq(TIM3_IRQn);

    TIM3->CR1 |= TIM_CR1_CEN;
}

/* ========================================================================== */
/* 应用逻辑                                                                   */
/* ========================================================================== */

/**
 * @brief  链路状态维护
 * @note   超过 CFG_LINK_TIMEOUT_MS 未收到上位机帧则判定断连, 并把所有
 *         输出复位到安全状态, 防止被测主控收到冻结的传感器信号
 */
static void app_check_link(void)
{
    bool alive = (sys_elapsed(protocol_get_last_rx_tick()) < CFG_LINK_TIMEOUT_MS);

    if (alive == g_link_ok) {
        return;
    }

    g_link_ok = alive;

    if (!alive) {
#if (CFG_SAFE_STATE_ON_LOST != 0)
        protocol_enter_safe_state();
#endif
        gpio_set_led(false);
    }
}

/**
 * @brief  状态灯指示
 * @note   链路正常: 500ms 慢闪; 链路断开: 100ms 快闪
 */
static void app_update_led(void)
{
    static uint32_t last_toggle;

    uint32_t period = g_link_ok ? CFG_HEARTBEAT_MS : LED_FAST_BLINK_MS;

    if (sys_elapsed(last_toggle) >= period) {
        last_toggle = sys_get_tick();
        gpio_toggle_led();
    }
}

/**
 * @brief  周期性心跳发送
 */
static void app_send_heartbeat(void)
{
    static uint32_t last_heartbeat;

    if (sys_elapsed(last_heartbeat) >= CFG_HEARTBEAT_MS) {
        last_heartbeat = sys_get_tick();
        (void)protocol_send_heartbeat();
    }
}

/**
 * @brief  100Hz 采集节拍任务: 消抖数字输入并启动 ADC 扫描
 */
static void app_sample_task(void)
{
    gpio_poll_inputs();
    adc_start_sample();
}

/** @brief 固件入口 */
int main(void)
{
    SystemClock_Config();
    systick_init();

    /* 外设初始化顺序: GPIO 优先(内含 JTAG 重映射), 之后是输出与采集通道 */
    gpio_init();
    dac_init();
    pwm_init();
    adc_init();
    uart_init();
    protocol_init();

    /* 上电自检: 采一轮 ADC, 失败则点亮 LED 但不阻塞运行 */
    if (!adc_sample_blocking()) {
        gpio_set_led(true);
    }

    tim3_tick_init();

    g_link_ok = false;
    g_sample_tick = false;

    for (;;) {
        /* 1. 处理上位机命令, 整帧回调在此上下文执行 */
        uart_poll();

        /* 2. 100Hz 节拍: 启动新一轮采集 */
        if (g_sample_tick) {
            g_sample_tick = false;
            app_sample_task();
        }

        /* 3. 采集完成后刷新滤波值并上报 */
        if (adc_poll()) {
#if (CFG_AUTO_REPORT_ENABLE != 0)
            if (g_link_ok) {
                (void)protocol_send_report();
            }
#endif
        }

        /* 4. 心跳与链路监控 */
        app_send_heartbeat();
        app_check_link();
        app_update_led();
    }
}

/**
 * @brief  致命错误处理
 * @note   关闭所有输出后快闪 LED, 便于现场定位
 */
void error_handler(void)
{
    __disable_irq();

    gpio_reset_all_outputs();
    pwm_stop_all();
    dac_reset_all();

    /* 时钟配置阶段就可能进入此处, 需自行保证 LED 端口时钟已使能 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    gpio_pin_mode(PIN_LED_PORT, PIN_LED, GPIO_MODE_OUT_PP_50M);

    for (;;) {
        gpio_toggle_led();
        for (volatile uint32_t i = 0U; i < 200000U; i++) {
        }
    }
}
