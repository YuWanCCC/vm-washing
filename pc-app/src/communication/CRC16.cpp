#include "CRC16.h"

namespace whm {
namespace {

/*!
 * \brief 编译期生成的 256 项 CRC 查找表。
 *
 * 表项 i 为字节 i 单独进入高 8 位后连续 8 次移位/异或的结果。
 */
struct CrcTable
{
    uint16_t entries[256] {};

    constexpr CrcTable()
    {
        for (int i = 0; i < 256; ++i) {
            uint16_t crc = static_cast<uint16_t>(i << 8);
            for (int bit = 0; bit < 8; ++bit) {
                if (crc & 0x8000) {
                    crc = static_cast<uint16_t>(static_cast<uint16_t>(crc << 1) ^ CRC16::Polynomial);
                } else {
                    crc = static_cast<uint16_t>(crc << 1);
                }
            }
            entries[i] = crc;
        }
    }
};

constexpr CrcTable kCrcTable {};

} // namespace

uint16_t CRC16::update(uint16_t crc, const uint8_t *data, size_t length)
{
    if (data == nullptr) {
        return crc;
    }

    for (size_t i = 0; i < length; ++i) {
        const uint8_t index = static_cast<uint8_t>((crc >> 8) ^ data[i]);
        crc = static_cast<uint16_t>(static_cast<uint16_t>(crc << 8) ^ kCrcTable.entries[index]);
    }

    return crc;
}

uint16_t CRC16::calculate(const uint8_t *data, size_t length)
{
    return update(InitValue, data, length);
}

bool CRC16::verify(const uint8_t *data, size_t length, uint16_t expected)
{
    return calculate(data, length) == expected;
}

} // namespace whm
