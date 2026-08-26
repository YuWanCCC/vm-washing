#ifndef WHM_MATSERIALMANAGER_H
#define WHM_MATSERIALMANAGER_H

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>

#include "MatCodec.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace whm {

/*!
 * \brief MAT 协议串口管理器
 *
 * PC 直连洗衣机主控板的 MAT 串口（独立于 HIL 的 STM32 串口）。
 * 上位机为主（发起方），主控板为从（应答方）。
 *
 * 职责：
 *   - 串口打开 / 关闭 / 参数配置（波特率 9600 / 19200 可选）
 *   - 发送查询帧、控制帧
 *   - 接收并解析主控回复帧，发射 statusReceived 信号
 *   - 轮询定时器（可选）：定期自动发送查询帧刷新状态
 *   - 调试日志（debugMessage 信号）
 *
 * 无自动重连策略（MAT 口为一次性手动连接），保持简单。
 */
class MatSerialManager : public QObject
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
    Q_PROPERTY(bool pollEnabled READ isPollEnabled WRITE setPollEnabled NOTIFY pollEnabledChanged)
    Q_PROPERTY(int pollIntervalMs READ pollIntervalMs WRITE setPollIntervalMs NOTIFY pollIntervalChanged)

public:
    enum ConnectionState {
        Disconnected,
        Connected,
        Error
    };
    Q_ENUM(ConnectionState)

    explicit MatSerialManager(QObject *parent = nullptr);
    ~MatSerialManager() override;

    // --- 连接管理 ---
    Q_INVOKABLE void connectPort();
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void refreshPorts();

    // --- 发送 MAT 帧 ---
    //! 发送查询帧（Command=0x01）
    Q_INVOKABLE void sendQuery();
    //! 发送控制帧（Command=0x08），b1~b6 为 6 字节执行器位图
    Q_INVOKABLE void sendControl(int b1, int b2,
                                 int b3 = 0, int b4 = 0,
                                 int b5 = 0, int b6 = 0);

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
    bool isPollEnabled() const;
    void setPollEnabled(bool enabled);
    int pollIntervalMs() const;
    void setPollIntervalMs(int ms);

signals:
    void connectionChanged(bool connected);
    void portNameChanged();
    void baudRateChanged();
    void connectionStateChanged();
    void portsChanged();
    void statsChanged();
    //! 收到并解析成功的主控状态回复
    void statusReceived(int workStatus, int faultCode,
                        int motorRpm, int waterLevel, int waterTemp,
                        int remainMin, int remainSec,
                        const QByteArray &rawSdu);
    //! 收到任意有效 MAT 帧（含控制帧回复等）
    void frameReceived(int command, const QByteArray &sdu);
    void errorOccurred(const QString &error);
    //! 调试信息（TX/RX 摘要、连接事件等）
    void debugMessage(const QString &message);
    void pollEnabledChanged();
    void pollIntervalChanged();

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onPollTimer();

private:
    void sendRaw(const QByteArray &frame);
    void setState(ConnectionState state);
    static QString formatHex(const QByteArray &data, int maxBytes = 32);

    QSerialPort *m_serial;
    QTimer *m_pollTimer;         //!< 周期查询定时器

    QString m_portName;
    int m_baudRate = 9600;
    ConnectionState m_state = Disconnected;
    QStringList m_availablePorts;

    int m_txCount = 0;
    int m_rxCount = 0;
    int m_errorCount = 0;

    bool m_pollEnabled = false;
    int m_pollIntervalMs = 500;

    QByteArray m_rxBuf;          //!< 串口接收缓冲（用于跨包拼帧）
};

} // namespace whm

#endif // WHM_MATSERIALMANAGER_H
