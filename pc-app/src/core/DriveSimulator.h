#ifndef WHM_DRIVESIMULATOR_H
#define WHM_DRIVESIMULATOR_H

#include "communication/DriveCodec.h"
#include <QObject>
#include <array>

namespace whm {

//! 独立的协议级板卡仿真，不修改 HIL 物理模型或真实 GPIO。
class DriveSimulator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList boards READ boards NOTIFY changed)
    Q_PROPERTY(int selectedAddress READ selectedAddress WRITE setSelectedAddress NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedBoard READ selectedBoard NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    explicit DriveSimulator(QObject *parent = nullptr);
    QVariantList boards() const;
    QVariantMap snapshot(int address) const;
    QVariantMap selectedBoard() const { return snapshot(m_selected); }
    int selectedAddress() const { return m_selected; }
    void setSelectedAddress(int address);
    bool active() const { return m_active; }
    bool enabled(int address) const;
    Q_INVOKABLE QVariantList fieldsFor(int address, bool request) const;
    Q_INVOKABLE void setEnabled(int address, bool enabled);
    Q_INVOKABLE bool setFeedback(int address, const QString &key, int rawValue);
    Q_INVOKABLE void followDoorLock();
    Q_INVOKABLE void resetBoard(int address);
    void beginSession(qint64 nowMs);
    void endSession(qint64 nowMs);
    void advance(qint64 nowMs);
    QByteArray receive(const DriveFrame &frame, qint64 nowMs);
    void markTransmitted(int address, qint64 nowMs);

signals:
    void changed();
    void selectionChanged();
    //! 串口层据此撤销尚未发送的旧应答。
    void boardInvalidated(int address);
    void eventOccurred(int address, const QString &message);

private:
    struct Board {
        bool enabled = true;
        bool seen = false;
        bool timedOut = false;
        bool commFault = false;
        bool manualLock = false;
        qint64 epochMs = 0;
        qint64 lastRxMs = -1;
        qint64 lastTxMs = -1;
        int rxCount = 0;
        int txCount = 0;
        double signedSpeed = 0;
        double acceleration = 0;
        double actualAcceleration = 0;
        int targetTemperature = 0;
        QVariantMap command;
        QVariantMap settings;
        QVariantMap feedback;
        QByteArray lastRequest;
        QByteArray lastResponse;
    };
    void initialize(int address, bool preserveSettings);
    void updateBoard(int address, double dt);
    static bool validAddress(int address) { return address >= 1 && address <= 4; }
    std::array<Board, 5> m_boards;
    int m_selected = 1;
    bool m_active = false;
    qint64 m_now = 0;
};

} // namespace whm
#endif
