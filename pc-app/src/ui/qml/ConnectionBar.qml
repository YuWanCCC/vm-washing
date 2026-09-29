import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Communication 1.0

/*!
    顶部状态栏：应用标题 + 仿真状态/计时 + HIL/MAT/驱动模式与串口连接。

    serial 为 HIL SerialManager，mat 为 MAT MatSerialManager，均允许为 null。
    workMode: 0=HIL, 1=MAT, 2=驱动，控制连接按钮操作的串口通道。
*/
Rectangle {
    id: root

    required property var engine
    //! whm::SerialManager 实例（HIL），可为 null
    property var serial: null
    //! whm::MatSerialManager 实例（MAT），可为 null
    property var mat: null
    property var drive: null
    readonly property var activeManager: workMode === 0 ? serial : workMode === 1 ? mat : drive
    readonly property bool channelBusy: isBusy(activeManager)
    signal connectionError(string message)
    signal workModeRequested(int mode)

    function isBusy(manager) {
        if (!manager) return false;
        return manager === serial ? manager.connectionState !== SerialManager.Disconnected : manager.connected;
    }

    function restorePortSelection() {
        if (!activeManager) return;
        var name = activeManager.portName;
        portCombo.currentIndex = portCombo.find(name);
        if (!name && portCombo.count > 0 && !channelBusy) {
            portCombo.currentIndex = 0;
            syncPortName(portCombo.currentText);
        }
    }

    onActiveManagerChanged: Qt.callLater(restorePortSelection)
    Component.onCompleted: Qt.callLater(restorePortSelection)
    Connections {
        target: root.activeManager
        function onPortNameChanged() { Qt.callLater(root.restorePortSelection); }
    }
    //! 工作模式: 0=HIL, 1=MAT, 2=驱动
    property int workMode: 0

    readonly property color colorSurface: "#333333"
    readonly property color colorBorder: "#454545"
    readonly property color colorFg: "#e0e0e0"
    readonly property color colorMuted: "#9e9e9e"
    readonly property color colorAccent: "#4fc3f7"

    //! HIL 链路指示灯颜色
    readonly property color hilLinkColor: {
        if (!serial) return "#616161";
        switch (serial.connectionState) {
        case SerialManager.Connected:   return "#66bb6a";
        case SerialManager.Connecting:  return "#ffca28";
        case SerialManager.Error:       return "#ef5350";
        default:                        return "#616161";
        }
    }

    //! MAT 链路指示灯颜色
    readonly property color matLinkColor: {
        if (!mat) return "#616161";
        switch (mat.connectionState) {
        case MatSerialManager.Connected: return "#66bb6a";
        case MatSerialManager.Error:     return "#ef5350";
        default:                         return "#616161";
        }
    }

    //! 仅刷新当前通道，保留各通道独立的端口配置
    function refreshAndSync() {
        if (activeManager) activeManager.refreshPorts();
        Qt.callLater(restorePortSelection);
    }

    //! 仅修改未连接的当前通道，不改写其他通道
    function syncPortName(name) {
        if (activeManager && !channelBusy && name) activeManager.portName = name;
    }

    implicitHeight: 38
    color: colorSurface
    radius: 8

    // 秒 → mm:ss
    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds));
        var mm = Math.floor(total / 60);
        var ss = total % 60;
        return (mm < 10 ? "0" : "") + mm + ":" + (ss < 10 ? "0" : "") + ss;
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12

        // --- 左侧：标题 ---
        Text {
            text: qsTr("洗衣机HIL仿真平台")
            color: root.colorAccent
            font.pixelSize: 16
            font.bold: true
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 20
            color: root.colorBorder
        }

        // --- 中间：仿真状态与计时 ---
        Text {
            text: root.workMode === 2 ? qsTr("驱动板从机仿真")
                                     : qsTr("状态: ") + (root.engine ? root.engine.stateText : "-")
            color: root.colorFg
            font.pixelSize: 14
        }

        Text {
            visible: root.workMode !== 2
            text: qsTr("运行: ") + root.formatTime(root.engine ? root.engine.elapsedTime : 0)
                  + (root.engine && root.engine.timeScale !== 1
                     ? "  ×" + root.engine.timeScale.toFixed(1) : "")
            color: root.colorMuted
            font.pixelSize: 12
        }

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.workMode === 2 ? (root.drive && root.drive.connected ? "#66bb6a" : "#616161")
                                      : (root.engine && root.engine.running ? "#66bb6a" : "#616161")
        }

        Text {
            text: root.workMode === 2 ? (root.drive && root.drive.connected ? qsTr("等待/响应主控") : qsTr("未连接"))
                                     : (root.engine && root.engine.running ? qsTr("仿真中") : qsTr("已暂停"))
            color: root.colorMuted
            font.pixelSize: 12
        }

        Item { Layout.fillWidth: true }

        // --- 右侧：串口控制区 ---

        // 模式切换请求交给主窗口，保留单向属性绑定
        Rectangle {
            Layout.preferredWidth: 158
            Layout.preferredHeight: 26
            radius: 4
            color: "#3a3a3a"
            border.width: 1
            border.color: root.colorBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0

                Repeater {
                    model: ["HIL", "MAT", qsTr("驱动")]
                    Rectangle {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 3
                        color: root.workMode === index ? root.colorAccent : "transparent"
                        Text {
                            anchors.centerIn: parent; text: parent.modelData
                            font.pixelSize: 12; font.bold: root.workMode === parent.index
                            color: root.workMode === parent.index ? "#10171d" : root.colorMuted
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: root.workModeRequested(parent.index)
                        }
                    }
                }
            }
        }

        // 三个通道共用配置位置，各自记住端口
        ComboBox {
            id: portCombo
            Layout.preferredWidth: 110
            Layout.preferredHeight: 26
            enabled: root.activeManager !== null && !root.channelBusy
            font.pixelSize: 12
            model: root.activeManager ? root.activeManager.availablePorts : []
            displayText: currentIndex < 0 && root.activeManager ? root.activeManager.portName : currentText
            onActivated: root.syncPortName(currentText)
            onModelChanged: Qt.callLater(root.restorePortSelection)
        }

        ToolButton {
            Layout.preferredHeight: 26
            enabled: root.activeManager !== null
            text: qsTr("刷新")
            font.pixelSize: 12
            onClicked: root.refreshAndSync()
        }

        // MAT 波特率选择（仅 MAT 模式可见）
        ComboBox {
            id: matBaudCombo
            Layout.preferredWidth: 72
            Layout.preferredHeight: 26
            visible: root.workMode === 1
            enabled: root.mat && !root.mat.connected
            font.pixelSize: 11
            model: [9600, 19200]
            currentIndex: root.mat && root.mat.baudRate === 19200 ? 1 : 0
            onActivated: { if (root.mat) root.mat.baudRate = parseInt(currentText) }
        }

        Text {
            visible: root.workMode === 2
            text: "4800 / 8N1"
            color: root.colorMuted
            font.pixelSize: 11
        }

        // 链路指示灯显示当前模式的状态
        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.workMode === 0 ? root.hilLinkColor : root.workMode === 1 ? root.matLinkColor
                   : !root.drive ? "#616161" : root.drive.connected ? "#66bb6a"
                   : root.drive.connectionState === DriveSerialManager.Error ? "#ef5350" : "#616161"

            SequentialAnimation on opacity {
                running: root.workMode === 0 && root.serial
                         ? root.serial.connectionState === SerialManager.Connecting : false
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 400 }
                NumberAnimation { to: 1.0; duration: 400 }
            }
        }

        // 连接/断开按钮（操作当前模式对应的串口）
        Button {
            Layout.preferredHeight: 26
            enabled: root.activeManager !== null
            font.pixelSize: 12
            text: root.channelBusy ? qsTr("断开") : qsTr("连接")
            onClicked: {
                var manager = root.activeManager;
                if (!manager) return;
                if (root.channelBusy) { manager.disconnectPort(); return; }
                var name = portCombo.currentText;
                if (!name) { root.connectionError(qsTr("请选择可用串口")); return; }
                var managers = [root.serial, root.mat, root.drive];
                for (var i = 0; i < managers.length; ++i) {
                    var other = managers[i];
                    if (other && other !== manager && root.isBusy(other)
                            && other.portName.toUpperCase() === name.toUpperCase()) {
                        root.connectionError(qsTr("串口 %1 已被其他通道占用，请先断开该通道").arg(name));
                        return;
                    }
                }
                root.syncPortName(name);
                manager.connectPort();
            }
        }
    }
}
