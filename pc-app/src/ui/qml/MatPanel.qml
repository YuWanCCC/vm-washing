import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Communication 1.0

/*!
    MAT 协议日志面板：TX / RX / 事件三栏调试日志。

    串口连接、操作按钮、执行器控制、主控状态均已迁移至
    顶部 ConnectionBar 与右侧 ControlPanel（MAT 模式）。
    本面板仅保留协议调试日志，供排查通信问题。
*/
Rectangle {
    id: root

    //! MatSerialManager 实例（可为 null）
    property var mat: null

    readonly property color colorFg:      "#e0e0e0"
    readonly property color colorMuted:   "#9e9e9e"
    readonly property color colorAccent:  "#4fc3f7"
    readonly property color colorOk:      "#66bb6a"
    readonly property color colorErr:     "#ef5350"

    color: "#2b2b2b"

    // 调试日志
    property var txLines: []
    property var rxLines: []
    property var evtLines: []
    property int maxLines: 200

    Connections {
        target: root.mat

        function onDebugMessage(message) {
            var ts    = Qt.formatTime(new Date(), "hh:mm:ss.zzz")
            var entry = "[" + ts + "] " + message
            if (message.indexOf("[TX]") === 0) {
                root.txLines.push(entry)
                if (root.txLines.length > root.maxLines)
                    root.txLines = root.txLines.slice(-Math.floor(root.maxLines * 0.8))
                txLogArea.text = root.txLines.join("\n")
            } else if (message.indexOf("[RX]") === 0) {
                root.rxLines.push(entry)
                if (root.rxLines.length > root.maxLines)
                    root.rxLines = root.rxLines.slice(-Math.floor(root.maxLines * 0.8))
                rxLogArea.text = root.rxLines.join("\n")
            } else {
                root.evtLines.push(entry)
                if (root.evtLines.length > root.maxLines)
                    root.evtLines = root.evtLines.slice(-Math.floor(root.maxLines * 0.8))
                evtLogArea.text = root.evtLines.join("\n")
            }
        }
    }

    // ----------------------------------------------------------------
    // 主布局：三栏日志
    // ----------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // 顶部统计行
        RowLayout {
            Layout.fillWidth: true; spacing: 12

            Text { text: "TX: " + root.txLines.length; color: root.colorAccent; font.pixelSize: 11; font.bold: true }
            Text { text: "RX: " + root.rxLines.length; color: root.colorOk;    font.pixelSize: 11; font.bold: true }
            Text { text: "EVT: " + root.evtLines.length; color: root.colorErr; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("清空"); color: root.colorMuted; font.pixelSize: 11
                MouseArea {
                    anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.txLines  = []; root.rxLines  = []; root.evtLines = []
                        txLogArea.text  = ""; rxLogArea.text  = ""; evtLogArea.text = ""
                    }
                }
            }
        }

        // 三栏日志区
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            // TX 列
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                Text { text: "▼ TX"; color: root.colorAccent; font.pixelSize: 11; font.bold: true }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    TextArea {
                        id: txLogArea
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"; font.pixelSize: 10
                        color: root.colorAccent
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        onTextChanged: { if (text.length > 0) cursorPosition = text.length }
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#333333" }

            // RX 列
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                Text { text: "▼ RX"; color: root.colorOk; font.pixelSize: 11; font.bold: true }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    TextArea {
                        id: rxLogArea
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"; font.pixelSize: 10
                        color: root.colorOk
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        onTextChanged: { if (text.length > 0) cursorPosition = text.length }
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#333333" }

            // 事件列
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                Text { text: "▼ 事件"; color: root.colorErr; font.pixelSize: 11; font.bold: true }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    TextArea {
                        id: evtLogArea
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"; font.pixelSize: 10
                        color: root.colorErr
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        onTextChanged: { if (text.length > 0) cursorPosition = text.length }
                    }
                }
            }
        }
    }
}
