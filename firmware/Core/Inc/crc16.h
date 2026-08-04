/**
  ******************************************************************************
  * @file    crc16.h
  * @brief   CRC16-CCITT (多项式 0x1021, 初值 0xFFFF, 高位先算)
  ******************************************************************************
  */

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC16_POLY      0x1021U
#define CRC16_INIT      0xFFFFU

/**
 * @brief  计算数据块的 CRC16-CCITT
 * @param  data 数据指针
 * @param  len  数据长度
 * @return 16 位校验值
 */
uint16_t crc16_calc(const uint8_t *data, uint16_t len);

/**
 * @brief  在已有 CRC 基础上继续累加(用于分段计算, 如帧头字段+载荷)
 * @param  crc  上一次的 CRC 中间值, 首次传入 CRC16_INIT
 * @param  data 数据指针
 * @param  len  数据长度
 * @return 更新后的 CRC 中间值
 */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t len);

/**
 * @brief  校验数据块 CRC 是否与期望值一致
 * @return true 表示校验通过
 */
bool crc16_verify(const uint8_t *data, uint16_t len, uint16_t expected);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
