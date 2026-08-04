#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/qqml.h>

#include "communication/SerialManager.h"
#include "core/DataRecorder.h"
#include "core/FaultInjector.h"
#include "core/IntegrationBridge.h"
#include "core/ScenarioManager.h"
#include "core/SimulationEngine.h"
#include "models/ActuatorModels.h"
#include "models/DoorLockModel.h"
#include "models/MotorModel.h"
#include "models/TemperatureModel.h"
#include "models/WaterLevelModel.h"

namespace {

//! QML 中 import HIL.Models 1.0 即可访问物理模型类型与枚举
constexpr const char *kModelsUri = "HIL.Models";
//! QML 中 import HIL.Core 1.0 即可访问仿真引擎与场景管理器
constexpr const char *kCoreUri = "HIL.Core";
//! QML 中 import HIL.Communication 1.0 即可访问串口与协议层类型
constexpr const char *kCommunicationUri = "HIL.Communication";
//! QML 中 import HIL.Integration 1.0 即可访问 PC↔MCU 闭环桥接类型
constexpr const char *kIntegrationUri = "HIL.Integration";
//! QML 中 import HIL.Fault 1.0 即可访问故障注入与数据记录类型
constexpr const char *kFaultUri = "HIL.Fault";

void registerModelTypes()
{
    qmlRegisterType<whm::WaterLevelModel>(kModelsUri, 1, 0, "WaterLevelModel");
    qmlRegisterType<whm::TemperatureModel>(kModelsUri, 1, 0, "TemperatureModel");
    qmlRegisterType<whm::MotorModel>(kModelsUri, 1, 0, "MotorModel");
    qmlRegisterType<whm::DoorLockModel>(kModelsUri, 1, 0, "DoorLockModel");
    qmlRegisterType<whm::ActuatorModels>(kModelsUri, 1, 0, "ActuatorModels");
}

void registerCoreTypes()
{
    // 仿真引擎聚合全部物理模型，SimState/SimMode 枚举随类型一并暴露给 QML
    qmlRegisterType<whm::SimulationEngine>(kCoreUri, 1, 0, "SimulationEngine");
    qmlRegisterType<whm::ScenarioManager>(kCoreUri, 1, 0, "ScenarioManager");
}

void registerCommunicationTypes()
{
    qmlRegisterType<whm::SerialManager>(kCommunicationUri, 1, 0, "SerialManager");
    // ProtocolCodec 由 SerialManager 持有，QML 侧只读不创建
    qmlRegisterUncreatableType<whm::ProtocolCodec>(
        kCommunicationUri, 1, 0, "ProtocolCodec",
        QStringLiteral("ProtocolCodec 由 SerialManager 创建，请通过 codec() 访问"));
}

void registerIntegrationTypes()
{
    // 闭环集成层：仿真引擎与串口链路之间的双向数据桥
    qmlRegisterType<whm::IntegrationBridge>(kIntegrationUri, 1, 0, "IntegrationBridge");
}

void registerFaultTypes()
{
    // 故障注入器与数据记录器：Channel/FaultType 枚举随类型一并暴露给 QML
    qmlRegisterType<whm::FaultInjector>(kFaultUri, 1, 0, "FaultInjector");
    qmlRegisterType<whm::DataRecorder>(kFaultUri, 1, 0, "DataRecorder");
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("洗衣机HIL仿真软件"));
    QGuiApplication::setOrganizationName(QStringLiteral("HIL"));
    QGuiApplication::setApplicationVersion(QStringLiteral(WMHIL_VERSION));

    // C++ 类型注册
    registerModelTypes();
    registerCoreTypes();
    registerCommunicationTypes();
    registerIntegrationTypes();
    registerFaultTypes();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("WashingMachineHIL", "Main");

    return app.exec();
}
