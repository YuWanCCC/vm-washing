import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property var drive: null
    property int addressFilter: 0
    property var entries: []
    readonly property int maxEntries: 600
    color: "#2b2b2b"

    function refreshLogs() {
        var tx = [], rx = [], events = [];
        for (var i = 0; i < entries.length; ++i) {
            var item = entries[i];
            if (addressFilter && item.address && item.address !== addressFilter) continue;
            if (item.kind === "TX") tx.push(item.text);
            else if (item.kind === "RX") rx.push(item.text);
            else events.push(item.text);
        }
        txColumn.lines = tx.join("\n");
        rxColumn.lines = rx.join("\n");
        evtColumn.lines = events.join("\n");
    }
    onAddressFilterChanged: refreshLogs()
    Connections {
        target: root.drive
        function onLogEntry(kind, address, message) {
            root.entries.push({kind: kind, address: address, text: "[" + Qt.formatTime(new Date(), "hh:mm:ss.zzz") + "] " + message});
            if (root.entries.length > root.maxEntries) root.entries.splice(0, root.entries.length - root.maxEntries);
            if (!refreshTimer.running) refreshTimer.start();
        }
    }
    Timer { id: refreshTimer; interval: 40; onTriggered: root.refreshLogs() }

    component LogColumn: ColumnLayout {
        id: logColumn
        property string title: ""
        property string lines: ""
        property color textColor: "#e0e0e0"
        spacing: 3
        Text { text: logColumn.title; color: logColumn.textColor; font.pixelSize: 12; font.bold: true }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            TextArea {
                text: logColumn.lines
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                font.family: "Consolas"; font.pixelSize: 11
                color: logColumn.textColor
                background: Rectangle { color: "#0d1117"; radius: 4 }
                onTextChanged: cursorPosition = text.length
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("驱动通信日志"); color: "#4fc3f7"; font.pixelSize: 16; font.bold: true }
            ComboBox {
                Layout.preferredWidth: 140; Layout.preferredHeight: 28
                model: [qsTr("全部地址"), qsTr("1 负载板"), qsTr("2 电机"), qsTr("3 压缩机"), qsTr("4 风机")]
                onActivated: root.addressFilter = currentIndex
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight; color: "#9e9e9e"; font.pixelSize: 11
                text: root.drive ? qsTr("TX %1  RX %2  CRC错 %3  长度错 %4  半帧超时 %5  忽略 %6  串口错 %7  队列满 %8")
                      .arg(root.drive.stats.tx).arg(root.drive.stats.rx).arg(root.drive.stats.crc)
                      .arg(root.drive.stats.length).arg(root.drive.stats.gap).arg(root.drive.stats.ignored)
                      .arg(root.drive.stats.serial).arg(root.drive.stats.queue) : ""
            }
            Button {
                Layout.preferredHeight: 26; text: qsTr("清空日志")
                onClicked: { root.entries = []; root.refreshLogs(); }
            }
            Button {
                Layout.preferredHeight: 26; text: qsTr("统计清零")
                onClicked: if (root.drive) root.drive.clearStats()
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
            LogColumn { id: txColumn; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; title: qsTr("TX 从机应答"); textColor: "#4fc3f7" }
            LogColumn { id: rxColumn; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; title: qsTr("RX 主控/总线"); textColor: "#66bb6a" }
            LogColumn { id: evtColumn; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; title: qsTr("事件 / 错误"); textColor: "#ffca28" }
        }
    }
}
