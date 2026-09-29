#ifndef WHM_DRIVESERIALMANAGER_H
#define WHM_DRIVESERIALMANAGER_H

#include "DriveCodec.h"
#include "core/DriveSimulator.h"
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QTimer>

namespace whm {

//! 不含串口对象的有界应答队列，可单独验证取消语义。
class DriveResponseQueue {
public:
    bool enqueue(const QByteArray &frame);
    QByteArray take();
    void cancel(int address);
    void clear() { m_frames.clear(); }
    bool empty() const { return m_frames.isEmpty(); }
    int size() const { return m_frames.size(); }
private:
    QQueue<QByteArray> m_frames;
};

class DriveSerialManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString portName READ portName WRITE setPortName NOTIFY portNameChanged)
    Q_PROPERTY(int baudRate READ baudRate CONSTANT)
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionChanged)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY portsChanged)
    Q_PROPERTY(whm::DriveSimulator* simulator READ simulator CONSTANT)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
public:
    enum ConnectionState { Disconnected, Connected, Error };
    Q_ENUM(ConnectionState)
    explicit DriveSerialManager(QObject *parent = nullptr);
    ~DriveSerialManager() override;
    bool connected() const { return m_state == Connected; }
    QString portName() const { return m_portName; }
    void setPortName(const QString &name);
    int baudRate() const { return 4800; }
    ConnectionState connectionState() const { return m_state; }
    QStringList availablePorts() const { return m_ports; }
    DriveSimulator *simulator() const { return m_simulator; }
    QVariantMap stats() const;
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void connectPort();
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void clearStats();

signals:
    void connectionChanged();
    void portNameChanged();
    void portsChanged();
    void statsChanged();
    void errorOccurred(const QString &message);
    //! kind=TX/RX/EVT；message 包含完整帧、地址、长度和校验结果。
    void logEntry(const QString &kind, int address, const QString &message);

private:
    void process(const QVector<DriveParseEvent> &events);
    void pump();
    void cancel(int address);
    void shutdown(ConnectionState state);
    void fail(const QString &message);
    void log(const QString &kind, int address, const QString &message, const QByteArray &raw = {});
    QSerialPort *m_serial;
    DriveSimulator *m_simulator;
    QTimer m_tick;
    QTimer m_retry;
    QElapsedTimer m_clock;
    DriveStreamParser m_parser;
    DriveResponseQueue m_queue;
    QByteArray m_current;
    qint64 m_submitted = 0;
    qint64 m_written = 0;
    quint64 m_generation = 0;
    ConnectionState m_state = Disconnected;
    bool m_closing = false;
    QString m_portName;
    QStringList m_ports;
    int m_tx = 0, m_rx = 0, m_crcErrors = 0, m_lengthErrors = 0, m_gapErrors = 0;
    int m_ignored = 0, m_serialErrors = 0, m_queueErrors = 0;
};

} // namespace whm
#endif
