#include "models/DoorLockModel.h"

#include <algorithm>

namespace whm {

namespace {
constexpr double kEpsilon = 1.0e-9;
} // namespace

DoorLockModel::DoorLockModel(QObject *parent)
    : QObject(parent)
{
}

void DoorLockModel::update(double dt)
{
    if (dt <= 0.0)
        return;

    if (m_state != Locking && m_state != Unlocking)
        return;

    m_transitionTimer += dt;
    emit progressChanged();

    const double delay = (m_state == Locking) ? m_lockDelay : m_unlockDelay;
    if (m_transitionTimer + kEpsilon < delay)
        return;

    m_transitionTimer = 0.0;
    if (m_state == Locking) {
        setState(Locked);
        emit locked();
    } else {
        setState(Unlocked);
        emit unlocked();
    }
    applyPendingRequest();
}

void DoorLockModel::reset()
{
    m_pendingRequest = 0;
    m_transitionTimer = 0.0;
    if (m_state != Unlocked) {
        setState(Unlocked);
        emit unlocked();
    }
    emit progressChanged();
}

void DoorLockModel::requestLock()
{
    switch (m_state) {
    case Unlocked:
        m_transitionTimer = 0.0;
        setState(Locking);
        break;
    case Unlocking:
        m_pendingRequest = 1;   // 解锁完成后再上锁
        break;
    case Locking:
    case Locked:
        break;                  // 已在上锁流程中
    }
}

void DoorLockModel::requestUnlock()
{
    switch (m_state) {
    case Locked:
        m_transitionTimer = 0.0;
        setState(Unlocking);
        break;
    case Locking:
        m_pendingRequest = -1;  // 上锁完成后再解锁
        break;
    case Unlocking:
    case Unlocked:
        break;
    }
}

DoorLockModel::DoorState DoorLockModel::state() const
{
    return m_state;
}

bool DoorLockModel::isLocked() const
{
    return m_state == Locked;
}

bool DoorLockModel::isTransitioning() const
{
    return m_state == Locking || m_state == Unlocking;
}

QString DoorLockModel::stateText() const
{
    switch (m_state) {
    case Unlocked:
        return QStringLiteral("未锁定");
    case Locking:
        return QStringLiteral("上锁中");
    case Locked:
        return QStringLiteral("已锁定");
    case Unlocking:
        return QStringLiteral("解锁中");
    }
    return QString();
}

double DoorLockModel::progress() const
{
    if (!isTransitioning())
        return 1.0;
    const double delay = (m_state == Locking) ? m_lockDelay : m_unlockDelay;
    if (delay <= kEpsilon)
        return 1.0;
    return std::clamp(m_transitionTimer / delay, 0.0, 1.0);
}

void DoorLockModel::setLockDelay(double seconds)
{
    m_lockDelay = std::max(0.0, seconds);
}

void DoorLockModel::setUnlockDelay(double seconds)
{
    m_unlockDelay = std::max(0.0, seconds);
}

double DoorLockModel::lockDelay() const
{
    return m_lockDelay;
}

double DoorLockModel::unlockDelay() const
{
    return m_unlockDelay;
}

void DoorLockModel::setState(DoorState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
    emit progressChanged();
}

void DoorLockModel::applyPendingRequest()
{
    const int request = m_pendingRequest;
    m_pendingRequest = 0;
    if (request > 0)
        requestLock();
    else if (request < 0)
        requestUnlock();
}

} // namespace whm
