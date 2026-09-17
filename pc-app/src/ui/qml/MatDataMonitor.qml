import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Communication 1.0

/*!
    MAT 数据监控面板：完全参照 HIL DataMonitor 布局风格。
    单层 ColumnLayout，无嵌套组件，避免重叠。
*/
Rectangle {
    id: root

    property var mat: null
    property bool expanded: true

    readonly property color colorSurface:    "#333333"
    readonly property color colorSurfaceAlt: "#3a3a3a"
    readonly property color colorBorder:     "#454545"
    readonly property color colorFg:         "#e0e0e0"
    readonly property color colorMuted:      "#9e9e9e"
    readonly property color colorAccent:     "#4fc3f7"
    readonly property color colorOk:         "#66bb6a"
    readonly property color colorErr:        "#ef5350"
    readonly property color colorSpeed:      "#ffca28"
    readonly property color colorTemp:       "#ef5350"
    readonly property color colorLevel:      "#42a5f5"
    readonly property color colorPower:      "#26c6da"
    readonly property color colorSwitch:     "#ab47bc"

    // 67 字节 SDU（JS 数组）
    property var sdu: []
    property int frameCount: 0
    property string lastRxTime: "--:--:--"

    Connections {
        target: root.mat
        function onSduRevisionChanged() {
            var hex = root.mat.lastSduHex
            if (!hex || hex.length < 2) return
            var arr = []
            for (var i = 0; i < hex.length; i += 2)
                arr.push(parseInt(hex.substring(i, i + 2), 16))
            root.sdu = arr
            root.frameCount++
            root.lastRxTime = Qt.formatTime(new Date(), "hh:mm:ss")
        }
    }

    // ---- SDU 解析（自适应长度）----
    readonly property int sduLen:      (sdu && sdu.length > 0) ? sdu.length : 0
    readonly property int protocolVer: sduLen >= 1  ? sdu[0] : 0
    readonly property int machineType: sduLen >= 2  ? sdu[1] : 0
    readonly property int byte3:       sduLen >= 3  ? sdu[2] : 0
    readonly property int byte4:       sduLen >= 4  ? sdu[3] : 0
    readonly property int byte5:       sduLen >= 5  ? sdu[4] : 0
    readonly property int humidity:     sduLen >= 7  ? ((sdu[6]  << 8) | sdu[5])  : 0
    readonly property int humidityRaw:  sduLen >= 9  ? ((sdu[8]  << 8) | sdu[7])  : 0
    readonly property int dryStep:      sduLen >= 10 ? sdu[9]  : 0
    readonly property int load:         sduLen >= 11 ? sdu[10] : 0
    readonly property int motorSpeed:   { if (sduLen < 13) return 0; var v = (sdu[12] << 8) | sdu[11]; return v > 32767 ? v - 65536 : v; }
    readonly property int ntc1Int:      sduLen >= 16 ? sdu[15] : 0
    readonly property int ntc1Dec:      sduLen >= 17 ? sdu[16] : 0
    readonly property int ntc1Raw:      sduLen >= 15 ? ((sdu[14] << 8) | sdu[13]) : 0
    readonly property int ntc2Int:      sduLen >= 20 ? sdu[19] : 0
    readonly property int ntc2Dec:      sduLen >= 21 ? sdu[20] : 0
    readonly property int ntc2Raw:      sduLen >= 19 ? ((sdu[18] << 8) | sdu[17]) : 0
    readonly property int programId:    sduLen >= 22 ? sdu[21] : 0
    readonly property int programTime:  sduLen >= 24 ? ((sdu[23] << 8) | sdu[22]) : 0
    readonly property int timeToEnd:    sduLen >= 26 ? ((sdu[25] << 8) | sdu[24]) : 0
    readonly property int fanSpeed:     sduLen >= 28 ? ((sdu[27] << 8) | sdu[26]) : 0
    readonly property int compSpeed:    sduLen >= 30 ? ((sdu[29] << 8) | sdu[28]) : 0
    readonly property int compPower:    sduLen >= 32 ? ((sdu[31] << 8) | sdu[30]) : 0
    readonly property int errorCode:    sduLen >= 48 ? sdu[47] : 0
    readonly property int motorErrCode: sduLen >= 49 ? sdu[48] : 0
    readonly property int fanErrCode:   sduLen >= 50 ? sdu[49] : 0
    readonly property int compErrCode:  sduLen >= 51 ? sdu[50] : 0
    readonly property int fanPower:     sduLen >= 63 ? ((sdu[62] << 8) | sdu[61]) : 0
    readonly property int electricPower:sduLen >= 65 ? ((sdu[64] << 8) | sdu[63]) : 0
    readonly property int humidityRatio:sduLen >= 66 ? sdu[65] : 0
    readonly property string machineTypeText: {
        switch (machineType) { case 1: return qsTr("滚筒"); case 2: return qsTr("波轮"); case 3: return qsTr("干衣机"); default: return machineType; }
    }

    function formatTime(totalSeconds) {
        var h = Math.floor(totalSeconds / 3600);
        var m = Math.floor((totalSeconds % 3600) / 60);
        var s = totalSeconds % 60;
        var pad = function(n) { return n < 10 ? "0" + n : "" + n; };
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s);
    }

    color: colorSurface
    radius: 8
    border.width: 1
    border.color: colorBorder

    // ------------------------------------------------------------------
    // 复用组件（与 DataMonitor 完全一致的风格）
    // ------------------------------------------------------------------

    //! 数值单元格 — 同 DataMonitor.ValueCell
    component ValueCell: Rectangle {
        id: cell
        property string label: ""
        property string value: "-"
        property color seriesColor: root.colorAccent

        Layout.fillWidth: true
        Layout.preferredHeight: 34
        radius: 6
        color: root.colorSurfaceAlt

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 6
            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: cell.seriesColor }
            Text { text: cell.label; color: root.colorMuted; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Text { text: cell.value; color: root.colorFg; font.pixelSize: 14; font.bold: true }
        }
    }

    //! 开关指示灯
    component SwitchDot: RowLayout {
        property string label: ""
        property bool on: false
        spacing: 4
        Rectangle { width: 8; height: 8; radius: 4; color: on ? root.colorOk : "#555555" }
        Text { text: label; color: root.colorFg; font.pixelSize: 11 }
    }

    // ------------------------------------------------------------------
    // 面板主体 — 单层 ColumnLayout，同 DataMonitor
    // ------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ========== 标题栏 ==========
        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Text {
                text: qsTr("MAT 数据监控")
                color: root.colorAccent
                font.pixelSize: 16
                font.bold: true
            }

            Rectangle { width: 8; height: 8; radius: 4; color: (root.mat && root.mat.connected) ? root.colorOk : "#555555" }
            Text {
                text: (root.mat && root.mat.connected) ? (root.mat.portName + " " + root.mat.baudRate + "bps") : qsTr("未连接")
                color: root.colorMuted; font.pixelSize: 12
            }

            Text {
                text: qsTr("SDU %1字节  %2帧  %3")
                      .arg(root.sduLen).arg(root.frameCount).arg(root.lastRxTime)
                color: root.colorMuted; font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Button {
                Layout.preferredHeight: 26
                text: qsTr("清零")
                font.pixelSize: 12
                onClicked: { root.frameCount = 0; root.lastRxTime = "--:--:--" }
            }

            Button {
                Layout.preferredHeight: 26
                text: root.expanded ? qsTr("折叠 ▾") : qsTr("展开 ▴")
                font.pixelSize: 12
                onClicked: root.expanded = !root.expanded
            }
        }

        // ========== 滚动数据区 ==========
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: root.expanded
            contentHeight: dataCol.implicitHeight + 16
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: dataCol
                width: parent.width
                spacing: 12

                // ---------- 基本信息 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorAccent }
                    Text { text: qsTr("基本信息"); color: root.colorAccent; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ValueCell { label: qsTr("协议版本"); value: root.protocolVer + ""; seriesColor: root.colorAccent }
                    ValueCell { label: qsTr("机器类型"); value: root.machineTypeText; seriesColor: root.colorAccent }
                    ValueCell { label: qsTr("程序 ID");  value: root.programId + ""; seriesColor: root.colorAccent }
                    ValueCell { label: qsTr("负载量");   value: root.load + ""; seriesColor: root.colorLevel }
                    ValueCell { label: qsTr("烘干进程"); value: root.dryStep + ""; seriesColor: root.colorMuted }
                    ValueCell { label: qsTr("程序时间"); value: root.formatTime(root.programTime); seriesColor: root.colorMuted }
                    ValueCell { label: qsTr("剩余时间"); value: root.formatTime(root.timeToEnd); seriesColor: root.colorAccent }
                }

                // ---------- 开关状态 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorSwitch }
                    Text { text: qsTr("开关状态"); color: root.colorSwitch; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    // BYTE3 (8项)
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 56
                        radius: 6; color: colorSurfaceAlt
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8; spacing: 4
                            Text { text: "BYTE 3  (0x" + (root.byte3 < 16 ? "0" : "") + root.byte3.toString(16).toUpperCase() + ")"; color: root.colorSwitch; font.pixelSize: 11; font.bold: true }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Repeater { model: [qsTr("防皱"), qsTr("门"), qsTr("筒灯"), qsTr("蓝光灯"), qsTr("UV灯"), qsTr("风扇"), qsTr("浮子"), qsTr("蒸汽缺水")]
                                    SwitchDot { label: modelData; on: root.byte3 & (0x80 >> index) }
                                }
                            }
                        }
                    }
                    // BYTE4 (8项，名称较长)
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 56
                        radius: 6; color: colorSurfaceAlt
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8; spacing: 4
                            Text { text: "BYTE 4  (0x" + (root.byte4 < 16 ? "0" : "") + root.byte4.toString(16).toUpperCase() + ")"; color: root.colorSwitch; font.pixelSize: 11; font.bold: true }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Repeater { model: [qsTr("压缩机"), qsTr("排水泵"), qsTr("电三通"), qsTr("蒸汽发生"), qsTr("负离子"), qsTr("左转"), qsTr("右转"), qsTr("电机停")]
                                    SwitchDot { label: modelData; on: root.byte4 & (0x80 >> index) }
                                }
                            }
                        }
                    }
                    // BYTE5 (3项)
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 56
                        radius: 6; color: colorSurfaceAlt
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8; spacing: 4
                            Text { text: "BYTE 5  (0x" + (root.byte5 < 16 ? "0" : "") + root.byte5.toString(16).toUpperCase() + ")"; color: root.colorSwitch; font.pixelSize: 11; font.bold: true }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Repeater { model: [qsTr("压缩机"), qsTr("加热管"), qsTr("变频风机")]
                                    SwitchDot { label: modelData; on: root.byte5 & (0x80 >> index) }
                                }
                            }
                        }
                    }
                }

                // ---------- 传感器 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorTemp }
                    Text { text: qsTr("传感器"); color: root.colorTemp; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ValueCell { label: qsTr("湿度");     value: root.humidity + ""; seriesColor: root.colorLevel }
                    ValueCell { label: qsTr("原始湿度"); value: root.humidityRaw + ""; seriesColor: root.colorLevel }
                    ValueCell { label: qsTr("湿度占比"); value: root.humidityRatio + ""; seriesColor: root.colorLevel }
                    ValueCell { label: qsTr("NTC1");     value: root.ntc1Int + "." + root.ntc1Dec + " °C"; seriesColor: root.colorTemp }
                    ValueCell { label: qsTr("NTC1原始"); value: root.ntc1Raw + ""; seriesColor: root.colorTemp }
                    ValueCell { label: qsTr("NTC2");     value: root.ntc2Int + "." + root.ntc2Dec + " °C"; seriesColor: root.colorTemp }
                    ValueCell { label: qsTr("NTC2原始"); value: root.ntc2Raw + ""; seriesColor: root.colorTemp }
                }

                // ---------- 故障码 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorErr }
                    Text { text: qsTr("故障码"); color: root.colorErr; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ValueCell {
                        label: qsTr("整机"); value: root.errorCode === 0 ? qsTr("正常") : ("E" + root.errorCode)
                        seriesColor: root.errorCode === 0 ? root.colorOk : root.colorErr
                    }
                    ValueCell {
                        label: qsTr("电机"); value: root.motorErrCode === 0 ? qsTr("正常") : ("E" + root.motorErrCode)
                        seriesColor: root.motorErrCode === 0 ? root.colorOk : root.colorErr
                    }
                    ValueCell {
                        label: qsTr("风机"); value: root.fanErrCode === 0 ? qsTr("正常") : ("E" + root.fanErrCode)
                        seriesColor: root.fanErrCode === 0 ? root.colorOk : root.colorErr
                    }
                    ValueCell {
                        label: qsTr("压缩机"); value: root.compErrCode === 0 ? qsTr("正常") : ("E" + root.compErrCode)
                        seriesColor: root.compErrCode === 0 ? root.colorOk : root.colorErr
                    }
                }

                // ---------- 变频 / 功率 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorPower }
                    Text { text: qsTr("变频 / 功率"); color: root.colorPower; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ValueCell { label: qsTr("电机转速");   value: root.motorSpeed + " r/min"; seriesColor: root.colorSpeed }
                    ValueCell { label: qsTr("风机转速");   value: root.fanSpeed + " rpm"; seriesColor: root.colorPower }
                    ValueCell { label: qsTr("压缩机转速"); value: root.compSpeed + " rpm"; seriesColor: root.colorPower }
                    ValueCell { label: qsTr("压缩机功率"); value: root.compPower + ""; seriesColor: root.colorPower }
                    ValueCell { label: qsTr("风机功率");   value: root.fanPower + ""; seriesColor: root.colorPower }
                    ValueCell { label: qsTr("电机功率");   value: root.electricPower + ""; seriesColor: root.colorPower }
                }

                // ---------- 版本信息 ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 3; height: 14; radius: 1.5; color: root.colorMuted }
                    Text { text: qsTr("版本信息"); color: root.colorMuted; font.pixelSize: 13; font.bold: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.colorBorder }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ValueCell { label: "A"; value: root.sduLen >= 52 ? root.sdu[51] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "B"; value: root.sduLen >= 53 ? root.sdu[52] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "C"; value: root.sduLen >= 54 ? root.sdu[53] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "E"; value: root.sduLen >= 55 ? root.sdu[54] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "F"; value: root.sduLen >= 56 ? root.sdu[55] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "P"; value: root.sduLen >= 57 ? root.sdu[56] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "T"; value: root.sduLen >= 58 ? root.sdu[57] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "D"; value: root.sduLen >= 59 ? root.sdu[58] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "G"; value: root.sduLen >= 60 ? root.sdu[59] + "" : "-"; seriesColor: root.colorMuted }
                    ValueCell { label: "O"; value: root.sduLen >= 61 ? root.sdu[60] + "" : "-"; seriesColor: root.colorMuted }
                }

                // 未连接提示
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !root.mat || !root.mat.connected
                    text: qsTr("请先连接 MAT 串口"); color: root.colorMuted; font.pixelSize: 13
                }

                Item { Layout.preferredHeight: 8 }
            }
        }

        // ========== 底部关键数值（同 DataMonitor 底部 ValueCell 行）==========
        RowLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: 8

            ValueCell {
                label: qsTr("电机转速")
                seriesColor: root.colorSpeed
                value: root.motorSpeed + " r/min"
            }
            ValueCell {
                label: qsTr("NTC1")
                seriesColor: root.colorTemp
                value: root.ntc1Int + "." + root.ntc1Dec + " °C"
            }
            ValueCell {
                label: qsTr("湿度")
                seriesColor: root.colorLevel
                value: root.humidity + ""
            }
            ValueCell {
                label: qsTr("剩余时间")
                seriesColor: root.colorAccent
                value: root.formatTime(root.timeToEnd)
            }
            ValueCell {
                label: qsTr("故障")
                seriesColor: root.errorCode === 0 ? root.colorOk : root.colorErr
                value: root.errorCode === 0 ? qsTr("正常") : ("E" + root.errorCode)
            }
        }
    }
}
