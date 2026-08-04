#ifndef WHM_PROTOCOLCODEC_H
#define WHM_PROTOCOLCODEC_H

#include <cstddef>
#include <cstdint>

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace whm {

/* ==========================================================================
 * 帧格式（与固件 firmware/Core/Inc/protocol_parser.h 严格一致）
 *
 *   +------+-----+-----+---------+-------+-------+------+
 *   | 0xAA | CMD | LEN | PAYLOAD | CRC_L | CRC_H | 0x55 |
 *   |  1B  | 1B  | 1B  | 0-250B  |  1B   |  1B   |  1B  |
 *   +------+-----+-----+---------+-------+-------+------+
 *
 * - CRC16-CCITT 覆盖范围: CMD + LEN + PAYLOAD（不含帧头/帧尾）
 * - CRC 传输顺序: 低字节先（小端）
 * - 载荷内多字节数值统一小端序
 * ========================================================================== */

//! 帧头
constexpr uint8_t FRAME_HEADER = 0xAA;
//! 帧尾
constexpr uint8_t FRAME_TAIL = 0x55;
//! 最小帧长: 头 + CMD + LEN + CRC(2) + 尾
constexpr size_t FRAME_MIN_SIZE = 6;
//! 最大帧长
constexpr size_t FRAME_MAX_SIZE = 256;
//! 最大载荷长度
constexpr size_t PAYLOAD_MAX_SIZE = 250;

//! 各命令的固定载荷长度
constexpr size_t PAYLOAD_LEN_SET_ANALOG    = 3;
constexpr size_t PAYLOAD_LEN_SET_DIGITAL   = 2;
constexpr size_t PAYLOAD_LEN_SET_FREQUENCY = 5;
constexpr size_t PAYLOAD_LEN_BATCH_SET     = 16;
constexpr size_t PAYLOAD_LEN_REPORT_INPUT  = 16;
constexpr size_t PAYLOAD_LEN_ACK           = 2;

//! 命令码
enum class CommandId : uint8_t {
    SET_ANALOG    = 0x01,   //!< PC->MCU [通道1B][值2B]
    SET_DIGITAL   = 0x02,   //!< PC->MCU [端口1B][状态1B]
    SET_FREQUENCY = 0x03,   //!< PC->MCU [通道1B][频率4B]
    READ_ALL      = 0x04,   //!< PC->MCU 无载荷
    BATCH_SET     = 0x05,   //!< PC->MCU 16B 批量数据
    REPORT_INPUT  = 0x81,   //!< MCU->PC 16B 输入数据
    ACK           = 0x82,   //!< MCU->PC [对应CMD1B][状态1B]
    HEARTBEAT     = 0xFE    //!< 双向, 无载荷
};

/*!
 * \brief ACK 状态码。
 *
 * 取值与固件 protocol_parser.h 中的 ACK_* 宏一一对应。
 * 注意：固件对 CRC 校验失败的帧直接静默丢弃，不会回 ACK，
 * 因此不存在 "CRC 错误" 状态码。
 */
enum class AckStatus : uint8_t {
    OK              = 0x00,   //!< 执行成功
    ERR_LENGTH      = 0x01,   //!< 载荷长度错误
    ERR_PARAM       = 0x02,   //!< 参数越界（通道号/数值）
    ERR_UNKNOWN_CMD = 0x03,   //!< 未知命令
    ERR_BUSY        = 0x04,   //!< 发送忙, 未能执行
    ERR_EXECUTE     = ERR_BUSY //!< 兼容别名：执行失败
};

//! SET_DIGITAL 端口编号
enum class DigitalPort : uint8_t {
    DoorLock   = 0x00,
    InletValve = 0x01,
    DrainPump  = 0x02,
    Heater     = 0x03,
    All        = 0xFF   //!< 状态低 4 位作为位图整体写入
};

//! SET_FREQUENCY / BATCH_SET 涉及的 PWM 通道编号
enum class PwmChannel : uint8_t {
    WaterLevel = 0x00,
    Hall       = 0x01
};

//! 解析完成且 CRC 校验通过的接收帧
struct ProtocolFrame
{
    CommandId command = CommandId::HEARTBEAT;
    QByteArray payload;
    bool valid = false;
};

//! BATCH_SET 载荷（16B，小端）
struct BatchSetData
{
    uint32_t waterLevelFreq = 0;   //!< 水位传感器频率 (Hz)   偏移 0-3
    uint16_t ntcDacValue    = 0;   //!< NTC DAC 原始值 (12bit) 偏移 4-5
    uint32_t hallFreq       = 0;   //!< 电机 Hall 频率 (Hz)   偏移 6-9
    uint8_t doorLock        = 0;   //!< 0=解锁, 1=上锁        偏移 10
    uint8_t inletValve      = 0;   //!< 0=关, 1=开            偏移 11
    uint8_t drainPump       = 0;   //!< 0=停, 1=转            偏移 12
    uint8_t heater          = 0;   //!< 0=关, 1=开            偏移 13
};

//! REPORT_INPUT 载荷（16B，小端）
struct ReportInputData
{
    uint16_t motorPwmDuty = 0;      //!< 电机 PWM 占空比，0.1% 单位 (0-1000) 偏移 0-1
    uint8_t relayBitmap   = 0;      //!< bit0=门锁 bit1=进水阀 bit2=排水泵 bit3=加热器 偏移 2
    uint16_t adcValues[4] {};       //!< 12bit ADC 原始值      偏移 3-10
    uint16_t gpioBitmap   = 0;      //!< GPIO 输入位图         偏移 11-12
};

/*!
 * \brief 协议编解码器：负责帧的构造与字节流的逐字节同步解析。
 *
 * 解析器为逐字节状态机，可正确处理分包（半帧）、粘包（多帧连在一起）
 * 与噪声数据；遇到非法长度/CRC 错误/帧尾错误时丢弃当前帧并重新同步，
 * 且若出错字节本身是帧头会立即作为新帧起点，避免连带丢帧。
 */
class ProtocolCodec : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolCodec(QObject *parent = nullptr);

    // --- 编码：构造发送帧 ---
    QByteArray encodeSetAnalog(uint8_t channel, uint16_t value);
    QByteArray encodeSetDigital(uint8_t port, uint8_t state);
    QByteArray encodeSetFrequency(uint8_t channel, uint32_t freqHz);
    QByteArray encodeReadAll();
    QByteArray encodeBatchSet(const BatchSetData &data);
    QByteArray encodeHeartbeat();

    //! 将收到的字节流喂入解析器，解析出完整帧时发射相应信号
    void feedData(const QByteArray &data);

    //! 清空解析器状态（串口重连时调用，避免残留半帧）
    void reset();

signals:
    void frameReceived(const ProtocolFrame &frame);
    void reportInputReceived(const ReportInputData &data);
    void ackReceived(CommandId cmd, AckStatus status);
    void heartbeatReceived();
    void parseError(const QString &error);

private:
    //! 帧同步状态机状态（CRC 顺序为低字节先，与固件一致）
    enum ParseState {
        WaitHeader,
        RecvCmd,
        RecvLen,
        RecvPayload,
        RecvCrcLow,
        RecvCrcHigh,
        RecvTail
    };

    void parseByte(uint8_t byte);
    QByteArray buildFrame(CommandId cmd, const QByteArray &payload);
    void processCompleteFrame();
    void parseReportInput(const QByteArray &payload);
    void parseAck(const QByteArray &payload);
    void resetParser();
    //! 出错后重新同步：若当前字节即帧头则直接作为新帧起点
    void resyncOn(uint8_t byte);

    ParseState m_parseState = WaitHeader;
    uint8_t m_currentCmd = 0;
    uint8_t m_payloadLen = 0;
    QByteArray m_payloadBuffer;
    uint8_t m_crcHigh = 0;
    uint8_t m_crcLow = 0;
};

} // namespace whm

Q_DECLARE_METATYPE(whm::CommandId)
Q_DECLARE_METATYPE(whm::AckStatus)
Q_DECLARE_METATYPE(whm::ProtocolFrame)
Q_DECLARE_METATYPE(whm::BatchSetData)
Q_DECLARE_METATYPE(whm::ReportInputData)

#endif // WHM_PROTOCOLCODEC_H
