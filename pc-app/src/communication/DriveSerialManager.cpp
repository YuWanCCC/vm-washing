#include "DriveSerialManager.h"
#include <QSerialPortInfo>

namespace whm {

bool DriveResponseQueue::enqueue(const QByteArray &frame)
{
    if (!DriveCodec::valid(frame, false) || m_frames.size() >= 64) return false;
    m_frames.enqueue(frame);
    return true;
}

QByteArray DriveResponseQueue::take()
{
    return m_frames.isEmpty() ? QByteArray{} : m_frames.dequeue();
}

void DriveResponseQueue::cancel(int address)
{
    for (int i = m_frames.size() - 1; i >= 0; --i)
        if (quint8(m_frames[i][2]) == address) m_frames.removeAt(i);
}

DriveSerialManager::DriveSerialManager(QObject *parent)
    : QObject(parent), m_serial(new QSerialPort(this)), m_simulator(new DriveSimulator(this))
{
    m_clock.start();
    m_tick.setInterval(20);
    m_tick.setTimerType(Qt::PreciseTimer);
    m_retry.setSingleShot(true);
    m_retry.setInterval(1);
    connect(&m_retry, &QTimer::timeout, this, &DriveSerialManager::pump);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (!connected()) return;
        const auto now = m_clock.elapsed();
        process(m_parser.expire(now));
        m_simulator->advance(now);
    });
    connect(m_serial, &QSerialPort::readyRead, this, [this] {
        const auto data = m_serial->readAll();
        if (connected()) process(m_parser.feed(data, m_clock.elapsed()));
    });
    connect(m_serial, &QSerialPort::bytesWritten, this, [this](qint64 count) {
        if (!connected() || m_current.isEmpty()) return;
        m_written += count;
        if (m_written >= m_current.size() && m_submitted >= m_current.size()) {
            const auto frame = m_current;
            m_current.clear();
            m_submitted = m_written = 0;
            ++m_tx;
            m_simulator->markTransmitted(quint8(frame[2]), m_clock.elapsed());
            log("TX", quint8(frame[2]), tr("应答 CRC=OK（串口已写出）"), frame);
            emit statsChanged();
        }
        m_retry.start();
    });
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || m_closing || !connected()) return;
        fail(tr("驱动串口异常：%1").arg(m_serial->errorString()));
    });
    connect(m_simulator, &DriveSimulator::boardInvalidated, this, &DriveSerialManager::cancel);
    connect(m_simulator, &DriveSimulator::eventOccurred, this, [this](int address, const QString &message) {
        log("EVT", address, message);
    });
    refreshPorts();
}

DriveSerialManager::~DriveSerialManager()
{
    m_closing = true;
    m_tick.stop();
    m_retry.stop();
    if (m_serial->isOpen()) m_serial->close();
}

void DriveSerialManager::setPortName(const QString &name)
{
    if (connected() || name == m_portName) return;
    m_portName = name;
    emit portNameChanged();
}

void DriveSerialManager::refreshPorts()
{
    QStringList ports;
    for (const auto &info : QSerialPortInfo::availablePorts()) ports.append(info.portName());
    ports.sort();
    if (ports == m_ports) return;
    m_ports = ports;
    emit portsChanged();
}

void DriveSerialManager::connectPort()
{
    if (connected()) return;
    if (m_portName.isEmpty()) { fail(tr("请先选择驱动串口")); return; }
    m_serial->setPortName(m_portName);
    const bool configured = m_serial->setBaudRate(4800)
        && m_serial->setDataBits(QSerialPort::Data8)
        && m_serial->setParity(QSerialPort::NoParity)
        && m_serial->setStopBits(QSerialPort::OneStop)
        && m_serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!configured || !m_serial->open(QIODevice::ReadWrite)) {
        fail(tr("打开 %1 @4800 8N1 失败：%2").arg(m_portName, m_serial->errorString()));
        return;
    }
    ++m_generation;
    m_parser.reset();
    m_queue.clear();
    m_current.clear();
    m_submitted = m_written = 0;
    m_serial->clear();
    m_state = Connected;
    m_simulator->beginSession(m_clock.elapsed());
    m_tick.start();
    emit connectionChanged();
    log("EVT", 0, tr("已连接 %1 @4800 8N1，等待主控请求；不主动发送").arg(m_portName));
}

void DriveSerialManager::shutdown(ConnectionState state)
{
    m_closing = true;
    ++m_generation;
    m_tick.stop();
    m_retry.stop();
    m_queue.clear();
    m_current.clear();
    m_submitted = m_written = 0;
    m_parser.reset();
    if (m_serial->isOpen()) {
        m_serial->clear();
        m_serial->close();
    }
    m_state = state;
    m_simulator->endSession(m_clock.elapsed());
    m_closing = false;
    emit connectionChanged();
}

void DriveSerialManager::disconnectPort()
{
    shutdown(Disconnected);
    log("EVT", 0, tr("已断开，负载停机，待发应答已取消"));
}

void DriveSerialManager::fail(const QString &message)
{
    ++m_serialErrors;
    shutdown(Error);
    log("EVT", 0, message);
    emit statsChanged();
    emit errorOccurred(message);
}

QVariantMap DriveSerialManager::stats() const
{
    return {{"tx", m_tx}, {"rx", m_rx}, {"crc", m_crcErrors}, {"length", m_lengthErrors},
            {"gap", m_gapErrors}, {"ignored", m_ignored}, {"serial", m_serialErrors}, {"queue", m_queueErrors}};
}

void DriveSerialManager::clearStats()
{
    m_tx = m_rx = m_crcErrors = m_lengthErrors = m_gapErrors = 0;
    m_ignored = m_serialErrors = m_queueErrors = 0;
    emit statsChanged();
}

void DriveSerialManager::log(const QString &kind, int address, const QString &message, const QByteArray &raw)
{
    QString detail = QStringLiteral("ADDR=%1 %2").arg(address).arg(message);
    if (!raw.isEmpty()) detail += QStringLiteral("  LEN=%1 HEX=%2")
        .arg(raw.size()).arg(QString::fromLatin1(raw.toHex(' ').toUpper()));
    emit logEntry(kind, address, detail);
}

void DriveSerialManager::process(const QVector<DriveParseEvent> &events)
{
    for (const auto &event : events) {
        if (!connected()) break;
        switch (event.kind) {
        case DriveParseEvent::Request: {
            ++m_rx;
            log("RX", event.address, tr("主控请求 CRC=OK"), event.raw);
            if (!m_simulator->enabled(event.address)) {
                ++m_ignored;
                log("EVT", event.address, tr("板卡未启用，忽略且不刷新看门狗"));
                break;
            }
            const auto reply = m_simulator->receive({event.address, event.raw, true}, m_clock.elapsed());
            if (!reply.isEmpty() && !m_queue.enqueue(reply)) {
                ++m_queueErrors;
                log("EVT", event.address, tr("应答队列已满，丢弃本次应答"));
            }
            break;
        }
        case DriveParseEvent::CrcError:
            ++m_crcErrors;
            log("RX", event.address, tr("CRC=错误，不应答"), event.raw);
            break;
        case DriveParseEvent::LengthError:
            ++m_lengthErrors;
            log("RX", event.address, tr("长度错误，重新同步"), event.raw);
            break;
        case DriveParseEvent::GapTimeout:
            ++m_gapErrors;
            log("EVT", event.address, tr("接收空闲超过50ms，丢弃半帧"), event.raw);
            break;
        case DriveParseEvent::Echo:
        case DriveParseEvent::Unknown:
            ++m_ignored;
            log("RX", event.address, event.kind == DriveParseEvent::Echo
                ? tr("应答回显 CRC=OK，不应答") : tr("未知地址 CRC=OK，不应答"), event.raw);
            break;
        case DriveParseEvent::Noise:
            log("EVT", 0, tr("丢弃非帧数据"), event.raw);
            break;
        }
    }
    emit statsChanged();
    pump();
}

void DriveSerialManager::pump()
{
    if (!connected()) return;
    if (m_current.isEmpty()) {
        m_current = m_queue.take();
        m_submitted = m_written = 0;
    }
    if (m_current.isEmpty() || m_submitted >= m_current.size()) return;
    const auto generation = m_generation;
    const auto count = m_serial->write(m_current.constData() + m_submitted,
                                      m_current.size() - m_submitted);
    if (generation != m_generation || !connected()) return;
    if (count < 0) { fail(tr("驱动应答写入失败：%1").arg(m_serial->errorString())); return; }
    m_submitted += count;
    if (m_submitted < m_current.size()) m_retry.start();
}

void DriveSerialManager::cancel(int address)
{
    m_queue.cancel(address);
    if (!m_current.isEmpty() && quint8(m_current[2]) == address) {
        ++m_generation;
        m_current.clear();
        m_submitted = m_written = 0;
        if (connected() && !m_closing) {
            m_serial->clear(QSerialPort::Output);
            log("EVT", address, tr("取消未完成应答；已在线路发送的字节无法撤回"));
        }
    }
    if (connected() && !m_closing) m_retry.start();
}

} // namespace whm
