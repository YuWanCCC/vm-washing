#include "models/ActuatorModels.h"

namespace whm {

namespace {
constexpr quint8 kInletValveBit = 0x01;
constexpr quint8 kDrainPumpBit = 0x02;
constexpr quint8 kHeaterBit = 0x04;
} // namespace

ActuatorModels::ActuatorModels(QObject *parent)
    : QObject(parent)
{
}

bool ActuatorModels::inletValveOpen() const
{
    return m_inletValveOpen;
}

bool ActuatorModels::drainPumpOn() const
{
    return m_drainPumpOn;
}

bool ActuatorModels::heaterOn() const
{
    return m_heaterOn;
}

bool ActuatorModels::anyActive() const
{
    return m_inletValveOpen || m_drainPumpOn || m_heaterOn;
}

void ActuatorModels::setInletValveOpen(bool open)
{
    if (m_inletValveOpen == open)
        return;
    m_inletValveOpen = open;
    emit inletValveChanged();
    notifyChanged();
}

void ActuatorModels::setDrainPumpOn(bool on)
{
    if (m_drainPumpOn == on)
        return;
    m_drainPumpOn = on;
    emit drainPumpChanged();
    notifyChanged();
}

void ActuatorModels::setHeaterOn(bool on)
{
    if (m_heaterOn == on)
        return;
    m_heaterOn = on;
    emit heaterChanged();
    notifyChanged();
}

void ActuatorModels::reset()
{
    setInletValveOpen(false);
    setDrainPumpOn(false);
    setHeaterOn(false);
}

quint8 ActuatorModels::toBitmask() const
{
    quint8 mask = 0;
    if (m_inletValveOpen)
        mask |= kInletValveBit;
    if (m_drainPumpOn)
        mask |= kDrainPumpBit;
    if (m_heaterOn)
        mask |= kHeaterBit;
    return mask;
}

void ActuatorModels::applyBitmask(quint8 mask)
{
    setInletValveOpen((mask & kInletValveBit) != 0);
    setDrainPumpOn((mask & kDrainPumpBit) != 0);
    setHeaterOn((mask & kHeaterBit) != 0);
}

void ActuatorModels::notifyChanged()
{
    emit anyActiveChanged();
    emit actuatorStateChanged();
}

} // namespace whm
