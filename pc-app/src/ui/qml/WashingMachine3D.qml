import QtQuick
import QtQuick.Layouts
import QtQuick3D
import HIL.Models 1.0

/*!
    洗衣机 3D 可视化视图（Qt Quick 3D 渲染）。

    物理量映射：
      - 水位高度   ← waterModel.waterLevel (0-500 mm)，通过水体圆柱缩放呈现
      - 滚筒旋转   ← motorModel.speedRpm 积分（绕 Y 轴，模拟前开门滚筒）
      - 门开合     ← doorModel.state（绕左铰链 Y 轴旋转）
      - 指示灯亮度 ← running / 进水阀 / 加热器 / 门锁
      - 视角控制   ← 鼠标拖拽旋转 + 滚轮缩放，双击复位
*/
Item {
    id: root

    required property var engine

    readonly property color colorMuted: "#9e9e9e"

    //! 满水位刻度 (mm)，水位显示按此归一化
    readonly property real fullLevelMm: 500
    readonly property real waterRatio: engine
        ? Math.max(0, Math.min(1, engine.waterModel.waterLevel / fullLevelMm)) : 0
    readonly property real speedRpm: engine ? engine.motorModel.speedRpm : 0
    readonly property bool doorOpen: engine
        ? engine.doorModel.state === DoorLockModel.Unlocked : true

    //! 滚筒累计转角 (度)，由动画定时器按当前转速积分
    property real drumAngle: 0
    //! 相机球坐标
    property real camAzimuth: 30      // 水平角
    property real camElevation: 12    // 仰角
    property real camDistance: 220    // 距场景中心距离
    property real lastX: 0
    property real lastY: 0

    // ---------------------------------------------------------------
    // 物理动画：16 ms 步进，滚筒转角与相机位置随仿真推进
    // ---------------------------------------------------------------
    Timer {
        interval: 16
        running: root.visible && (root.engine ? root.engine.running : false)
        repeat: true
        onTriggered: {
            // 显示转速上限 240°/帧，避免高转速视觉抖动
            var deltaDeg = root.speedRpm * 6 * 0.016;
            root.drumAngle = (root.drumAngle
                              + Math.max(-24, Math.min(24, deltaDeg))) % 360;
        }
    }

    //! 指示灯点亮判定
    function lampOn(key) {
        if (!engine) return false;
        switch (key) {
        case "run":   return engine.running;
        case "inlet": return engine.actuators.inletValveOpen;
        case "heat":  return engine.actuators.heaterOn;
        case "lock":  return engine.doorModel.isLocked;
        default:      return false;
        }
    }

    // ---------------------------------------------------------------
    // 3D 场景
    // ---------------------------------------------------------------
    View3D {
        id: view3d
        anchors.fill: parent
        environment: sceneEnv

        SceneEnvironment {
            id: sceneEnv
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            backgroundMode: SceneEnvironment.Color
            clearColor: "#181c22"
            lightProbe: null
            aoStrength: 0.35
            aoDistance: 28
        }

        // 主方向光（带柔和阴影）
        DirectionalLight {
            eulerRotation.x: -45
            eulerRotation.y: -30
            color: "#ffffff"
            brightness: 1.1
            castsShadow: true
            shadowFactor: 60
        }

        // 填充光（暖色调，从右前侧补光）
        DirectionalLight {
            eulerRotation.x: -15
            eulerRotation.y: 60
            color: "#fff4e6"
            brightness: 0.45
        }

        // 底部环境补光（避免底部过暗）
        PointLight {
            position: Qt.vector3d(0, -60, 80)
            color: "#ddeeff"
            brightness: 0.35
            constantFade: 1.0
            linearFade: 0.01
        }

        // 透视相机：由方位角 / 仰角 / 距离驱动
        PerspectiveCamera {
            id: camera
            fieldOfView: 38
            clipNear: 1
            clipFar: 2000
            position: Qt.vector3d(
                root.camDistance * Math.cos(root.camElevation * Math.PI / 180)
                                 * Math.sin(root.camAzimuth * Math.PI / 180),
                root.camDistance * Math.sin(root.camElevation * Math.PI / 180),
                root.camDistance * Math.cos(root.camElevation * Math.PI / 180)
                                 * Math.cos(root.camAzimuth * Math.PI / 180))
            lookAt: Qt.vector3d(0, 6, 0)
        }

        // ----------------------------------------------------------
        // 洗衣机主体（坐标系约定：Y 向上，尺寸单位按视觉比例）
        //   机柜：宽 60 × 高 85 × 深 55
        //   前门圆心：(0, 5, 27.5)，直径 38
        // ----------------------------------------------------------
        readonly property real bw: 60
        readonly property real bh: 85
        readonly property real bd: 55
        readonly property real doorCY: 5
        readonly property real doorDia: 38
        readonly property real drumLen: 42   // 滚筒深度
        readonly property real drumDia: 35   // 滚筒直径

        Node {
            id: machine

            // ===== 地面 =====
            Model {
                source: "#Rectangle"
                scale: Qt.vector3d(4, 4, 1)
                position: Qt.vector3d(0, -view3d.bh / 2 - 0.5, 0)
                eulerRotation.x: -90
                materials: DefaultMaterial {
                    diffuseColor: "#10141a"
                }
            }

            // ===== 机柜（金属漆白机身）=====
            Model {
                source: "#Cube"
                scale: Qt.vector3d(view3d.bw / 2, view3d.bh / 2, view3d.bd / 2)
                materials: DefaultMaterial {
                    diffuseColor: "#e4e7ea"
                    specularColor: "#ffffff"
                    specularAmount: 0.35
                }
            }

            // 侧边细微装饰槽（左）
            Model {
                source: "#Cube"
                scale: Qt.vector3d(0.15, view3d.bh / 2 - 6, 1.0)
                position: Qt.vector3d(-view3d.bw / 2 - 0.05, 0, 0)
                materials: DefaultMaterial { diffuseColor: "#c9ced3" }
            }
            // 侧边细微装饰槽（右）
            Model {
                source: "#Cube"
                scale: Qt.vector3d(0.15, view3d.bh / 2 - 6, 1.0)
                position: Qt.vector3d(view3d.bw / 2 + 0.05, 0, 0)
                materials: DefaultMaterial { diffuseColor: "#c9ced3" }
            }

            // ===== 顶部控制面板 =====
            Model {
                source: "#Cube"
                scale: Qt.vector3d(view3d.bw / 2 - 0.5,
                                   view3d.bh * 0.08,
                                   view3d.bd / 2 - 0.5)
                position: Qt.vector3d(0, view3d.bh / 2 - view3d.bh * 0.08, 0)
                materials: DefaultMaterial {
                    diffuseColor: "#cfd5db"
                    specularColor: "#ffffff"
                    specularAmount: 0.4
                }
            }

            // 洗涤剂抽屉
            Model {
                source: "#Cube"
                scale: Qt.vector3d(10, 1.2, 0.6)
                position: Qt.vector3d(-view3d.bw / 2 + 13,
                                      view3d.bh / 2 - view3d.bh * 0.18,
                                      view3d.bd / 2 + 0.3)
                materials: DefaultMaterial { diffuseColor: "#b8bec4" }
            }

            // ===== 前门金属外框（门洞装饰环）=====
            Model {
                source: "#Cylinder"
                scale: Qt.vector3d(view3d.doorDia / 2 + 2.5,
                                   0.6,
                                   view3d.doorDia / 2 + 2.5)
                position: Qt.vector3d(0, view3d.doorCY, view3d.bd / 2 + 0.3)
                eulerRotation.x: 90
                materials: DefaultMaterial {
                    diffuseColor: "#b8bfc6"
                    specularColor: "#ffffff"
                    specularAmount: 0.6
                }
            }

            // 前门内圈深色密封环
            Model {
                source: "#Cylinder"
                scale: Qt.vector3d(view3d.doorDia / 2 + 0.5,
                                   0.5,
                                   view3d.doorDia / 2 + 0.5)
                position: Qt.vector3d(0, view3d.doorCY, view3d.bd / 2 + 0.1)
                eulerRotation.x: 90
                materials: DefaultMaterial { diffuseColor: "#2a2f35" }
            }

            // ===== 滚筒组件 =====
            Node {
                id: drumNode
                position: Qt.vector3d(0, view3d.doorCY, view3d.bd / 2 - 2)

                Node {
                    id: drumRotator
                    eulerRotation.y: root.drumAngle

                    // 滚筒筒体（半透明不锈钢，透过玻璃可见）
                    Model {
                        source: "#Cylinder"
                        scale: Qt.vector3d(view3d.drumDia / 2,
                                           view3d.drumLen / 2,
                                           view3d.drumDia / 2)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: "#aab5c2"
                            metalness: 0.92
                            roughness: 0.28
                            opacity: 0.55
                        }
                    }

                    // 滚筒后盖（不透明深灰金属）
                    Model {
                        source: "#Cylinder"
                        scale: Qt.vector3d(view3d.drumDia / 2, 0.4,
                                           view3d.drumDia / 2)
                        position: Qt.vector3d(0, 0, -view3d.drumLen / 2)
                        eulerRotation.x: 90
                        materials: DefaultMaterial {
                            diffuseColor: "#4a525a"
                            specularColor: "#cccccc"
                            specularAmount: 0.5
                        }
                    }

                    // 三片提升筋（120° 均布，内壁挂筋）
                    Repeater {
                        model: 3

                        Model {
                            id: paddle
                            required property int index

                            source: "#Cube"
                            scale: Qt.vector3d(1.2,
                                               view3d.drumLen * 0.82,
                                               3.5)
                            materials: DefaultMaterial {
                                diffuseColor: "#d0d6dc"
                                specularColor: "#ffffff"
                                specularAmount: 0.4
                            }

                            Node {
                                // 将提升筋平移到筒体内壁，再随 index*120° 旋转
                                position: Qt.vector3d(0, 0,
                                                      view3d.drumDia / 2 - 3)
                                eulerRotation.z: paddle.index * 120
                            }
                        }
                    }

                    // 滚筒排水孔（前后两圈，各 24 孔）
                    Repeater {
                        model: 48

                        Model {
                            id: hole
                            required property int index

                            readonly property bool frontRing: index < 24
                            readonly property real theta:
                                (index % 24) * (Math.PI * 2 / 24)
                            readonly property real hr: view3d.drumDia / 2 - 0.8

                            source: "#Cylinder"
                            scale: Qt.vector3d(0.55, 0.7, 0.55)
                            position: Qt.vector3d(
                                Math.cos(theta) * hr,
                                Math.sin(theta) * hr,
                                frontRing
                                    ? view3d.drumLen / 2 - 1.5
                                    : -view3d.drumLen / 2 + 1.5)
                            eulerRotation.x: 90
                            materials: DefaultMaterial {
                                diffuseColor: "#1a1e24"
                            }
                        }
                    }

                    // 中心轴承
                    Model {
                        source: "#Cylinder"
                        scale: Qt.vector3d(2.2, 1.0, 2.2)
                        eulerRotation.x: 90
                        materials: DefaultMaterial {
                            diffuseColor: "#6a737c"
                            specularColor: "#ffffff"
                            specularAmount: 0.7
                        }
                    }
                }

                // ===== 水体（半透明蓝色柱体，高度随水位变化）=====
                Model {
                    id: waterVolume

                    readonly property real maxH: view3d.drumDia - 3
                    readonly property real h: Math.max(0.1,
                                                     maxH * root.waterRatio)

                    source: "#Cylinder"
                    scale: Qt.vector3d(view3d.drumDia / 2 - 1.2,
                                       h / 2,
                                       view3d.drumLen / 2 - 2.5)
                    // 锚定到筒体底部，随 waterRatio 向上生长
                    position: Qt.vector3d(0,
                        -view3d.drumDia / 2 + h / 2 + 1.5,
                        0)
                    eulerRotation.x: 90

                    materials: PrincipledMaterial {
                        baseColor: Qt.rgba(0.18, 0.55, 0.88, 0.62)
                        metalness: 0.0
                        roughness: 0.12
                        opacity: 0.72
                        ior: 1.33
                        transmissionFactor: 0.25
                        thicknessFactor: 2.5
                        attenuationColor: Qt.rgba(0.15, 0.5, 0.8, 1.0)
                        attenuationDistance: 18
                    }

                    Behavior on scale.y {
                        NumberAnimation { duration: 320; easing.type: Easing.OutQuad }
                    }
                    Behavior on position.y {
                        NumberAnimation { duration: 320; easing.type: Easing.OutQuad }
                    }
                }
            }

            // ===== 玻璃门（可开合）=====
            // 门节点本地原点在左铰链处，旋转轴即为铰链
            Node {
                id: doorPivot
                position: Qt.vector3d(-view3d.doorDia / 2 - 1,
                                      view3d.doorCY,
                                      view3d.bd / 2 + 0.4)

                property real openAngle: root.doorOpen ? 52 : 0
                Behavior on openAngle {
                    NumberAnimation { duration: 650; easing.type: Easing.InOutCubic }
                }
                eulerRotation.y: doorPivot.openAngle

                // 门体向右延伸（铰链在左，门板向右展开）
                Node {
                    position: Qt.vector3d(view3d.doorDia / 2 + 1, 0, 0)

                    // 玻璃（半透明淡蓝，PBR 透光）
                    Model {
                        source: "#Cylinder"
                        scale: Qt.vector3d(view3d.doorDia / 2, 0.7,
                                           view3d.doorDia / 2)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: Qt.rgba(0.75, 0.90, 1.0, 0.28)
                            metalness: 0.0
                            roughness: 0.04
                            opacity: 0.28
                            ior: 1.52
                            transmissionFactor: 0.85
                            thicknessFactor: 0.5
                        }
                    }

                    // 门外金属框
                    Model {
                        source: "#Cylinder"
                        scale: Qt.vector3d(view3d.doorDia / 2 + 1.2, 0.35,
                                           view3d.doorDia / 2 + 1.2)
                        position: Qt.vector3d(0, 0, 0.8)
                        eulerRotation.x: 90
                        materials: DefaultMaterial {
                            diffuseColor: "#9fa7ae"
                            specularColor: "#ffffff"
                            specularAmount: 0.7
                        }
                    }

                    // 玻璃反光条（高光长条，增强玻璃质感）
                    Model {
                        source: "#Cube"
                        scale: Qt.vector3d(3.5, view3d.doorDia * 0.32, 0.15)
                        position: Qt.vector3d(-view3d.doorDia * 0.18,
                                              view3d.doorDia * 0.1, 1.2)
                        eulerRotation.z: -22
                        materials: DefaultMaterial {
                            diffuseColor: "#ffffff"
                            opacity: 0.35
                        }
                    }

                    // 门把手
                    Model {
                        source: "#Cube"
                        scale: Qt.vector3d(1.4, 6, 1.0)
                        position: Qt.vector3d(view3d.doorDia / 2 - 1, 0, 1.6)
                        materials: DefaultMaterial {
                            diffuseColor: "#8c949c"
                            specularColor: "#ffffff"
                            specularAmount: 0.6
                        }
                    }
                }
            }

            // ===== 顶部控制面板上的 LED 指示灯 =====
            Node {
                position: Qt.vector3d(0,
                                      view3d.bh / 2 - view3d.bh * 0.08,
                                      view3d.bd / 2)

                Repeater {
                    model: [
                        { key: "run",   color: "#4caf50", x: -22 },
                        { key: "inlet", color: "#2196f3", x: -10 },
                        { key: "heat",  color: "#f44336", x:   2 },
                        { key: "lock",  color: "#ffc107", x:  14 }
                    ]

                    Node {
                        required property var modelData
                        readonly property bool lit: root.lampOn(modelData.key)

                        position: Qt.vector3d(modelData.x, 3, 0.5)

                        Model {
                            source: "#Sphere"
                            scale: Qt.vector3d(1.1, 1.1, 1.1)
                            materials: PrincipledMaterial {
                                baseColor: lit
                                    ? Qt.lighter(modelData.color, 1.2)
                                    : Qt.darker(modelData.color, 2.0)
                                emissiveColor: lit
                                    ? modelData.color
                                    : Qt.rgba(0, 0, 0, 1)
                                emissiveIntensity: lit ? 3.5 : 0
                                metalness: 0.0
                                roughness: 0.35
                            }
                        }
                    }
                }
            }

            // ===== 底脚 =====
            Repeater {
                model: [
                    { x: -view3d.bw / 2 + 4, z:  view3d.bd / 2 - 4 },
                    { x:  view3d.bw / 2 - 4, z:  view3d.bd / 2 - 4 },
                    { x: -view3d.bw / 2 + 4, z: -view3d.bd / 2 + 4 },
                    { x:  view3d.bw / 2 - 4, z: -view3d.bd / 2 + 4 }
                ]

                Model {
                    required property var modelData
                    source: "#Cylinder"
                    scale: Qt.vector3d(2.2, 0.8, 2.2)
                    position: Qt.vector3d(modelData.x,
                                          -view3d.bh / 2 - 0.4,
                                          modelData.z)
                    eulerRotation.x: 90
                    materials: DefaultMaterial {
                        diffuseColor: "#3a3f45"
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // 鼠标交互：拖拽旋转 / 滚轮缩放 / 双击复位
    // ---------------------------------------------------------------
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        z: -1

        onPositionChanged: (mouse) => {
            if (pressed) {
                var dx = mouse.x - root.lastX;
                var dy = mouse.y - root.lastY;
                if (mouse.buttons & Qt.LeftButton) {
                    root.camAzimuth   -= dx * 0.5;
                    root.camElevation  = Math.max(-75, Math.min(75,
                                          root.camElevation + dy * 0.4));
                } else if (mouse.buttons & Qt.RightButton) {
                    // 右键拖拽 = 平滑缩放
                    root.camDistance = Math.max(100, Math.min(480,
                                         root.camDistance - dy * 1.2));
                }
            }
            root.lastX = mouse.x;
            root.lastY = mouse.y;
        }

        onWheel: (wheel) => {
            root.camDistance = Math.max(100, Math.min(480,
                                 root.camDistance - wheel.angleDelta.y / 10));
        }

        onDoubleClicked: {
            resetAnim.restart();
        }
    }

    SequentialAnimation {
        id: resetAnim
        NumberAnimation { target: root; property: "camAzimuth";
                          to: 30; duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camElevation";
                          to: 12; duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camDistance";
                          to: 220; duration: 400; easing.type: Easing.OutQuad }
    }

    // ---------------------------------------------------------------
    // 2D 信息叠层（左上角读数 + 右下角操控提示）
    // ---------------------------------------------------------------
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: col.implicitWidth + 22
        height: col.implicitHeight + 14
        radius: 8
        color: "#1b2430"
        opacity: 0.88
        border.width: 1
        border.color: "#2d3844"

        ColumnLayout {
            id: col
            anchors.centerIn: parent
            spacing: 2

            Text {
                text: root.engine ? root.engine.stateText : "--"
                color: "#7fdcff"
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                text: root.engine
                      ? root.engine.tempModel.temperature.toFixed(0) + " °C    "
                        + Math.abs(root.speedRpm).toFixed(0) + " rpm"
                      : "--"
                color: "#4fc3f7"
                font.pixelSize: 11
            }
            Text {
                text: root.engine
                      ? qsTr("水位 ")
                        + root.engine.waterModel.waterLevel.toFixed(0) + " mm    "
                        + qsTr("水量 ")
                        + root.engine.waterModel.volume.toFixed(1) + " L"
                      : "--"
                color: "#8ab4c8"
                font.pixelSize: 11
            }
        }
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        text: qsTr("左键拖拽旋转   滚轮缩放   双击复位")
        color: "#5a636c"
        font.pixelSize: 11
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        text: qsTr("洗衣机 3D 运行视图")
        color: root.colorMuted
        font.pixelSize: 12
    }
}
