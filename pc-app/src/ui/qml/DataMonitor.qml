import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/*!
    底部数据监控面板：水位/水温/转速三条曲线的滚动时间窗图表。

    采用 Canvas 自绘而非 Qt Charts，避免额外的图形后端依赖。
    三条曲线量纲不同，各自按满量程归一化后共用 0-100% 的纵轴，
    满量程会在实测值超限时自动放大（图例中标注当前量程）。
*/
Rectangle {
    id: root

    required property var engine
    //! 面板展开状态（折叠时只保留标题栏）
    property bool expanded: true

    readonly property color colorSurface: "#333333"
    readonly property color colorSurfaceAlt: "#3a3a3a"
    readonly property color colorBorder: "#454545"
    readonly property color colorFg: "#e0e0e0"
    readonly property color colorMuted: "#9e9e9e"
    readonly property color colorAccent: "#4fc3f7"

    readonly property color colorLevel: "#42a5f5"
    readonly property color colorTemp: "#ef5350"
    readonly property color colorSpeed: "#ffca28"

    //! 时间窗口(s)
    readonly property real windowSec: 60
    //! 采样周期(ms)
    readonly property int samplePeriodMs: 100

    //! 历史采样点数组，元素 { t, level, temp, rpm }
    property var samples: []
    //! 当前缓冲点数（samples 为 var 数组，内容变化不会触发绑定，故单独维护）
    property int sampleCount: 0
    //! 各曲线满量程（随实测峰值上调）
    property real fullScaleLevel: 500
    property real fullScaleTemp: 95
    property real fullScaleSpeed: 1600

    //! 鼠标悬停命中的采样点索引，-1 表示未命中
    property int hoverIndex: -1

    color: colorSurface
    radius: 8
    border.width: 1
    border.color: colorBorder

    // ------------------------------------------------------------------
    // 采样
    // ------------------------------------------------------------------
    function appendSample() {
        if (!engine)
            return;

        var t = engine.elapsedTime;
        var buffer = samples;

        // 引擎复位后时间回退，历史数据作废
        if (buffer.length > 0 && t < buffer[buffer.length - 1].t)
            buffer = [];

        buffer.push({
            t: t,
            level: engine.waterModel.waterLevel,
            temp: engine.tempModel.temperature,
            rpm: Math.abs(engine.motorModel.speedRpm)
        });

        // 只保留时间窗内的点（留一个窗外点用于左边界连线）
        var cutoff = t - windowSec;
        var firstKeep = 0;
        while (firstKeep < buffer.length - 1 && buffer[firstKeep + 1].t < cutoff)
            ++firstKeep;
        if (firstKeep > 0)
            buffer = buffer.slice(firstKeep);

        samples = buffer;
        sampleCount = buffer.length;
        updateFullScales();
        chart.requestPaint();
    }

    //! 量程自适应：实测峰值超过当前量程时按 10% 余量上调
    function updateFullScales() {
        var maxLevel = 0, maxTemp = 0, maxRpm = 0;
        for (var i = 0; i < samples.length; ++i) {
            maxLevel = Math.max(maxLevel, samples[i].level);
            maxTemp = Math.max(maxTemp, samples[i].temp);
            maxRpm = Math.max(maxRpm, samples[i].rpm);
        }
        fullScaleLevel = Math.max(500, maxLevel * 1.1);
        fullScaleTemp = Math.max(95, maxTemp * 1.1);
        fullScaleSpeed = Math.max(1600, maxRpm * 1.1);
    }

    function clearSamples() {
        samples = [];
        sampleCount = 0;
        hoverIndex = -1;
        chart.requestPaint();
    }

    //! 时间窗右端（当前时刻，不足一个窗口时按窗口长度显示）
    function windowEnd() {
        if (samples.length === 0)
            return windowSec;
        return Math.max(windowSec, samples[samples.length - 1].t);
    }

    Timer {
        interval: root.samplePeriodMs
        running: root.engine !== null
        repeat: true
        onTriggered: root.appendSample()
    }

    // ------------------------------------------------------------------
    // 复用组件
    // 内联组件不共享外层文档的 id 作用域，因此主题色在此以字面量声明。
    // ------------------------------------------------------------------

    //! 图例条目：色块 + 名称 + 量程
    component LegendItem: RowLayout {
        id: legend

        property string label: ""
        property string range: ""
        property color seriesColor: "#4fc3f7"

        spacing: 5

        Rectangle {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 3
            radius: 1.5
            color: legend.seriesColor
        }

        Text {
            text: legend.label
            color: "#e0e0e0"
            font.pixelSize: 12
        }

        Text {
            text: legend.range
            color: "#9e9e9e"
            font.pixelSize: 12
        }
    }

    //! 当前数值单元格
    component ValueCell: Rectangle {
        id: cell

        property string label: ""
        property string value: "-"
        property color seriesColor: "#4fc3f7"

        Layout.fillWidth: true
        Layout.preferredHeight: 34
        radius: 6
        color: "#3a3a3a"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: cell.seriesColor
            }

            Text {
                text: cell.label
                color: "#9e9e9e"
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Text {
                text: cell.value
                color: "#e0e0e0"
                font.pixelSize: 14
                font.bold: true
            }
        }
    }

    // ------------------------------------------------------------------
    // 面板主体
    // ------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // --- 标题栏 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Text {
                text: qsTr("数据监控")
                color: root.colorAccent
                font.pixelSize: 16
                font.bold: true
            }

            LegendItem {
                label: qsTr("水位")
                range: "0-" + root.fullScaleLevel.toFixed(0) + "mm"
                seriesColor: root.colorLevel
            }

            LegendItem {
                label: qsTr("水温")
                range: "0-" + root.fullScaleTemp.toFixed(0) + "°C"
                seriesColor: root.colorTemp
            }

            LegendItem {
                label: qsTr("转速")
                range: "0-" + root.fullScaleSpeed.toFixed(0) + "rpm"
                seriesColor: root.colorSpeed
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("窗口 %1 s / 采样 %2 ms / %3 点")
                        .arg(root.windowSec).arg(root.samplePeriodMs).arg(root.sampleCount)
                color: root.colorMuted
                font.pixelSize: 12
            }

            Button {
                Layout.preferredHeight: 26
                text: qsTr("清空")
                font.pixelSize: 12
                onClicked: root.clearSamples()
            }

            Button {
                Layout.preferredHeight: 26
                text: root.expanded ? qsTr("折叠 ▾") : qsTr("展开 ▴")
                font.pixelSize: 12
                onClicked: root.expanded = !root.expanded
            }
        }

        // --- 图表 ---
        Item {
            id: chartArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.expanded
            clip: true

            Canvas {
                id: chart

                anchors.fill: parent
                anchors.leftMargin: 46
                anchors.bottomMargin: 16
                renderStrategy: Canvas.Cooperative
                renderTarget: Canvas.Image

                //! 绘制单条曲线：值经 fullScale 归一化
                function drawSeries(ctx, key, fullScale, strokeColor, tStart, tSpan) {
                    var pts = root.samples;
                    if (pts.length < 2)
                        return;

                    ctx.beginPath();
                    ctx.strokeStyle = strokeColor;
                    ctx.lineWidth = 1.8;
                    ctx.lineJoin = "round";

                    var started = false;
                    for (var i = 0; i < pts.length; ++i) {
                        var x = (pts[i].t - tStart) / tSpan * width;
                        var y = height - Math.max(0, Math.min(1, pts[i][key] / fullScale)) * height;
                        if (!started) {
                            ctx.moveTo(x, y);
                            started = true;
                        } else {
                            ctx.lineTo(x, y);
                        }
                    }
                    ctx.stroke();
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();

                    // 背景
                    ctx.fillStyle = "#262626";
                    ctx.fillRect(0, 0, width, height);

                    // 横向网格（0/25/50/75/100%）
                    ctx.strokeStyle = "#3f3f3f";
                    ctx.lineWidth = 1;
                    for (var g = 0; g <= 4; ++g) {
                        var gy = Math.round(height * g / 4) + 0.5;
                        ctx.beginPath();
                        ctx.moveTo(0, gy);
                        ctx.lineTo(width, gy);
                        ctx.stroke();
                    }

                    // 纵向网格：每 10 s 一格
                    var tEnd = root.windowEnd();
                    var tStart = tEnd - root.windowSec;
                    var tSpan = root.windowSec;
                    var firstTick = Math.ceil(tStart / 10) * 10;
                    for (var tt = firstTick; tt <= tEnd; tt += 10) {
                        var gx = Math.round((tt - tStart) / tSpan * width) + 0.5;
                        ctx.beginPath();
                        ctx.moveTo(gx, 0);
                        ctx.lineTo(gx, height);
                        ctx.stroke();
                    }

                    // 三条曲线
                    drawSeries(ctx, "level", root.fullScaleLevel, root.colorLevel, tStart, tSpan);
                    drawSeries(ctx, "temp", root.fullScaleTemp, root.colorTemp, tStart, tSpan);
                    drawSeries(ctx, "rpm", root.fullScaleSpeed, root.colorSpeed, tStart, tSpan);
                }
            }

            // 纵轴刻度（百分比）
            Repeater {
                model: 5

                Text {
                    required property int index

                    x: 0
                    y: chart.y + chart.height * index / 4 - height / 2
                    width: 40
                    horizontalAlignment: Text.AlignRight
                    text: (100 - index * 25) + "%"
                    color: root.colorMuted
                    font.pixelSize: 12
                }
            }

            // 横轴刻度（相对当前时刻的秒数）
            Repeater {
                model: 4

                Text {
                    required property int index

                    readonly property int secondsAgo: Math.round(root.windowSec * (1 - index / 3))

                    x: chart.x + chart.width * index / 3 - (index === 0 ? 0 : width / 2)
                    y: chart.y + chart.height + 2
                    text: secondsAgo === 0 ? qsTr("当前") : "-" + secondsAgo + "s"
                    color: root.colorMuted
                    font.pixelSize: 12
                }
            }

            // 悬停十字线（hoverArea 与 chart 重合，故需叠加 chart.x 偏移）
            Rectangle {
                visible: root.hoverIndex >= 0
                width: 1
                color: "#8d99a6"
                x: chart.x + hoverArea.mouseX
                y: chart.y
                height: chart.height
            }

            // 悬停数值提示
            Rectangle {
                id: tooltip

                readonly property var point: root.hoverIndex >= 0 && root.hoverIndex < root.samples.length
                                             ? root.samples[root.hoverIndex] : null

                visible: point !== null
                width: tooltipText.implicitWidth + 16
                height: tooltipText.implicitHeight + 12
                radius: 6
                color: "#1f1f1f"
                border.width: 1
                border.color: root.colorBorder
                // 靠近右边界时向左翻转，避免超出画布
                x: Math.min(chart.x + hoverArea.mouseX + 12, chartArea.width - width - 2)
                y: chart.y + 6

                Text {
                    id: tooltipText

                    anchors.centerIn: parent
                    color: root.colorFg
                    font.pixelSize: 12
                    text: tooltip.point
                          ? qsTr("t=%1s\n水位 %2 mm\n水温 %3 °C\n转速 %4 r/min")
                              .arg(tooltip.point.t.toFixed(1))
                              .arg(tooltip.point.level.toFixed(1))
                              .arg(tooltip.point.temp.toFixed(1))
                              .arg(tooltip.point.rpm.toFixed(0))
                          : ""
                }
            }

            MouseArea {
                id: hoverArea

                anchors.fill: chart
                hoverEnabled: true
                onExited: root.hoverIndex = -1
                onPositionChanged: {
                    // 按 x 坐标折算时刻，二分查找最近采样点
                    if (root.samples.length === 0 || chart.width <= 0) {
                        root.hoverIndex = -1;
                        return;
                    }
                    var tEnd = root.windowEnd();
                    var tStart = tEnd - root.windowSec;
                    var t = tStart + mouseX / chart.width * root.windowSec;

                    var best = 0;
                    var bestDelta = Number.MAX_VALUE;
                    for (var i = 0; i < root.samples.length; ++i) {
                        var delta = Math.abs(root.samples[i].t - t);
                        if (delta < bestDelta) {
                            bestDelta = delta;
                            best = i;
                        }
                    }
                    root.hoverIndex = best;
                }
            }

            // 无数据提示
            Text {
                anchors.centerIn: chart
                visible: root.sampleCount < 2
                text: qsTr("等待仿真数据…")
                color: root.colorMuted
                font.pixelSize: 14
            }
        }

        // --- 当前数值表格 ---
        RowLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: 8

            ValueCell {
                label: qsTr("水位")
                seriesColor: root.colorLevel
                value: root.engine
                       ? root.engine.waterModel.waterLevel.toFixed(1) + " mm" : "-"
            }

            ValueCell {
                label: qsTr("水温")
                seriesColor: root.colorTemp
                value: root.engine
                       ? root.engine.tempModel.temperature.toFixed(1) + " °C" : "-"
            }

            ValueCell {
                label: qsTr("转速")
                seriesColor: root.colorSpeed
                value: root.engine
                       ? Math.abs(root.engine.motorModel.speedRpm).toFixed(0) + " r/min" : "-"
            }

            ValueCell {
                label: qsTr("水位频率")
                seriesColor: "#26c6da"
                value: root.engine
                       ? (root.engine.waterModel.sensorFrequency / 1000).toFixed(2) + " kHz" : "-"
            }

            ValueCell {
                label: qsTr("NTC")
                seriesColor: "#ab47bc"
                value: root.engine
                       ? (root.engine.tempModel.ntcResistance / 1000).toFixed(2) + " kΩ" : "-"
            }
        }
    }
}
