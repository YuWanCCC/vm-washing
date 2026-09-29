#include <QtTest>
#include "communication/DriveCodec.h"
#include "communication/MatCRC8.h"

using namespace whm;
namespace {
//! 与查表实现独立的逐位参考算法。
quint8 referenceCrc(const QByteArray &bytes)
{
    quint8 crc = 0;
    for (char ch : bytes) {
        crc ^= quint8(ch);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? quint8((crc << 1) ^ 0x9b) : quint8(crc << 1);
    }
    return crc;
}
int countKind(const QVector<DriveParseEvent> &events, DriveParseEvent::Kind kind)
{
    int count = 0;
    for (const auto &event : events) if (event.kind == kind) ++count;
    return count;
}
}

class TestDriveCodec : public QObject {
    Q_OBJECT
private slots:
    void frameLengthsAndCrc();
    void littleEndianAndReserved();
    void bitLayout();
    void splitAndCoalesced();
    void invalidAndRecovery();
    void gapIsNotFrameTimeout();
    void echoesUnknownAndBounds();
};

void TestDriveCodec::frameLengthsAndCrc()
{
    const int req[] = {0, 22, 18, 21, 17};
    const int res[] = {0, 29, 32, 29, 21};
    QCOMPARE(referenceCrc("123456789"), quint8(0xea));
    for (int address = 1; address <= 4; ++address) {
        for (bool request : {true, false}) {
            const auto raw = DriveCodec::encode(address, request, {});
            QCOMPARE(raw.size(), request ? req[address] : res[address]);
            QCOMPARE(quint8(raw[0]), quint8(0xfa));
            QCOMPARE(quint8(raw[1]), quint8(0xfa));
            QCOMPARE(quint8(raw[3]), raw.size() - 4);
            QCOMPARE(quint8(raw.back()), referenceCrc(raw.chopped(1)));
            QVERIFY(DriveCodec::valid(raw, request));
            QVERIFY(!DriveCodec::valid(raw, !request));
            auto wrongCoverage = raw;
            wrongCoverage[wrongCoverage.size() - 1] = char(referenceCrc(raw.mid(2, raw.size() - 3)));
            QVERIFY(!DriveCodec::valid(wrongCoverage, request));
        }
    }
    QVERIFY(DriveCodec::encode(5, true, {}).isEmpty());
    QVERIFY(!DriveCodec::valid({}, true));
}

void TestDriveCodec::littleEndianAndReserved()
{
    const auto motor = DriveCodec::encode(2, true, {{"speed", 0x1234}, {"ratio", 52}});
    QCOMPARE(quint8(motor[8]), quint8(0x34));
    QCOMPARE(quint8(motor[9]), quint8(0x12));
    const auto decoded = DriveCodec::decode({2, motor, true});
    QCOMPARE(decoded.value("speed").toInt(), 0x1234);
    QVERIFY(DriveCodec::decode({1, motor, true}).isEmpty());
    for (int address = 1; address <= 4; ++address) {
        for (bool request : {true, false}) {
            QVariantMap values;
            const auto &fields = DriveCodec::fields(address, request);
            QByteArray used(DriveCodec::length(address, request) + 4, char(0));
            for (const auto &f : fields) {
                values[f.key] = f.mask;
                used[f.byte + 1] = char(quint8(used[f.byte + 1]) | (f.mask << f.shift));
                if (f.width == 2) used[f.byte + 2] = char(255);
            }
            const auto raw = DriveCodec::encode(address, request, values);
            for (int i = 4; i < raw.size() - 1; ++i) QCOMPARE(raw[i], used[i]);
            const auto roundTrip = DriveCodec::decode({address, raw, request});
            for (const auto &f : fields) QCOMPARE(roundTrip.value(f.key).toInt(), f.mask);
        }
    }
    const auto load = DriveCodec::encode(1, true, {{"load2", 0xc0}});
    QCOMPARE(quint8(load[16]), quint8(0x80)); // bit6预留不得影响bit7。
}

void TestDriveCodec::bitLayout()
{
    const auto load = DriveCodec::encode(1, true, {{"lockCommand", 2}, {"lockMaxAttempts", 9}, {"load2", 0xbf}});
    QCOMPARE(quint8(load[10]), quint8(0x92));
    const auto status = DriveCodec::encode(1, false, {{"door", 2}, {"waterFull", 1}, {"waterEmpty", 0}, {"floatSwitch", 1}});
    QCOMPARE(quint8(status[15]), quint8(0x16));
    const auto config = DriveCodec::encode(3, true, {{"pfcOff", 1}, {"suppressionOff", 1}, {"mode", 2}});
    QCOMPARE(quint8(config[4]), quint8(0x32));
    const auto reply = DriveCodec::encode(3, false, {{"pfcOff", 1}, {"suppression", 1}, {"mode", 2}});
    QCOMPARE(quint8(reply[5]), quint8(0x26));
    const auto fan = DriveCodec::encode(4, false, {{"testMode", 1}, {"actualDirection", 2}, {"direction", 1}});
    QCOMPARE(quint8(fan[5]), quint8(0x89));
}

void TestDriveCodec::splitAndCoalesced()
{
    const auto frame = DriveCodec::encode(1, true, {{"charge", 0xfafa}, {"humidityPeriod", 0xfa}});
    for (int split = 0; split <= frame.size(); ++split) {
        DriveStreamParser parser;
        auto result = parser.feed(frame.left(split), 0);
        result += parser.feed(frame.mid(split), 5);
        QCOMPARE(countKind(result, DriveParseEvent::Request), 1);
        QCOMPARE(parser.bufferedSize(), 0);
    }
    DriveStreamParser parser;
    QByteArray all = QByteArray::fromHex("0011fa00");
    for (int a = 1; a <= 4; ++a) all += DriveCodec::encode(a, true, {});
    const auto events = parser.feed(all, 0);
    QCOMPARE(countKind(events, DriveParseEvent::Request), 4);
    QCOMPARE(parser.bufferedSize(), 0);
}

void TestDriveCodec::invalidAndRecovery()
{
    DriveStreamParser parser;
    const auto good = DriveCodec::encode(2, true, {{"speed", 123}});
    auto crcBad = good;
    crcBad[5] = char(quint8(crcBad[5]) ^ 1);
    auto lengthBad = good;
    lengthBad[3] = char(255);
    const auto events = parser.feed(crcBad + lengthBad + good, 0);
    QCOMPARE(countKind(events, DriveParseEvent::CrcError), 1);
    QVERIFY(countKind(events, DriveParseEvent::LengthError) >= 1);
    QCOMPARE(countKind(events, DriveParseEvent::Request), 1);
    QCOMPARE(parser.bufferedSize(), 0);
}

void TestDriveCodec::gapIsNotFrameTimeout()
{
    DriveStreamParser parser;
    const auto frame = DriveCodec::encode(2, false, {});
    QVector<DriveParseEvent> events;
    for (int i = 0; i < frame.size(); ++i) events += parser.feed(frame.mid(i, 1), i * 3);
    QCOMPARE(countKind(events, DriveParseEvent::Echo), 1); // 总帧耗时93ms仍有效。
    parser.feed(frame.left(8), 200);
    QVERIFY(parser.expire(250).isEmpty());
    QCOMPARE(countKind(parser.expire(251), DriveParseEvent::GapTimeout), 1);
    events = parser.feed(DriveCodec::encode(4, true, {}), 252);
    QCOMPARE(countKind(events, DriveParseEvent::Request), 1);
    parser.feed(frame.left(4), 300);
    events = parser.feed(DriveCodec::encode(1, true, {}), 351);
    QCOMPARE(countKind(events, DriveParseEvent::GapTimeout), 1);
    QCOMPARE(countKind(events, DriveParseEvent::Request), 1);
}

void TestDriveCodec::echoesUnknownAndBounds()
{
    DriveStreamParser parser;
    auto unknown = QByteArray::fromHex("fafa050100");
    unknown[4] = char(referenceCrc(unknown.chopped(1)));
    QByteArray data = unknown;
    for (int a = 1; a <= 4; ++a) data += DriveCodec::encode(a, false, {});
    auto events = parser.feed(data, 0);
    QCOMPARE(countKind(events, DriveParseEvent::Echo), 4);
    QCOMPARE(countKind(events, DriveParseEvent::Unknown), 1);
    QCOMPARE(countKind(events, DriveParseEvent::Request), 0);
    events = parser.feed(QByteArray(20000, char(0xfa)), 10);
    QVERIFY(parser.bufferedSize() < 4096);
    parser.reset();
    QCOMPARE(parser.bufferedSize(), 0);
    QByteArray many;
    for (int i = 0; i < 1000; ++i) many += DriveCodec::encode(1, true, {});
    QCOMPARE(countKind(parser.feed(many, 20), DriveParseEvent::Request), 1000);
}

QTEST_GUILESS_MAIN(TestDriveCodec)
#include "tst_drivecodec.moc"
