import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HIL.Core 1.0
import HIL.Models 1.0
import HIL.Communication 1.0

/*!
    右侧控制面板：运行控制 / 自动程序 / 手动执行器 / 实时状态 / 故障注入 / 数据记录。

    手动模式下界面直接写 engine.actuators 与 engine.motorModel.motorPwmDuty，
    自动模式下这些控件转为只读显示（由状态机驱动）。
*/
Rectangle {
    id: root

    required property var engine
    //! 故障注入器（由 Main.qml 传入，可为 null）
    property var faultInjector: null
    //! 数据记录器（由 Main.qml 传入，可为 null）
    property var recorder: null
    //! MatSerialManager 实例（MAT 模式直连主控板）
    property var mat: null
    //! 工作模式: 0=HIL, 1=MAT
    property int workMode: 0

    readonly property color colorSurface: "#333333"
    readonly property color colorSurfaceAlt: "#3a3a3a"
    readonly property color colorBorder: "#454545"
    readonly property color colorFg: "#e0e0e0"
    readonly property color colorMuted: "#9e9e9e"
    readonly property color colorAccent: "#4fc3f7"

    readonly property bool manualMode: engine && engine.mode === SimulationEngine.Manual
    readonly property bool autoMode: engine && engine.mode === SimulationEngine.Automatic

    //! 手动模式下的目标转速(RPM)，经每帧比例调节逼近
    property real manualTargetRpm: 0

    color: "#2b2b2b"

    // 秒 → mm:ss
    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds));
        var mm = Math.floor(total / 60);
        var ss = total % 60;
        return (mm < 10 ? "0" : "") + mm + ":" + (ss < 10 ? "0" : "") + ss;
    }

    /*!
        洗衣周期整体进度估算：按状态在流程中的次序给出粗略百分比，
        用于自动模式的进度条（引擎未暴露总时长，故不做精确插值）。
    */
    function cycleProgress() {
        if (!engine)
            return 0;
        switch (engine.state) {
        case SimulationEngine.Idle: return 0;
        case SimulationEngine.Fill: return 0.12;
        case SimulationEngine.Heat: return 0.28;
        case SimulationEngine.Wash: return 0.48;
        case SimulationEngine.Drain: return 0.62;
        case SimulationEngine.Rinse: return 0.75;
        case SimulationEngine.Spin: return 0.9;
        case SimulationEngine.Complete: return 1;
        default: return 0;
        }
    }

    //! 手动模式转速闭环：按误差微调占空比（引擎只接受 PWM 占空比输入）
    function updateManualMotor() {
        if (!engine || !manualMode)
            return;
        var motor = engine.motorModel;
        if (manualTargetRpm <= 0.5) {
            motor.motorPwmDuty = 0;
            return;
        }
        var error = (manualTargetRpm - motor.speedRpm) / 1600;
        var duty = motor.motorPwmDuty + error * 0.35;
        motor.motorPwmDuty = Math.max(0, Math.min(1, duty));
    }

    // MAT 状态缓存（从 67 字节 rawSdu 解析）
    property var matSdu: null          //!< 原始 67 字节 SDU
    property int matProtocolVer: 0
    property int matMachineType: 0
    property int matMotorSpeed: 0      //!< 有符号转速
    property int matTimeToEnd: 0       //!< 剩余时间（分钟）
    property int matErrorCode: 0
    property int matLoad: 0
    property int matHumidity: 0
    property int matCtrlB1: 0
    property int matCtrlB2: 0

    Connections {
        target: root.engine
        function onTicked(elapsedTime) { root.updateManualMotor(); }
    }

    Connections {
        target: root.mat
        function onSduRevisionChanged() {
            var hex = root.mat.lastSduHex
            if (!hex || hex.length < 2) return  // 至少 1 字节
            // 从 hex 字符串解析指定字节: byte[n] = hex.substring(n*2, n*2+2)
            function b(n) { return (n * 2 + 2 <= hex.length) ? parseInt(hex.substring(n * 2, n * 2 + 2), 16) : 0 }
            root.matProtocolVer = b(0)
            root.matMachineType = b(1)
            root.matMotorSpeed  = (b(12) << 8) | b(11)
            if (root.matMotorSpeed > 32767) root.matMotorSpeed -= 65536
            root.matTimeToEnd = (b(25) << 8) | b(24)
            root.matErrorCode = b(47)
            root.matLoad      = b(10)
            root.matHumidity  = (b(6) << 8) | b(5)
        }
    }

    // ------------------------------------------------------------------
    // 复用组件
    // 内联组件不共享外层文档的 id 作用域，因此颜色等与 root 相同的
    // 取值在此处以字面量声明，可变部分通过属性由使用处传入。
    // ------------------------------------------------------------------

    //! 分组卡片：标题 + 内容列
    component GroupCard: Rectangle {
        id: card

        default property alias content: cardBody.data
        property string title: ""

        Layout.fillWidth: true
        implicitHeight: cardBody.implicitHeight + cardHeader.implicitHeight + 28
        color: "#333333"
        radius: 8
        border.width: 1
        border.color: "#454545"

        Text {
            id: cardHeader
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            text: card.title
            color: "#4fc3f7"
            font.pixelSize: 16
            font.bold: true
        }

        ColumnLayout {
            id: cardBody
            anchors.top: cardHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 8
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
        }
    }

    //! 带进度条的数值行
    component MeterRow: ColumnLayout {
        id: meter

        property string label: ""
        property real value: 0
        property string unit: ""
        property real maxValue: 100
        property int decimals: 1
        property color barColor: "#4fc3f7"

        Layout.fillWidth: true
        spacing: 3

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: meter.label
                color: "#9e9e9e"
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Text {
                text: meter.value.toFixed(meter.decimals) + " " + meter.unit
                color: "#e0e0e0"
                font.pixelSize: 14
                font.bold: true
            }
        }

        Rectangle {
            id: meterTrack

            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: "#3a3a3a"

            Rectangle {
                width: meterTrack.width
                       * Math.max(0, Math.min(1, meter.value / meter.maxValue))
                height: meterTrack.height
                radius: meterTrack.radius
                color: meter.barColor

                Behavior on width {
                    NumberAnimation { duration: 120 }
                }
            }
        }
    }

    //! 执行器开关行：手动模式可操作，自动模式只读回显
    component ActuatorSwitch: RowLayout {
        id: actuatorRow

        property string label: ""
        property bool active: false
        property color activeColor: "#66bb6a"
        //! 是否允许手动操作（由使用处绑定模式）
        property bool editable: false

        signal requestChange(bool on)

        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            radius: 5
            color: actuatorRow.active ? actuatorRow.activeColor : "#555555"
        }

        Text {
            text: actuatorRow.label
            color: "#e0e0e0"
            font.pixelSize: 14
        }

        Item { Layout.fillWidth: true }

        Switch {
            checked: actuatorRow.active
            enabled: actuatorRow.editable
            onToggled: actuatorRow.requestChange(checked)
        }
    }

    //! 可折叠分组卡片：点击标题栏展开/收起内容
    component CollapseCard: Rectangle {
        id: collapse

        default property alias content: collapseBody.data
        property string title: ""
        property bool expanded: false

        Layout.fillWidth: true
        implicitHeight: collapseHeader.implicitHeight + 20
                        + (collapse.expanded ? collapseBody.implicitHeight + 18 : 0)
        color: "#333333"
        radius: 8
        border.width: 1
        border.color: "#454545"

        RowLayout {
            id: collapseHeader

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            spacing: 6

            Text {
                text: collapse.expanded ? "\u25BE" : "\u25B8"
                color: "#4fc3f7"
                font.pixelSize: 14
            }

            Text {
                text: collapse.title
                color: "#4fc3f7"
                font.pixelSize: 16
                font.bold: true
            }

            Item { Layout.fillWidth: true }
        }

        MouseArea {
            anchors.fill: collapseHeader
            onClicked: collapse.expanded = !collapse.expanded
        }

        ColumnLayout {
            id: collapseBody

            anchors.top: collapseHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 8
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            visible: collapse.expanded
        }
    }

    //! MAT 执行器位开关：带指示灯的 Switch 行
    component BitSwitch: RowLayout {
        id: bs
        property string label: ""
        property bool checked: false
        signal toggled(bool on)
        Layout.fillWidth: true; spacing: 6

        Rectangle {
            width: 10; height: 10; radius: 5
            color: bs.checked ? "#66bb6a" : "#555555"
        }
        Text { text: bs.label; color: "#e0e0e0"; font.pixelSize: 13 }
        Item { Layout.fillWidth: true }
        Switch {
            checked: bs.checked
            onToggled: bs.toggled(checked)
        }
    }

    // ------------------------------------------------------------------
    // 面板主体
    // ------------------------------------------------------------------
    ScrollView {
        id: panelScroll

        anchors.fill: parent
        anchors.margins: 10
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: panelScroll.availableWidth
            spacing: 10

            // ============ 运行控制（HIL 模式）============
            GroupCard {
                title: qsTr("运行控制")
                visible: root.workMode === 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("手动模式")
                        font.pixelSize: 14
                        highlighted: root.manualMode
                        onClicked: root.engine.mode = SimulationEngine.Manual
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("联动模式")
                        font.pixelSize: 14
                        highlighted: root.autoMode
                        onClicked: root.engine.mode = SimulationEngine.Automatic
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: root.engine && root.engine.running ? qsTr("暂停") : qsTr("开始")
                        font.pixelSize: 14
                        highlighted: !(root.engine && root.engine.running)
                        onClicked: {
                            if (root.engine.running)
                                root.engine.stop();
                            else
                                root.engine.start();
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("单步")
                        font.pixelSize: 14
                        enabled: root.engine && !root.engine.running
                        onClicked: root.engine.stepOnce()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("重置")
                        font.pixelSize: 14
                        onClicked: {
                            // 拖动后滑块的 value 绑定已失效，需同时回归控件位置
                            root.manualTargetRpm = 0;
                            motorSlider.value = 0;
                            root.engine.reset();
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("倍速")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Slider {
                        id: scaleSlider
                        Layout.fillWidth: true
                        from: 0.1
                        to: 10
                        stepSize: 0.1
                        value: root.engine ? root.engine.timeScale : 1
                        onMoved: root.engine.timeScale = value
                    }

                    Text {
                        text: "×" + scaleSlider.value.toFixed(1)
                        color: root.colorAccent
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Button {
                        text: qsTr("1×")
                        font.pixelSize: 12
                        onClicked: {
                            root.engine.timeScale = 1;
                            scaleSlider.value = 1;
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.colorBorder
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("当前状态")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.engine ? root.engine.stateText : "-"
                        color: root.colorFg
                        font.pixelSize: 16
                        font.bold: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("累计 / 本阶段")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.formatTime(root.engine ? root.engine.elapsedTime : 0) + " / "
                              + root.formatTime(root.engine ? root.engine.stateElapsedTime : 0)
                        color: root.colorFg
                        font.pixelSize: 14
                    }
                }
            }

            // ============ 自动程序（HIL 联动模式）============
            GroupCard {
                title: qsTr("洗涤程序（联动模式）")
                visible: root.workMode === 0 && root.autoMode

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ComboBox {
                        id: programCombo
                        Layout.fillWidth: true
                        font.pixelSize: 14
                        model: root.engine && root.engine.scenarios
                               ? root.engine.scenarios.availableScenarios : []
                        displayText: currentIndex < 0
                                     ? qsTr("请选择洗涤程序")
                                     : root.engine.scenarios.displayName(currentText)

                        delegate: ItemDelegate {
                            required property string modelData
                            required property int index

                            width: programCombo.width
                            highlighted: programCombo.highlightedIndex === index
                            text: root.engine.scenarios.displayName(modelData)
                            font.pixelSize: 14
                        }
                    }

                    Button {
                        text: qsTr("刷新")
                        font.pixelSize: 12
                        onClicked: root.engine.scenarios.refresh()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("启动洗涤程序")
                        font.pixelSize: 14
                        highlighted: true
                        enabled: programCombo.currentIndex >= 0
                        onClicked: root.engine.startWashProgram(programCombo.currentText)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("中止程序")
                        font.pixelSize: 14
                        onClicked: root.engine.abortWashProgram()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("程序")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.engine && root.engine.programName !== ""
                              ? root.engine.programName : qsTr("未启动")
                        color: root.colorFg
                        font.pixelSize: 14
                    }
                }

                MeterRow {
                    label: qsTr("流程进度") + (root.engine && root.engine.rinseIndex > 0
                                              ? qsTr("（第 %1 次漂洗）").arg(root.engine.rinseIndex) : "")
                    value: root.cycleProgress() * 100
                    unit: "%"
                    maxValue: 100
                    decimals: 0
                    barColor: "#66bb6a"
                }

                // 各阶段指示链
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [
                            { label: qsTr("注水"), state: SimulationEngine.Fill },
                            { label: qsTr("加热"), state: SimulationEngine.Heat },
                            { label: qsTr("洗涤"), state: SimulationEngine.Wash },
                            { label: qsTr("排水"), state: SimulationEngine.Drain },
                            { label: qsTr("漂洗"), state: SimulationEngine.Rinse },
                            { label: qsTr("脱水"), state: SimulationEngine.Spin },
                            { label: qsTr("完成"), state: SimulationEngine.Complete }
                        ]

                        Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            radius: 4
                            color: root.engine && root.engine.state === modelData.state
                                   ? root.colorAccent : root.colorSurfaceAlt

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: 12
                                color: root.engine && root.engine.state === modelData.state
                                       ? "#10171d" : root.colorMuted
                            }
                        }
                    }
                }
            }

            // ============ 手动控制（HIL 模式）============
            GroupCard {
                title: qsTr("手动控制")
                visible: root.workMode === 0 && root.manualMode

                ActuatorSwitch {
                    label: qsTr("进水阀")
                    activeColor: "#42a5f5"
                    editable: root.manualMode
                    active: root.engine ? root.engine.actuators.inletValveOpen : false
                    onRequestChange: function(on) { root.engine.actuators.inletValveOpen = on; }
                }

                ActuatorSwitch {
                    label: qsTr("排水泵")
                    activeColor: "#26c6da"
                    editable: root.manualMode
                    active: root.engine ? root.engine.actuators.drainPumpOn : false
                    onRequestChange: function(on) { root.engine.actuators.drainPumpOn = on; }
                }

                ActuatorSwitch {
                    label: qsTr("加热器")
                    activeColor: "#ef5350"
                    editable: root.manualMode
                    active: root.engine ? root.engine.actuators.heaterOn : false
                    onRequestChange: function(on) { root.engine.actuators.heaterOn = on; }
                }

                // 门体开关控制（控制 DOOR_1/DOOR_2 信号输出）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("门体")
                        color: root.colorFg
                        font.pixelSize: 14
                    }

                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 5
                        color: root.engine && root.engine.doorOpenState ? "#66bb6a" : "#555555"
                    }

                    Text {
                        text: root.engine ? root.engine.doorStateText : "-"
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("开门")
                        font.pixelSize: 12
                        highlighted: root.engine && root.engine.doorOpenState
                        enabled: root.engine && !root.engine.doorOpenState
                        onClicked: root.engine.requestDoorOpen()
                    }

                    Button {
                        text: qsTr("关门")
                        font.pixelSize: 12
                        highlighted: root.engine && !root.engine.doorOpenState
                        enabled: root.engine && root.engine.doorOpenState
                        onClicked: root.engine.requestDoorClose()
                    }
                }

                MeterRow {
                    label: qsTr("电机目标转速")
                    value: root.manualTargetRpm
                    unit: "r/min"
                    maxValue: 1600
                    decimals: 0
                    barColor: "#ffca28"
                }

                Slider {
                    id: motorSlider

                    Layout.fillWidth: true
                    from: 0
                    to: 1600
                    stepSize: 10
                    value: root.manualTargetRpm
                    onMoved: {
                        root.manualTargetRpm = value;
                        root.updateManualMotor();
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("PWM 占空比")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.engine
                              ? (root.engine.motorModel.motorPwmDuty * 100).toFixed(0) + " %" : "-"
                        color: root.colorFg
                        font.pixelSize: 14
                    }
                }
            }

            // ============ 状态显示 ============
            GroupCard {
                title: qsTr("实时状态")

                MeterRow {
                    label: qsTr("水位")
                    value: root.engine ? root.engine.waterModel.waterLevel : 0
                    unit: "mm"
                    maxValue: 500
                    barColor: "#42a5f5"
                }

                MeterRow {
                    label: qsTr("水温")
                    value: root.engine ? root.engine.tempModel.temperature : 0
                    unit: "°C"
                    maxValue: 95
                    barColor: "#ef5350"
                }

                MeterRow {
                    label: qsTr("滚筒转速")
                    value: root.engine ? Math.abs(root.engine.motorModel.speedRpm) : 0
                    unit: "r/min"
                    maxValue: 1600
                    decimals: 0
                    barColor: "#ffca28"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.colorBorder
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        radius: 7
                        color: root.engine && root.engine.doorOpenState ? "#66bb6a" : "#555555"
                    }

                    Text {
                        text: qsTr("门体")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.engine ? root.engine.doorStateText : "-"
                        color: root.colorFg
                        font.pixelSize: 14
                        font.bold: true
                    }
                }

                // 传感器输出（下位机需要复现的物理量，HIL 模式可见）
                GridLayout {
                    visible: root.workMode === 0
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 4
                    columnSpacing: 8

                    Text {
                        text: qsTr("水位传感器")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.engine
                              ? (root.engine.waterModel.sensorFrequency / 1000).toFixed(2) + " kHz  ("
                                + root.engine.waterModel.pressure.toFixed(2) + " kPa)" : "-"
                        color: root.colorFg
                        font.pixelSize: 12
                    }

                    Text {
                        text: qsTr("NTC 阻值")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.engine
                              ? (root.engine.tempModel.ntcResistance / 1000).toFixed(2) + " kΩ" : "-"
                        color: root.colorFg
                        font.pixelSize: 12
                    }

                    Text {
                        text: qsTr("霍尔频率")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.engine
                              ? root.engine.motorModel.hallFrequency.toFixed(1) + " Hz" : "-"
                        color: root.colorFg
                        font.pixelSize: 12
                    }

                    Text {
                        text: qsTr("桶内水量")
                        color: root.colorMuted
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.engine
                              ? root.engine.waterModel.volume.toFixed(1) + " L  ("
                                + root.engine.waterModel.fillPercent.toFixed(0) + "%)" : "-"
                        color: root.colorFg
                        font.pixelSize: 12
                    }
                }
            }

            // ============ MAT 操作（MAT 模式）============
            GroupCard {
                title: qsTr("MAT 操作")
                visible: root.workMode === 1

                RowLayout {
                    Layout.fillWidth: true; spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("查询状态")
                        font.pixelSize: 13
                        highlighted: true
                        enabled: root.mat && root.mat.connected
                        onClicked: root.mat.sendQuery()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("发送控制")
                        font.pixelSize: 13
                        enabled: root.mat && root.mat.connected
                        onClicked: root.mat.sendControl(root.matCtrlB1, root.matCtrlB2)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 8

                    Text { text: qsTr("自动轮询"); color: root.colorMuted; font.pixelSize: 13 }
                    Switch {
                        checked: root.mat ? root.mat.pollEnabled : false
                        onToggled: root.mat && (root.mat.pollEnabled = checked)
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("间隔(ms)"); color: root.colorMuted; font.pixelSize: 12 }
                    SpinBox {
                        from: 200; to: 5000; stepSize: 100
                        value: root.mat ? root.mat.pollIntervalMs : 500
                        onValueModified: root.mat && (root.mat.pollIntervalMs = value)
                        font.pixelSize: 12
                    }
                }
            }

            // ============ MAT 执行器控制 BYTE1（MAT 模式）============
            GroupCard {
                title: qsTr("执行器控制 BYTE1")
                visible: root.workMode === 1

                BitSwitch {
                    label: qsTr("预洗进水阀")
                    checked: root.matCtrlB1 & 0x80
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x80 : root.matCtrlB1 & ~0x80 }
                }
                BitSwitch {
                    label: qsTr("主洗进水阀")
                    checked: root.matCtrlB1 & 0x40
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x40 : root.matCtrlB1 & ~0x40 }
                }
                BitSwitch {
                    label: qsTr("冲洗进水阀")
                    checked: root.matCtrlB1 & 0x20
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x20 : root.matCtrlB1 & ~0x20 }
                }
                BitSwitch {
                    label: qsTr("冷凝进水阀")
                    checked: root.matCtrlB1 & 0x10
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x10 : root.matCtrlB1 & ~0x10 }
                }
                BitSwitch {
                    label: qsTr("排水泵")
                    checked: root.matCtrlB1 & 0x08
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x08 : root.matCtrlB1 & ~0x08 }
                }
                BitSwitch {
                    label: qsTr("水加热器")
                    checked: root.matCtrlB1 & 0x04
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x04 : root.matCtrlB1 & ~0x04 }
                }
                BitSwitch {
                    label: qsTr("风干加热")
                    checked: root.matCtrlB1 & 0x02
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x02 : root.matCtrlB1 & ~0x02 }
                }
                BitSwitch {
                    label: qsTr("风机")
                    checked: root.matCtrlB1 & 0x01
                    onToggled: function(on) { root.matCtrlB1 = on ? root.matCtrlB1 | 0x01 : root.matCtrlB1 & ~0x01 }
                }
            }

            // ============ MAT 执行器控制 BYTE2（MAT 模式）============
            GroupCard {
                title: qsTr("执行器控制 BYTE2")
                visible: root.workMode === 1

                BitSwitch {
                    label: qsTr("门锁")
                    checked: root.matCtrlB2 & 0x80
                    onToggled: function(on) { root.matCtrlB2 = on ? root.matCtrlB2 | 0x80 : root.matCtrlB2 & ~0x80 }
                }
                BitSwitch {
                    label: qsTr("洗涤剂投入")
                    checked: root.matCtrlB2 & 0x40
                    onToggled: function(on) { root.matCtrlB2 = on ? root.matCtrlB2 | 0x40 : root.matCtrlB2 & ~0x40 }
                }
                BitSwitch {
                    label: qsTr("柔顺剂投入")
                    checked: root.matCtrlB2 & 0x20
                    onToggled: function(on) { root.matCtrlB2 = on ? root.matCtrlB2 | 0x20 : root.matCtrlB2 & ~0x20 }
                }
                BitSwitch {
                    label: qsTr("UV杀菌灯")
                    checked: root.matCtrlB2 & 0x10
                    onToggled: function(on) { root.matCtrlB2 = on ? root.matCtrlB2 | 0x10 : root.matCtrlB2 & ~0x10 }
                }
            }

            // ============ MAT 主控状态（MAT 模式）============
            GroupCard {
                title: qsTr("主控状态")
                visible: root.workMode === 1

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2; rowSpacing: 6; columnSpacing: 8

                    Text { text: qsTr("协议版本"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.matProtocolVer
                        color: root.colorFg; font.pixelSize: 14; font.bold: true
                    }

                    Text { text: qsTr("机器类型"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: { switch (root.matMachineType) {
                            case 1: return qsTr("滚筒");
                            case 2: return qsTr("波轮");
                            case 3: return qsTr("干衣机");
                            default: return root.matMachineType; } }
                        color: root.colorFg; font.pixelSize: 14
                    }

                    Text { text: qsTr("电机转速"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.matMotorSpeed + " r/min"
                        color: root.colorFg; font.pixelSize: 14
                    }

                    Text { text: qsTr("剩余时间"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.matTimeToEnd + " min"
                        color: root.colorFg; font.pixelSize: 14
                    }

                    Text { text: qsTr("整机故障码"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.matErrorCode === 0 ? qsTr("正常") : ("E" + root.matErrorCode)
                        color: root.matErrorCode === 0 ? "#66bb6a" : "#ef5350"
                        font.pixelSize: 14; font.bold: true
                    }

                    Text { text: qsTr("负载量"); color: root.colorMuted; font.pixelSize: 13 }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.matLoad + ""
                        color: root.colorFg; font.pixelSize: 14
                    }
                }
            }

            // ============ 故障注入（可折叠）============
            CollapseCard {
                title: qsTr("故障注入")

                FaultPanel {
                    Layout.fillWidth: true
                    faultInjector: root.faultInjector
                }
            }

            // ============ 数据记录（可折叠）============
            CollapseCard {
                title: qsTr("数据记录")

                RecordPanel {
                    Layout.fillWidth: true
                    recorder: root.recorder
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 4
            }
        }
    }
}
