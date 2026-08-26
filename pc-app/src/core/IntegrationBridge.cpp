#include "core/IntegrationBridge.h"

#include <QDateTime>
#include <QtGlobal>

#include "core/DataRecorder.h"
#include "core/FaultInjector.h"
#include "models/ActuatorModels.h"
#include "models/DoorLockModel.h"
#include "models/MotorModel.h"
#include "models/TemperatureModel.h"
#include "models/WaterLevelModel.h"

namespace whm {

namespace {

// ---------------------------------------------------------------------------
// 位图位序映射
//
// 固件(REPORT_INPUT relayBitmap): bit0 门锁, bit1 进水阀, bit2 排水泵, bit3 加热器
// PC侧(ActuatorModels::toBitmask): bit0 进水阀, bit1 排水泵, bit2 加热器
// 桥接层负责在两种位序之间转换；门锁不参与 ActuatorModels。
// ---------------------------------------------------------------------------

//! 固件继电器位图 → ActuatorModels 位图
quint8 firmwareRelayToActuatorMask(uint8_t relayBitmap)
{
    quint8 mask = 0;
    if (relayBitmap & 0x02)  //!< bit1 进水阀 → bit0
        mask |= 0x01;
    if (relayBitmap & 0x04)  //!< bit2 排水泵 → bit1
        mask |= 0x02;
    if (relayBitmap & 0x08)  //!< bit3 加热器 → bit2
        mask |= 0x04;
    return mask;
}

} // namespace

IntegrationBridge::IntegrationBridge(QObject *parent)
    : QObject(parent)
{
}

void IntegrationBridge::setEngine(SimulationEngine *e)
{
    if (m_engine == e)
        return;

    if (m_engine != nullptr)
        disconnect(m_engine, nullptr, this, nullptr);

    m_engine = e;

    if (m_engine != nullptr) {
        // 引擎每步推进后触发下行发送（槽内再做节流与模式判断）
        connect(m_engine, &SimulationEngine::ticked,
                this, &IntegrationBridge::onEngineTicked);
    }
    emit engineChanged();
}

void IntegrationBridge::setSerial(SerialManager *s)
{
    if (m_serial == s)
        return;

    if (m_serial != nullptr)
        disconnect(m_serial, nullptr, this, nullptr);

    m_serial = s;

    if (m_serial != nullptr) {
        // 主控上报的输入帧 → 缓存并回灌引擎
        connect(m_serial, &SerialManager::dataReceived,
                this, &IntegrationBridge::onSerialData);
    }
    emit serialChanged();
}

void IntegrationBridge::setFaultInjector(FaultInjector *f)
{
    if (m_faultInjector == f)
        return;
    m_faultInjector = f;
    emit faultInjectorChanged();
}

void IntegrationBridge::setRecorder(DataRecorder *r)
{
    if (m_recorder == r)
        return;
    m_recorder = r;
    emit recorderChanged();
}

void IntegrationBridge::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    if (on)
        m_lastSendTime = 0;  //!< 激活后立即允许首帧发送
    emit activeChanged();
}

void IntegrationBridge::setBridgeMode(BridgeMode mode)
{
    if (m_bridgeMode == mode)
        return;
    m_bridgeMode = mode;
    emit bridgeModeChanged();
}

void IntegrationBridge::setSendPeriodMs(int ms)
{
    const int clamped = qMax(1, ms);
    if (m_sendPeriodMs == clamped)
        return;
    m_sendPeriodMs = clamped;
    emit sendPeriodChanged();
}

void IntegrationBridge::setNtcRange(double rMinOhm, double rMaxOhm)
{
    if (rMinOhm <= 0.0 || rMaxOhm <= rMinOhm)
        return;
    m_ntcRMin = rMinOhm;
    m_ntcRMax = rMaxOhm;
}

void IntegrationBridge::sendOnce()
{
    if (m_engine == nullptr || m_serial == nullptr)
        return;
    if (!m_serial->isConnected())
        return;
    m_serial->sendBatchSet(buildBatchSet());
}

void IntegrationBridge::onEngineTicked()
{
    // 数据记录：与桥接模式无关，只要记录器在记录就逐帧采样关键通道
    recordChannels();

    // 未激活或纯仿真模式：不与硬件交互
    if (!m_active || m_bridgeMode == SimulationOnly)
        return;
    if (m_engine == nullptr || m_serial == nullptr)
        return;
    if (!m_serial->isConnected())
        return;

    // 节流：距上次发送不足 sendPeriodMs 则跳过
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSendTime < m_sendPeriodMs)
        return;
    m_lastSendTime = now;

    m_serial->sendBatchSet(buildBatchSet());
}

void IntegrationBridge::onSerialData(const ReportInputData &data)
{
    // 缓存最新反馈并通知 UI（无论桥接模式如何，上报数据都可观测）
    m_feedbackMotorDuty = static_cast<double>(data.motorPwmDuty) / 1000.0; //!< 0.1%单位 → 0~1
    m_feedbackRelayBitmap = data.relayBitmap;
    m_feedbackGpioBitmap = data.gpioBitmap;
    emit feedbackUpdated();

    if (!m_active || m_bridgeMode != ClosedLoop)
        return;
    if (m_engine == nullptr)
        return;
    // 仅联动(Automatic)模式下由主控反馈驱动模型；手动模式保留界面控制权
    if (m_engine->mode() != SimulationEngine::Automatic)
        return;

    applyFeedback(data);
}

BatchSetData IntegrationBridge::buildBatchSet()
{
    BatchSetData data{};
    if (m_engine == nullptr)
        return data;

    // 传感器模拟量（物理模型输出 → MCU 侧对应采集通道）
    double waterFreq = m_engine->waterModel()->sensorFrequency();
    double ntcOhm = m_engine->tempModel()->ntcResistance();
    double hallFreq = m_engine->motorModel()->hallFrequency();

    // 故障注入：发送前对传感器输出应用故障效果，模拟真实故障工况
    if (m_faultInjector != nullptr) {
        // 仿真步长(s)：故障持续时间按仿真时间推进，与倍速保持一致
        const double dt = m_engine->timeStepMs() / 1000.0 * m_engine->timeScale();
        waterFreq = m_faultInjector->applyFault(FaultInjector::WaterLevelSensor, waterFreq, dt);
        ntcOhm = m_faultInjector->applyFault(FaultInjector::TempSensor, ntcOhm, dt);
        hallFreq = m_faultInjector->applyFault(FaultInjector::MotorHall, hallFreq, dt);
    }

    // 频率类输出钳位到协议可表达的合理范围（短路故障拉满为 100kHz）
    data.waterLevelFreq = static_cast<uint32_t>(qBound(0.0, waterFreq, 100000.0));
    data.ntcDacValue = ntcToDac(ntcOhm);
    data.hallFreq = static_cast<uint32_t>(qBound(0.0, hallFreq, 100000.0));

    // 执行器开关量
    data.doorLock = 0;  // 门锁信号不再使用，固定为 0

    // ActuatorModels 位序: bit0 进水阀 / bit1 排水泵 / bit2 加热器
    const quint8 actuatorMask = m_engine->actuators()->toBitmask();
    data.inletValve = (actuatorMask & 0x01) ? 1 : 0;
    data.drainPump = (actuatorMask & 0x02) ? 1 : 0;
    data.heater = (actuatorMask & 0x04) ? 1 : 0;

    // 门开关信号（独立链路：模型输出 → STM32 → 主控板）
    // DOOR_1: 高电平=门开, DOOR_2: 高电平=门关（互补）
    const bool doorIsOpen = m_engine->doorOpenState();
    data.doorSignal1 = doorIsOpen ? 1 : 0;
    data.doorSignal2 = doorIsOpen ? 0 : 1;

    return data;
}

void IntegrationBridge::recordChannels()
{
    if (m_recorder == nullptr || !m_recorder->isRecording())
        return;
    if (m_engine == nullptr)
        return;

    // 时间戳用仿真累计时间(ms)，便于导出后按仿真时间轴分析
    const qint64 ts = static_cast<qint64>(m_engine->elapsedTime() * 1000.0);

    // 物理量通道
    m_recorder->record(QStringLiteral("water_level_mm"), m_engine->waterModel()->waterLevel(), ts);
    m_recorder->record(QStringLiteral("temperature_c"), m_engine->tempModel()->temperature(), ts);
    m_recorder->record(QStringLiteral("motor_rpm"), m_engine->motorModel()->speedRpm(), ts);

    // 传感器输出通道（发送给主控的原始量，未叠加故障）
    m_recorder->record(QStringLiteral("water_sensor_hz"), m_engine->waterModel()->sensorFrequency(), ts);
    m_recorder->record(QStringLiteral("ntc_ohm"), m_engine->tempModel()->ntcResistance(), ts);
    m_recorder->record(QStringLiteral("hall_hz"), m_engine->motorModel()->hallFrequency(), ts);
    m_recorder->record(QStringLiteral("door_open_state"), m_engine->doorOpenState() ? 1.0 : 0.0, ts);
}

void IntegrationBridge::applyFeedback(const ReportInputData &data)
{
    // 固件继电器位图(bit0门锁/bit1进水/bit2排水/bit3加热) → 执行器位序
    m_engine->actuators()->applyBitmask(firmwareRelayToActuatorMask(data.relayBitmap));

    // 电机 PWM: 0-1000(0.1%) → 占空比 0~1（协议无方向位，取正转工况）
    const double duty = qBound(0.0, static_cast<double>(data.motorPwmDuty) / 1000.0, 1.0);
    m_engine->motorModel()->setMotorPwmDuty(duty);
}

uint16_t IntegrationBridge::ntcToDac(double resistanceOhm) const
{
    // 钳位到 [rMin, rMax] 后线性映射: rMin→4095, rMax→0（NTC 温度越高阻值越小、DAC 越大）
    const double r = qBound(m_ntcRMin, resistanceOhm, m_ntcRMax);
    const double ratio = (r - m_ntcRMin) / (m_ntcRMax - m_ntcRMin); //!< 0~1
    const double dac = 4095.0 * (1.0 - ratio);
    return static_cast<uint16_t>(qRound(dac));
}

} // namespace whm
