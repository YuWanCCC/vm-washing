import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/*!
    串口调试面板：RX/TX 分左右两列显示，按时间戳排列。

    serial 属性传入 SerialManager 实例即可。
*/
Rectangle {
    id: root

    //! SerialManager 实例（可为 null）
    property var serial: null
    //! 最大缓存行数（每侧）
    property int maxLines: 300
    //! 是否自动滚动到底部
    property bool autoScroll: true

    readonly property color colorFg: "#e0e0e0"
    readonly property color colorMuted: "#9e9e9e"
    readonly property color colorTx: "#4fc3f7"
    readonly property color colorRx: "#66bb6a"
    readonly property color colorErr: "#ef5350"

    color: "#1e1e1e"
    radius: 6

    //! TX 日志
    property var txLines: []
    //! RX 日志
    property var rxLines: []
    //! 错误日志
    property var errLines: []

    // 连接 SerialManager 的 debugMessage 信号
    Connections {
        target: root.serial
        function onDebugMessage(message) {
            var ts = Qt.formatTime(new Date(), "hh:mm:ss.zzz");
            var entry = "[" + ts + "] " + message;

            if (message.indexOf("[TX]") === 0) {
                root.txLines.push(entry);
                if (root.txLines.length > root.maxLines)
                    root.txLines = root.txLines.slice(-Math.floor(root.maxLines * 0.8));
            } else if (message.indexOf("[RX]") === 0) {
                root.rxLines.push(entry);
                if (root.rxLines.length > root.maxLines)
                    root.rxLines = root.rxLines.slice(-Math.floor(root.maxLines * 0.8));
            } else if (message.indexOf("[ERR]") === 0 || message.indexOf("[CON]") === 0) {
                root.errLines.push(entry);
                if (root.errLines.length > root.maxLines)
                    root.errLines = root.errLines.slice(-Math.floor(root.maxLines * 0.8));
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // 统计栏
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "TX: " + root.txLines.length
                color: root.colorTx
                font.pixelSize: 11
                font.bold: true
            }
            Text {
                text: "RX: " + root.rxLines.length
                color: root.colorRx
                font.pixelSize: 11
                font.bold: true
            }
            Text {
                text: "ERR/CON: " + root.errLines.length
                color: root.colorErr
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("清空")
                color: root.colorMuted
                font.pixelSize: 11

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.txLines = [];
                        root.rxLines = [];
                        root.errLines = [];
                    }
                }
            }
        }

        // 三列内容区
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            // TX 列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2

                Text {
                    text: "▼ TX (PC → STM32)"
                    color: root.colorTx
                    font.pixelSize: 11
                    font.bold: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"
                        font.pixelSize: 10
                        color: root.colorTx
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        text: root.txLines.join("\n")

                        onTextChanged: {
                            if (root.autoScroll)
                                cursorPosition = text.length;
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: "#333333"
            }

            // RX 列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2

                Text {
                    text: "▼ RX (STM32 → PC)"
                    color: root.colorRx
                    font.pixelSize: 11
                    font.bold: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"
                        font.pixelSize: 10
                        color: root.colorRx
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        text: root.rxLines.join("\n")

                        onTextChanged: {
                            if (root.autoScroll)
                                cursorPosition = text.length;
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: "#333333"
            }

            // 错误/连接事件列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2

                Text {
                    text: "▼ 事件"
                    color: root.colorErr
                    font.pixelSize: 11
                    font.bold: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: "Consolas, Courier New, monospace"
                        font.pixelSize: 10
                        color: root.colorErr
                        background: Rectangle { color: "#0d1117"; radius: 3 }
                        text: root.errLines.join("\n")

                        onTextChanged: {
                            if (root.autoScroll)
                                cursorPosition = text.length;
                        }
                    }
                }
            }
        }
    }
}
