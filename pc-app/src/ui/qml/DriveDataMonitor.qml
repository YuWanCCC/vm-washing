import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var drive: null
    readonly property var simulator: drive ? drive.simulator : null
    readonly property int address: simulator ? simulator.selectedAddress : 1
    readonly property var boardState: simulator ? simulator.selectedBoard : ({})
    readonly property var requestFields: simulator ? simulator.fieldsFor(address, true) : []
    readonly property var responseFields: simulator ? simulator.fieldsFor(address, false) : []
    property bool expanded: true
    color: "#2b2b2b"

    function timeText(ms) { return ms === undefined || ms < 0 ? "--" : (ms / 1000).toFixed(3) + "s"; }
    function fieldText(meta, values, available) {
        if (!available || !values || values[meta.key] === undefined) return "--";
        var raw = values[meta.key];
        var result = raw + "  [0x" + raw.toString(16).toUpperCase().padStart(meta.width * 2, "0") + "]";
        if (meta.unit) result += "  " + (raw * meta.scale + meta.offset).toFixed(meta.scale < 1 ? 1 : 0) + " " + meta.unit;
        var onBits = [];
        for (var i = 0; i < meta.bits.length; ++i)
            if (meta.bits[i] && (raw & (1 << i))) onBits.push(meta.bits[i]);
        if (onBits.length) result += "  " + onBits.join(" / ");
        return result;
    }

    component FieldColumn: ColumnLayout {
        id: column
        property string title: ""
        property var fields: []
        property var values: ({})
        property bool available: true
        property var formatter
        spacing: 4
        Text { text: column.title; color: "#4fc3f7"; font.bold: true; font.pixelSize: 12 }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: parent.width
                spacing: 3
                Repeater {
                    model: column.fields
                    Rectangle {
                        id: fieldRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: fieldLabels.implicitHeight + 10
                        color: "#333333"; radius: 4
                        ColumnLayout {
                            id: fieldLabels
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 5; spacing: 2
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: "B" + fieldRow.modelData.byte + "  " + fieldRow.modelData.label
                                color: "#9e9e9e"; font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: column.formatter(fieldRow.modelData, column.values, column.available)
                                color: "#e0e0e0"; font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 5
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Text { text: qsTr("驱动数据监控"); color: "#4fc3f7"; font.pixelSize: 16; font.bold: true }
            ComboBox {
                objectName: "driveMonitorBoard"
                Layout.preferredWidth: 156; Layout.preferredHeight: 28
                model: [qsTr("1 显示负载板"), qsTr("2 变频电机"), qsTr("3 变频压缩机"), qsTr("4 变频风机")]
                currentIndex: root.address - 1
                onActivated: if (root.simulator) root.simulator.selectedAddress = currentIndex + 1
            }
            Rectangle {
                width: 8; height: 8; radius: 4
                color: root.boardState.commFault || root.boardState.timedOut ? "#ef5350"
                       : root.drive && root.drive.connected && root.boardState.seen && root.boardState.enabled ? "#66bb6a" : "#616161"
            }
            Text {
                text: !root.drive || !root.drive.connected ? qsTr("未连接")
                      : !root.boardState.enabled ? qsTr("已禁用") : root.boardState.commFault ? qsTr("通信故障")
                      : root.boardState.timedOut ? qsTr("超时停机") : root.boardState.seen ? qsTr("通信正常") : qsTr("尚未轮询")
                color: "#9e9e9e"; font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text { text: root.drive && root.drive.connected ? root.drive.portName + " 4800/8N1" : ""; color: "#9e9e9e"; font.pixelSize: 11 }
            Button { text: root.expanded ? qsTr("折叠") : qsTr("展开"); Layout.preferredHeight: 26; onClicked: root.expanded = !root.expanded }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9e9e9e"; font.pixelSize: 11
            text: qsTr("RX %1 / TX %2  最近请求 %3 · 应答 %4（应用单调时钟）  请求距今 %5")
                  .arg(root.boardState.rxCount || 0).arg(root.boardState.txCount || 0)
                  .arg(root.timeText(root.boardState.lastRxMs)).arg(root.timeText(root.boardState.lastTxMs)).arg(root.timeText(root.boardState.ageMs))
                  + (root.address === 2 ? qsTr("  电机轴转速 %1rpm").arg(Number(root.boardState.shaftSpeed || 0).toFixed(1)) : "")
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.expanded; spacing: 10
            FieldColumn {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                title: qsTr("主控请求 · 设定值")
                fields: root.requestFields; values: root.boardState.command || ({})
                available: root.boardState.seen === true; formatter: root.fieldText
            }
            FieldColumn {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                title: qsTr("仿真反馈 · 下次应答值（非实板测量）")
                fields: root.responseFields; values: root.boardState.feedback || ({})
                formatter: root.fieldText
            }
        }
    }
}
