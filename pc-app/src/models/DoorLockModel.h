#ifndef WHM_DOORLOCKMODEL_H
#define WHM_DOORLOCKMODEL_H

#include <QObject>

namespace whm {

/*!
 * \brief 门锁模型：PTC 门锁上锁/解锁的延时状态机。
 *
 * 状态迁移：
 *   Unlocked --requestLock()--> Locking --(lockDelay)--> Locked
 *   Locked --requestUnlock()--> Unlocking --(unlockDelay)--> Unlocked
 * 过渡过程中的反向请求会被记录，待当前迁移完成后立即执行。
 */
class DoorLockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DoorState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY stateChanged)
    Q_PROPERTY(bool isTransitioning READ isTransitioning NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
    enum DoorState {
        Unlocked,
        Locking,
        Locked,
        Unlocking
    };
    Q_ENUM(DoorState)

    explicit DoorLockModel(QObject *parent = nullptr);

    //! 推进模型 dt 秒
    void update(double dt);
    //! 立即回到解锁状态
    void reset();

    Q_INVOKABLE void requestLock();
    Q_INVOKABLE void requestUnlock();

    DoorState state() const;
    bool isLocked() const;
    bool isTransitioning() const;
    QString stateText() const;
    //! 当前迁移进度 0.0-1.0（非迁移状态返回 1.0）
    double progress() const;

    void setLockDelay(double seconds);
    void setUnlockDelay(double seconds);
    double lockDelay() const;
    double unlockDelay() const;

signals:
    void stateChanged();
    void progressChanged();
    void locked();
    void unlocked();

private:
    void setState(DoorState state);
    void applyPendingRequest();

    DoorState m_state = Unlocked;
    double m_transitionTimer = 0.0;   //!< 当前迁移已用时间 (s)
    double m_lockDelay = 1.5;         //!< 上锁耗时 (s)
    double m_unlockDelay = 1.0;       //!< 解锁耗时 (s)
    int m_pendingRequest = 0;         //!< 0: 无, 1: 上锁, -1: 解锁
};

} // namespace whm

#endif // WHM_DOORLOCKMODEL_H
