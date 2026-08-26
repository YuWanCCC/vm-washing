#include "MatCodec.h"
#include "MatCRC8.h"

#include <cstring>

namespace whm {

// ---- 私有辅助 --------------------------------------------------------

QByteArray MatCodec::buildFrame(uint8_t address, uint8_t command,
                                const QByteArray &sdu)
{
    // Length = 1(command) + sdu.size()，不含 FCS
    const uint8_t length = static_cast<uint8_t>(1 + sdu.size());

    QByteArray frame;
    frame.reserve(4 + sdu.size() + 1);

    frame.append(static_cast<char>(MatFrame::Header));
    frame.append(static_cast<char>(address));
    frame.append(static_cast<char>(length));
    frame.append(static_cast<char>(command));
    frame.append(sdu);

    // FCS 覆盖 Address + Length + Command + SDU
    const auto *fcsData = reinterpret_cast<const uint8_t *>(frame.constData() + 1);
    const size_t fcsLen = static_cast<size_t>(frame.size() - 1);
    const uint8_t fcs = MatCRC8::calculate(fcsData, fcsLen);

    frame.append(static_cast<char>(fcs));
    return frame;
}

// ---- 编码 ------------------------------------------------------------

QByteArray MatCodec::encodeQuery()
{
    QByteArray sdu;
    sdu.append(static_cast<char>(0x01));
    return buildFrame(MatFrame::AddrPcToMcu,
                      static_cast<uint8_t>(MatCommand::Query), sdu);
}

QByteArray MatCodec::encodeControl(uint8_t b1, uint8_t b2,
                                   uint8_t b3, uint8_t b4,
                                   uint8_t b5, uint8_t b6)
{
    QByteArray sdu;
    sdu.append(static_cast<char>(b1));
    sdu.append(static_cast<char>(b2));
    sdu.append(static_cast<char>(b3));
    sdu.append(static_cast<char>(b4));
    sdu.append(static_cast<char>(b5));
    sdu.append(static_cast<char>(b6));
    return buildFrame(MatFrame::AddrPcToMcu,
                      static_cast<uint8_t>(MatCommand::Control), sdu);
}

// ---- 解码 ------------------------------------------------------------

MatFrame MatCodec::parseFrame(const QByteArray &buf, int *consumed)
{
    MatFrame frame;
    frame.isValid = false;

    // 从头搜索帧头 0xFA
    int start = -1;
    for (int i = 0; i < buf.size(); ++i) {
        if (static_cast<uint8_t>(buf[i]) == MatFrame::Header) {
            start = i;
            break;
        }
    }

    if (start < 0) {
        // 整段数据无帧头，全部丢弃
        *consumed = buf.size();
        return frame;
    }

    if (start > 0) {
        // 帧头前有垃圾字节，跳过
        *consumed = start;
        return frame;
    }

    // buf[0] = 0xFA，最小帧：Header(1) + Addr(1) + Len(1) + Cmd(至少1) + FCS(1) = 5B
    if (buf.size() < 5) {
        *consumed = 0;
        return frame;
    }

    const uint8_t address = static_cast<uint8_t>(buf[1]);
    const uint8_t length  = static_cast<uint8_t>(buf[2]);   // cmd+sdu 的总字节数

    if (length < 1) {
        // 长度非法，跳过此帧头
        *consumed = 1;
        return frame;
    }

    // 完整帧总长 = 1(hdr) + 1(addr) + 1(len) + length(cmd+sdu) + 1(fcs)
    const int totalLen = 4 + static_cast<int>(length);
    if (buf.size() < totalLen) {
        // 数据不足，等待更多字节
        *consumed = 0;
        return frame;
    }

    // 验证 FCS
    const auto *fcsData = reinterpret_cast<const uint8_t *>(buf.constData() + 1);
    const size_t fcsLen = static_cast<size_t>(2 + length);  // addr+len+cmd+sdu
    const uint8_t receivedFcs = static_cast<uint8_t>(buf[3 + length]);
    if (!MatCRC8::verify(fcsData, fcsLen, receivedFcs)) {
        // FCS 错误，跳过此帧头
        *consumed = 1;
        return frame;
    }

    frame.address = address;
    frame.command = static_cast<uint8_t>(buf[3]);
    if (length > 1)
        frame.sdu = buf.mid(4, length - 1);
    frame.isValid = true;
    *consumed = totalLen;
    return frame;
}

MatStatus MatCodec::decodeStatus(const MatFrame &frame)
{
    MatStatus status;

    if (!frame.isValid
        || frame.command != static_cast<uint8_t>(MatCommand::Query)
        || frame.sdu.size() < 10) {
        return status;
    }

    status.rawBytes   = frame.sdu;
    const auto *d     = reinterpret_cast<const uint8_t *>(frame.sdu.constData());
    status.workStatus = d[0];
    status.faultCode  = d[1];
    status.childLock  = d[2];
    // d[3] 保留
    status.motorRpm   = static_cast<uint16_t>(d[4] | (d[5] << 8));
    status.waterLevel = d[6];
    status.waterTemp  = d[7];
    status.remainMin  = d[8];
    status.remainSec  = d[9];
    status.isValid    = true;
    return status;
}

} // namespace whm
