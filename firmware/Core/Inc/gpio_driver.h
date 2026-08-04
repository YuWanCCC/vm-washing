/**
  ******************************************************************************
  * @file    gpio_driver.h
  * @brief   数字量输出/输入驱动
  ******************************************************************************
  */

#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 数字输出通道(仿真给主控的开关信号) */
typedef enum {
    GPIO_OUT_DOOR_LOCK   = 0,  /*!< PB0 门锁                                */
    GPIO_OUT_INLET_VALVE = 1,  /*!< PB1 进水阀                              */
    GPIO_OUT_DRAIN_PUMP  = 2,  /*!< PB2 排水泵                              */
    GPIO_OUT_HEATER      = 3,  /*!< PB3 加热器继电器                        */
    GPIO_OUT_COUNT       = 4
} gpio_out_id_t;

/** @brief 数字输入通道(采集主控驱动输出) */
typedef enum {
    GPIO_IN_MOTOR_EN    = 0,   /*!< PB4 主控电机使能                        */
    GPIO_IN_HEAT_EN     = 1,   /*!< PB5 主控加热使能                        */
    GPIO_IN_INLET_VALVE = 2,   /*!< PB6 主控进水阀                          */
    GPIO_IN_DRAIN_PUMP  = 3,   /*!< PB7 主控排水泵                          */
    GPIO_IN_COUNT       = 4
} gpio_in_id_t;

/**
 * @brief  初始化所有数字输出/输入引脚与状态灯
 * @note   会关闭 JTAG(保留 SWD) 以释放 PB3/PB4 作普通 IO
 */
void gpio_init(void);

/**
 * @brief  设置数字输出电平
 * @param  pin_id GPIO_OUT_xxx
 * @param  state  true 输出高
 * @return true 设置成功; false 通道非法
 */
bool gpio_write(gpio_out_id_t pin_id, bool state);

/**
 * @brief  按位图一次写入 4 路数字输出
 * @param  bitmap bit0-bit3 依次对应门锁/进水阀/排水泵/加热器
 */
void gpio_write_all_outputs(uint8_t bitmap);

/**
 * @brief  读取数字输入电平(已消抖)
 * @param  pin_id GPIO_IN_xxx
 */
bool gpio_read(gpio_in_id_t pin_id);

/**
 * @brief  读取所有数字输入, 返回位图
 * @return bit0-bit3 依次对应电机使能/加热使能/进水阀/排水泵
 */
uint16_t gpio_read_all_inputs(void);

/** @brief 读取当前 4 路数字输出状态位图 */
uint8_t gpio_get_output_bitmap(void);

/**
 * @brief  输入消抖采样, 需在周期性 tick 中调用
 * @note   连续 GPIO_DEBOUNCE_COUNT 次一致才更新稳定值
 */
void gpio_poll_inputs(void);

/** @brief 所有数字输出复位为低电平(安全状态) */
void gpio_reset_all_outputs(void);

/** @brief 状态指示灯控制 */
void gpio_set_led(bool on);

/** @brief 状态指示灯翻转 */
void gpio_toggle_led(void);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_DRIVER_H */
