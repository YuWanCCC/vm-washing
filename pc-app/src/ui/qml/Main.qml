import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Core 1.0
import HIL.Communication 1.0
import HIL.Integration 1.0
import HIL.Fault 1.0

/*!
    主窗口：顶部链路状态栏 + 左右分栏（洗衣机视图 / 控制面板）+ 底部可折叠数据监控。

    SimulationEngine 在此唯一实例化并向下传递；场景管理器直接取引擎内部的
    engine.scenarios，保证界面选择的程序与状态机实际加载的场景是同一份数据。
*/
ApplicationWindow {
    id: root

    readonly property color colorBackground: "#2b2b2b"
    readonly property color colorAccent: "#4fc3f7"

    //! 数据监控面板目标高度（折叠时仅留标题栏）
    property real monitorHeight: dataMonitor.expanded ? 260 : 48

    width: 1280
    height: 800
    visible: true
    color: colorBackground
    title: qsTr("洗衣机HIL仿真平台")

    Behavior on monitorHeight {
        NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
    }

    // ------------------------------------------------------------------
    // 后端对象
    // id 刻意不叫 engine：子组件均有同名属性，避开 engine: engine 的名称歧义
    // ------------------------------------------------------------------
    SimulationEngine {
        id: simEngine
    }

    SerialManager {
        id: serialManager
    }

    //! 故障注入器：对下行传感器输出通道注入断线/短路等故障
    FaultInjector {
        id: faultInjector
    }

    //! 数据记录器：逐帧缓存关键通道数据，支持导出 CSV
    DataRecorder {
        id: dataRecorder
    }

    //! PC↔MCU 闭环数据桥：串口连接后自动进入闭环模式，把模型输出发给主控并回灌主控反馈
    IntegrationBridge {
        id: integrationBridge

        engine: simEngine
        serial: serialManager
        faultInjector: faultInjector
        recorder: dataRecorder
        bridgeMode: serialManager.connected ? IntegrationBridge.ClosedLoop
                                            : IntegrationBridge.SimulationOnly
        active: simEngine.running && serialManager.connected
    }

    //! 引擎自带的场景管理器（startWashProgram 就是从它取场景配置）
    readonly property var scenarios: simEngine.scenarios

    Component.onCompleted: serialManager.refreshPorts()

    // 引擎保护动作（溢流/过热/超时）以浮层提示
    Connections {
        target: simEngine
        function onWarning(message) { warningBanner.show(message); }
    }

    Connections {
        target: simEngine
        function onWashProgramFinished() { warningBanner.show(qsTr("洗涤程序已完成")); }
    }

    Connections {
        target: serialManager
        function onErrorOccurred(error) { warningBanner.show(qsTr("串口: ") + error); }
    }

    // ------------------------------------------------------------------
    // 全局快捷键
    // ------------------------------------------------------------------
    Shortcut {
        sequence: "Space"
        onActivated: {
            if (simEngine.running)
                simEngine.stop();
            else
                simEngine.start();
        }
    }

    Shortcut {
        sequence: "R"
        onActivated: simEngine.reset()
    }

    Shortcut {
        sequence: "D"
        onActivated: dataMonitor.expanded = !dataMonitor.expanded
    }

    // ------------------------------------------------------------------
    // 布局
    // ------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        ConnectionBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            engine: simEngine
            serial: serialManager
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 6
                color: SplitHandle.pressed ? root.colorAccent
                                           : (SplitHandle.hovered ? "#555555" : "#3a3a3a")
                radius: 3
            }

            WashingMachine3D {
                SplitView.preferredWidth: root.width * 0.6
                SplitView.minimumWidth: 360
                SplitView.fillWidth: true
                engine: simEngine
            }

            ControlPanel {
                SplitView.preferredWidth: root.width * 0.4
                SplitView.minimumWidth: 340
                engine: simEngine
                faultInjector: faultInjector
                recorder: dataRecorder
            }
        }

        DataMonitor {
            id: dataMonitor

            Layout.fillWidth: true
            Layout.preferredHeight: root.monitorHeight
            Layout.minimumHeight: 48
            engine: simEngine
        }
    }

    // ------------------------------------------------------------------
    // 提示浮层
    // ------------------------------------------------------------------
    Rectangle {
        id: warningBanner

        function show(message) {
            bannerText.text = message;
            opacity = 1;
            hideTimer.restart();
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 56
        width: bannerText.implicitWidth + 32
        height: 34
        radius: 8
        z: 100
        color: "#4a3a1c"
        border.width: 1
        border.color: "#ffca28"
        opacity: 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }

        Text {
            id: bannerText

            anchors.centerIn: parent
            color: "#ffe082"
            font.pixelSize: 14
        }

        Timer {
            id: hideTimer
            interval: 3500
            onTriggered: warningBanner.opacity = 0
        }

        MouseArea {
            anchors.fill: parent
            onClicked: warningBanner.opacity = 0
        }
    }

    // 快捷键提示
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        text: qsTr("Space 暂停/继续    R 重置    D 折叠监控")
        color: "#6f7780"
        font.pixelSize: 12
        z: 50
    }
}
