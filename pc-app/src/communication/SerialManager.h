#ifndef WHM_SERIALMANAGER_H
#define WHM_SERIALMANAGER_H

#include <cstdint>

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>

#include "ProtocolCodec.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace whm {

/*!
 * \brief 串口链路管理：负责端口打开/关闭、心跳保活、断线重连与收发统计。
 *
 * 采用 Qt SerialPort 异步模式，readyRead 信号驱动接收，所有串口操作都在
 * 创建本对象的线程（主线程 Qt 事件循环）中完成，无需额外加锁。
 *
 * 保活策略：每 500ms 发送一帧 HEARTBEAT；固件收到后原样回一帧心跳。
 * 若连续 2s 未收到任何有效帧则判定链路失效，关闭端口并每 3s 重连一次。
 */
class SerialManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString portName READ portName WRITE setPortName NOTIFY portNameChanged)
    Q_PROPERTY(int baudRate READ baudRate WRITE setBaudRate NOTIFY baudRateChanged)
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY portsChanged)
    Q_PROPERTY(int txCount READ txCount NOTIFY statsChanged)
    Q_PROPERTY(int rxCount READ rxCount NOTIFY statsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY statsChanged)

public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Error
    };
    Q_ENUM(ConnectionState)

    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager() override;

    // --- 连接管理 ---
    Q_INVOKABLE void connectPort();
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void refreshPorts();

    // --- 发送命令（经 ProtocolCodec 编码后写入串口）---
    Q_INVOKABLE void sendBatchSet(const BatchSetData &data);
    Q_INVOKABLE void sendSetFrequency(uint8_t channel, uint32_t freqHz);
    Q_INVOKABLE void sendSetAnalog(uint8_t channel, uint16_t value);
    Q_INVOKABLE void sendSetDigital(uint8_t port, uint8_t state);
    Q_INVOKABLE void sendReadAll();
    Q_INVOKABLE void sendHeartbeat();

    // --- 属性访问 ---
    bool isConnected() const;
    QString portName() const;
    void setPortName(const QString &name);
    int baudRate() const;
    void setBaudRate(int rate);
    ConnectionState connectionState() const;
    QStringList availablePorts() const;
    int txCount() const;
    int rxCount() const;
    int errorCount() const;

    ProtocolCodec *codec() const { return m_codec; }

signals:
    void connectionChanged(bool connected);
    void portNameChanged();
    void baudRateChanged();
    void connectionStateChanged();
    void portsChanged();
    void statsChanged();
    void dataReceived(const ReportInputData &data);
    void ackReceived(CommandId cmd, AckStatus status);
    void heartbeatReceived();
    void errorOccurred(const QString &error);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onHeartbeatTimer();
    void onReconnectTimer();

private:
    void sendRaw(const QByteArray &frame);
    void setState(ConnectionState state);
    //! 关闭端口并进入 Error 态，启动重连（链路异常/心跳超时时调用）
    void handleLinkFailure(const QString &reason);
    //! 收到任意有效帧时刷新链路存活时间戳
    void refreshLiveness();
    void countError();

    QSerialPort *m_serial;
    ProtocolCodec *m_codec;
    QTimer *m_heartbeatTimer;    //!< 500ms 心跳发送 + 超时检测
    QTimer *m_reconnectTimer;    //!< 断线重连定时器

    QString m_portName;
    int m_baudRate = 115200;
    ConnectionState m_state = Disconnected;
    QStringList m_availablePorts;

    //! 统计（均以"帧"为单位）
    int m_txCount = 0;
    int m_rxCount = 0;
    int m_errorCount = 0;

    //! 心跳超时检测
    qint64 m_lastHeartbeatTime = 0;
    //! 是否允许自动重连（用户主动断开后置 false）
    bool m_autoReconnect = false;

    static constexpr int HEARTBEAT_INTERVAL_MS = 500;
    static constexpr int HEARTBEAT_TIMEOUT_MS = 2000;
    static constexpr int RECONNECT_INTERVAL_MS = 3000;
};

} // namespace whm

#endif // WHM_SERIALMANAGER_H
