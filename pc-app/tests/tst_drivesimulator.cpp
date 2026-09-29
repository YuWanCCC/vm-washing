#include <QtTest>
#include "core/DriveSimulator.h"
#include "communication/DriveSerialManager.h"

using namespace whm;
namespace {
QVariantMap feedback(const DriveSimulator &sim, int address)
{
    return sim.snapshot(address).value("feedback").toMap();
}
QByteArray send(DriveSimulator &sim, int address, const QVariantMap &values, qint64 now)
{
    return sim.receive({address, DriveCodec::encode(address, true, values), true}, now);
}
int fb(const DriveSimulator &sim, int address, const char *key)
{
    return feedback(sim, address).value(QLatin1String(key)).toInt();
}
}

class TestDriveSimulator : public QObject {
    Q_OBJECT
private slots:
    void mixedBoardsAndIsolation();
    void loadProtectionAndDoorLock();
    void motorRampRatioAndReverse();
    void compressorAndFan();
    void independentWatchdogs();
    void testModeAndFaultRecovery();
    void resetDisableAndQueue();
    void feedbackValidation();
};

void TestDriveSimulator::mixedBoardsAndIsolation()
{
    DriveSimulator sim;
    QVERIFY(send(sim, 1, {}, 0).isEmpty());
    sim.beginSession(0);
    for (int a = 1; a <= 4; ++a) {
        const auto reply = send(sim, a, {}, a * 10);
        QVERIFY(DriveCodec::valid(reply, false));
        QCOMPARE(sim.snapshot(a).value("rxCount").toInt(), 1);
        QCOMPARE(sim.snapshot(a).value("txCount").toInt(), 0);
        QVERIFY(sim.snapshot(a).value("seen").toBool());
        QVERIFY(sim.receive({a, reply, false}, 50).isEmpty());
    }
    sim.setEnabled(2, false);
    QVERIFY(send(sim, 2, {}, 60).isEmpty());
    QVERIFY(!sim.snapshot(2).value("seen").toBool());
    auto corrupt = DriveCodec::encode(1, true, {});
    corrupt[5] = 1;
    QVERIFY(sim.receive({1, corrupt, true}, 100).isEmpty());
    QVERIFY(sim.receive({5, {}, true}, 100).isEmpty());
    QCOMPARE(sim.snapshot(1).value("lastRxMs").toLongLong(), 10);
}

void TestDriveSimulator::loadProtectionAndDoorLock()
{
    DriveSimulator sim;
    sim.beginSession(0);
    QVariantMap cmd{{"load1", 255}, {"load2", 0x3f}, {"lockCommand", 2},
                    {"lockPeriod", 300}, {"lockMaxAttempts", 5}};
    send(sim, 1, cmd, 0);
    QCOMPARE(fb(sim, 1, "load1"), 255);
    QCOMPARE(fb(sim, 1, "load2"), 0x3f);
    QCOMPARE(fb(sim, 1, "lockPeriod"), 300);
    sim.setFeedback(1, "door", 1);
    QCOMPARE(fb(sim, 1, "load1"), 0);
    QCOMPARE(fb(sim, 1, "load2"), 0);
    QCOMPARE(sim.snapshot(1).value("command").toMap().value("load1").toInt(), 255);
    sim.setFeedback(1, "door", 2);
    sim.setFeedback(1, "fault", 4);
    QCOMPARE(fb(sim, 1, "load1"), 0);
    QCOMPARE(fb(sim, 1, "load2") & 2, 0);
    sim.setFeedback(1, "fault", 0);
    cmd["load2"] = 0x3d; // PWM关闭时其余负载也关闭。
    send(sim, 1, cmd, 10);
    QCOMPARE(fb(sim, 1, "load1"), 0);
    cmd["load2"] = 0x3f;
    cmd["lockCommand"] = 1;
    send(sim, 1, cmd, 20);
    QCOMPARE(fb(sim, 1, "lockState"), 1);
    QCOMPARE(fb(sim, 1, "load1"), 0);
    sim.setFeedback(1, "lockState", 3);
    cmd["lockCommand"] = 2;
    send(sim, 1, cmd, 30);
    QCOMPARE(fb(sim, 1, "lockState"), 3);
    sim.followDoorLock();
    QCOMPARE(fb(sim, 1, "lockState"), 2);
    sim.setFeedback(1, "lockAttempts", 4);
    cmd["lockMaxAttempts"] = 0;
    send(sim, 1, cmd, 40);
    QCOMPARE(fb(sim, 1, "lockAttempts"), 0);
    cmd["lockCommand"] = 0;
    send(sim, 1, cmd, 50);
    QCOMPARE(fb(sim, 1, "lockState"), 2);
}

void TestDriveSimulator::motorRampRatioAndReverse()
{
    DriveSimulator sim;
    sim.beginSession(0);
    QVariantMap cmd{{"direction", 1}, {"speed", 50}, {"acceleration", 10}, {"ratio", 52}, {"motorCode", 4}};
    send(sim, 2, cmd, 0);
    sim.advance(2000);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 20);
    QCOMPARE(sim.snapshot(2).value("shaftSpeed").toDouble(), 1040.0);
    QCOMPARE(fb(sim, 2, "speed"), 50);
    QCOMPARE(fb(sim, 2, "actualAcceleration"), 10);
    cmd["direction"] = 2;
    send(sim, 2, cmd, 2000);
    sim.advance(3000);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 10);
    QCOMPARE(fb(sim, 2, "actualDirection"), 1);
    sim.advance(4000);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 0);
    sim.advance(5000);
    QCOMPARE(fb(sim, 2, "actualDirection"), 2);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 10);
    cmd["ratio"] = 0;
    cmd["acceleration"] = 0;
    const auto reply = send(sim, 2, cmd, 5000);
    QCOMPARE(DriveCodec::decode({2, reply, false}).value("acceleration").toInt(), 10);
    QCOMPARE(sim.snapshot(2).value("shaftSpeed").toDouble(), -520.0);
    sim.setFeedback(2, "fault", 0x20);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 0);
    QCOMPARE(fb(sim, 2, "speed"), 50);
}

void TestDriveSimulator::compressorAndFan()
{
    DriveSimulator sim;
    sim.beginSession(0);
    QVariantMap compressor{{"speed", 1000}, {"acceleration", 200}, {"pfcOff", 1},
                           {"suppressionOff", 1}, {"mode", 3}, {"model", 4}, {"suppressionRatio", 25}, {"targetTemp", 65}};
    send(sim, 3, compressor, 0);
    sim.advance(1000);
    QCOMPARE(fb(sim, 3, "actualSpeed"), 200);
    QCOMPARE(fb(sim, 3, "suppression"), 1);
    QCOMPARE(fb(sim, 3, "model"), 4);
    QCOMPARE(fb(sim, 3, "targetTemp"), 65);
    compressor["targetTemp"] = 0;
    sim.setFeedback(3, "fault2", 0x40);
    compressor["clearFault"] = 1;
    send(sim, 3, compressor, 1000);
    QCOMPARE(fb(sim, 3, "fault2"), 0x40);
    QCOMPARE(fb(sim, 3, "targetTemp"), 65);
    QCOMPARE(fb(sim, 3, "actualSpeed"), 0);
    sim.setFeedback(3, "fault2", 0);
    compressor["reset"] = 1;
    send(sim, 3, compressor, 1100);
    QCOMPARE(fb(sim, 3, "actualSpeed"), 0);
    QCOMPARE(fb(sim, 3, "speed"), 1000);
    QCOMPARE(fb(sim, 3, "targetTemp"), 0);
    const QVariantMap fan{{"direction", 2}, {"speed", 35}, {"acceleration", 20}};
    send(sim, 4, fan, 1100);
    sim.advance(2100);
    QCOMPARE(fb(sim, 4, "actualSpeed"), 8);
    QCOMPARE(fb(sim, 4, "actualAcceleration"), 20);
    QCOMPARE(fb(sim, 4, "actualDirection"), 2);
    QCOMPARE(fb(sim, 4, "speed"), 35);
    sim.setFeedback(4, "fault", 1);
    QCOMPARE(fb(sim, 4, "actualSpeed"), 0);
}

void TestDriveSimulator::independentWatchdogs()
{
    DriveSimulator sim;
    sim.beginSession(0);
    const QVariantMap load{{"load1", 255}, {"load2", 2}};
    send(sim, 1, load, 0);
    send(sim, 2, {{"speed", 50}, {"direction", 1}}, 0);
    sim.advance(9999);
    QVERIFY(!sim.snapshot(1).value("timedOut").toBool());
    send(sim, 4, {}, 10000);
    QVERIFY(sim.snapshot(1).value("timedOut").toBool());
    QCOMPARE(fb(sim, 1, "load1"), 0);
    QCOMPARE(fb(sim, 2, "actualSpeed"), 0);
    sim.advance(59999);
    QVERIFY(!sim.snapshot(2).value("commFault").toBool());
    sim.advance(60000);
    QCOMPARE(fb(sim, 2, "fault"), 128);
    QCOMPARE(fb(sim, 1, "fault") & 128, 0); // 带电机时负载板不报通信位。
    QCOMPARE(fb(sim, 4, "fault"), 0); // 风机没有通信故障协议位。
    send(sim, 2, {}, 60001);
    QCOMPARE(fb(sim, 2, "fault"), 0);
    sim.setEnabled(2, false);
    QCOMPARE(fb(sim, 1, "fault") & 128, 128);
    send(sim, 1, {{"load1", 0}, {"load2", 2}}, 60002);
    QCOMPARE(fb(sim, 1, "fault"), 0);
    QCOMPARE(fb(sim, 1, "load1"), 0); // 不能恢复旧负载位。
}

void TestDriveSimulator::testModeAndFaultRecovery()
{
    DriveSimulator sim;
    sim.setEnabled(2, false);
    sim.beginSession(0);
    send(sim, 1, {{"load1", 255}, {"load2", 0x82}}, 0);
    send(sim, 4, {{"testMode", 1}, {"speed", 20}, {"direction", 1}}, 0);
    sim.advance(4999);
    QCOMPARE(fb(sim, 1, "fault"), 0);
    sim.advance(5000);
    QCOMPARE(fb(sim, 1, "fault"), 128);
    QCOMPARE(fb(sim, 1, "load2"), 128);
    QVERIFY(sim.snapshot(4).value("commFault").toBool());
    QCOMPARE(fb(sim, 4, "fault"), 0);
    sim.setFeedback(1, "fault", 4);
    send(sim, 1, {{"load2", 2}}, 5001);
    QCOMPARE(fb(sim, 1, "fault"), 4);
    QVERIFY(!sim.snapshot(1).value("commFault").toBool());
}

void TestDriveSimulator::resetDisableAndQueue()
{
    DriveSimulator sim;
    DriveResponseQueue queue;
    QObject::connect(&sim, &DriveSimulator::boardInvalidated, &sim, [&queue](int a) { queue.cancel(a); });
    sim.beginSession(0);
    for (int a = 1; a <= 4; ++a) QVERIFY(queue.enqueue(send(sim, a, {}, 0)));
    sim.resetBoard(1);
    sim.setEnabled(2, false);
    QCOMPARE(queue.size(), 2);
    QCOMPARE(quint8(queue.take()[2]), quint8(3));
    sim.endSession(1);
    QVERIFY(queue.empty());
    QVERIFY(send(sim, 4, {}, 2).isEmpty());
    sim.beginSession(3);
    QVERIFY(!sim.snapshot(4).value("seen").toBool());
    QVERIFY(!sim.enabled(2));
    for (int i = 0; i < 64; ++i) QVERIFY(queue.enqueue(DriveCodec::encode(1, false, {})));
    QVERIFY(!queue.enqueue(DriveCodec::encode(1, false, {})));
    queue.clear();
    QVERIFY(queue.take().isEmpty());
    QVERIFY(!queue.enqueue(DriveCodec::encode(1, true, {})));
}

void TestDriveSimulator::feedbackValidation()
{
    DriveSimulator sim;
    QVERIFY(!sim.setFeedback(5, "version", 1));
    QVERIFY(!sim.setFeedback(1, "load1", 255));
    QVERIFY(!sim.setFeedback(1, "door", 3));
    QVERIFY(!sim.setFeedback(3, "fault2", 4));
    QVERIFY(!sim.setFeedback(1, "humidity", 65536));
    QVERIFY(sim.setFeedback(1, "humidity", 65535));
    QVERIFY(sim.setFeedback(2, "dCurrent", 255));
    sim.beginSession(0);
    auto reply = send(sim, 1, {}, 0);
    QCOMPARE(DriveCodec::decode({1, reply, false}).value("humidity").toInt(), 65535);
    reply = send(sim, 2, {}, 0);
    QCOMPARE(DriveCodec::decode({2, reply, false}).value("dCurrent").toInt(), 255);
    sim.resetBoard(1);
    QCOMPARE(fb(sim, 1, "humidity"), 0);
    QCOMPARE(fb(sim, 1, "door"), 2);
}

QTEST_GUILESS_MAIN(TestDriveSimulator)
#include "tst_drivesimulator.moc"
