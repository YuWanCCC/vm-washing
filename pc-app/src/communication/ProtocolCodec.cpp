#include "ProtocolCodec.h"

#include "CRC16.h"

namespace whm {
namespace {

//! 写入小端 16 位
void appendU16(QByteArray &buffer, uint16_t value)
{
    buffer.append(static_cast<char>(value & 0xFF));
    buffer.append(static_cast<char>((value >> 8) & 0xFF));
}

//! 写入小端 32 位
void appendU32(QByteArray &buffer, uint32_t value)
{
    buffer.append(static_cast<char>(value & 0xFF));
    buffer.append(static_cast<char>((value >> 8) & 0xFF));
    buffer.append(static_cast<char>((value >> 16) & 0xFF));
    buffer.append(static_cast<char>((value >> 24) & 0xFF));
}

//! 读取小端 16 位
uint16_t readU16(const QByteArray &buffer, int offset)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(buffer.at(offset))
                                 | (static_cast<uint16_t>(static_cast<uint8_t>(buffer.at(offset + 1))) << 8));
}

//! 判断命令码是否为协议已定义的取值
bool isKnownCommand(uint8_t cmd)
{
    switch (static_cast<CommandId>(cmd)) {
    case CommandId::SET_ANALOG:
    case CommandId::SET_DIGITAL:
    case CommandId::SET_FREQUENCY:
    case CommandId::READ_ALL:
    case CommandId::BATCH_SET:
    case CommandId::REPORT_INPUT:
    case CommandId::ACK:
    case CommandId::HEARTBEAT:
        return true;
    }
    return false;
}

//! 注册自定义类型，保证跨线程/队列连接与 QVariant 可用
void registerProtocolMetaTypes()
{
    static const bool registered = [] {
        qRegisterMetaType<CommandId>("whm::CommandId");
        qRegisterMetaType<AckStatus>("whm::AckStatus");
        qRegisterMetaType<ProtocolFrame>("whm::ProtocolFrame");
        qRegisterMetaType<BatchSetData>("whm::BatchSetData");
        qRegisterMetaType<ReportInputData>("whm::ReportInputData");
        return true;
    }();
    Q_UNUSED(registered)
}

} // namespace

ProtocolCodec::ProtocolCodec(QObject *parent)
    : QObject(parent)
{
    registerProtocolMetaTypes();
    m_payloadBuffer.reserve(static_cast<int>(PAYLOAD_MAX_SIZE));
}

/* ==========================================================================
 * 编码
 * ========================================================================== */

/*!
 * \brief 组帧: [0xAA][CMD][LEN][PAYLOAD][CRC_L][CRC_H][0x55]
 *
 * CRC 覆盖 CMD+LEN+PAYLOAD，低字节先发，与固件 uart_send_frame() 一致。
 */
QByteArray ProtocolCodec::buildFrame(CommandId cmd, const QByteArray &payload)
{
    if (static_cast<size_t>(payload.size()) > PAYLOAD_MAX_SIZE) {
        emit parseError(tr("载荷长度 %1 超过上限 %2").arg(payload.size()).arg(PAYLOAD_MAX_SIZE));
        return {};
    }

    QByteArray body;
    body.reserve(payload.size() + 2);
    body.append(static_cast<char>(cmd));
    body.append(static_cast<char>(payload.size()));
    body.append(payload);

    const uint16_t crc = CRC16::calculate(reinterpret_cast<const uint8_t *>(body.constData()),
                                          static_cast<size_t>(body.size()));

    QByteArray frame;
    frame.reserve(body.size() + 4);
    frame.append(static_cast<char>(FRAME_HEADER));
    frame.append(body);
    appendU16(frame, crc);
    frame.append(static_cast<char>(FRAME_TAIL));

    return frame;
}

QByteArray ProtocolCodec::encodeSetAnalog(uint8_t channel, uint16_t value)
{
    QByteArray payload;
    payload.append(static_cast<char>(channel));
    appendU16(payload, value);
    return buildFrame(CommandId::SET_ANALOG, payload);
}

QByteArray ProtocolCodec::encodeSetDigital(uint8_t port, uint8_t state)
{
    QByteArray payload;
    payload.append(static_cast<char>(port));
    payload.append(static_cast<char>(state));
    return buildFrame(CommandId::SET_DIGITAL, payload);
}

QByteArray ProtocolCodec::encodeSetFrequency(uint8_t channel, uint32_t freqHz)
{
    QByteArray payload;
    payload.append(static_cast<char>(channel));
    appendU32(payload, freqHz);
    return buildFrame(CommandId::SET_FREQUENCY, payload);
}

QByteArray ProtocolCodec::encodeReadAll()
{
    return buildFrame(CommandId::READ_ALL, QByteArray());
}

/*!
 * \brief 组装 BATCH_SET 载荷(16B, 小端)
 *
 * [0-3] 水位频率 [4-5] NTC DAC [6-9] Hall 频率
 * [10] 门锁 [11] 进水阀 [12] 排水泵 [13] 加热器 [14-15] 保留
 */
QByteArray ProtocolCodec::encodeBatchSet(const BatchSetData &data)
{
    QByteArray payload;
    payload.reserve(static_cast<int>(PAYLOAD_LEN_BATCH_SET));

    appendU32(payload, data.waterLevelFreq);
    appendU16(payload, data.ntcDacValue);
    appendU32(payload, data.hallFreq);
    payload.append(static_cast<char>(data.doorLock ? 1 : 0));
    payload.append(static_cast<char>(data.inletValve ? 1 : 0));
    payload.append(static_cast<char>(data.drainPump ? 1 : 0));
    payload.append(static_cast<char>(data.heater ? 1 : 0));
    payload.append(2, '\0');   // 保留 2B

    return buildFrame(CommandId::BATCH_SET, payload);
}

QByteArray ProtocolCodec::encodeHeartbeat()
{
    return buildFrame(CommandId::HEARTBEAT, QByteArray());
}

/* ==========================================================================
 * 解码
 * ========================================================================== */

void ProtocolCodec::feedData(const QByteArray &data)
{
    for (char byte : data) {
        parseByte(static_cast<uint8_t>(byte));
    }
}

void ProtocolCodec::reset()
{
    resetParser();
}

void ProtocolCodec::resetParser()
{
    m_parseState = WaitHeader;
    m_currentCmd = 0;
    m_payloadLen = 0;
    m_payloadBuffer.clear();
    m_crcLow = 0;
    m_crcHigh = 0;
}

void ProtocolCodec::resyncOn(uint8_t byte)
{
    resetParser();
    if (byte == FRAME_HEADER) {
        m_parseState = RecvCmd;
    }
}

/*!
 * \brief 逐字节推进帧同步状态机
 *
 * WaitHeader -> RecvCmd -> RecvLen -> RecvPayload
 *            -> RecvCrcLow -> RecvCrcHigh -> RecvTail
 */
void ProtocolCodec::parseByte(uint8_t byte)
{
    switch (m_parseState) {
    case WaitHeader:
        if (byte == FRAME_HEADER) {
            m_parseState = RecvCmd;
        }
        break;

    case RecvCmd:
        m_currentCmd = byte;
        m_parseState = RecvLen;
        break;

    case RecvLen:
        if (static_cast<size_t>(byte) > PAYLOAD_MAX_SIZE) {
            emit parseError(tr("非法载荷长度 %1，丢弃并重新同步").arg(byte));
            resyncOn(byte);
            break;
        }
        m_payloadLen = byte;
        m_payloadBuffer.clear();
        m_parseState = (byte == 0) ? RecvCrcLow : RecvPayload;
        break;

    case RecvPayload:
        m_payloadBuffer.append(static_cast<char>(byte));
        if (static_cast<size_t>(m_payloadBuffer.size()) >= m_payloadLen) {
            m_parseState = RecvCrcLow;
        }
        break;

    case RecvCrcLow:
        m_crcLow = byte;
        m_parseState = RecvCrcHigh;
        break;

    case RecvCrcHigh:
        m_crcHigh = byte;
        m_parseState = RecvTail;
        break;

    case RecvTail:
        if (byte != FRAME_TAIL) {
            emit parseError(tr("帧尾错误：期望 0x55，实际 0x%1")
                                .arg(byte, 2, 16, QLatin1Char('0')));
            resyncOn(byte);
            break;
        }
        processCompleteFrame();
        resetParser();
        break;
    }
}

/*!
 * \brief 校验 CRC 并分发一个完整帧
 */
void ProtocolCodec::processCompleteFrame()
{
    QByteArray body;
    body.reserve(m_payloadBuffer.size() + 2);
    body.append(static_cast<char>(m_currentCmd));
    body.append(static_cast<char>(m_payloadLen));
    body.append(m_payloadBuffer);

    const uint16_t received = static_cast<uint16_t>(m_crcLow | (static_cast<uint16_t>(m_crcHigh) << 8));
    const uint16_t expected = CRC16::calculate(reinterpret_cast<const uint8_t *>(body.constData()),
                                               static_cast<size_t>(body.size()));

    if (received != expected) {
        emit parseError(tr("CRC 校验失败：收到 0x%1，计算 0x%2（CMD=0x%3）")
                            .arg(received, 4, 16, QLatin1Char('0'))
                            .arg(expected, 4, 16, QLatin1Char('0'))
                            .arg(m_currentCmd, 2, 16, QLatin1Char('0')));
        return;
    }

    if (!isKnownCommand(m_currentCmd)) {
        emit parseError(tr("未知命令码 0x%1").arg(m_currentCmd, 2, 16, QLatin1Char('0')));
        return;
    }

    ProtocolFrame frame;
    frame.command = static_cast<CommandId>(m_currentCmd);
    frame.payload = m_payloadBuffer;
    frame.valid = true;
    emit frameReceived(frame);

    switch (frame.command) {
    case CommandId::REPORT_INPUT:
        parseReportInput(frame.payload);
        break;
    case CommandId::ACK:
        parseAck(frame.payload);
        break;
    case CommandId::HEARTBEAT:
        emit heartbeatReceived();
        break;
    default:
        // PC->MCU 方向的命令在上位机侧不应出现，仅通过 frameReceived 透传
        break;
    }
}

/*!
 * \brief 解析 REPORT_INPUT 载荷(16B, 小端)
 *
 * [0-1] PWM 占空比(0.1%) [2] 继电器位图 [3-10] ADC1-4
 * [11-12] GPIO 输入位图 [13-15] 保留
 */
void ProtocolCodec::parseReportInput(const QByteArray &payload)
{
    if (static_cast<size_t>(payload.size()) != PAYLOAD_LEN_REPORT_INPUT) {
        emit parseError(tr("REPORT_INPUT 长度错误：期望 %1，实际 %2")
                            .arg(PAYLOAD_LEN_REPORT_INPUT)
                            .arg(payload.size()));
        return;
    }

    ReportInputData data;
    data.motorPwmDuty = readU16(payload, 0);
    data.relayBitmap = static_cast<uint8_t>(payload.at(2));
    for (int i = 0; i < 4; ++i) {
        data.adcValues[i] = readU16(payload, 3 + i * 2);
    }
    data.gpioBitmap = readU16(payload, 11);

    emit reportInputReceived(data);
}

/*!
 * \brief 解析 ACK 载荷: [对应CMD 1B][状态 1B]
 */
void ProtocolCodec::parseAck(const QByteArray &payload)
{
    if (static_cast<size_t>(payload.size()) != PAYLOAD_LEN_ACK) {
        emit parseError(tr("ACK 长度错误：期望 %1，实际 %2")
                            .arg(PAYLOAD_LEN_ACK)
                            .arg(payload.size()));
        return;
    }

    emit ackReceived(static_cast<CommandId>(static_cast<uint8_t>(payload.at(0))),
                     static_cast<AckStatus>(static_cast<uint8_t>(payload.at(1))));
}

} // namespace whm
