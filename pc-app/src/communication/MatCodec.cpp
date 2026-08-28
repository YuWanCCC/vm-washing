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

    // FCS 覆盖 Header(0xFA) + Address + Length + Command + SDU
    const auto *fcsData = reinterpret_cast<const uint8_t *>(frame.constData());
    const size_t fcsLen = static_cast<size_t>(frame.size());
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

    // 验证 FCS（覆盖 Header + Address + Length + Command + SDU）
    const auto *fcsData = reinterpret_cast<const uint8_t *>(buf.constData());
    const size_t fcsLen = static_cast<size_t>(3 + length);  // hdr+addr+len+cmd+sdu
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
    MatStatus s;

    if (!frame.isValid
        || frame.command != static_cast<uint8_t>(MatCommand::Query)
        || frame.sdu.size() < 1) {
        return s;
    }

    s.rawBytes = frame.sdu;
    s.sduLen   = frame.sdu.size();
    const auto *d = reinterpret_cast<const uint8_t *>(frame.sdu.constData());
    const int len = s.sduLen;  // 实际可用长度

    // ---- BYTE 1-2: 协议基本信息（偏移 0-1）----
    if (len >= 1) s.protocolVersion = d[0];
    if (len >= 2) s.machineType     = d[1];

    // ---- BYTE 3 (偏移 2): 开关状态 1 ----
    if (len >= 3) {
        s.antiCrease    = d[2] & 0x80;
        s.doorSwitch    = d[2] & 0x40;
        s.drumLight     = d[2] & 0x20;
        s.blueLight     = d[2] & 0x10;
        s.uvLight       = d[2] & 0x08;
        s.fan           = d[2] & 0x04;
        s.floatSwitch   = d[2] & 0x02;
        s.steamLowWater = d[2] & 0x01;
    }

    // ---- BYTE 4 (偏移 3): 开关状态 2 ----
    if (len >= 4) {
        s.compressorSwitch = d[3] & 0x80;
        s.drainPump        = d[3] & 0x40;
        s.electricTee      = d[3] & 0x20;
        s.steamGenerator   = d[3] & 0x10;
        s.anion            = d[3] & 0x08;
        s.rotateLeft       = d[3] & 0x04;
        s.rotateRight      = d[3] & 0x02;
        s.motorStop        = d[3] & 0x01;
    }

    // ---- BYTE 5 (偏移 4): 开关状态 3 ----
    if (len >= 5) {
        s.compressor = d[4] & 0x04;
        s.heater     = d[4] & 0x02;
        s.ctrlFan    = d[4] & 0x01;
    }

    // ---- 传感器数据（偏移 5-20）----
    if (len >= 7)  s.humidity     = static_cast<uint16_t>(d[5]  | (d[6]  << 8));
    if (len >= 9)  s.humidityRaw  = static_cast<uint16_t>(d[7]  | (d[8]  << 8));
    if (len >= 10) s.dryStep      = d[9];
    if (len >= 11) s.load         = d[10];
    if (len >= 13) s.motorSpeed   = static_cast<int16_t>(static_cast<uint16_t>(d[11] | (d[12] << 8)));
    if (len >= 15) s.ntc1Raw      = static_cast<uint16_t>(d[13] | (d[14] << 8));
    if (len >= 16) s.ntc1Int      = d[15];
    if (len >= 17) s.ntc1Dec      = d[16];
    if (len >= 19) s.ntc2Raw      = static_cast<uint16_t>(d[17] | (d[18] << 8));
    if (len >= 20) s.ntc2Int      = d[19];
    if (len >= 21) s.ntc2Dec      = d[20];

    // ---- 程序与时间（偏移 21-25）----
    if (len >= 22) s.programId    = d[21];
    if (len >= 24) s.programTime  = static_cast<uint16_t>(d[22] | (d[23] << 8));
    if (len >= 26) s.timeToEnd    = static_cast<uint16_t>(d[24] | (d[25] << 8));

    // ---- 变频数据（偏移 26-34）----
    if (len >= 28) s.fanSpeed        = static_cast<uint16_t>(d[26] | (d[27] << 8));
    if (len >= 30) s.compressorSpeed = static_cast<uint16_t>(d[28] | (d[29] << 8));
    if (len >= 32) s.compressorPower = static_cast<uint16_t>(d[30] | (d[31] << 8));
    if (len >= 33) s.paramP = static_cast<int8_t>(d[32]);
    if (len >= 34) s.paramI = static_cast<int8_t>(d[33]);
    if (len >= 35) s.paramD = static_cast<int8_t>(d[34]);

    // ---- 3D 传感器（偏移 35-46）----
    if (len >= 37) s.sensor3dRef      = static_cast<uint16_t>(d[35] | (d[36] << 8));
    if (len >= 39) s.sensor3dSense    = static_cast<uint16_t>(d[37] | (d[38] << 8));
    if (len >= 41) s.sensor3dMoisture = static_cast<uint16_t>(d[39] | (d[40] << 8));
    if (len >= 43) s.sensor3dStdDev   = static_cast<uint16_t>(d[41] | (d[42] << 8));
    if (len >= 45) s.sensor3dFitting  = static_cast<int16_t>(static_cast<uint16_t>(d[43] | (d[44] << 8)));
    if (len >= 47) s.sensor3dFitStd   = static_cast<uint16_t>(d[45] | (d[46] << 8));

    // ---- 故障码（偏移 47-50）----
    if (len >= 48) s.errorCode      = d[47];
    if (len >= 49) s.motorErrorCode = d[48];
    if (len >= 50) s.fanErrorCode   = d[49];
    if (len >= 51) s.compErrorCode  = d[50];

    // ---- 版本信息（偏移 51-60）----
    if (len >= 52) s.versionA = d[51];
    if (len >= 53) s.versionB = d[52];
    if (len >= 54) s.versionC = d[53];
    if (len >= 55) s.versionE = d[54];
    if (len >= 56) s.versionF = d[55];
    if (len >= 57) s.versionP = d[56];
    if (len >= 58) s.versionT = d[57];
    if (len >= 59) s.versionD = d[58];
    if (len >= 60) s.versionG = d[59];
    if (len >= 61) s.versionO = d[60];

    // ---- 功率与湿度（偏移 61-66）----
    if (len >= 63) s.fanPower       = static_cast<uint16_t>(d[61] | (d[62] << 8));
    if (len >= 65) s.electricPower  = static_cast<uint16_t>(d[63] | (d[64] << 8));
    if (len >= 66) s.humidityRatio  = d[65];
    if (len >= 67) s.temp3dFitting  = d[66];

    s.isValid = true;
    return s;
}

} // namespace whm
