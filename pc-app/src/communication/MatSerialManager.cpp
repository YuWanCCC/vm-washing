#include "MatSerialManager.h"
#include "MatCodec.h"

#include <QSerialPortInfo>
#include <QTimer>

namespace whm {

/* =========================================================================
 * 调试辅助
 * ========================================================================= */

QString MatSerialManager::formatHex(const QByteArray &data, int maxBytes)
{
    if (data.isEmpty())
        return QStringLiteral("(empty)");
    QString hex;
    const int limit = qMin(data.size(), maxBytes);
    for (int i = 0; i < limit; ++i) {
        if (i > 0) hex += QChar(' ');
        hex += QString::number(static_cast<uint8_t>(data.at(i)), 16)
               .rightJustified(2, QChar('0')).toUpper();
    }
    if (data.size() > maxBytes)
        hex += QStringLiteral(" ...");
    return hex;
}

/* =========================================================================
 * 构造 / 析构
 * ========================================================================= */

MatSerialManager::MatSerialManager(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(m_pollIntervalMs);

    connect(m_serial, &QSerialPort::readyRead,
            this, &MatSerialManager::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred,
            this, &MatSerialManager::onSerialError);
    connect(m_pollTimer, &QTimer::timeout,
            this, &MatSerialManager::onPollTimer);

    refreshPorts();
}

MatSerialManager::~MatSerialManager()
{
    if (m_serial->isOpen())
        m_serial->close();
}

/* ==========================================================================
 * 连接管理
 * ========================================================================== */

void MatSerialManager::connectPort()
{
    if (m_serial->isOpen())
        return;

    if (m_portName.isEmpty()) {
        emit errorOccurred(tr("未指定串口号"));
        emit debugMessage(QStringLiteral("[ERR] 未指定串口号，无法连接"));
        setState(Error);
        return;
    }

    emit debugMessage(QStringLiteral("[CON] 正在打开 %1 @ %2")
                      .arg(m_portName).arg(m_baudRate));

    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString reason = tr("打开串口 %1 失败：%2").arg(m_portName, m_serial->errorString());
        ++m_errorCount;
        emit statsChanged();
        emit errorOccurred(reason);
        emit debugMessage(QStringLiteral("[ERR] %1").arg(reason));
        setState(Error);
        return;
    }

    m_serial->clear();
    m_rxBuf.clear();

    setState(Connected);
    emit debugMessage(QStringLiteral("[CON] 已连接 %1 @ %2")
                      .arg(m_portName).arg(m_baudRate));

    if (m_pollEnabled)
        m_pollTimer->start();

    // 连接后立即发一帧查询，得到初始状态
    sendQuery();
}

void MatSerialManager::disconnectPort()
{
    m_pollTimer->stop();

    if (m_serial->isOpen())
        m_serial->close();

    m_rxBuf.clear();
    emit debugMessage(QStringLiteral("[CON] 已断开 %1").arg(m_portName));
    setState(Disconnected);
}

void MatSerialManager::refreshPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    ports.reserve(infos.size());
    for (const QSerialPortInfo &info : infos)
        ports.append(info.portName());

    if (ports != m_availablePorts) {
        m_availablePorts = ports;
        emit portsChanged();
    }
}

/* ==========================================================================
 * 槽函数
 * ========================================================================== */

void MatSerialManager::onReadyRead()
{
    m_rxBuf.append(m_serial->readAll());

    // 循环提取帧，直到缓冲不足或无帧头
    while (!m_rxBuf.isEmpty()) {
        int consumed = 0;
        MatFrame frame = MatCodec::parseFrame(m_rxBuf, &consumed);

        if (consumed > 0 && !frame.isValid) {
            // 丢弃垃圾字节
            m_rxBuf.remove(0, consumed);
            continue;
        }

        if (!frame.isValid) {
            // 数据不足，等待更多字节
            break;
        }

        // 成功解析一帧
        m_rxBuf.remove(0, consumed);
        ++m_rxCount;
        emit statsChanged();
        emit debugMessage(
            QStringLiteral("[RX] CMD=0x%1  sdu_len=%2  sdu=%3")
            .arg(frame.command, 2, 16, QChar('0')).toUpper()
            .arg(frame.sdu.size())
            .arg(formatHex(frame.sdu)));

        emit frameReceived(frame.command, frame.sdu);

        // 尝试解析查询回复
        if (frame.command == static_cast<uint8_t>(MatCommand::Query)) {
            MatStatus st = MatCodec::decodeStatus(frame);
            if (st.isValid) {
                emit statusReceived(st.workStatus, st.faultCode,
                                    st.motorRpm, st.waterLevel,
                                    st.waterTemp, st.remainMin,
                                    st.remainSec, st.rawBytes);
            }
        }
    }
}

void MatSerialManager::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    const QString msg = tr("串口错误：%1").arg(m_serial->errorString());
    ++m_errorCount;
    emit statsChanged();
    emit errorOccurred(msg);
    emit debugMessage(QStringLiteral("[ERR] %1").arg(msg));

    if (error == QSerialPort::ResourceError
        || error == QSerialPort::DeviceNotFoundError) {
        // 设备拔出，关闭端口
        m_pollTimer->stop();
        if (m_serial->isOpen())
            m_serial->close();
        m_rxBuf.clear();
        setState(Error);
    }
}

void MatSerialManager::onPollTimer()
{
    if (m_serial->isOpen())
        sendQuery();
}

/* ==========================================================================
 * 发送
 * ========================================================================== */

void MatSerialManager::sendRaw(const QByteArray &frame)
{
    if (frame.isEmpty())
        return;

    if (!m_serial->isOpen()) {
        ++m_errorCount;
        emit statsChanged();
        emit errorOccurred(tr("串口未连接，发送被忽略"));
        emit debugMessage(QStringLiteral("[ERR] 串口未连接，发送被忽略"));
        return;
    }

    const qint64 written = m_serial->write(frame);
    if (written != frame.size()) {
        ++m_errorCount;
        emit statsChanged();
        emit errorOccurred(tr("串口写入不完整：%1/%2 字节").arg(written).arg(frame.size()));
        return;
    }

    // Windows 下 QSerialPort::write() 仅放入内部缓冲区，
    // 必须显式 flush 确保数据推送到物理串口，避免事件循环延迟导致数据滞留
    m_serial->flush();

    ++m_txCount;
    emit statsChanged();
    emit debugMessage(QStringLiteral("[TX] %1 bytes: %2")
                      .arg(frame.size()).arg(formatHex(frame)));
}

void MatSerialManager::sendQuery()
{
    sendRaw(MatCodec::encodeQuery());
}

void MatSerialManager::sendControl(int b1, int b2,
                                   int b3, int b4, int b5, int b6)
{
    sendRaw(MatCodec::encodeControl(
        static_cast<uint8_t>(b1), static_cast<uint8_t>(b2),
        static_cast<uint8_t>(b3), static_cast<uint8_t>(b4),
        static_cast<uint8_t>(b5), static_cast<uint8_t>(b6)));
}

/* ==========================================================================
 * 属性访问
 * ========================================================================== */

bool MatSerialManager::isConnected() const { return m_state == Connected; }
QString MatSerialManager::portName() const { return m_portName; }
int MatSerialManager::baudRate() const { return m_baudRate; }
MatSerialManager::ConnectionState MatSerialManager::connectionState() const { return m_state; }
QStringList MatSerialManager::availablePorts() const { return m_availablePorts; }
int MatSerialManager::txCount() const { return m_txCount; }
int MatSerialManager::rxCount() const { return m_rxCount; }
int MatSerialManager::errorCount() const { return m_errorCount; }
bool MatSerialManager::isPollEnabled() const { return m_pollEnabled; }
int MatSerialManager::pollIntervalMs() const { return m_pollIntervalMs; }

void MatSerialManager::setPortName(const QString &name)
{
    if (m_portName == name) return;
    m_portName = name;
    emit portNameChanged();
}

void MatSerialManager::setBaudRate(int rate)
{
    if (m_baudRate == rate || rate <= 0) return;
    m_baudRate = rate;
    if (m_serial->isOpen())
        m_serial->setBaudRate(rate);
    emit baudRateChanged();
}

void MatSerialManager::setPollEnabled(bool enabled)
{
    if (m_pollEnabled == enabled) return;
    m_pollEnabled = enabled;
    if (m_serial->isOpen()) {
        if (enabled) m_pollTimer->start();
        else         m_pollTimer->stop();
    }
    emit pollEnabledChanged();
}

void MatSerialManager::setPollIntervalMs(int ms)
{
    if (m_pollIntervalMs == ms || ms <= 0) return;
    m_pollIntervalMs = ms;
    m_pollTimer->setInterval(ms);
    emit pollIntervalChanged();
}

void MatSerialManager::setState(ConnectionState state)
{
    if (m_state == state) return;
    const bool wasConnected = isConnected();
    m_state = state;
    emit connectionStateChanged();
    if (wasConnected != isConnected())
        emit connectionChanged(isConnected());
}

} // namespace whm
