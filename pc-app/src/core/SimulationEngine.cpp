#include "core/SimulationEngine.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace whm {

namespace {
constexpr double kDefaultTimeStepMs = 20.0;    // 与 default_params.json 的 simulation.timeStepMs 一致
constexpr double kAgitateRunSec = 15.0;        // 洗涤单向运转时间
constexpr double kAgitatePauseSec = 5.0;       // 换向间歇（避免瞬时反转冲击）
constexpr double kSpeedLoopGain = 4.0;         // 转速环增益（占空比/归一化误差/秒）
constexpr double kDrainedVolumeL = 1.0;        // 判定“排空”的余水量，与场景 waterVolume<=1.0 一致
constexpr double kFillTimeoutSec = 1800.0;     // 注水超时保护
constexpr double kHeatTimeoutSec = 3600.0;     // 加热超时保护
constexpr double kDrainTimeoutSec = 2400.0;    // 排水超时保护
constexpr double kDoorTimeoutSec = 10.0;       // 门锁到位超时提示
constexpr double kFullThresholdPercent = 99.0; // 接近满桶阈值
constexpr double kRadPerSecToRpm = 60.0 / (2.0 * 3.14159265358979323846);

//! 水量(L) → 水位(mm)：h = V*1e-3 / A_cross，再换算为 mm
double levelFromVolume(double litre, double crossAreaM2)
{
    if (crossAreaM2 <= 1.0e-9)
        return 0.0;
    return litre * 1.0e-3 / crossAreaM2 * 1000.0;
}

//! 读取 JSON 数值，缺省时返回 fallback
double jsonDouble(const QJsonObject &obj, const char *key, double fallback)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}
} // namespace

SimulationEngine::SimulationEngine(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_waterModel(new WaterLevelModel(this))
    , m_tempModel(new TemperatureModel(this))
    , m_motorModel(new MotorModel(this))
    , m_doorModel(new DoorLockModel(this))
    , m_actuators(new ActuatorModels(this))
    , m_scenarios(new ScenarioManager(this))
{
    m_timeStepMs = kDefaultTimeStepMs;
    m_dt = m_timeStepMs / 1000.0 * m_timeScale;

    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(qRound(m_timeStepMs));
    connect(m_timer, &QTimer::timeout, this, &SimulationEngine::tick);

    // 模型侧的保护事件统一转成 warning 抛给界面，并立即断开对应执行器
    connect(m_waterModel, &WaterLevelModel::overflowDetected, this, [this]() {
        m_actuators->setInletValveOpen(false);
        emit warning(tr("水位达到桶体最大容量，进水阀已关闭"));
    });
    connect(m_tempModel, &TemperatureModel::overheatDetected, this, [this](double temperature) {
        m_actuators->setHeaterOn(false);
        emit warning(tr("水温过热 %1 °C，加热器已断开").arg(temperature, 0, 'f', 1));
    });
    connect(m_motorModel, &MotorModel::speedLimitReached, this, [this](double speedRpm) {
        emit warning(tr("电机转速达到限速 %1 r/min").arg(speedRpm, 0, 'f', 0));
    });

    // 启动即套用随程序发布的默认参数（找不到配置文件时沿用各模型内置默认值）
    loadConfig(QString());
}

// ---------------------------------------------------------------------------
// 运行控制
// ---------------------------------------------------------------------------

void SimulationEngine::start()
{
    if (m_running)
        return;
    m_timer->setInterval(qRound(m_timeStepMs));
    m_timer->start();
    m_running = true;
    emit runningChanged();
}

void SimulationEngine::stop()
{
    if (!m_running)
        return;
    m_timer->stop();
    m_running = false;
    emit runningChanged();
}

void SimulationEngine::reset()
{
    stop();

    m_waterModel->reset();
    m_tempModel->reset();
    m_motorModel->reset();
    m_doorModel->reset();
    m_actuators->reset();

    m_elapsedTime = 0.0;
    m_rinseDone = 0;
    m_drainNext = Idle;
    m_rinseFilling = false;
    transitionTo(Idle);

    emit elapsedTimeChanged();
}

void SimulationEngine::stepOnce()
{
    // 调试用：不依赖定时器手动推进一帧
    tick();
}

// ---------------------------------------------------------------------------
// 自动洗衣程序
// ---------------------------------------------------------------------------

void SimulationEngine::startWashProgram(const QString &programName)
{
    // 1) 内置默认参数：默认按标准洗涤，名称含 quick/快 时按快洗
    WashProgram program;
    const bool quick = programName.contains(QStringLiteral("quick"), Qt::CaseInsensitive)
                       || programName.contains(QStringLiteral("快"));
    if (quick) {
        program.targetWaterLevel = levelFromVolume(20.0, m_waterModel->crossSectionArea());
        program.targetTemperature = 0.0;   // 常温快洗，不加热
        program.washDuration = 300.0;
        program.rinseCycles = 1.0;
        program.spinDuration = 180.0;
        program.spinSpeed = 800.0;
        program.washSpeed = 45.0;
        program.loadTorque = 0.3;
    } else {
        program.targetWaterLevel = levelFromVolume(30.0, m_waterModel->crossSectionArea());
    }

    // 2) 若 config/scenarios 下存在同名场景，用场景中的参数覆盖内置默认值
    const QVariantMap scenario = m_scenarios->getScenarioConfig(programName);
    if (!scenario.isEmpty()) {
        parseProgramFromScenario(scenario, &program);
        m_scenarios->loadScenario(programName);
    } else if (!programName.isEmpty()) {
        emit warning(tr("未找到场景 %1，使用内置默认参数").arg(programName));
    }

    m_currentProgram = program;
    m_programName = programName.isEmpty() ? tr("内置标准程序") : programName;
    emit programChanged();

    // 3) 衣物负载体现在电机负载转矩上
    m_motorModel->setLoadTorque(program.loadTorque);

    // 4) 联动模式 + 门锁上锁 → 进入注水（注水在门锁到位后才真正开阀）
    setMode(Automatic);
    m_rinseDone = 0;
    m_drainNext = Idle;
    m_rinseFilling = false;
    m_doorModel->requestLock();
    transitionTo(Fill);

    if (!m_running)
        start();
}

void SimulationEngine::abortWashProgram()
{
    m_actuators->reset();
    stopMotor();
    m_doorModel->requestUnlock();
    m_rinseDone = 0;
    m_drainNext = Idle;
    m_rinseFilling = false;
    transitionTo(Idle);
}

bool SimulationEngine::parseProgramFromScenario(const QVariantMap &scenario, WashProgram *program) const
{
    if (!program)
        return false;

    bool matched = false;

    // 显式给出的 program 段优先（便于后续场景直接描述洗衣程序参数）
    const QVariantMap explicitProgram = scenario.value(QStringLiteral("program")).toMap();
    const auto takeDouble = [&explicitProgram, &matched](const QString &key, double *target) {
        if (explicitProgram.contains(key)) {
            *target = explicitProgram.value(key).toDouble();
            matched = true;
        }
    };
    takeDouble(QStringLiteral("targetWaterLevel"), &program->targetWaterLevel);
    takeDouble(QStringLiteral("targetTemperature"), &program->targetTemperature);
    takeDouble(QStringLiteral("washDuration"), &program->washDuration);
    takeDouble(QStringLiteral("rinseCycles"), &program->rinseCycles);
    takeDouble(QStringLiteral("spinDuration"), &program->spinDuration);
    takeDouble(QStringLiteral("spinSpeed"), &program->spinSpeed);
    takeDouble(QStringLiteral("washSpeed"), &program->washSpeed);

    const QVariantMap parameters = scenario.value(QStringLiteral("parameters")).toMap();
    if (parameters.contains(QStringLiteral("loadTorque"))) {
        program->loadTorque = parameters.value(QStringLiteral("loadTorque")).toDouble();
        matched = true;
    }

    // waitUntil 形如 "waterVolume >= 30.0" / "temperature >= 40.0"
    static const QRegularExpression conditionRe(
        QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*(>=|<=|==|>|<)\\s*(-?\\d+(?:\\.\\d+)?)"));

    bool heatStepFound = false;
    double heatTarget = 0.0;

    const QVariantList steps = scenario.value(QStringLiteral("steps")).toList();
    for (const QVariant &stepValue : steps) {
        const QVariantMap step = stepValue.toMap();
        const QVariantMap stepActuators = step.value(QStringLiteral("actuators")).toMap();
        const QVariantMap stepMotor = step.value(QStringLiteral("motor")).toMap();
        const double duration = step.value(QStringLiteral("durationSec")).toDouble();

        QString conditionVar;
        double conditionValue = 0.0;
        const QRegularExpressionMatch match =
            conditionRe.match(step.value(QStringLiteral("waitUntil")).toString());
        if (match.hasMatch()) {
            conditionVar = match.captured(1);
            conditionValue = match.captured(3).toDouble();
        }

        const bool inletOpen = stepActuators.value(QStringLiteral("inletValveOpen")).toBool();
        const bool heaterOn = stepActuators.value(QStringLiteral("heaterOn")).toBool();
        const double duty = stepMotor.value(QStringLiteral("pwmDuty")).toDouble();
        const double targetSpeed = stepMotor.value(QStringLiteral("targetSpeedRpm")).toDouble();

        if (inletOpen && conditionVar == QStringLiteral("waterVolume")) {
            // 进水步骤：目标水量(L) 换算为目标水位(mm)
            program->targetWaterLevel = levelFromVolume(conditionValue, m_waterModel->crossSectionArea());
            matched = true;
        } else if (heaterOn && conditionVar == QStringLiteral("temperature")) {
            heatStepFound = true;
            heatTarget = conditionValue;
            matched = true;
        } else if (targetSpeed > 0.0 && duration > 0.0) {
            // 脱水步骤：直接给出目标转速与时长
            program->spinSpeed = targetSpeed;
            program->spinDuration = duration;
            matched = true;
        } else if (duration > 0.0 && std::abs(duty) > 1.0e-6) {
            // 洗涤步骤：只给占空比，按稳态平衡 K_t*duty = b*w + T_load 反推转速
            const double torque = m_motorModel->torqueConstant() * std::abs(duty);
            const double friction = std::max(1.0e-6, m_motorModel->frictionCoeff());
            const double omega = std::max(0.0, (torque - program->loadTorque) / friction);
            program->washDuration = duration;
            program->washSpeed = std::clamp(omega * kRadPerSecToRpm, 10.0, m_motorModel->maxSpeedRpm());
            matched = true;
        }
    }

    // 场景未包含加热步骤即视为常温程序（跳过 Heat 状态）
    if (heatStepFound)
        program->targetTemperature = heatTarget;
    else if (!explicitProgram.contains(QStringLiteral("targetTemperature")))
        program->targetTemperature = 0.0;

    return matched;
}

// ---------------------------------------------------------------------------
// 配置加载
// ---------------------------------------------------------------------------

void SimulationEngine::loadConfig(const QString &configPath)
{
    QString path = ScenarioManager::resolveLocalPath(configPath);
    if (path.isEmpty()) {
        const QString configDir = ScenarioManager::defaultConfigDir();
        if (configDir.isEmpty())
            return;   // 未找到 config 目录：沿用各模型内置默认参数
        path = QDir(configDir).filePath(QStringLiteral("default_params.json"));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit warning(tr("无法打开配置文件: %1").arg(path));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit warning(tr("配置文件解析失败: %1 (%2)").arg(path, parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();

    const QJsonObject water = root.value(QStringLiteral("waterLevel")).toObject();
    if (!water.isEmpty()) {
        m_waterModel->setInletFlowRateLpm(jsonDouble(water, "inletFlowRate", m_waterModel->inletFlowRateLpm()));
        m_waterModel->setDrainFlowRateLpm(jsonDouble(water, "drainFlowRate", m_waterModel->drainFlowRateLpm()));
        m_waterModel->setMaxCapacity(jsonDouble(water, "maxCapacity", m_waterModel->maxCapacity()));
        m_waterModel->setCrossSectionArea(jsonDouble(water, "crossSectionArea", m_waterModel->crossSectionArea()));
    }

    const QJsonObject temp = root.value(QStringLiteral("temperature")).toObject();
    if (!temp.isEmpty()) {
        m_tempModel->setHeaterPower(jsonDouble(temp, "heaterPower", m_tempModel->heaterPower()));
        m_tempModel->setSpecificHeat(jsonDouble(temp, "specificHeat", m_tempModel->specificHeat()));
        m_tempModel->setHeatLossCoeff(jsonDouble(temp, "heatLossCoefficient", m_tempModel->heatLossCoeff()));
        m_tempModel->setEnvironmentTemp(jsonDouble(temp, "environmentTemp", m_tempModel->environmentTemp()));
        m_tempModel->setNtcParameters(jsonDouble(temp, "ntcR25", m_tempModel->ntcR25()),
                                      jsonDouble(temp, "ntcBValue", m_tempModel->ntcB()));
    }

    const QJsonObject motor = root.value(QStringLiteral("motor")).toObject();
    if (!motor.isEmpty()) {
        m_motorModel->setInertia(jsonDouble(motor, "inertia", m_motorModel->inertia()));
        m_motorModel->setFrictionCoeff(jsonDouble(motor, "frictionCoefficient", m_motorModel->frictionCoeff()));
        m_motorModel->setTorqueConstant(jsonDouble(motor, "torqueConstant", m_motorModel->torqueConstant()));
        m_motorModel->setPolePairs(jsonDouble(motor, "polePairs", m_motorModel->polePairs()));
        m_motorModel->setMaxSpeedRpm(jsonDouble(motor, "maxSpeedRpm", m_motorModel->maxSpeedRpm()));
    }

    const QJsonObject door = root.value(QStringLiteral("doorLock")).toObject();
    if (!door.isEmpty()) {
        m_doorModel->setLockDelay(jsonDouble(door, "lockDelay", m_doorModel->lockDelay()));
        m_doorModel->setUnlockDelay(jsonDouble(door, "unlockDelay", m_doorModel->unlockDelay()));
    }

    const QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    if (!simulation.isEmpty()) {
        const double stepMs = jsonDouble(simulation, "timeStepMs", m_timeStepMs);
        if (stepMs > 0.0 && !qFuzzyCompare(stepMs, m_timeStepMs)) {
            m_timeStepMs = stepMs;
            m_timer->setInterval(qRound(m_timeStepMs));
            emit timeStepChanged();
        }
        setTimeScale(jsonDouble(simulation, "timeScale", m_timeScale));
        m_dt = m_timeStepMs / 1000.0 * m_timeScale;
    }

    emit configLoaded(path);
}

// ---------------------------------------------------------------------------
// 核心更新循环
// ---------------------------------------------------------------------------

void SimulationEngine::tick()
{
    const double dt = m_dt;
    if (dt <= 0.0)
        return;

    // 1. 联动模式：先跑状态机，产出本帧执行器与电机指令；手动模式下由界面直接操作执行器
    if (m_mode == Automatic)
        updateAutomaticMode();

    // 2. 执行器状态注入物理模型（ActuatorModels 是唯一输入源）
    m_waterModel->setInletValveOpen(m_actuators->inletValveOpen());
    m_waterModel->setDrainPumpOn(m_actuators->drainPumpOn());
    m_tempModel->setHeaterOn(m_actuators->heaterOn());

    // 3. 模型间联动：桶内水质量决定升温/降温速率
    m_tempModel->setWaterMass(m_waterModel->waterMass());

    // 4. 推进所有物理模型
    m_waterModel->update(dt);
    m_tempModel->update(dt);
    m_motorModel->update(dt);
    m_doorModel->update(dt);

    // 5. 累计仿真时间并通知界面刷新
    m_elapsedTime += dt;
    emit elapsedTimeChanged();
    emit ticked(m_elapsedTime);
}

void SimulationEngine::updateAutomaticMode()
{
    m_stateTimer += m_dt;

    switch (m_state) {
    case Idle:
    case Complete:
        break;   // 等待 startWashProgram() 触发
    case Fill:
        updateFillState();
        break;
    case Heat:
        updateHeatState();
        break;
    case Wash:
        updateWashState();
        break;
    case Drain:
        updateDrainState();
        break;
    case Rinse:
        updateRinseState();
        break;
    case Spin:
        updateSpinState();
        break;
    }
}

void SimulationEngine::updateFillState()
{
    // 门锁未到位前不进水（对应场景第一步“门锁上锁”）
    if (!m_doorModel->isLocked()) {
        m_actuators->setInletValveOpen(false);
        if (m_stateTimer > kDoorTimeoutSec) {
            emit warning(tr("门锁上锁超时，无法开始注水"));
            m_stateTimer = 0.0;
        }
        return;
    }

    m_actuators->setInletValveOpen(true);
    m_actuators->setDrainPumpOn(false);
    m_actuators->setHeaterOn(false);

    const bool reached = m_waterModel->waterLevel() >= m_currentProgram.targetWaterLevel;
    if (!reached) {
        if (m_waterModel->fillPercent() >= kFullThresholdPercent) {
            emit warning(tr("接近满桶但未达目标水位，请检查目标水位设置"));
        } else if (m_stateTimer > kFillTimeoutSec) {
            emit warning(tr("注水超时，强制进入下一阶段"));
        } else {
            return;
        }
    }

    m_actuators->setInletValveOpen(false);
    // 目标温度高于当前水温才需要加热，常温程序（如快洗）直接进入洗涤
    if (m_currentProgram.targetTemperature > m_tempModel->temperature() + 0.5)
        transitionTo(Heat);
    else
        transitionTo(Wash);
}

void SimulationEngine::updateHeatState()
{
    m_actuators->setInletValveOpen(false);
    m_actuators->setDrainPumpOn(false);
    m_actuators->setHeaterOn(true);
    stopMotor();

    const bool reached = m_tempModel->temperature() >= m_currentProgram.targetTemperature;
    if (!reached) {
        if (m_stateTimer <= kHeatTimeoutSec)
            return;
        emit warning(tr("加热超时，未达目标温度即进入洗涤"));
    }

    m_actuators->setHeaterOn(false);
    transitionTo(Wash);
}

void SimulationEngine::updateWashState()
{
    m_actuators->setInletValveOpen(false);
    m_actuators->setDrainPumpOn(false);
    m_actuators->setHeaterOn(false);

    // 正反转交替搅拌
    runAgitation(m_currentProgram.washSpeed);

    if (m_stateTimer >= m_currentProgram.washDuration) {
        stopMotor();
        // 洗涤结束先排水，排完按漂洗次数决定去向
        m_drainNext = (qRound(m_currentProgram.rinseCycles) > 0) ? Rinse : Spin;
        transitionTo(Drain);
    }
}

void SimulationEngine::updateDrainState()
{
    m_actuators->setInletValveOpen(false);
    m_actuators->setHeaterOn(false);
    m_actuators->setDrainPumpOn(true);
    stopMotor();

    if (!isDrained()) {
        if (m_stateTimer <= kDrainTimeoutSec)
            return;
        emit warning(tr("排水超时，桶内仍有余水"));
    }

    m_actuators->setDrainPumpOn(false);
    transitionTo(m_drainNext == Idle ? Complete : m_drainNext);
}

void SimulationEngine::updateRinseState()
{
    if (m_rinseFilling) {
        // 漂洗注水：不加热
        m_actuators->setInletValveOpen(true);
        m_actuators->setDrainPumpOn(false);
        m_actuators->setHeaterOn(false);
        stopMotor();

        const bool reached = m_waterModel->waterLevel() >= m_currentProgram.targetWaterLevel
                             || m_waterModel->fillPercent() >= kFullThresholdPercent;
        if (!reached) {
            if (m_stateTimer <= kFillTimeoutSec)
                return;
            emit warning(tr("漂洗注水超时，直接进入漂洗搅拌"));
        }

        m_actuators->setInletValveOpen(false);
        m_rinseFilling = false;
        m_stateTimer = 0.0;
        m_agitationTimer = 0.0;
        emit stateChanged();   // 子阶段变化，刷新界面文本
        return;
    }

    // 漂洗搅拌：时长取洗涤时间的 20%，限幅 60~300 s
    const double rinseWashDuration = std::clamp(m_currentProgram.washDuration * 0.2, 60.0, 300.0);
    runAgitation(m_currentProgram.washSpeed);

    if (m_stateTimer >= rinseWashDuration) {
        stopMotor();
        ++m_rinseDone;
        // 还有剩余次数则排水后再漂一轮，否则排水后脱水
        m_drainNext = (m_rinseDone < qRound(m_currentProgram.rinseCycles)) ? Rinse : Spin;
        transitionTo(Drain);
    }
}

void SimulationEngine::updateSpinState()
{
    m_actuators->setInletValveOpen(false);
    m_actuators->setHeaterOn(false);
    m_actuators->setDrainPumpOn(true);   // 脱水甩出的水需持续排出

    driveMotorToSpeed(m_currentProgram.spinSpeed);

    if (m_stateTimer >= m_currentProgram.spinDuration) {
        stopMotor();
        transitionTo(Complete);
    }
}

void SimulationEngine::transitionTo(SimState newState)
{
    m_state = newState;
    m_stateTimer = 0.0;
    m_agitationTimer = 0.0;

    // 进入动作：把执行器摆到该状态的初始组合
    switch (newState) {
    case Idle:
        m_actuators->reset();
        stopMotor();
        break;
    case Fill:
        m_actuators->setDrainPumpOn(false);
        m_actuators->setHeaterOn(false);
        m_actuators->setInletValveOpen(m_doorModel->isLocked());
        stopMotor();
        break;
    case Heat:
        m_actuators->setInletValveOpen(false);
        m_actuators->setDrainPumpOn(false);
        m_actuators->setHeaterOn(true);
        stopMotor();
        break;
    case Wash:
        m_actuators->setInletValveOpen(false);
        m_actuators->setDrainPumpOn(false);
        m_actuators->setHeaterOn(false);
        break;
    case Rinse:
        m_rinseFilling = true;
        m_actuators->setInletValveOpen(true);
        m_actuators->setDrainPumpOn(false);
        m_actuators->setHeaterOn(false);
        stopMotor();
        break;
    case Drain:
        m_actuators->setInletValveOpen(false);
        m_actuators->setHeaterOn(false);
        m_actuators->setDrainPumpOn(true);
        stopMotor();
        break;
    case Spin:
        m_actuators->setInletValveOpen(false);
        m_actuators->setHeaterOn(false);
        m_actuators->setDrainPumpOn(true);
        break;
    case Complete:
        m_actuators->reset();
        stopMotor();
        m_doorModel->requestUnlock();
        break;
    }

    emit stateChanged();
    emit stateEntered(newState);
    if (newState == Complete)
        emit washProgramFinished();
}

// ---------------------------------------------------------------------------
// 电机控制辅助
// ---------------------------------------------------------------------------

void SimulationEngine::driveMotorToSpeed(double targetRpm)
{
    const double maxRpm = std::max(1.0, m_motorModel->maxSpeedRpm());
    const double target = std::clamp(targetRpm, -maxRpm, maxRpm);
    // 归一化转速误差的积分调节：duty += k * (target - actual)/maxRpm * dt
    const double error = (target - m_motorModel->speedRpm()) / maxRpm;
    const double duty = m_motorModel->motorPwmDuty() + kSpeedLoopGain * error * m_dt;
    m_motorModel->setMotorPwmDuty(std::clamp(duty, -1.0, 1.0));
}

void SimulationEngine::stopMotor()
{
    m_motorModel->setMotorPwmDuty(0.0);
}

void SimulationEngine::runAgitation(double speedRpm)
{
    // 节拍：15 s 正转 → 5 s 停 → 15 s 反转 → 5 s 停，循环往复
    const double cycle = 2.0 * (kAgitateRunSec + kAgitatePauseSec);
    m_agitationTimer = std::fmod(m_agitationTimer + m_dt, cycle);

    const double phase = m_agitationTimer;
    if (phase < kAgitateRunSec)
        driveMotorToSpeed(speedRpm);
    else if (phase < kAgitateRunSec + kAgitatePauseSec)
        stopMotor();
    else if (phase < 2.0 * kAgitateRunSec + kAgitatePauseSec)
        driveMotorToSpeed(-speedRpm);
    else
        stopMotor();
}

bool SimulationEngine::isDrained() const
{
    return m_waterModel->volume() <= kDrainedVolumeL;
}

// ---------------------------------------------------------------------------
// 属性读写
// ---------------------------------------------------------------------------

SimulationEngine::SimState SimulationEngine::state() const
{
    return m_state;
}

QString SimulationEngine::stateText() const
{
    switch (m_state) {
    case Idle:
        return tr("待机");
    case Fill:
        return tr("注水");
    case Heat:
        return tr("加热");
    case Wash:
        return tr("洗涤");
    case Rinse:
        return m_rinseFilling ? tr("漂洗注水") : tr("漂洗");
    case Spin:
        return tr("脱水");
    case Drain:
        return tr("排水");
    case Complete:
        return tr("完成");
    }
    return QString();
}

SimulationEngine::SimMode SimulationEngine::mode() const
{
    return m_mode;
}

void SimulationEngine::setMode(SimMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    // 切回手动模式后执行器保持当前状态，交由界面接管
    emit modeChanged();
}

bool SimulationEngine::isRunning() const
{
    return m_running;
}

double SimulationEngine::elapsedTime() const
{
    return m_elapsedTime;
}

double SimulationEngine::stateElapsedTime() const
{
    return m_stateTimer;
}

double SimulationEngine::timeStepMs() const
{
    return m_timeStepMs;
}

double SimulationEngine::timeScale() const
{
    return m_timeScale;
}

void SimulationEngine::setTimeScale(double scale)
{
    const double clamped = std::clamp(scale, 0.1, 100.0);
    if (qFuzzyCompare(clamped, m_timeScale))
        return;
    m_timeScale = clamped;
    m_dt = m_timeStepMs / 1000.0 * m_timeScale;
    emit timeScaleChanged();
}

QString SimulationEngine::programName() const
{
    return m_programName;
}

int SimulationEngine::rinseIndex() const
{
    return m_state == Rinse ? m_rinseDone + 1 : m_rinseDone;
}

WaterLevelModel *SimulationEngine::waterModel() const
{
    return m_waterModel;
}

TemperatureModel *SimulationEngine::tempModel() const
{
    return m_tempModel;
}

MotorModel *SimulationEngine::motorModel() const
{
    return m_motorModel;
}

DoorLockModel *SimulationEngine::doorModel() const
{
    return m_doorModel;
}

ActuatorModels *SimulationEngine::actuators() const
{
    return m_actuators;
}

ScenarioManager *SimulationEngine::scenarioManager() const
{
    return m_scenarios;
}

} // namespace whm
