/**
  ******************************************************************************
  * @file    crc16.c
  * @brief   CRC16-CCITT 实现(逐位算法, 无查表, 省 512B FLASH)
  ******************************************************************************
  */

#include <stddef.h>

#include "crc16.h"

/**
 * @brief  在已有 CRC 中间值上继续累加数据
 * @note   MSB first: 每字节先左移进高 8 位, 再逐位判断最高位决定是否异或多项式
 */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t len)
{
    if (data == NULL) {
        return crc;
    }

    for (uint16_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1U) ^ CRC16_POLY);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

/** @brief 计算数据块 CRC16-CCITT, 初值 0xFFFF */
uint16_t crc16_calc(const uint8_t *data, uint16_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}

/** @brief 校验 CRC 是否与期望值一致 */
bool crc16_verify(const uint8_t *data, uint16_t len, uint16_t expected)
{
    return (crc16_calc(data, len) == expected);
}
