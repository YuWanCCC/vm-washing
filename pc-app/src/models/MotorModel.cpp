#include "models/MotorModel.h"

#include <algorithm>
#include <cmath>

namespace whm {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr double kRpmPerRadPerSec = 60.0 / kTwoPi;
constexpr double kEpsilon = 1.0e-9;
} // namespace

MotorModel::MotorModel(QObject *parent)
    : QObject(parent)
{
}

void MotorModel::update(double dt)
{
    if (dt <= 0.0 || m_inertia <= kEpsilon)
        return;

    const double previous = m_angularVelocity;

    // tau_motor = K_t * duty
    const double motorTq = m_motorTorqueConst * m_pwmDuty;
    // tau_friction = b * w（始终与运动方向相反）
    const double frictionTq = m_frictionCoeff * m_angularVelocity;
    // 负载转矩阻碍旋转，方向与当前转向相反；静止时按驱动转矩方向取符号
    const double direction = std::abs(m_angularVelocity) > kEpsilon
        ? (m_angularVelocity > 0.0 ? 1.0 : -1.0)
        : (motorTq > 0.0 ? 1.0 : (motorTq < 0.0 ? -1.0 : 0.0));
    const double loadTq = m_loadTorque * direction;

    // dw/dt = (tau_motor - tau_friction - tau_load) / J
    const double alpha = (motorTq - frictionTq - loadTq) / m_inertia;
    m_angularVelocity += alpha * dt;

    // 静摩擦近似：驱动为零且转速极低时直接停机，避免负载转矩造成反向抖振
    if (std::abs(motorTq) <= kEpsilon && std::abs(m_angularVelocity) < 0.05)
        m_angularVelocity = 0.0;

    // 越过零点时若无驱动则钳位为 0，防止负载把电机“拖反”
    if (previous * m_angularVelocity < 0.0 && std::abs(motorTq) <= kEpsilon)
        m_angularVelocity = 0.0;

    const double limit = maxAngularVelocity();
    if (std::abs(m_angularVelocity) >= limit) {
        m_angularVelocity = m_angularVelocity > 0.0 ? limit : -limit;
        if (!m_speedLimitReported) {
            m_speedLimitReported = true;
            emit speedLimitReached(speedRpm());
        }
    } else if (std::abs(m_angularVelocity) < limit * 0.98) {
        m_speedLimitReported = false;
    }

    if (std::abs(m_angularVelocity - previous) > kEpsilon) {
        emit speedChanged();
        emit hallFrequencyChanged();
    }
}

void MotorModel::reset()
{
    setMotorPwmDuty(0.0);
    setLoadTorque(0.0);
    m_speedLimitReported = false;
    if (std::abs(m_angularVelocity) > kEpsilon) {
        m_angularVelocity = 0.0;
        emit speedChanged();
        emit hallFrequencyChanged();
    }
}

double MotorModel::speedRpm() const
{
    return m_angularVelocity * kRpmPerRadPerSec;
}

double MotorModel::angularVelocity() const
{
    return m_angularVelocity;
}

double MotorModel::hallFrequency() const
{
    // f_hall = |w| * polePairs / (2*pi)，即每转 polePairs 个电周期
    return std::abs(m_angularVelocity) * m_polePairs / kTwoPi;
}

double MotorModel::motorTorque() const
{
    return m_motorTorqueConst * m_pwmDuty;
}

double MotorModel::frictionTorque() const
{
    return m_frictionCoeff * m_angularVelocity;
}

double MotorModel::motorPwmDuty() const
{
    return m_pwmDuty;
}

void MotorModel::setMotorPwmDuty(double duty)
{
    const double value = std::clamp(duty, -1.0, 1.0);
    if (std::abs(value - m_pwmDuty) <= kEpsilon)
        return;
    m_pwmDuty = value;
    emit pwmDutyChanged();
}

double MotorModel::loadTorque() const
{
    return m_loadTorque;
}

void MotorModel::setLoadTorque(double torque)
{
    const double value = std::max(0.0, torque);
    if (std::abs(value - m_loadTorque) <= kEpsilon)
        return;
    m_loadTorque = value;
    emit loadTorqueChanged();
}

void MotorModel::setInertia(double kgm2)
{
    m_inertia = std::max(kEpsilon, kgm2);
}

void MotorModel::setFrictionCoeff(double nmsPerRad)
{
    m_frictionCoeff = std::max(0.0, nmsPerRad);
}

void MotorModel::setTorqueConstant(double nmAtFullDuty)
{
    m_motorTorqueConst = nmAtFullDuty;
    emit pwmDutyChanged();
}

void MotorModel::setPolePairs(double polePairs)
{
    m_polePairs = std::max(1.0, polePairs);
    emit hallFrequencyChanged();
}

void MotorModel::setMaxSpeedRpm(double rpm)
{
    m_maxSpeedRpm = std::max(1.0, rpm);
}

double MotorModel::inertia() const
{
    return m_inertia;
}

double MotorModel::frictionCoeff() const
{
    return m_frictionCoeff;
}

double MotorModel::torqueConstant() const
{
    return m_motorTorqueConst;
}

double MotorModel::polePairs() const
{
    return m_polePairs;
}

double MotorModel::maxSpeedRpm() const
{
    return m_maxSpeedRpm;
}

double MotorModel::maxAngularVelocity() const
{
    return m_maxSpeedRpm / kRpmPerRadPerSec;
}

} // namespace whm
