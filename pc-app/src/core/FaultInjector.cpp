#include "core/FaultInjector.h"

#include <QtMath>

namespace whm {

namespace {

//! 短路故障时频率通道的拉满值(Hz)
constexpr double kShortFreqHz = 100000.0;
//! 断线故障时 NTC 开路阻值(Ω)，远大于标定上限，等效开路
constexpr double kOpenNtcOhm = 10.0e6;

} // namespace

FaultInjector::FaultInjector(QObject *parent)
    : QObject(parent)
    , m_rng(QRandomGenerator::securelySeeded())
{
}

void FaultInjector::injectFault(int channel, int faultType,
                                double durationSec, double magnitude)
{
    if (channel < WaterLevelSensor || channel > DoorLockSignal)
        return;
    if (faultType < None || faultType > ShortCircuit)
        return;

    const Channel ch = static_cast<Channel>(channel);
    const FaultType type = static_cast<FaultType>(faultType);

    // 选择"无故障"等价于清除
    if (type == None) {
        clearFault(channel);
        return;
    }

    FaultConfig cfg;
    cfg.type = type;
    cfg.durationSec = qMax(0.0, durationSec);
    cfg.magnitude = magnitude;
    cfg.elapsed = 0;
    cfg.active = true;
    cfg.stuckValue = 0;
    cfg.stuckCaptured = false;
    m_faults.insert(ch, cfg);

    emit faultsChanged();
    emit faultInjected(channel, faultType);
}

void FaultInjector::clearFault(int channel)
{
    if (channel < WaterLevelSensor || channel > DoorLockSignal)
        return;

    const Channel ch = static_cast<Channel>(channel);
    auto it = m_faults.find(ch);
    if (it == m_faults.end() || !it->active)
        return;

    it->active = false;
    it->type = None;
    emit faultsChanged();
    emit faultCleared(channel);
}

void FaultInjector::clearAllFaults()
{
    bool changed = false;
    for (auto it = m_faults.begin(); it != m_faults.end(); ++it) {
        if (it->active) {
            it->active = false;
            it->type = None;
            emit faultCleared(it.key());
            changed = true;
        }
    }
    if (changed)
        emit faultsChanged();
}

double FaultInjector::applyFault(Channel ch, double originalValue, double dt)
{
    auto it = m_faults.find(ch);
    if (it == m_faults.end() || !it->active || it->type == None)
        return originalValue;

    FaultConfig &cfg = it.value();

    // 限时故障：按仿真步长计时，到期自动清除
    if (cfg.durationSec > 0) {
        cfg.elapsed += dt;
        if (cfg.elapsed >= cfg.durationSec) {
            clearFault(ch);
            return originalValue;
        }
    }

    switch (cfg.type) {
    case Disconnected: return applyDisconnected(ch, originalValue);
    case OutOfRange:   return applyOutOfRange(ch, originalValue, cfg.magnitude);
    case Jitter:       return applyJitter(ch, originalValue, cfg.magnitude);
    case Stuck:        return applyStuck(ch, originalValue);
    case ShortCircuit: return applyShortCircuit(ch);
    default:           return originalValue;
    }
}

bool FaultInjector::isFaultActive(int channel) const
{
    if (channel < WaterLevelSensor || channel > DoorLockSignal)
        return false;
    const auto it = m_faults.find(static_cast<Channel>(channel));
    return it != m_faults.end() && it->active && it->type != None;
}

QString FaultInjector::faultDescription(int channel) const
{
    if (!isFaultActive(channel))
        return QString();

    const Channel ch = static_cast<Channel>(channel);
    const FaultConfig &cfg = m_faults.value(ch);
    QString desc = channelName(ch) + QStringLiteral("：") + faultTypeName(cfg.type);

    if (cfg.type == OutOfRange || cfg.type == Jitter)
        desc += QStringLiteral("（系数 %1）").arg(cfg.magnitude, 0, 'f', 1);
    if (cfg.durationSec > 0)
        desc += QStringLiteral("，剩余 %1s").arg(qMax(0.0, cfg.durationSec - cfg.elapsed), 0, 'f', 1);
    return desc;
}

bool FaultInjector::anyFaultActive() const
{
    for (auto it = m_faults.constBegin(); it != m_faults.constEnd(); ++it) {
        if (it->active && it->type != None)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 各故障类型的具体效果
// ---------------------------------------------------------------------------

double FaultInjector::applyDisconnected(Channel ch, double value)
{
    Q_UNUSED(value)
    switch (ch) {
    case WaterLevelSensor: return 0.0;         //!< 频率通道断线 → 0Hz
    case TempSensor:       return kOpenNtcOhm; //!< NTC 断线 → 开路（极大阻值）
    case MotorHall:        return 0.0;         //!< Hall 断线 → 0Hz
    case DoorLockSignal:   return 0.0;         //!< 门锁断线 → 固定解锁
    }
    return value;
}

double FaultInjector::applyOutOfRange(Channel ch, double value, double mag)
{
    // 输出值 × magnitude，超出主控标定范围
    const double out = value * mag;
    // 门锁为电平量，超范围后仍按电平语义输出
    if (ch == DoorLockSignal)
        return out > 0.5 ? 1.0 : 0.0;
    return qMax(0.0, out);
}

double FaultInjector::applyJitter(Channel ch, double value, double mag)
{
    // 噪声幅度 = magnitude × |原值| × 0.1，均匀分布于 [-amp, +amp]
    double amp = mag * qAbs(value) * 0.1;
    // 原值为 0 时给出微小绝对抖动，保证断流/静止工况仍可见噪声
    if (amp < 1e-9)
        amp = mag * 0.01;
    const double noise = (static_cast<double>(m_rng.bounded(20001)) / 10000.0 - 1.0) * amp;
    const double out = value + noise;
    if (ch == DoorLockSignal)
        return out > 0.5 ? 1.0 : 0.0;
    return qMax(0.0, out);
}

double FaultInjector::applyStuck(Channel ch, double value)
{
    Q_UNUSED(ch)
    FaultConfig &cfg = m_faults[ch];
    // 故障生效后首次采样时捕获冻结值（近似"注入时刻的值"）
    if (!cfg.stuckCaptured) {
        cfg.stuckValue = value;
        cfg.stuckCaptured = true;
    }
    return cfg.stuckValue;
}

double FaultInjector::applyShortCircuit(Channel ch)
{
    switch (ch) {
    case WaterLevelSensor: return kShortFreqHz; //!< 频率拉满 100kHz
    case TempSensor:       return 0.0;          //!< NTC 短路 → 0Ω
    case MotorHall:        return kShortFreqHz; //!< 频率拉满 100kHz
    case DoorLockSignal:   return 1.0;          //!< 门锁短路 → 固定锁定
    }
    return 0.0;
}

QString FaultInjector::channelName(Channel ch)
{
    switch (ch) {
    case WaterLevelSensor: return QStringLiteral("水位传感器");
    case TempSensor:       return QStringLiteral("NTC温度传感器");
    case MotorHall:        return QStringLiteral("Hall转速传感器");
    case DoorLockSignal:   return QStringLiteral("门锁信号");
    }
    return QStringLiteral("未知通道");
}

QString FaultInjector::faultTypeName(FaultType type)
{
    switch (type) {
    case None:         return QStringLiteral("无故障");
    case Disconnected: return QStringLiteral("断线");
    case OutOfRange:   return QStringLiteral("超范围");
    case Jitter:       return QStringLiteral("抖动");
    case Stuck:        return QStringLiteral("卡死");
    case ShortCircuit: return QStringLiteral("短路");
    }
    return QStringLiteral("未知");
}

} // namespace whm
