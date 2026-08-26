/**
  ******************************************************************************
  * @file    pin_config.h
  * @brief   引脚分配定义(洗衣机 HIL 仿真板)
  *
  * 目标器件: STM32F103ZET6 (144 脚)。现有信号均分配在 PA/PB/PC 上,
  * 与原 64 脚方案兼容; PD-PG 以及 ADC2/ADC3、TIM5-TIM8、UART2-5
  * 等大容量外设保留作后续扩展。
  *
  * 信号方向以 MCU 为参照:
  *   输出 = MCU 仿真传感器/执行器状态送给被测洗衣机主控
  *   输入 = 采集被测主控的驱动输出
  ******************************************************************************
  */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include "stm32f103_reg.h"

/* ========================================================================== */
/* UART1 - 上位机通信                                                         */
/* ========================================================================== */

#define PIN_UART_TX_PORT        GPIOA
#define PIN_UART_TX             9U          /*!< PA9  USART1_TX               */
#define PIN_UART_RX_PORT        GPIOA
#define PIN_UART_RX             10U         /*!< PA10 USART1_RX               */

/* ========================================================================== */
/* PWM 输出 - 模拟传感器频率信号                                              */
/* ========================================================================== */

#define PIN_PWM_WATER_PORT      GPIOA
#define PIN_PWM_WATER           8U          /*!< PA8  TIM1_CH1 水位传感器频率 */
#define PIN_PWM_HALL_PORT       GPIOA
#define PIN_PWM_HALL            1U          /*!< PA1  TIM2_CH2 Hall 传感器频率*/

/* ========================================================================== */
/* DAC 输出 - 模拟量                                                          */
/* ========================================================================== */

#define PIN_DAC1_PORT           GPIOA
#define PIN_DAC1                4U          /*!< PA4  DAC_OUT1 NTC 阻值模拟   */
#define PIN_DAC2_PORT           GPIOA
#define PIN_DAC2                5U          /*!< PA5  DAC_OUT2 备用模拟量     */

/* ========================================================================== */
/* GPIO 输出 - 数字量(仿真执行器反馈/开关信号)                                */
/* ========================================================================== */

#define PIN_OUT_PORT            GPIOB
#define PIN_OUT_DOOR_LOCK       0U          /*!< PB0 门锁状态                 */
#define PIN_OUT_INLET_VALVE     1U          /*!< PB1 进水阀                   */
#define PIN_OUT_DRAIN_PUMP      2U          /*!< PB2 排水泵                   */
#define PIN_OUT_HEATER          3U          /*!< PB3 加热器继电器             */
#define PIN_OUT_MASK            0x000FU     /*!< PB0-PB3                      */

/* ========================================================================== */
/* GPIO 输出 - 门开关信号(送给主控板的门状态开关量)                           */
/* ========================================================================== */

#define PIN_DOOR_OUT_PORT       GPIOC
#define PIN_DOOR_1              0U          /*!< PC0 DOOR_1 高=门开, 低=门关 */
#define PIN_DOOR_2              1U          /*!< PC1 DOOR_2 高=门关, 低=门开 */
#define PIN_DOOR_OUT_MASK       0x0003U     /*!< PC0-PC1                     */

/* ========================================================================== */
/* GPIO 输入 - 采集主控数字输出                                               */
/* ========================================================================== */

#define PIN_IN_PORT             GPIOB
#define PIN_IN_MOTOR_EN         4U          /*!< PB4 主控电机使能             */
#define PIN_IN_HEAT_EN          5U          /*!< PB5 主控加热使能             */
#define PIN_IN_INLET_VALVE      6U          /*!< PB6 主控进水阀               */
#define PIN_IN_DRAIN_PUMP       7U          /*!< PB7 主控排水泵               */
#define PIN_IN_MASK             0x00F0U     /*!< PB4-PB7                      */
#define PIN_IN_SHIFT            4U

/* ========================================================================== */
/* ADC 输入 - 采集主控模拟输出                                                */
/* ========================================================================== */

#define PIN_ADC_PORT            GPIOA
#define PIN_ADC_MOTOR_PWM       0U          /*!< PA0 ADC_IN0  电机 PWM 占空比 */
#define PIN_ADC_SPARE1          2U          /*!< PA2 ADC_IN2  备用            */
#define PIN_ADC_SPARE2          3U          /*!< PA3 ADC_IN3  备用            */
#define PIN_ADC_SPARE3          6U          /*!< PA6 ADC_IN6  备用            */

#define ADC_CH_MOTOR_PWM        0U
#define ADC_CH_SPARE1           2U
#define ADC_CH_SPARE2           3U
#define ADC_CH_SPARE3           6U

/* ========================================================================== */
/* 状态指示灯                                                                 */
/* ========================================================================== */

#define PIN_LED_PORT            GPIOC
#define PIN_LED                 13U         /*!< PC13 板载 LED, 低电平点亮    */

/* ========================================================================== */
/* GPIO 配置辅助宏 (CRL/CRH 每引脚 4 位)                                      */
/* ========================================================================== */

#define GPIO_MODE_IN_ANALOG     0x0U        /*!< 模拟输入                     */
#define GPIO_MODE_IN_FLOATING   0x4U        /*!< 浮空输入                     */
#define GPIO_MODE_IN_PULL       0x8U        /*!< 上/下拉输入(由 ODR 决定)     */
#define GPIO_MODE_OUT_PP_50M    0x3U        /*!< 推挽输出 50MHz               */
#define GPIO_MODE_OUT_OD_50M    0x7U        /*!< 开漏输出 50MHz               */
#define GPIO_MODE_AF_PP_50M     0xBU        /*!< 复用推挽输出 50MHz           */
#define GPIO_MODE_AF_OD_50M     0xFU        /*!< 复用开漏输出 50MHz           */

/**
 * @brief  配置单个引脚的模式
 * @param  port  GPIO 端口指针
 * @param  pin   引脚号 0-15
 * @param  mode  上面的 GPIO_MODE_xxx 之一
 */
static inline void gpio_pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    if (pin < 8U) {
        uint32_t shift = pin * 4U;
        port->CRL = (port->CRL & ~(0xFUL << shift)) | (mode << shift);
    } else {
        uint32_t shift = (pin - 8U) * 4U;
        port->CRH = (port->CRH & ~(0xFUL << shift)) | (mode << shift);
    }
}

/** @brief 置高引脚 */
static inline void gpio_pin_set(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = 1UL << pin;
}

/** @brief 置低引脚 */
static inline void gpio_pin_reset(GPIO_TypeDef *port, uint32_t pin)
{
    port->BRR = 1UL << pin;
}

/** @brief 读取引脚电平 */
static inline uint32_t gpio_pin_get(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR >> pin) & 1UL;
}

/** @brief 翻转引脚电平 */
static inline void gpio_pin_toggle(GPIO_TypeDef *port, uint32_t pin)
{
    port->ODR ^= 1UL << pin;
}

#endif /* PIN_CONFIG_H */
