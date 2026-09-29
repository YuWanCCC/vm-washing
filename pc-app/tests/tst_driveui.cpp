#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include "communication/DriveSerialManager.h"
#include "communication/MatSerialManager.h"
#include "communication/SerialManager.h"

using namespace whm;

class TestDriveUi : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void panelsLoadAndUpdate();
    void serialSelectionIsolation();
};

void TestDriveUi::initTestCase()
{
    qmlRegisterType<DriveSerialManager>("HIL.Communication", 1, 0, "DriveSerialManager");
    qmlRegisterType<MatSerialManager>("HIL.Communication", 1, 0, "MatSerialManager");
    qmlRegisterType<SerialManager>("HIL.Communication", 1, 0, "SerialManager");
    qmlRegisterUncreatableType<DriveSimulator>("HIL.Communication", 1, 0, "DriveSimulator", "owned");
}

void TestDriveUi::panelsLoadAndUpdate()
{
    DriveSerialManager manager;
    QQmlEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    for (const QString &file : {QStringLiteral("DriveDataMonitor.qml"), QStringLiteral("DriveControlPanel.qml"), QStringLiteral("DrivePanel.qml")}) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DRIVE_QML_DIR) + "/" + file));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"drive", QVariant::fromValue(&manager)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(item);
        QQuickWindow window;
        window.resize(file == "DriveControlPanel.qml" ? 460 : 1100, 400);
        item->setParentItem(window.contentItem());
        item->setWidth(window.width());
        if (file != "DriveControlPanel.qml") item->setHeight(260);
        window.show();
        manager.simulator()->beginSession(0);
        for (int address = 1; address <= 4; ++address) {
            manager.simulator()->setSelectedAddress(address);
            manager.simulator()->setFeedback(address, "version", 29);
            const auto raw = DriveCodec::encode(address, true, {{"speed", 30}, {"direction", 1}, {"load2", 2}, {"ratio", 52}});
            manager.simulator()->receive({address, raw, true}, address * 100);
            manager.simulator()->advance(address * 100 + 50);
            manager.logEntry("RX", address, "测试完整帧日志");
            QTest::qWait(60);
        }
        if (file == "DrivePanel.qml") {
            object->setProperty("addressFilter", 2);
            QTest::qWait(60);
            QCOMPARE(object->property("entries").value<QJSValue>().property("length").toInt(), 4);
        }
        item->setWidth(600);
        QTest::qWait(50);
        QVERIFY(item->width() > 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        item->setParentItem(nullptr);
        manager.simulator()->endSession(1000);
    }
}

void TestDriveUi::serialSelectionIsolation()
{
    SerialManager hil;
    MatSerialManager mat;
    DriveSerialManager drive;
    hil.setPortName("HIL_TEST");
    mat.setPortName("MAT_TEST");
    mat.setBaudRate(19200);
    drive.setPortName("DRIVE_TEST");
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DRIVE_QML_DIR) + "/ConnectionBar.qml"));
    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({
        {"serial", QVariant::fromValue(&hil)}, {"mat", QVariant::fromValue(&mat)},
        {"drive", QVariant::fromValue(&drive)}, {"engine", QVariant::fromValue<QObject *>(nullptr)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    object->setProperty("workMode", 2);
    QVERIFY(QMetaObject::invokeMethod(object.data(), "syncPortName", Q_ARG(QVariant, QVariant("DRIVE_OTHER"))));
    QCOMPARE(drive.portName(), QStringLiteral("DRIVE_OTHER"));
    QCOMPARE(hil.portName(), QStringLiteral("HIL_TEST"));
    QCOMPARE(mat.portName(), QStringLiteral("MAT_TEST"));
    object->setProperty("workMode", 1);
    QVERIFY(QMetaObject::invokeMethod(object.data(), "syncPortName", Q_ARG(QVariant, QVariant("MAT_OTHER"))));
    QCOMPARE(drive.portName(), QStringLiteral("DRIVE_OTHER"));
    QCOMPARE(mat.portName(), QStringLiteral("MAT_OTHER"));
    QCOMPARE(mat.baudRate(), 19200);
    QCOMPARE(drive.baudRate(), 4800);
    QVERIFY(QMetaObject::invokeMethod(object.data(), "refreshAndSync"));
    QTest::qWait(50);
    QCOMPARE(drive.portName(), QStringLiteral("DRIVE_OTHER"));
    QCOMPARE(hil.portName(), QStringLiteral("HIL_TEST"));
}

QTEST_MAIN(TestDriveUi)
#include "tst_driveui.moc"
