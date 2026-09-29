import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var drive: null
    readonly property var simulator: drive ? drive.simulator : null
    readonly property int address: simulator ? simulator.selectedAddress : 1
    readonly property var boardState: simulator ? simulator.selectedBoard : ({})
    readonly property var schema: simulator ? simulator.fieldsFor(address, false).filter(function(f) { return f.editable; }) : []
    spacing: 10

    function setting(key) { return boardState.settings && boardState.settings[key] !== undefined ? boardState.settings[key] : 0; }
    function setRaw(key, value) {
        if (simulator && !simulator.setFeedback(address, key, value))
            validation.text = qsTr("数值超出字段范围或包含预留位，未应用");
        else validation.text = "";
    }

    Text { text: qsTr("驱动板仿真配置"); color: "#4fc3f7"; font.pixelSize: 18; font.bold: true }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: "#9e9e9e"; font.pixelSize: 12
        text: qsTr("PC 按地址模拟从机。只在主控请求后应答；配置不主动发送。与 HIL 物理模型、3D 及 MAT 独立。")
    }
    GridLayout {
        Layout.fillWidth: true; columns: 2; columnSpacing: 4
        Repeater {
            model: [qsTr("1 显示负载板"), qsTr("2 变频电机"), qsTr("3 变频压缩机"), qsTr("4 变频风机")]
            CheckBox {
                required property int index
                required property string modelData
                text: modelData; font.pixelSize: 12
                checked: root.simulator ? root.simulator.boards[index].enabled : false
                onToggled: if (root.simulator) root.simulator.setEnabled(index + 1, checked)
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            objectName: "driveControlBoard"
            Layout.fillWidth: true
            model: [qsTr("显示负载板"), qsTr("变频电机"), qsTr("变频压缩机"), qsTr("变频风机")]
            currentIndex: root.address - 1
            onActivated: if (root.simulator) root.simulator.selectedAddress = currentIndex + 1
        }
        Button {
            text: qsTr("重置本板")
            onClicked: if (root.simulator) root.simulator.resetBoard(root.address)
        }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffca28"; font.pixelSize: 12
        text: root.boardState.enabled === false ? qsTr("本板已禁用，不应答；以下设置仍可预配置。")
              : qsTr("反馈输入为协议原始整数。明确量纲同时显示换算值；NTC、电机电流不推测换算。故障注入代表持续存在的外部故障。")
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9e9e9e"; font.pixelSize: 11
        text: qsTr("超时：10s停机/60s通信故障；测试模式5s通信故障为待实板验证的仿真假设。无自动温度曲线、硬件PWM或休眠GPIO。")
    }
    Button {
        visible: root.address === 1
        text: root.boardState.manualLock ? qsTr("门锁已手动覆盖：恢复跟随主控") : qsTr("门锁自动跟随主控")
        enabled: root.boardState.manualLock === true
        onClicked: root.simulator.followDoorLock()
    }
    Text { id: validation; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ef5350"; visible: text.length > 0 }

    Repeater {
        model: root.schema
        Rectangle {
            id: fieldCard
            required property var modelData
            readonly property var meta: modelData
            readonly property int raw: root.setting(meta.key)
            Layout.fillWidth: true
            implicitHeight: fieldContent.implicitHeight + 16
            color: "#333333"; radius: 6; border.color: "#454545"
            ColumnLayout {
                id: fieldContent
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 8; spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: "B" + fieldCard.meta.byte + "  " + fieldCard.meta.label
                        color: "#e0e0e0"; font.pixelSize: 12
                    }
                    SpinBox {
                        visible: fieldCard.meta.bits.length === 0
                        Layout.preferredWidth: 132
                        editable: true; from: 0
                        to: fieldCard.meta.key === "door" ? 2 : fieldCard.meta.max
                        value: fieldCard.raw
                        onValueModified: root.setRaw(fieldCard.meta.key, value)
                    }
                    Text {
                        visible: fieldCard.meta.bits.length > 0
                        text: "0x" + fieldCard.raw.toString(16).toUpperCase().padStart(2, "0")
                        color: "#ef5350"; font.family: "Consolas"; font.pixelSize: 14
                    }
                }
                Text {
                    visible: fieldCard.meta.unit.length > 0
                    text: qsTr("换算值：") + (fieldCard.raw * fieldCard.meta.scale + fieldCard.meta.offset).toFixed(fieldCard.meta.scale < 1 ? 1 : 0) + " " + fieldCard.meta.unit
                    color: "#9e9e9e"; font.pixelSize: 11
                }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; visible: fieldCard.meta.bits.length > 0
                    Repeater {
                        model: fieldCard.meta.bits
                        CheckBox {
                            required property int index
                            required property string modelData
                            visible: modelData.length > 0
                            text: "b" + index + " " + modelData
                            font.pixelSize: 11
                            checked: !!(fieldCard.raw & (1 << index))
                            onToggled: root.setRaw(fieldCard.meta.key, checked ? fieldCard.raw | (1 << index) : fieldCard.raw & ~(1 << index))
                        }
                    }
                }
            }
        }
    }
}
