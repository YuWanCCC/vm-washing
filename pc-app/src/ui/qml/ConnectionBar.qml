import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Communication 1.0

/*!
    顶部状态栏：应用标题 + 仿真状态/计时 + 下位机串口链路状态。

    serial 允许为 null（串口层未接入时仅显示占位），此时端口选择与连接按钮禁用。
*/
Rectangle {
    id: root

    required property var engine
    //! whm::SerialManager 实例，可为 null
    property var serial: null

    readonly property color colorSurface: "#333333"
    readonly property color colorBorder: "#454545"
    readonly property color colorFg: "#e0e0e0"
    readonly property color colorMuted: "#9e9e9e"
    readonly property color colorAccent: "#4fc3f7"

    //! 链路指示灯颜色：绿=已连接，黄=连接中，红=错误，灰=未连接
    readonly property color linkColor: {
        if (!serial)
            return "#616161";
        switch (serial.connectionState) {
        case SerialManager.Connected: return "#66bb6a";
        case SerialManager.Connecting: return "#ffca28";
        case SerialManager.Error: return "#ef5350";
        default: return "#616161";
        }
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

        // --- 右侧：串口链路 ---
        Text {
            text: root.serial
                  ? qsTr("TX %1 / RX %2").arg(root.serial.txCount).arg(root.serial.rxCount)
                  : qsTr("串口层未接入")
            color: root.colorMuted
            font.pixelSize: 12
        }

        ComboBox {
            id: portCombo
            Layout.preferredWidth: 130
            Layout.preferredHeight: 26
            enabled: root.serial && !root.serial.connected
            font.pixelSize: 12
            model: root.serial ? root.serial.availablePorts : []
            onActivated: if (root.serial) root.serial.portName = currentText
        }

        ToolButton {
            Layout.preferredHeight: 26
            enabled: root.serial !== null
            text: qsTr("刷新")
            font.pixelSize: 12
            onClicked: if (root.serial) root.serial.refreshPorts()
        }

        Rectangle {
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            radius: 5
            color: root.linkColor

            SequentialAnimation on opacity {
                running: root.serial ? root.serial.connectionState === SerialManager.Connecting : false
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 400 }
                NumberAnimation { to: 1.0; duration: 400 }
            }
        }

        Text {
            text: {
                if (!root.serial)
                    return qsTr("离线");
                if (root.serial.connected)
                    return root.serial.portName;
                switch (root.serial.connectionState) {
                case SerialManager.Connecting: return qsTr("连接中…");
                case SerialManager.Error: return qsTr("链路异常");
                default: return qsTr("未连接");
                }
            }
            color: root.colorFg
            font.pixelSize: 12
        }

        Button {
            Layout.preferredHeight: 26
            enabled: root.serial !== null
            font.pixelSize: 12
            text: root.serial && root.serial.connected ? qsTr("断开") : qsTr("连接")
            onClicked: {
                if (!root.serial)
                    return;
                if (root.serial.connected)
                    root.serial.disconnectPort();
                else
                    root.serial.connectPort();
            }
        }
    }
}
