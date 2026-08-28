#ifndef WHM_MATCRC8_H
#define WHM_MATCRC8_H

#include <cstddef>
#include <cstdint>

namespace whm {

/*!
 * \brief CRC-8 校验，与主控板实际使用的算法完全一致。
 *
 * 算法参数：
 *   - 多项式: 0x9B（x^8 + x^7 + x^4 + x^3 + x^1 + 1）
 *   - 初値:   0x00
 *   - REFIN:   false（正向，MSB-first）
 *   - REFOUT:  false
 *   - XorOut:  0x00
 *
 * 校验覆盖范围：Header(0xFA) + Address + Length + Command + SDU（不含 FCS 自身）。
 */
class MatCRC8
{
public:
    static constexpr uint8_t Polynomial = 0x9B;   //!< 生成多项式
    static constexpr uint8_t InitValue  = 0x00;   //!< 初値

    //! 计算数据块校验值（覆盖 data[0..length-1]）
    static uint8_t calculate(const uint8_t *data, size_t length);

    //! 在已有中间值上继续累加，用于分段计算（首次传入 InitValue）
    static uint8_t update(uint8_t crc, const uint8_t *data, size_t length);

    //! 校验数据块 FCS 是否正确
    static bool verify(const uint8_t *data, size_t length, uint8_t expected);

private:
    //! 256 项查找表（多项式 0x9B 预计算，MSB-first）
    static const uint8_t s_table[256];
};

} // namespace whm

#endif // WHM_MATCRC8_H
