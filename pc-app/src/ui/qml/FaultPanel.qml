import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Fault 1.0

/*!
    故障注入面板：4 个传感器通道各一行，选择故障类型与持续时间后注入/清除，
    底部列出当前所有活跃故障。通过 faultInjector 属性注入 FaultInjector 实例。
*/
Item {
    id: root

    required property var faultInjector

    implicitHeight: body.implicitHeight
    implicitWidth: body.implicitWidth

    //! faultsChanged 仅用于触发依赖它的绑定重新求值（方法调用本身无通知能力）
    property int refreshTick: 0

    Connections {
        target: root.faultInjector
        function onFaultsChanged() { root.refreshTick++; }
    }

    //! 通道名称，顺序与 FaultInjector.Channel 枚举值一致
    readonly property var channelNames: [
        qsTr("水位传感器"), qsTr("NTC温度传感器"),
        qsTr("Hall转速传感器"), qsTr("门锁信号")
    ]
    //! 故障类型名称，顺序与 FaultInjector.FaultType 枚举值一致
    readonly property var faultNames: [
        qsTr("无故障"), qsTr("断线"), qsTr("超范围"),
        qsTr("抖动"), qsTr("卡死"), qsTr("短路")
    ]

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        Repeater {
            model: 4

            RowLayout {
                required property int index

                Layout.fillWidth: true
                spacing: 6

                // 通道名
                Text {
                    Layout.preferredWidth: 104
                    text: root.channelNames[index]
                    color: root.faultInjector && root.faultInjector.isFaultActive(index)
                           ? "#ffa726" : "#e0e0e0"
                    font.pixelSize: 13
                }

                // 故障类型选择
                ComboBox {
                    id: typeCombo

                    Layout.fillWidth: true
                    font.pixelSize: 12
                    model: root.faultNames
                    currentIndex: 0
                }

                // 持续时间(s)，0 = 持续直到手动清除
                TextField {
                    id: durationInput

                    Layout.preferredWidth: 64
                    text: "0"
                    placeholderText: qsTr("秒(0=永久)")
                    selectByMouse: true
                    font.pixelSize: 12
                    validator: DoubleValidator { bottom: 0; decimals: 1 }
                }

                // 注入按钮（选"无故障"时禁用）
                Button {
                    text: qsTr("注入")
                    font.pixelSize: 12
                    enabled: typeCombo.currentIndex > 0
                    onClicked: {
                        var duration = parseFloat(durationInput.text);
                        if (isNaN(duration) || duration < 0)
                            duration = 0;
                        root.faultInjector.injectFault(index, typeCombo.currentIndex, duration, 1.0);
                    }
                }

                // 清除按钮（仅该通道有激活故障时可用）
                Button {
                    text: qsTr("清除")
                    font.pixelSize: 12
                    enabled: root.refreshTick >= 0 && root.faultInjector
                             && root.faultInjector.isFaultActive(index)
                    onClicked: root.faultInjector.clearFault(index)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.refreshTick >= 0 && root.faultInjector && root.faultInjector.anyFaultActive
                      ? qsTr("活跃故障：") : qsTr("当前无活跃故障")
                color: root.faultInjector && root.faultInjector.anyFaultActive
                       ? "#ffa726" : "#9e9e9e"
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("清除全部")
                font.pixelSize: 12
                enabled: root.refreshTick >= 0 && root.faultInjector
                         && root.faultInjector.anyFaultActive
                onClicked: root.faultInjector.clearAllFaults()
            }
        }

        // 活跃故障清单
        Repeater {
            model: 4

            Text {
                required property int index

                Layout.fillWidth: true
                visible: root.refreshTick >= 0 && root.faultInjector
                         && root.faultInjector.isFaultActive(index)
                text: visible ? ("● " + root.faultInjector.faultDescription(index)) : ""
                color: "#ffa726"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }
}
