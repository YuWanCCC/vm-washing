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
    //! 曲线记录器（MatCurveRecorder，由 Main 注入），负责采样点落盘与历史读取
    property var recorder: null
    property bool expanded: true
    // 视图模式：0=仅曲线 1=仅卡片 2=曲线+卡片（默认两者同屏，可手动切换）
    property int viewMode: 2
    //! 历史回放模式：true 显示加载的历史会话，实时采样继续落盘但不进显示缓冲
    property bool historyMode: false
    //! 当前回放的历史会话文件名
    property string historyName: ""
    //! 实时采样缓冲（历史模式下仍持续追加与裁剪，返回实时时直接恢复）
    property var liveSamples: []
    //! 已加载的历史会话全部采样点（未裁剪，供切换时间窗时重新截取）
    property var historyFull: []
    //! 历史会话元信息列表（新→旧），每项 {name,label,points,durationSec,sizeKb,isCurrent}
    property var sessionInfos: []
    //! 历史回放可见范围（占整段会话的比例 [0..1, 0..1]），由 RangeSlider 拖动/缩放
    property var historyRange: [0, 1]
    //! 时间窗下拉框当前项（4=全程）
    property int windowIndex: 1
    //! 是否正在录制（镜像 recorder.recording，经 sessionChanged 刷新）
    property bool isRecording: false
    //! 当前录制会话已写入点数（镜像 recorder.recordedPoints）
    property int liveRecordedPoints: 0

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

    // ---- 曲线绘制（参照 MAT 上位机多 Y 轴实时图）----
    //! 时间窗口(s)，可通过下拉框切换
    property real windowSec: 300
    //! 历史采样点数组，元素 { t, speed, ntc1, ntc2, hum, fan, comp, cpw, fpw, epw }
    property var samples: []
    //! 当前缓冲点数（samples 为 var 数组，内容变化不触发绑定，故单独维护）
    property int sampleCount: 0
    //! 各坐标轴当前取值范围（axisId -> {min, max}，随窗口内数据自适应）
    property var axisRanges: ({})
    //! 曲线显示开关（与 seriesDefs 顺序一致）
    property var seriesEnabled: [true, true, true, true, true, true, false, false, false]
    //! 鼠标悬停命中的采样点索引，-1 表示未命中
    property int hoverIndex: -1

    //! 坐标轴定义：左侧 2 轴 + 右侧 3 轴，同轴曲线共用取值范围
    readonly property var axisDefs: [
        { id: "temp",  side: "left",  label: qsTr("温度/代码"),     color: "#9ccc65" },
        { id: "hum",   side: "left",  label: qsTr("湿度"),          color: "#29b6f6" },
        { id: "power", side: "right", label: qsTr("功率"),          color: "#26c6da" },
        { id: "fc",    side: "right", label: qsTr("风机/压缩机转速"), color: "#5c6bc0" },
        { id: "motor", side: "right", label: qsTr("电机转速"),      color: "#ffca28" }
    ]

    //! 曲线定义：SDU 查询到的数值字段（key 与采样点字段一致，axis 为所属坐标轴）
    readonly property var seriesDefs: [
        { key: "hum",   label: qsTr("湿度"),       unit: "",      color: "#29b6f6", axis: "hum" },
        { key: "ntc1",  label: qsTr("NTC1"),       unit: "°C",    color: "#ffa726", axis: "temp" },
        { key: "ntc2",  label: qsTr("NTC2"),       unit: "°C",    color: "#7e57c2", axis: "temp" },
        { key: "speed", label: qsTr("电机转速"),   unit: "r/min", color: "#ffca28", axis: "motor" },
        { key: "comp",  label: qsTr("压缩机转速"), unit: "rpm",   color: "#5c6bc0", axis: "fc" },
        { key: "fan",   label: qsTr("风机转速"),   unit: "rpm",   color: "#ef5350", axis: "fc" },
        { key: "cpw",   label: qsTr("压缩机功率"), unit: "W",     color: "#26a69a", axis: "power" },
        { key: "fpw",   label: qsTr("风机功率"),   unit: "W",     color: "#9ccc65", axis: "power" },
        { key: "epw",   label: qsTr("电机功率"),   unit: "W",     color: "#f48fb1", axis: "power" }
    ]

    //! 启动不再自动开录（避免空会话），仅加载历史列表；录制由用户显式开始
    Component.onCompleted: {
        root.refreshSessions();
    }

    //! 面板销毁（应用退出）时收尾：关闭正在写入的会话文件，释放句柄
    Component.onDestruction: {
        if (root.recorder && root.recorder.recording)
            root.recorder.closeSession();
    }

    // 录制状态镜像：recorder 为注入的 var，信号驱动刷新本地布尔/计数并同步历史列表
    Connections {
        target: root.recorder
        function onSessionChanged() {
            root.isRecording = root.recorder ? root.recorder.recording : false;
            root.liveRecordedPoints = root.recorder ? root.recorder.recordedPoints : 0;
        }
        function onRecordedPointsChanged() {
            root.liveRecordedPoints = root.recorder ? root.recorder.recordedPoints : 0;
        }
    }

    //! 开始录制：创建（惰性）新会话，首帧数据到达时才真正建文件；顺带刷新历史列表
    function startRecording() {
        if (root.recorder)
            root.recorder.startSession();
        root.refreshSessions();
    }

    //! 停止录制并保存当前会话（无数据则不留空文件），刷新列表
    function stopRecording() {
        if (root.recorder)
            root.recorder.closeSession();
        root.refreshSessions();
    }

    //! 结束当前会话并立即开始一个新的（连续分段录制）
    function restartRecording() {
        if (!root.recorder)
            return;
        root.recorder.closeSession();
        root.recorder.startSession();
        root.refreshSessions();
    }

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
            if (arr.length >= 13)
                root.appendSample()
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

    // ------------------------------------------------------------------
    // 曲线采样（同 DataMonitor 的 Canvas 自绘方案）
    // ------------------------------------------------------------------

    //! 收到一帧查询 SDU 后追加一个采样点：始终写入实时缓冲并落盘，
    //! 仅非历史模式下同步到显示缓冲
    function appendSample() {
        var now = Date.now() / 1000;
        var buffer = liveSamples;

        // 系统时间回退（或长时间断连后重连）时历史数据作废
        if (buffer.length > 0 && now < buffer[buffer.length - 1].t)
            buffer = [];

        var point = {
            t: now,
            speed: motorSpeed,
            ntc1: ntc1Int + ntc1Dec / 10,
            ntc2: ntc2Int + ntc2Dec / 10,
            hum: humidityRaw,
            fan: fanSpeed,
            comp: compSpeed,
            cpw: compPower,
            fpw: fanPower,
            epw: electricPower
        };
        buffer.push(point);
        if (root.recorder)
            root.recorder.append(point);

        // 只保留时间窗内的点（留一个窗外点用于左边界连线）；windowSec<=0 表示全程不裁剪
        if (windowSec > 0) {
            var cutoff = now - windowSec;
            var firstKeep = 0;
            while (firstKeep < buffer.length - 1 && buffer[firstKeep + 1].t < cutoff)
                ++firstKeep;
            if (firstKeep > 0)
                buffer = buffer.slice(firstKeep);
        }

        liveSamples = buffer;

        if (!historyMode) {
            samples = buffer;
            sampleCount = buffer.length;
            computeAxisRanges();
            chart.requestPaint();
        }
    }

    //! 坐标轴范围自适应：按窗口内已启用曲线的 min/max 计算，正值轴下限归零
    function computeAxisRanges() {
        var ranges = {};
        var a, i, s;
        for (a = 0; a < axisDefs.length; ++a)
            ranges[axisDefs[a].id] = { min: Number.MAX_VALUE, max: -Number.MAX_VALUE };

        for (s = 0; s < seriesDefs.length; ++s) {
            if (!seriesEnabled[s])
                continue;
            var r = ranges[seriesDefs[s].axis];
            for (i = 0; i < samples.length; ++i) {
                var v = samples[i][seriesDefs[s].key];
                if (v < r.min) r.min = v;
                if (v > r.max) r.max = v;
            }
        }

        for (a = 0; a < axisDefs.length; ++a) {
            var rg = ranges[axisDefs[a].id];
            if (rg.min > rg.max) {          // 无启用曲线或无数据：默认 0-1
                rg.min = 0; rg.max = 1;
            } else {
                if (rg.min >= 0) rg.min = 0;                 // 全正则从 0 起
                else rg.min *= 1.1;                          // 含负值则留 10% 余量
                rg.max = (rg.max === 0) ? 1 : rg.max * 1.1;  // 上限留 10% 余量
            }
        }
        axisRanges = ranges;
    }

    function clearSamples() {
        if (historyMode)
            exitHistory();
        liveSamples = [];
        samples = [];
        sampleCount = 0;
        hoverIndex = -1;
        axisRanges = ({});
        chart.requestPaint();
    }

    //! 切换时间窗（idx 对应 winCombo，4=全程）后按新窗口重取数据
    function setWindow(idx) {
        windowIndex = idx;
        windowSec = [60, 300, 900, 3600, 0][idx];
        if (historyMode)
            applyHistoryWindow();
        else
            applyLiveWindow();
        computeAxisRanges();
        chart.requestPaint();
    }

    //! 按当前时间窗从实时缓冲重取显示数据
    function applyLiveWindow() {
        var buffer = liveSamples;
        if (windowSec > 0 && buffer.length > 0) {
            var cutoff = buffer[buffer.length - 1].t - windowSec;
            var i = 0;
            while (i < buffer.length - 1 && buffer[i + 1].t < cutoff)
                ++i;
            if (i > 0)
                buffer = buffer.slice(i);
        }
        samples = buffer;
        sampleCount = buffer.length;
        hoverIndex = -1;
    }

    //! 按当前回放范围从已加载的历史会话重取显示数据
    function applyHistoryWindow() {
        var buffer = historyFull;
        if (buffer.length === 0) {
            samples = [];
            sampleCount = 0;
            hoverIndex = -1;
            return;
        }
        var t0 = buffer[0].t;
        var span = buffer[buffer.length - 1].t - t0;
        var lo = t0 + historyRange[0] * span;
        var hi = t0 + historyRange[1] * span;
        // 截取 [lo, hi] 内的点，并各多保留一个界外点用于左右边界连线
        var start = 0;
        while (start < buffer.length - 1 && buffer[start + 1].t < lo)
            ++start;
        var end = buffer.length - 1;
        while (end > 0 && buffer[end - 1].t > hi)
            --end;
        if (end < start)
            end = start;
        var sub = buffer.slice(start, end + 1);
        samples = sub;
        sampleCount = sub.length;
        hoverIndex = -1;
    }

    //! 范围滑块手柄提交后：按新范围重取并重绘（仅历史模式生效）
    function updateHistoryRange() {
        if (!historyMode)
            return;
        historyRange = [rangeSlider.first.value, rangeSlider.second.value];
        applyHistoryWindow();
        computeAxisRanges();
        chart.requestPaint();
    }

    // ---- 历史会话回放 ----

    //! 刷新历史会话元信息列表（新→旧）
    function refreshSessions() {
        sessionInfos = root.recorder ? root.recorder.sessionInfos() : [];
    }

    //! 进入历史回放：加载指定会话并显示整段（实时采样继续落盘）
    function enterHistory(name) {
        if (!root.recorder || !name)
            return;
        historyFull = root.recorder.loadSession(name);
        historyName = name;
        historyMode = true;
        historyRange = [0, 1];
        rangeSlider.first.value = 0;
        rangeSlider.second.value = 1;
        applyHistoryWindow();
        computeAxisRanges();
        chart.requestPaint();
    }

    //! 退出历史回放，恢复实时显示
    function exitHistory() {
        historyMode = false;
        historyName = "";
        historyFull = [];
        historyRange = [0, 1];
        applyLiveWindow();
        computeAxisRanges();
        chart.requestPaint();
    }

    //! 时间窗右端（实时=当前时刻滚动；历史/全程=固定到最后一个采样点）
    function windowEnd() {
        if (samples.length === 0)
            return Date.now() / 1000;
        if (historyMode || windowSec <= 0)
            return samples[samples.length - 1].t;
        return Math.max(samples[samples.length - 1].t, Date.now() / 1000 - windowSec);
    }

    //! 当前有效时间跨度：历史/全程按显示数据首末点自适应，其余按 windowSec
    function windowSpan() {
        if (historyMode || windowSec <= 0) {
            if (samples.length >= 2) {
                var sp = samples[samples.length - 1].t - samples[0].t;
                return sp > 0 ? sp : 1;
            }
            return 1;
        }
        return windowSec;
    }

    //! X 轴刻度步长：按时间窗取 1/2/5/10/… 序列中最接近的整刻度，
    //! 且不小于采集周期（轮询间隔），保证刻度落在采样时刻上
    function niceTickSec(tSpan) {
        var steps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
        var minStep = (root.mat && root.mat.pollIntervalMs > 0)
                      ? root.mat.pollIntervalMs / 1000 : 1;
        var want = Math.max(tSpan / 6, minStep);
        for (var i = 0; i < steps.length; ++i)
            if (steps[i] >= want)
                return steps[i];
        return 7200;
    }

    //! X 轴刻度标签格式：短窗（≤2 分钟）用 mm:ss，长窗用 hh:mm:ss
    function fmtTick(tSec) {
        var d = new Date(tSec * 1000);
        return windowSec <= 120 ? Qt.formatTime(d, "mm:ss") : Qt.formatTime(d, "hh:mm:ss");
    }

    color: colorSurface
    radius: 8
    border.width: 1
    border.color: colorBorder

    // ------------------------------------------------------------------
    // 复用组件（与 DataMonitor 完全一致的风格）
    // ------------------------------------------------------------------

    //! 数值单元格：标签与数值分行，放大字体后仍可容纳多列卡片
    component ValueCell: Rectangle {
        id: cell
        property string label: ""
        property string value: "-"
        property color seriesColor: root.colorAccent

        Layout.fillWidth: true
        Layout.preferredHeight: 58
        radius: 6
        color: root.colorSurfaceAlt

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: cell.seriesColor }
                Text {
                    Layout.fillWidth: true
                    text: cell.label; color: root.colorMuted; font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
            Text {
                Layout.fillWidth: true
                text: cell.value; color: root.colorFg; font.pixelSize: 16; font.bold: true
                elide: Text.ElideRight
            }
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

            ComboBox {
                Layout.preferredWidth: 110
                Layout.preferredHeight: 30
                font.pixelSize: 12
                model: [qsTr("曲线视图"), qsTr("卡片视图"), qsTr("曲线+卡片")]
                currentIndex: root.viewMode
                onActivated: root.viewMode = currentIndex
            }

            Button {
                Layout.preferredHeight: 30
                text: qsTr("清零")
                font.pixelSize: 12
                onClicked: { root.frameCount = 0; root.lastRxTime = "--:--:--" }
            }

            Button {
                Layout.preferredHeight: 30
                text: root.expanded ? qsTr("折叠 ▾") : qsTr("展开 ▴")
                font.pixelSize: 12
                onClicked: root.expanded = !root.expanded
            }
        }

        // ========== 数据记录工具条（录制 + 历史，统一一行：录制组居左、历史组居右）==========
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.expanded && root.viewMode !== 1
            // 切到曲线视图时自动刷新会话列表，免去手动点“刷新”
            onVisibleChanged: if (visible && !root.historyMode) root.refreshSessions()

            // --- 录制组（左）---
            Rectangle {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                Layout.alignment: Qt.AlignVCenter
                radius: 6
                color: root.isRecording ? root.colorErr : "#666666"
                // 录制中闪烁提示
                SequentialAnimation on opacity {
                    running: root.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
            Text {
                id: recStatus
                text: root.isRecording
                      ? qsTr("正在录制 · %1 点").arg(root.liveRecordedPoints)
                      : qsTr("未录制")
                color: root.isRecording ? root.colorErr : root.colorMuted
                font.pixelSize: 12
                font.bold: root.isRecording
                ToolTip.delay: 400
                ToolTip.timeout: 4000
                ToolTip.visible: recTipArea.containsMouse
                ToolTip.text: (root.isRecording && root.recorder)
                              ? root.recorder.currentSession : qsTr("未开始录制")
                MouseArea {
                    id: recTipArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
            Button {
                Layout.preferredHeight: 26
                text: root.isRecording ? qsTr("停止录制") : qsTr("开始录制")
                font.pixelSize: 11
                onClicked: root.isRecording ? root.stopRecording() : root.startRecording()
            }
            Button {
                Layout.preferredHeight: 26
                text: qsTr("重新分段")
                font.pixelSize: 11
                visible: root.isRecording
                onClicked: root.restartRecording()
            }

            // 中部弹性空白：把历史组推到右侧，避免下拉框被拉伸过长
            Item { Layout.fillWidth: true }

            // --- 分隔线 ---
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 22
                Layout.alignment: Qt.AlignVCenter
                color: root.colorLine
            }

            // --- 历史组（右）---
            Text {
                text: qsTr("历史记录")
                color: root.historyMode ? root.colorAccent : root.colorMuted
                font.pixelSize: 12
                font.bold: root.historyMode
            }
            // 会话选择：显示“时间 · 点数 · 时长”，选中即回放；打开下拉前自动刷新（浏览态）
            ComboBox {
                id: sessionCombo
                Layout.preferredWidth: 260
                Layout.preferredHeight: 26
                font.pixelSize: 11
                textRole: "label"
                model: root.sessionInfos
                visible: !root.historyMode
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < root.sessionInfos.length)
                        root.enterHistory(root.sessionInfos[currentIndex].name);
                }
                // ComboBox 无 pressed 信号，改在其 popup 弹出前刷新会话列表
                Connections {
                    target: sessionCombo.popup
                    function onAboutToShow() { root.refreshSessions() }
                }
            }
            // 回放态：状态文字固定宽度省略，使浏览/回放两态历史组右缘对齐一致
            Text {
                Layout.preferredWidth: 220
                visible: root.historyMode
                text: root.historyMode
                      ? qsTr("回放中：%1（%2 点）").arg(root.historyName).arg(root.sampleCount) : ""
                color: root.colorMuted
                font.pixelSize: 11
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignRight
            }
            Button {
                Layout.preferredHeight: 26
                text: qsTr("显示全程")
                font.pixelSize: 11
                visible: root.historyMode
                onClicked: {
                    rangeSlider.first.value = 0;
                    rangeSlider.second.value = 1;
                    root.historyRange = [0, 1];
                    root.applyHistoryWindow();
                    root.computeAxisRanges();
                    chart.requestPaint();
                }
            }
            Button {
                Layout.preferredHeight: 26
                text: qsTr("返回实时")
                font.pixelSize: 11
                visible: root.historyMode
                onClicked: {
                    root.exitHistory();
                    root.refreshSessions();
                }
            }
        }

        // ========== 历史回放范围滑块（仅历史模式，拖动/缩放定位片段）==========
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.expanded && root.viewMode !== 1 && root.historyMode

            Text {
                text: qsTr("范围")
                color: root.colorMuted
                font.pixelSize: 11
            }

            RangeSlider {
                id: rangeSlider
                Layout.fillWidth: true
                from: 0
                to: 1
                first.value: 0
                second.value: 1
                // live:false → 释放手柄时才提交 value，避免拖动过程频繁重算大会话
                live: false
                first.onValueChanged: root.updateHistoryRange()
                second.onValueChanged: root.updateHistoryRange()
            }

            Text {
                Layout.preferredWidth: 150
                text: {
                    if (root.historyFull.length < 2)
                        return "";
                    var t0 = root.historyFull[0].t;
                    var span = root.historyFull[root.historyFull.length - 1].t - t0;
                    var lo = new Date((t0 + root.historyRange[0] * span) * 1000);
                    var hi = new Date((t0 + root.historyRange[1] * span) * 1000);
                    return Qt.formatTime(lo, "hh:mm:ss") + " — " + Qt.formatTime(hi, "hh:mm:ss");
                }
                color: root.colorMuted
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
            }
        }

        // ========== 查询数据曲线（多 Y 轴实时图，参照 MAT 上位机）==========
        Rectangle {
            Layout.fillWidth: true
            // 与下方数据区按比例分配高度（3:2，由 preferredHeight 提示），空间不足时同步收缩，避免裁切
            Layout.fillHeight: true
            Layout.preferredHeight: 300
            Layout.minimumHeight: 120
            visible: root.expanded && root.viewMode !== 1
            radius: 6
            color: colorSurfaceAlt

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                // --- 图例（窄窗口自动换行，点击切换曲线）+ 时间窗 / 清空 ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Flow {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        spacing: 6

                        Repeater {
                            model: root.seriesDefs

                            delegate: Rectangle {
                                id: chip
                                required property var modelData
                                required property int index

                                readonly property bool on: root.seriesEnabled[index]
                                implicitWidth: chipRow.implicitWidth + 12
                                implicitHeight: 24
                                radius: 12
                                color: "transparent"
                                opacity: on ? 1.0 : 0.35

                                RowLayout {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Rectangle {
                                        Layout.preferredWidth: 14
                                        Layout.preferredHeight: 2
                                        radius: 1
                                        color: chip.modelData.color
                                    }
                                    Text {
                                        text: chip.modelData.label
                                        color: root.colorFg
                                        font.pixelSize: 12
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var arr = root.seriesEnabled.slice();
                                        arr[chip.index] = !arr[chip.index];
                                        root.seriesEnabled = arr;
                                        root.computeAxisRanges();
                                        chart.requestPaint();
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: root.sampleCount + qsTr(" 点")
                        color: root.colorMuted
                        font.pixelSize: 11
                    }

                    ComboBox {
                        id: winCombo
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 24
                        font.pixelSize: 11
                        // 历史回放用下方范围滑块定位，时间窗下拉仅实时模式可见
                        visible: !root.historyMode
                        model: [qsTr("1 分钟"), qsTr("5 分钟"), qsTr("15 分钟"), qsTr("1 小时"), qsTr("全程")]
                        currentIndex: root.windowIndex
                        onActivated: root.setWindow(currentIndex)
                    }

                    Button {
                        Layout.preferredHeight: 24
                        text: qsTr("清空曲线")
                        font.pixelSize: 11
                        visible: !root.historyMode
                        onClicked: root.clearSamples()
                    }
                }

                // --- 图表 ---
                Item {
                    id: chartArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Canvas {
                        id: chart
                        anchors.fill: parent
                        renderStrategy: Canvas.Cooperative
                        renderTarget: Canvas.Image

                        // 尺寸或视图变化后立即重绘，无数据时也更新坐标轴
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onVisibleChanged: if (visible) requestPaint()

                        //! 绘图区边距与放大后的刻度字体匹配，悬停映射共用此布局
                        readonly property int axisColW: 56
                        readonly property int axisTitleW: 16
                        readonly property int leftW: 2 * (axisColW + axisTitleW)
                        readonly property int rightW: 3 * (axisColW + axisTitleW)
                        readonly property int topH: 10
                        readonly property int bottomH: 24
                        readonly property int plotW: width - leftW - rightW
                        readonly property int plotH: height - topH - bottomH

                        //! 坐标轴刻度数值格式（同 MAT 上位机保留 1 位小数）
                        function fmtAxis(v) { return v.toFixed(1); }

                        //! 绘制单条曲线：值按其所属坐标轴范围归一化到绘图区
                        function drawSeries(ctx, def, range, tStart, tSpan) {
                            var pts = root.samples;
                            if (pts.length < 2 || range.max <= range.min || plotW <= 0)
                                return;

                            ctx.beginPath();
                            ctx.strokeStyle = def.color;
                            ctx.lineWidth = 1.6;
                            ctx.lineJoin = "round";

                            var started = false;
                            for (var i = 0; i < pts.length; ++i) {
                                var x = leftW + (pts[i].t - tStart) / tSpan * plotW;
                                var n = (pts[i][def.key] - range.min) / (range.max - range.min);
                                var y = topH + (1 - Math.max(0, Math.min(1, n))) * plotH;
                                if (!started) { ctx.moveTo(x, y); started = true; }
                                else ctx.lineTo(x, y);
                            }
                            ctx.stroke();
                        }

                        //! 绘制单根坐标轴：5 个刻度值 + 旋转轴名（位于绘图区外侧）
                        function drawAxis(ctx, axisDef, col, side) {
                            var range = root.axisRanges[axisDef.id] || { min: 0, max: 1 };
                            var colStep = axisColW + axisTitleW;
                            ctx.fillStyle = axisDef.color;
                            ctx.font = "12px sans-serif";

                            for (var g = 0; g <= 4; ++g) {
                                var v = range.max - (range.max - range.min) * g / 4;
                                var y = topH + plotH * g / 4 + 3;
                                if (side === "left") {
                                    ctx.textAlign = "right";
                                    ctx.fillText(fmtAxis(v), col * colStep + axisColW - 4, y);
                                } else {
                                    ctx.textAlign = "left";
                                    ctx.fillText(fmtAxis(v), leftW + plotW + col * colStep + 4, y);
                                }
                            }

                            // 旋转轴名（竖排，位于本轴刻度列外侧）
                            var tx = (side === "left")
                                     ? col * colStep + axisColW + axisTitleW / 2
                                     : leftW + plotW + col * colStep + axisColW + axisTitleW / 2;
                            ctx.save();
                            ctx.translate(tx, topH + plotH / 2);
                            ctx.rotate(-Math.PI / 2);
                            ctx.textAlign = "center";
                            ctx.fillText(axisDef.label, 0, 3);
                            ctx.restore();
                        }

                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();

                            // 背景
                            ctx.fillStyle = "#262626";
                            ctx.fillRect(0, 0, width, height);

                            if (plotW <= 0 || plotH <= 0)
                                return;

                            // 横向网格（5 条，对应各轴 5 个刻度）
                            ctx.strokeStyle = "#3f3f3f";
                            ctx.lineWidth = 1;
                            for (var g = 0; g <= 4; ++g) {
                                var gy = Math.round(topH + plotH * g / 4) + 0.5;
                                ctx.beginPath();
                                ctx.moveTo(leftW, gy);
                                ctx.lineTo(leftW + plotW, gy);
                                ctx.stroke();
                            }

                            // 纵向网格 + 横轴时间刻度（步长随时间窗/采集周期自适应）
                            var tEnd = root.windowEnd();
                            var tSpan = root.windowSpan();
                            var tStart = tEnd - tSpan;
                            ctx.font = "12px sans-serif";
                            ctx.fillStyle = root.colorMuted;
                            ctx.textAlign = "center";
                            var tick = root.niceTickSec(tSpan);
                            var firstTick = Math.ceil(tStart / tick) * tick;
                            for (var tv = firstTick; tv <= tEnd + 0.001; tv += tick) {
                                var gx = Math.round(leftW + (tv - tStart) / tSpan * plotW) + 0.5;
                                ctx.strokeStyle = "#3f3f3f";
                                ctx.beginPath();
                                ctx.moveTo(gx, topH);
                                ctx.lineTo(gx, topH + plotH);
                                ctx.stroke();
                                ctx.fillText(root.fmtTick(tv), gx, topH + plotH + 17);
                            }

                            // 坐标轴：左 2 右 3
                            var li = 0, ri = 0;
                            for (var a = 0; a < root.axisDefs.length; ++a) {
                                var ax = root.axisDefs[a];
                                if (ax.side === "left") drawAxis(ctx, ax, li++, "left");
                                else drawAxis(ctx, ax, ri++, "right");
                            }

                            // 已启用的曲线
                            for (var s = 0; s < root.seriesDefs.length; ++s) {
                                if (!root.seriesEnabled[s])
                                    continue;
                                var def = root.seriesDefs[s];
                                drawSeries(ctx, def, root.axisRanges[def.axis] || { min: 0, max: 1 }, tStart, tSpan);
                            }
                        }
                    }

                    // 悬停十字线
                    Rectangle {
                        visible: root.hoverIndex >= 0
                        width: 1
                        color: "#8d99a6"
                        x: chart.x + hoverArea.mouseX
                        y: chart.y
                        height: chart.height
                    }

                    // 悬停数值提示：时刻 + 所有已启用曲线的原始值
                    Rectangle {
                        id: tooltip
                        readonly property var point: root.hoverIndex >= 0 && root.hoverIndex < root.samples.length
                                                     ? root.samples[root.hoverIndex] : null
                        visible: point !== null
                        width: tooltipCol.implicitWidth + 16
                        height: tooltipCol.implicitHeight + 12
                        radius: 6
                        color: "#1f1f1f"
                        border.width: 1
                        border.color: root.colorBorder
                        x: Math.min(chart.x + hoverArea.mouseX + 12, chartArea.width - width - 2)
                        y: chart.y + 6

                        // 逐行着色：时间行用前景色，各曲线行与其曲线同色
                        Column {
                            id: tooltipCol
                            anchors.centerIn: parent
                            spacing: 2

                            Text {
                                text: tooltip.point
                                      ? Qt.formatTime(new Date(tooltip.point.t * 1000), "hh:mm:ss") : ""
                                color: root.colorFg
                                font.pixelSize: 11
                            }

                            Repeater {
                                model: root.seriesDefs

                                delegate: Text {
                                    required property var modelData
                                    required property int index
                                    visible: root.seriesEnabled[index] && tooltip.point !== null
                                    text: modelData.label + "  "
                                          + (tooltip.point ? tooltip.point[modelData.key].toFixed(1) : "")
                                          + (modelData.unit ? " " + modelData.unit : "")
                                    color: modelData.color
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: chart
                        hoverEnabled: true
                        onExited: root.hoverIndex = -1
                        onPositionChanged: {
                            if (root.samples.length === 0 || chart.plotW <= 0
                                || mouseX < chart.leftW || mouseX > chart.leftW + chart.plotW) {
                                root.hoverIndex = -1;
                                return;
                            }
                            var tEnd = root.windowEnd();
                            var tSpan = root.windowSpan();
                            var t = tEnd - tSpan
                                    + (mouseX - chart.leftW) / chart.plotW * tSpan;

                            var best = 0;
                            var bestDelta = Number.MAX_VALUE;
                            for (var i = 0; i < root.samples.length; ++i) {
                                var delta = Math.abs(root.samples[i].t - t);
                                if (delta < bestDelta) { bestDelta = delta; best = i; }
                            }
                            root.hoverIndex = best;
                        }
                    }

                    // 无数据提示
                    Text {
                        anchors.centerIn: chart
                        visible: root.sampleCount < 2
                        text: qsTr("等待查询数据…")
                        color: root.colorMuted
                        font.pixelSize: 13
                    }
                }
            }
        }

        // 曲线随时间滚动重绘（即使暂无新帧到达）
        Timer {
            interval: 1000
            running: root.expanded && root.viewMode !== 1 && !root.historyMode && root.sampleCount > 0
            repeat: true
            onTriggered: chart.requestPaint()
        }

        // ========== 滚动数据区 ==========
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 200
            Layout.minimumHeight: 80
            clip: true
            visible: root.expanded && root.viewMode !== 0
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
    }
}
