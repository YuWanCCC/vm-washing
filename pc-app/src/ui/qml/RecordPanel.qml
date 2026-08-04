import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HIL.Fault 1.0

/*!
    数据记录面板：开始/停止记录、计数显示、CSV 导出、清除记录与最近记录预览。
    通过 recorder 属性注入 DataRecorder 实例。
*/
Item {
    id: root

    required property var recorder

    implicitHeight: body.implicitHeight
    implicitWidth: body.implicitWidth

    //! 预览列表数据（定时器节流刷新，避免每帧重建模型）
    property var previewRecords: []

    Timer {
        interval: 500
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.previewRecords = root.recorder ? root.recorder.recentRecords(30) : []
    }

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: root.recorder && root.recorder.recording
                      ? qsTr("停止记录") : qsTr("开始记录")
                font.pixelSize: 13
                highlighted: !(root.recorder && root.recorder.recording)
                onClicked: {
                    if (root.recorder.recording)
                        root.recorder.stopRecording();
                    else
                        root.recorder.startRecording();
                }
            }

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: root.recorder && root.recorder.recording ? "#ef5350" : "#555555"
            }

            Text {
                text: qsTr("已记录 %1 条").arg(root.recorder ? root.recorder.recordCount : 0)
                color: "#e0e0e0"
                font.pixelSize: 13
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: qsTr("导出CSV")
                font.pixelSize: 12
                enabled: root.recorder && root.recorder.recordCount > 0
                onClicked: csvDialog.open()
            }

            Button {
                text: qsTr("清除记录")
                font.pixelSize: 12
                enabled: root.recorder && root.recorder.recordCount > 0
                onClicked: root.recorder.clearRecords()
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            text: qsTr("最近记录（新→旧）")
            color: "#9e9e9e"
            font.pixelSize: 12
        }

        // 最近记录预览
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            color: "#2e2e2e"
            radius: 4
            border.width: 1
            border.color: "#454545"

            ListView {
                id: previewList

                anchors.fill: parent
                anchors.margins: 4
                clip: true
                // previewRecords 按时间正序，倒序显示使最新记录在最上方
                model: root.previewRecords.slice().reverse()
                boundsBehavior: Flickable.StopAtBounds

                delegate: RowLayout {
                    required property var modelData

                    width: previewList.width
                    spacing: 8

                    Text {
                        Layout.preferredWidth: 70
                        text: (modelData.timestampMs / 1000).toFixed(2) + "s"
                        color: "#9e9e9e"
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.preferredWidth: 110
                        text: modelData.channel
                        color: "#4fc3f7"
                        font.pixelSize: 11
                    }
                    Text {
                        text: Number(modelData.value).toPrecision(6)
                        color: "#e0e0e0"
                        font.pixelSize: 11
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: previewList.count === 0
                    text: qsTr("暂无记录")
                    color: "#6f7780"
                    font.pixelSize: 12
                }
            }
        }
    }

    //! CSV 导出文件对话框
    FileDialog {
        id: csvDialog

        title: qsTr("导出数据记录为 CSV")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV 文件 (*.csv)")]
        defaultSuffix: "csv"

        onAccepted: {
            // QUrl → 本地路径（去掉 file:/// 前缀）
            var path = selectedFile.toString().replace(/^file:\/\/\//, "");
            if (!root.recorder.exportToCsv(path))
                exportFailBanner.visible = true;
            else
                root.recorder.outputPath = path;
        }
    }

    Text {
        id: exportFailBanner

        anchors.top: body.bottom
        anchors.left: parent.left
        anchors.topMargin: 4
        visible: false
        text: qsTr("导出失败：无法写入文件")
        color: "#ef5350"
        font.pixelSize: 12
    }
}
