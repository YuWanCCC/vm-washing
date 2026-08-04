#ifndef WHM_CRC16_H
#define WHM_CRC16_H

#include <cstddef>
#include <cstdint>

namespace whm {

/*!
 * \brief CRC16-CCITT 校验（多项式 0x1021，初值 0xFFFF，高位先算）。
 *
 * 采用 256 项查找表实现，逐字节推进：
 *   crc = (crc << 8) ^ table[((crc >> 8) ^ byte) & 0xFF]
 * 与固件 firmware/Core/Src/crc16.c 的逐位算法在数学上等价，
 * 二者结果必须完全一致，否则 PC 与 MCU 无法通信。
 */
class CRC16
{
public:
    static constexpr uint16_t Polynomial = 0x1021;   //!< 生成多项式
    static constexpr uint16_t InitValue  = 0xFFFF;   //!< 初值

    //! 计算数据块校验值
    static uint16_t calculate(const uint8_t *data, size_t length);

    //! 在已有中间值上继续累加，用于分段计算（首次传入 InitValue）
    static uint16_t update(uint16_t crc, const uint8_t *data, size_t length);

    //! 校验数据块 CRC 是否与期望值一致
    static bool verify(const uint8_t *data, size_t length, uint16_t expected);
};

} // namespace whm

#endif // WHM_CRC16_H
