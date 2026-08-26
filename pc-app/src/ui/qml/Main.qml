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

    底部面板标签页：0=数据监控, 1=串口调试（HIL）, 2=MAT协议
*/
ApplicationWindow {
    id: root

    readonly property color colorBackground: "#2b2b2b"
    readonly property color colorAccent: "#4fc3f7"

    //! 底部面板当前选中的标签页（0=数据监控, 1=串口调试, 2=MAT协议）
    property int bottomTab: 0
    //! 工作模式: 0=HIL, 1=MAT
    property int workMode: 0
    //! 底部面板是否展开
    property bool bottomExpanded: true
    //! 底部面板目标高度
    property real monitorHeight: bottomExpanded ? 260 : 40

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

    //! MAT 协议串口管理器（直连洗衣机主控板，独立于 HIL 串口）
    MatSerialManager {
        id: matSerial
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

    Component.onCompleted: {
        serialManager.refreshPorts()
        matSerial.refreshPorts()
    }

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

    Connections {
        target: matSerial
        function onErrorOccurred(error) { warningBanner.show(qsTr("MAT: ") + error); }
    }

    // ------------------------------------------------------------------
    // 全局快捷键
    // ------------------------------------------------------------------
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (simEngine.running)
                simEngine.stop();
            else
                simEngine.start();
        }
    }

    Shortcut {
        sequence: "R"
        context: Qt.ApplicationShortcut
        onActivated: simEngine.reset()
    }

    Shortcut {
        sequence: "D"
        context: Qt.ApplicationShortcut
        onActivated: root.bottomExpanded = !root.bottomExpanded
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
            mat: matSerial
            workMode: root.workMode
            onWorkModeChanged: root.workMode = workMode
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
                workMode: root.workMode
                mat: matSerial
            }
        }

        // ---------------------------------------------------------------
        // 底部标签页面板：数据监控 / 串口调试
        // ---------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.monitorHeight
            Layout.minimumHeight: 40
            color: "#2b2b2b"
            radius: 8

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 0

                // 标签栏
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: "#333333"
                    radius: 8

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4

                        // 展开/折叠按钮
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 6
                            color: expandArea.containsMouse ? "#3a3a3a" : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: root.bottomExpanded ? "\u25BE" : "\u25B8"
                                color: root.colorAccent
                                font.pixelSize: 16
                            }

                            MouseArea {
                                id: expandArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.bottomExpanded = !root.bottomExpanded
                            }
                        }

                        // 标签页按钮
                        Repeater {
                            model: [qsTr("数据监控"), qsTr("串口调试 HIL"), qsTr("MAT协议")]

                            Rectangle {
                                Layout.preferredHeight: 28
                                Layout.fillWidth: false
                                implicitWidth: tabText.implicitWidth + 20
                                radius: 6
                                color: root.bottomTab === index ? root.colorAccent : "transparent"

                                Text {
                                    id: tabText
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: 13
                                    font.bold: root.bottomTab === index
                                    color: root.bottomTab === index ? "#10171d" : "#9e9e9e"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.bottomTab = index;
                                        root.bottomExpanded = true;
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // 串口调试页专用：消息计数
                        Text {
                            visible: root.bottomTab === 1
                            text: serialDebug.logLines.length + " 条"
                            color: "#9e9e9e"
                            font.pixelSize: 11
                        }

                        // 串口调试页专用：清空按钮
                        Rectangle {
                            visible: root.bottomTab === 1
                            Layout.preferredWidth: clearText.implicitWidth + 12
                            Layout.preferredHeight: 24
                            radius: 4
                            color: clearArea.containsMouse ? "#2a3a4a" : "transparent"

                            Text {
                                id: clearText
                                anchors.centerIn: parent
                                text: qsTr("清空")
                                color: "#4fc3f7"
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: clearArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: serialDebug.logLines = []
                            }
                        }

                        // MAT 页专用：连接状态指示灯已移至 ConnectionBar，此处仅保留占位
                    }
                }

                // 面板内容区
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.bottomExpanded
                    clip: true

                    DataMonitor {
                        id: dataMonitor
                        anchors.fill: parent
                        engine: simEngine
                        mat: matSerial
                        workMode: root.workMode
                        expanded: true
                        visible: root.bottomTab === 0
                    }

                    SerialDebugPanel {
                        id: serialDebug
                        anchors.fill: parent
                        serial: serialManager
                        visible: root.bottomTab === 1
                    }

                    MatPanel {
                        id: matPanel
                        anchors.fill: parent
                        mat: matSerial
                        visible: root.bottomTab === 2
                    }
                }
            }
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
        text: qsTr("Space 暂停/继续    R 重置    D 折叠/展开")
        color: "#6f7780"
        font.pixelSize: 12
        z: 50
    }
}
