#include "SerialManager.h"

#include <QDateTime>
#include <QSerialPortInfo>
#include <QTimer>

namespace whm {

SerialManager::SerialManager(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_codec(new ProtocolCodec(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);

    connect(m_serial, &QSerialPort::readyRead, this, &SerialManager::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialManager::onSerialError);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &SerialManager::onHeartbeatTimer);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::onReconnectTimer);

    // 任何有效帧都视为链路存活证据，而不仅仅是心跳回复
    connect(m_codec, &ProtocolCodec::frameReceived, this, [this](const ProtocolFrame &) {
        ++m_rxCount;
        refreshLiveness();
        emit statsChanged();
    });
    connect(m_codec, &ProtocolCodec::reportInputReceived, this, &SerialManager::dataReceived);
    connect(m_codec, &ProtocolCodec::ackReceived, this, &SerialManager::ackReceived);
    connect(m_codec, &ProtocolCodec::heartbeatReceived, this, &SerialManager::heartbeatReceived);
    connect(m_codec, &ProtocolCodec::parseError, this, [this](const QString &error) {
        countError();
        emit errorOccurred(error);
    });

    refreshPorts();
}

SerialManager::~SerialManager()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
}

/* ==========================================================================
 * 连接管理
 * ========================================================================== */

void SerialManager::connectPort()
{
    if (m_serial->isOpen()) {
        return;
    }

    if (m_portName.isEmpty()) {
        emit errorOccurred(tr("未指定串口号"));
        setState(Error);
        return;
    }

    setState(Connecting);

    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString reason = tr("打开串口 %1 失败：%2").arg(m_portName, m_serial->errorString());
        countError();
        emit errorOccurred(reason);
        setState(Error);
        // 首次打开失败也纳入自动重连，便于设备上电顺序不确定时自愈
        m_autoReconnect = true;
        m_reconnectTimer->start();
        return;
    }

    m_serial->clear();
    m_codec->reset();

    m_autoReconnect = true;
    m_reconnectTimer->stop();
    refreshLiveness();
    m_heartbeatTimer->start();

    setState(Connected);

    // 连接建立后立即索取一次全量输入，避免界面等待首个上报周期
    sendReadAll();
}

void SerialManager::disconnectPort()
{
    // 用户主动断开：关闭自动重连，避免与用户意图冲突
    m_autoReconnect = false;
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();

    if (m_serial->isOpen()) {
        m_serial->close();
    }
    m_codec->reset();

    setState(Disconnected);
}

void SerialManager::refreshPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    ports.reserve(infos.size());
    for (const QSerialPortInfo &info : infos) {
        ports.append(info.portName());
    }

    if (ports != m_availablePorts) {
        m_availablePorts = ports;
        emit portsChanged();
    }
}

/*!
 * \brief 链路异常统一处理：关闭端口、置 Error 态并启动重连
 */
void SerialManager::handleLinkFailure(const QString &reason)
{
    m_heartbeatTimer->stop();

    if (m_serial->isOpen()) {
        m_serial->close();
    }
    m_codec->reset();

    countError();
    emit errorOccurred(reason);
    setState(Error);

    if (m_autoReconnect && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

void SerialManager::refreshLiveness()
{
    m_lastHeartbeatTime = QDateTime::currentMSecsSinceEpoch();
}

void SerialManager::countError()
{
    ++m_errorCount;
    emit statsChanged();
}

/* ==========================================================================
 * 槽函数
 * ========================================================================== */

void SerialManager::onReadyRead()
{
    const QByteArray data = m_serial->readAll();
    if (!data.isEmpty()) {
        m_codec->feedData(data);
    }
}

void SerialManager::onSerialError(QSerialPort::SerialPortError error)
{
    switch (error) {
    case QSerialPort::NoError:
        return;
    case QSerialPort::ResourceError:
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::TimeoutError:
    case QSerialPort::WriteError:
    case QSerialPort::ReadError:
        // 设备被拔出或不可访问，必须关闭端口后重连
        handleLinkFailure(tr("串口错误：%1").arg(m_serial->errorString()));
        break;
    default:
        countError();
        emit errorOccurred(tr("串口错误：%1").arg(m_serial->errorString()));
        break;
    }
}

/*!
 * \brief 心跳定时器：发送心跳并检测接收超时
 */
void SerialManager::onHeartbeatTimer()
{
    if (!m_serial->isOpen()) {
        return;
    }

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastHeartbeatTime;
    if (elapsed > HEARTBEAT_TIMEOUT_MS) {
        handleLinkFailure(tr("心跳超时 %1ms，判定链路断开").arg(elapsed));
        return;
    }

    sendHeartbeat();
}

void SerialManager::onReconnectTimer()
{
    if (!m_autoReconnect) {
        m_reconnectTimer->stop();
        return;
    }
    if (m_serial->isOpen()) {
        m_reconnectTimer->stop();
        return;
    }

    // 设备重新枚举后端口名可能变化，重连前刷新一次列表
    refreshPorts();
    connectPort();
}

/* ==========================================================================
 * 发送
 * ========================================================================== */

void SerialManager::sendRaw(const QByteArray &frame)
{
    if (frame.isEmpty()) {
        return;
    }

    if (!m_serial->isOpen()) {
        countError();
        emit errorOccurred(tr("串口未连接，发送被忽略"));
        return;
    }

    const qint64 written = m_serial->write(frame);
    if (written != frame.size()) {
        countError();
        emit errorOccurred(tr("串口写入不完整：%1/%2 字节").arg(written).arg(frame.size()));
        return;
    }

    ++m_txCount;
    emit statsChanged();
}

void SerialManager::sendBatchSet(const BatchSetData &data)
{
    sendRaw(m_codec->encodeBatchSet(data));
}

void SerialManager::sendSetFrequency(uint8_t channel, uint32_t freqHz)
{
    sendRaw(m_codec->encodeSetFrequency(channel, freqHz));
}

void SerialManager::sendSetAnalog(uint8_t channel, uint16_t value)
{
    sendRaw(m_codec->encodeSetAnalog(channel, value));
}

void SerialManager::sendSetDigital(uint8_t port, uint8_t state)
{
    sendRaw(m_codec->encodeSetDigital(port, state));
}

void SerialManager::sendReadAll()
{
    sendRaw(m_codec->encodeReadAll());
}

void SerialManager::sendHeartbeat()
{
    sendRaw(m_codec->encodeHeartbeat());
}

/* ==========================================================================
 * 属性访问
 * ========================================================================== */

bool SerialManager::isConnected() const
{
    return m_state == Connected;
}

QString SerialManager::portName() const
{
    return m_portName;
}

void SerialManager::setPortName(const QString &name)
{
    if (m_portName == name) {
        return;
    }
    m_portName = name;
    emit portNameChanged();
}

int SerialManager::baudRate() const
{
    return m_baudRate;
}

void SerialManager::setBaudRate(int rate)
{
    if (m_baudRate == rate || rate <= 0) {
        return;
    }
    m_baudRate = rate;
    // 已打开的端口支持在线改波特率
    if (m_serial->isOpen()) {
        m_serial->setBaudRate(rate);
    }
    emit baudRateChanged();
}

SerialManager::ConnectionState SerialManager::connectionState() const
{
    return m_state;
}

QStringList SerialManager::availablePorts() const
{
    return m_availablePorts;
}

int SerialManager::txCount() const
{
    return m_txCount;
}

int SerialManager::rxCount() const
{
    return m_rxCount;
}

int SerialManager::errorCount() const
{
    return m_errorCount;
}

void SerialManager::setState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }

    const bool wasConnected = isConnected();
    m_state = state;
    emit connectionStateChanged();

    if (wasConnected != isConnected()) {
        emit connectionChanged(isConnected());
    }
}

} // namespace whm
