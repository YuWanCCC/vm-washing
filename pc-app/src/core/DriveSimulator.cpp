#include "DriveSimulator.h"
#include <cmath>

namespace whm {
namespace {
int value(const QVariantMap &map, const char *key) { return map.value(QLatin1String(key)).toInt(); }
QString hex(const QByteArray &data) { return QString::fromLatin1(data.toHex(' ').toUpper()); }
}

DriveSimulator::DriveSimulator(QObject *parent) : QObject(parent)
{
    for (int a = 1; a <= 4; ++a) initialize(a, false);
}

void DriveSimulator::initialize(int address, bool preserveSettings)
{
    auto &b = m_boards[address];
    const bool enabled = b.enabled;
    const auto settings = b.settings;
    const bool manualLock = b.manualLock;
    b = Board{};
    b.enabled = enabled;
    b.epochMs = m_now;
    b.acceleration = address == 2 ? 10 : address == 3 ? 200 : 800;
    for (const auto &f : DriveCodec::fields(address, false)) b.settings[f.key] = 0;
    b.settings["version"] = 1;
    if (address == 1) {
        b.settings["door"] = 2;
        b.settings["lockState"] = 2;
        b.settings["frequency"] = 50;
    }
    if (preserveSettings) {
        b.settings = settings;
        b.manualLock = manualLock;
    }
    for (const auto &f : DriveCodec::fields(address, true)) b.command[f.key] = 0;
    if (address == 2) b.command["ratio"] = 52;
    updateBoard(address, 0);
}

bool DriveSimulator::enabled(int address) const
{
    return validAddress(address) && m_boards[address].enabled;
}

QVariantList DriveSimulator::fieldsFor(int address, bool request) const
{
    return DriveCodec::fieldMetadata(address, request);
}

QVariantMap DriveSimulator::snapshot(int address) const
{
    if (!validAddress(address)) return {};
    const auto &b = m_boards[address];
    const int ratio = value(b.command, "ratio");
    return {{"address", address}, {"name", DriveCodec::boardName(address)}, {"enabled", b.enabled},
            {"seen", b.seen}, {"timedOut", b.timedOut}, {"commFault", b.commFault},
            {"manualLock", b.manualLock}, {"rxCount", b.rxCount}, {"txCount", b.txCount},
            {"lastRxMs", b.lastRxMs}, {"lastTxMs", b.lastTxMs},
            {"ageMs", b.lastRxMs < 0 ? -1 : m_now - b.lastRxMs},
            {"command", b.command}, {"feedback", b.feedback}, {"settings", b.settings},
            {"signedSpeed", b.signedSpeed}, {"shaftSpeed", b.signedSpeed * (ratio ? ratio : 52)},
            {"lastRequest", hex(b.lastRequest)}, {"lastResponse", hex(b.lastResponse)}};
}

QVariantList DriveSimulator::boards() const
{
    QVariantList list;
    for (int a = 1; a <= 4; ++a) list.append(snapshot(a));
    return list;
}

void DriveSimulator::setSelectedAddress(int address)
{
    if (!validAddress(address) || address == m_selected) return;
    m_selected = address;
    emit selectionChanged();
    emit changed();
}

void DriveSimulator::setEnabled(int address, bool enabledValue)
{
    if (!validAddress(address) || m_boards[address].enabled == enabledValue) return;
    m_boards[address].enabled = enabledValue;
    initialize(address, true);
    emit boardInvalidated(address);
    emit eventOccurred(address, enabledValue ? tr("板卡已启用，等待新命令") : tr("板卡已禁用，不再应答"));
    // 地址2的配置也影响单负载板通信故障位。
    updateBoard(1, 0);
    emit changed();
}

bool DriveSimulator::setFeedback(int address, const QString &key, int rawValue)
{
    if (!validAddress(address)) return false;
    for (const auto &f : DriveCodec::fields(address, false)) {
        if (f.key != key || !f.editable) continue;
        if (rawValue < 0 || rawValue > f.mask || (rawValue & ~f.mask)) return false;
        if ((key == "door" && rawValue > 2) || (key == "lockState" && rawValue > 3)) return false;
        auto &b = m_boards[address];
        b.settings[key] = rawValue;
        if (address == 1 && key == "lockState") b.manualLock = true;
        updateBoard(address, 0);
        emit changed();
        return true;
    }
    return false;
}

void DriveSimulator::followDoorLock()
{
    auto &b = m_boards[1];
    b.manualLock = false;
    const int command = value(b.command, "lockCommand");
    if (b.seen && (command == 1 || command == 2)) b.settings["lockState"] = command;
    updateBoard(1, 0);
    emit changed();
}

void DriveSimulator::resetBoard(int address)
{
    if (!validAddress(address)) return;
    emit boardInvalidated(address);
    initialize(address, false);
    emit eventOccurred(address, tr("已重置本板仿真状态与故障注入，等待新命令"));
    emit changed();
}

void DriveSimulator::beginSession(qint64 nowMs)
{
    m_now = nowMs;
    m_active = true;
    for (int a = 1; a <= 4; ++a) initialize(a, true);
    emit changed();
}

void DriveSimulator::endSession(qint64 nowMs)
{
    m_now = qMax(m_now, nowMs);
    m_active = false;
    for (int a = 1; a <= 4; ++a) {
        updateBoard(a, 0);
        emit boardInvalidated(a);
    }
    emit changed();
}

void DriveSimulator::advance(qint64 nowMs)
{
    if (nowMs < m_now) return;
    const double dt = (nowMs - m_now) / 1000.0;
    m_now = nowMs;
    if (!m_active) return;
    for (int a = 1; a <= 4; ++a) updateBoard(a, dt);
    emit changed();
}

QByteArray DriveSimulator::receive(const DriveFrame &frame, qint64 nowMs)
{
    if (!m_active || !frame.request || !enabled(frame.address)
        || !DriveCodec::valid(frame.raw, true)) return {};
    const auto command = DriveCodec::decode(frame);
    if (command.isEmpty()) return {};
    advance(nowMs);
    auto &b = m_boards[frame.address];
    if (frame.address == 3 && value(command, "reset")) {
        emit boardInvalidated(frame.address);
        initialize(frame.address, true);
        emit eventOccurred(frame.address, tr("压机软件复位，持续注入的外部故障保留"));
    }
    if (frame.address == 3 && value(command, "clearFault")) {
        b.commFault = false;
        emit eventOccurred(frame.address, tr("压机清故障：通信故障清除；持续注入故障需在控制区解除"));
    }
    b.command = command;
    b.seen = true;
    b.lastRxMs = m_now;
    ++b.rxCount;
    b.lastRequest = frame.raw;
    const int acceleration = value(command, "acceleration");
    if (acceleration > 0) b.acceleration = acceleration * (frame.address == 4 ? 40 : 1);
    if (frame.address == 3 && value(command, "targetTemp") > 0)
        b.targetTemperature = value(command, "targetTemp");
    if (frame.address == 1) {
        if (value(command, "lockMaxAttempts") == 0) b.settings["lockAttempts"] = 0;
        const int lock = value(command, "lockCommand");
        if (!b.manualLock && (lock == 1 || lock == 2)) b.settings["lockState"] = lock;
    }
    if (frame.address == 2 && value(command, "ratio") == 0)
        emit eventOccurred(frame.address, tr("传动比0非法：请求原值保留，电机轴显示采用默认52"));
    updateBoard(frame.address, 0);
    return DriveCodec::encode(frame.address, false, b.feedback);
}

void DriveSimulator::markTransmitted(int address, qint64 nowMs)
{
    if (!enabled(address) || !m_active) return;
    auto &b = m_boards[address];
    b.lastTxMs = nowMs;
    ++b.txCount;
    emit changed();
}

void DriveSimulator::updateBoard(int address, double dt)
{
    auto &b = m_boards[address];
    const auto &c = b.command;
    const qint64 age = m_now - (b.seen ? b.lastRxMs : b.epochMs);
    const bool testMode = (address == 1 && (value(c, "load2") & 0x80))
                       || (address == 4 && value(c, "testMode"));
    const bool wasTimeout = b.timedOut;
    const bool wasFault = b.commFault;
    b.timedOut = m_active && b.enabled && age >= 10000;
    b.commFault = m_active && b.enabled && age >= (testMode ? 5000 : 60000);
    if (b.timedOut && !wasTimeout) emit eventOccurred(address, tr("10秒未收到本地址有效命令，安全停机"));
    if (b.commFault && !wasFault) emit eventOccurred(address, tr("本地址通信故障（测试5秒/正常60秒），不主动上报"));
    if (!b.commFault && wasFault) emit eventOccurred(address, tr("有效通信恢复，自动通信故障清除"));

    b.feedback = b.settings;
    auto &f = b.feedback;
    // 有同名字段的设定参数回显；测量字段不从命令中生成。
    for (const auto &meta : DriveCodec::fields(address, false))
        if (!meta.editable && c.contains(meta.key)) f[meta.key] = c.value(meta.key);

    if (address == 1) {
        const int fault = value(b.settings, "fault") | (b.commFault && !enabled(2) ? 0x80 : 0);
        f["fault"] = fault;
        int load1 = value(c, "load1");
        int load2 = value(c, "load2") & 0xbf;
        const bool off = !m_active || !b.enabled || !b.seen || b.timedOut || b.commFault
            || value(f, "door") == 1 || value(f, "lockState") == 1 || fault != 0
            || !(load2 & 0x02);
        if (off) {
            load1 = 0;
            load2 &= 0x80; // 测试标志不是负载，负载与PWM先后归零。
        }
        f["load1"] = load1;
        f["load2"] = load2;
        f["lockPeriod"] = value(c, "lockPeriod");
    } else {
        const bool compressor = address == 3;
        const int fault = value(b.settings, compressor ? "fault1" : "fault");
        const int fault2 = compressor ? value(b.settings, "fault2") : 0;
        if (address == 2) f["fault"] = fault | (b.commFault ? 0x80 : 0);
        if (compressor) {
            f["fault1"] = fault;
            f["fault2"] = fault2 | (b.commFault ? 0x02 : 0);
            f["suppression"] = value(c, "suppressionOff") ? 1 : 0;
            f["targetTemp"] = b.targetTemperature;
        }
        const double speedUnit = address == 4 ? 100 : 1;
        const double accelUnit = address == 4 ? 40 : 1;
        const int direction = compressor ? 1 : value(c, "direction");
        double target = value(c, "speed") * speedUnit;
        if (direction == 2) target = -target;
        else if (direction != 1) target = 0;
        const bool stop = !m_active || !b.enabled || !b.seen || b.timedOut || b.commFault || fault || fault2;
        if (dt > 0 || stop) b.actualAcceleration = 0;
        if (stop) {
            b.signedSpeed = 0;
        } else if (dt > 0) {
            // 把跨零的一步拆成减速与反向加速，避免符号瞬时翻转。
            const double old = b.signedSpeed;
            double remaining = dt;
            if (old * target < 0) {
                const double brakeTime = std::abs(old) / b.acceleration;
                const double used = qMin(remaining, brakeTime);
                b.signedSpeed += (old > 0 ? -1 : 1) * b.acceleration * used;
                remaining -= used;
                if (used >= brakeTime) b.signedSpeed = 0;
            }
            const double delta = target - b.signedSpeed;
            b.signedSpeed += qBound(-b.acceleration * remaining, delta, b.acceleration * remaining);
            if (std::abs(old - b.signedSpeed) > 0.000001) b.actualAcceleration = b.acceleration;
        }
        f["actualSpeed"] = int(std::floor(std::abs(b.signedSpeed) / speedUnit));
        f["actualDirection"] = b.signedSpeed < 0 ? 2 : b.signedSpeed > 0 ? 1 : 0;
        f["actualAcceleration"] = int(b.actualAcceleration / accelUnit);
        f["acceleration"] = int(b.acceleration / accelUnit);
    }
    // lastResponse 是当前可应答帧；真实发送帧由串口日志独立保留。
    b.lastResponse = DriveCodec::encode(address, false, b.feedback);
}

} // namespace whm
