#include <QtTest>

#include "models/ActuatorModels.h"
#include "models/DoorLockModel.h"
#include "models/MotorModel.h"
#include "models/TemperatureModel.h"
#include "models/WaterLevelModel.h"

using namespace whm;

namespace {
//! 以固定步长推进模型
template <typename Model>
void advance(Model &model, double seconds, double dt = 0.01)
{
    const int steps = static_cast<int>(seconds / dt);
    for (int i = 0; i < steps; ++i)
        model.update(dt);
}
} // namespace

class PhysicsModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void waterLevelFillAndDrain();
    void waterLevelSensorCurve();
    void temperatureHeating();
    void temperatureNtcRoundTrip();
    void motorSpinUpAndLimit();
    void motorCoastDown();
    void doorLockTransitions();
    void actuatorBitmask();
};

void PhysicsModelsTest::waterLevelFillAndDrain()
{
    WaterLevelModel model;
    QCOMPARE(model.volume(), 0.0);

    // 2 L/min 进水 60 s => 2 L
    model.setInletValveOpen(true);
    advance(model, 60.0);
    QVERIFY(qAbs(model.volume() - 2.0) < 0.01);

    // h = V / A = 0.002 m³ / 0.1 m² = 0.02 m = 20 mm
    QVERIFY(qAbs(model.waterLevel() - 20.0) < 0.1);
    // P = rho * g * h = 196.1 Pa ≈ 0.196 kPa
    QVERIFY(qAbs(model.pressure() - 0.1961) < 0.005);

    // 同时开排水：净流量 0.5 L/min
    model.setDrainPumpOn(true);
    advance(model, 60.0);
    QVERIFY(qAbs(model.volume() - 2.5) < 0.01);

    // 仅排水直至排空并发出信号
    model.setInletValveOpen(false);
    QSignalSpy emptySpy(&model, &WaterLevelModel::emptyDetected);
    advance(model, 200.0);
    QCOMPARE(model.volume(), 0.0);
    QCOMPARE(emptySpy.count(), 1);
}

void PhysicsModelsTest::waterLevelSensorCurve()
{
    WaterLevelModel model;
    // 空桶：标定表 0 kPa 对应 26 kHz
    QVERIFY(qAbs(model.sensorFrequency() - 26000.0) < 1.0);

    // 水位升高时频率单调下降
    model.setInletValveOpen(true);
    advance(model, 600.0);
    const double f1 = model.sensorFrequency();
    advance(model, 600.0);
    const double f2 = model.sensorFrequency();
    QVERIFY(model.volume() > 0.0);
    QVERIFY(f1 < 26000.0);
    QVERIFY(f2 < f1);
}

void PhysicsModelsTest::temperatureHeating()
{
    TemperatureModel model;
    model.setWaterMass(30.0);
    model.setTemperature(20.0);
    QCOMPARE(model.heaterOn(), false);

    // dT/dt = (2000 - 5*(20-25)) / (30*4186) ≈ 0.0161 K/s => 60 s 约 +0.97 K
    model.setHeaterOn(true);
    advance(model, 60.0);
    QVERIFY(qAbs(model.temperature() - 20.97) < 0.05);

    // 关闭加热后向环境温度收敛（此处环境高于水温，故继续小幅上升）
    model.setHeaterOn(false);
    const double before = model.temperature();
    advance(model, 600.0);
    QVERIFY(model.temperature() > before);
    QVERIFY(model.temperature() < model.environmentTemp() + 0.01);

    // 过热保护信号
    QSignalSpy overheatSpy(&model, &TemperatureModel::overheatDetected);
    model.setTemperature(94.0);
    model.setHeaterOn(true);
    advance(model, 600.0);
    QCOMPARE(overheatSpy.count(), 1);
    QVERIFY(model.temperature() <= 100.0);
}

void PhysicsModelsTest::temperatureNtcRoundTrip()
{
    TemperatureModel model;
    // 25 °C 时等于标称阻值
    QVERIFY(qAbs(model.resistanceAt(25.0) - 10000.0) < 1.0);
    // 负温度系数：升温阻值下降
    QVERIFY(model.resistanceAt(60.0) < model.resistanceAt(25.0));
    QVERIFY(model.resistanceAt(5.0) > model.resistanceAt(25.0));
    // 60 °C / B=3950 => 约 2.5 kΩ
    QVERIFY(qAbs(model.resistanceAt(60.0) - 2490.0) < 60.0);
    // 正反解一致
    for (double t : {5.0, 25.0, 40.0, 60.0, 90.0}) {
        const double r = model.resistanceAt(t);
        QVERIFY(qAbs(model.temperatureFromResistance(r) - t) < 1e-6);
    }
}

void PhysicsModelsTest::motorSpinUpAndLimit()
{
    MotorModel model;
    QCOMPARE(model.speedRpm(), 0.0);

    QSignalSpy limitSpy(&model, &MotorModel::speedLimitReached);
    model.setMotorPwmDuty(1.0);
    QCOMPARE(model.motorTorque(), 2.0);

    // 稳态角速度 tau/b = 2/0.005 = 400 rad/s，被 1600 r/min 限速钳位
    advance(model, 5.0);
    QVERIFY(qAbs(model.speedRpm() - 1600.0) < 1.0);
    QCOMPARE(limitSpy.count(), 1);

    // f_hall = w * polePairs / 2pi，1600 r/min 时约 106.7 Hz
    QVERIFY(qAbs(model.hallFrequency() - 106.67) < 0.5);

    // 低占空比时稳态转速由摩擦决定：w = Kt*duty/b = 40 rad/s ≈ 382 r/min
    MotorModel low;
    low.setMotorPwmDuty(0.1);
    advance(low, 30.0);
    QVERIFY(qAbs(low.speedRpm() - 381.97) < 2.0);

    // 负载转矩降低稳态转速
    low.setLoadTorque(0.1);
    advance(low, 30.0);
    QVERIFY(qAbs(low.speedRpm() - 190.99) < 2.0);
}

void PhysicsModelsTest::motorCoastDown()
{
    MotorModel model;
    model.setMotorPwmDuty(0.5);
    advance(model, 10.0);
    QVERIFY(model.speedRpm() > 100.0);

    // 断电惰行：摩擦使转速衰减到 0 且不反转
    model.setMotorPwmDuty(0.0);
    advance(model, 60.0);
    QCOMPARE(model.speedRpm(), 0.0);
    QCOMPARE(model.hallFrequency(), 0.0);

    // 反向占空比得到负转速
    model.setMotorPwmDuty(-0.2);
    advance(model, 30.0);
    QVERIFY(model.speedRpm() < -100.0);
    QVERIFY(model.hallFrequency() > 0.0);   // 频率取绝对值
}

void PhysicsModelsTest::doorLockTransitions()
{
    DoorLockModel model;
    QCOMPARE(model.state(), DoorLockModel::Unlocked);
    QCOMPARE(model.isLocked(), false);

    QSignalSpy lockedSpy(&model, &DoorLockModel::locked);
    QSignalSpy unlockedSpy(&model, &DoorLockModel::unlocked);

    model.requestLock();
    QCOMPARE(model.state(), DoorLockModel::Locking);
    advance(model, 1.0);
    QCOMPARE(model.state(), DoorLockModel::Locking);   // 1.5 s 才完成
    QVERIFY(model.progress() > 0.6 && model.progress() < 0.7);
    advance(model, 0.6);
    QCOMPARE(model.state(), DoorLockModel::Locked);
    QCOMPARE(model.isLocked(), true);
    QCOMPARE(lockedSpy.count(), 1);

    model.requestUnlock();
    QCOMPARE(model.state(), DoorLockModel::Unlocking);
    advance(model, 0.9);
    QCOMPARE(model.state(), DoorLockModel::Unlocking); // 1.0 s 才完成
    advance(model, 0.2);
    QCOMPARE(model.state(), DoorLockModel::Unlocked);
    QCOMPARE(unlockedSpy.count(), 1);

    // 上锁过程中请求解锁：先完成上锁再自动解锁
    model.requestLock();
    advance(model, 0.5);
    model.requestUnlock();
    advance(model, 1.1);
    QCOMPARE(model.state(), DoorLockModel::Unlocking);
    advance(model, 1.1);
    QCOMPARE(model.state(), DoorLockModel::Unlocked);
}

void PhysicsModelsTest::actuatorBitmask()
{
    ActuatorModels model;
    QCOMPARE(model.anyActive(), false);
    QCOMPARE(model.toBitmask(), quint8(0x00));

    QSignalSpy changedSpy(&model, &ActuatorModels::actuatorStateChanged);
    model.setInletValveOpen(true);
    model.setHeaterOn(true);
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(model.toBitmask(), quint8(0x05));
    QCOMPARE(model.anyActive(), true);

    model.applyBitmask(0x02);
    QCOMPARE(model.inletValveOpen(), false);
    QCOMPARE(model.drainPumpOn(), true);
    QCOMPARE(model.heaterOn(), false);

    model.reset();
    QCOMPARE(model.anyActive(), false);
}

QTEST_MAIN(PhysicsModelsTest)

#include "tst_physicsmodels.moc"
