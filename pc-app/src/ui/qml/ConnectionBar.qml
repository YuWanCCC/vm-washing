import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Communication 1.0

/*!
    顶部状态栏：应用标题 + 仿真状态/计时 + HIL/MAT 模式切换与串口连接。

    serial 为 HIL SerialManager，mat 为 MAT MatSerialManager，均允许为 null。
    workMode: 0=HIL, 1=MAT，控制连接按钮操作的串口通道。
*/
Rectangle {
    id: root

    required property var engine
    //! whm::SerialManager 实例（HIL），可为 null
    property var serial: null
    //! whm::MatSerialManager 实例（MAT），可为 null
    property var mat: null
    //! 工作模式: 0=HIL, 1=MAT
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

    //! 刷新两个串口管理器的可用端口列表，并将选中端口同步给两者
    function refreshAndSync() {
        if (serial) serial.refreshPorts();
        if (mat)    mat.refreshPorts();
    }

    //! 将 portCombo 当前选中的端口同步写入两个串口管理器
    function syncPortName(name) {
        if (serial) serial.portName = name;
        if (mat)    mat.portName    = name;
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
            text: qsTr("状态: ") + (root.engine ? root.engine.stateText : "-")
            color: root.colorFg
            font.pixelSize: 14
        }

        Text {
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
            color: root.engine && root.engine.running ? "#66bb6a" : "#616161"
        }

        Text {
            text: root.engine && root.engine.running ? qsTr("仿真中") : qsTr("已暂停")
            color: root.colorMuted
            font.pixelSize: 12
        }

        Item { Layout.fillWidth: true }

        // --- 右侧：串口控制区 ---

        // 模式切换: HIL ↔ MAT
        Rectangle {
            Layout.preferredWidth: 104
            Layout.preferredHeight: 26
            radius: 4
            color: "#3a3a3a"
            border.width: 1
            border.color: root.colorBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 3
                    color: root.workMode === 0 ? root.colorAccent : "transparent"
                    Text {
                        anchors.centerIn: parent; text: "HIL"
                        font.pixelSize: 12; font.bold: root.workMode === 0
                        color: root.workMode === 0 ? "#10171d" : root.colorMuted
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: root.workMode = 0
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 3
                    color: root.workMode === 1 ? root.colorAccent : "transparent"
                    Text {
                        anchors.centerIn: parent; text: "MAT"
                        font.pixelSize: 12; font.bold: root.workMode === 1
                        color: root.workMode === 1 ? "#10171d" : root.colorMuted
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: root.workMode = 1
                    }
                }
            }
        }

        // 端口选择（HIL 与 MAT 共用）
        ComboBox {
            id: portCombo
            Layout.preferredWidth: 110
            Layout.preferredHeight: 26
            enabled: root.serial || root.mat
            font.pixelSize: 12
            model: root.serial ? root.serial.availablePorts
                 : root.mat    ? root.mat.availablePorts
                 : []
            onActivated: root.syncPortName(currentText)
            onModelChanged: {
                if (count > 0) {
                    currentIndex = 0;
                    root.syncPortName(textAt(0));
                }
            }
        }

        ToolButton {
            Layout.preferredHeight: 26
            enabled: root.serial || root.mat
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
            currentIndex: 0
            onActivated: { if (root.mat) root.mat.baudRate = parseInt(currentText) }
        }

        // 链路指示灯（根据当前模式显示 HIL 或 MAT 状态）
        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.workMode === 0 ? root.hilLinkColor : root.matLinkColor

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
            enabled: root.workMode === 0 ? root.serial !== null : root.mat !== null
            font.pixelSize: 12
            text: {
                if (root.workMode === 0)
                    return root.serial && root.serial.connected ? qsTr("断开") : qsTr("连接");
                return root.mat && root.mat.connected ? qsTr("断开") : qsTr("连接");
            }
            onClicked: {
                if (root.workMode === 0) {
                    if (!root.serial) return;
                    if (root.serial.connected)
                        root.serial.disconnectPort();
                    else {
                        root.syncPortName(portCombo.currentText);
                        root.serial.connectPort();
                    }
                } else {
                    if (!root.mat) return;
                    if (root.mat.connected)
                        root.mat.disconnectPort();
                    else {
                        root.syncPortName(portCombo.currentText);
                        root.mat.baudRate = parseInt(matBaudCombo.currentText);
                        root.mat.connectPort();
                    }
                }
            }
        }
    }
}
