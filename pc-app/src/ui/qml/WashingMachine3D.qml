import QtQuick
import QtQuick3D
import HIL.Models 1.0

/*!
    洗衣机 3D 可视化视图（Qt Quick 3D）。

    坐标与尺寸约定
    --------------
    世界单位 = 毫米，箱体中心位于原点。Qt Quick 3D 内置图元（#Cube/#Cylinder/
    #Sphere/#Rectangle）的原始边长为 100 单位，因此所有 Model 的 scale 必须通过
    dim() 把毫米换算成缩放系数，position 则直接使用毫米。

    材质与光照
    ----------
    机身统一使用 PrincipledMaterial 模拟家电烤漆：baseColor 避开纯白以防高光过曝，
    各面板用微小色差制造明暗层次。场景采用 Filmic 色调映射压制高光，主光负责造型与
    投影，另有天光、轮廓光两盏弱填充光，滚筒内部单独布一盏点光源保证透过玻璃可见。

    贴纸
    ----
    显示屏、旋钮刻度盘、品牌铭牌、能效标签等均由 Texture.sourceItem 把 QML 绘制内容
    转为纹理，贴在 #Rectangle 面片上。Texture 的 flipU/flipV 对 sourceItem 纹理不足以
    纠正方向（实测：单设 flipV 为左右镜像，flipU + flipV 为上下颠倒，单设 flipU 仍为
    左右镜像），因此改由 StickerCanvas 在 2D 侧做一次行为确定的水平翻转来抵消镜像。
    显示屏使用 NoLighting 呈现自发光效果并绑定实时数据。

    结构对应真实滚筒洗衣机
      - 五面板围成的机身（顶/底/左/右/后），正面留出方形开口
      - 正面板四块围出开口，再由圆环段修饰成圆形门洞
      - 门洞内黑色橡胶密封套筒，后方是不锈钢滚筒（提升筋 + 排水孔）
      - 滚筒内水位随 waterModel.waterLevel 变化
      - 舱门常闭（门锁模型不含开合语义），滚筒随 motorModel.speedRpm 旋转
      - 顶部凸出控制面板（显示屏 / 旋钮 / 四枚指示灯）+ 洗涤剂抽屉

    视角：左键拖拽环绕 / 滚轮或右键拖拽缩放 / 双击复位。
*/
Item {
    id: root

    required property var engine

    readonly property color colorMuted: "#9e9e9e"

    readonly property real fullLevelMm: 500
    readonly property real waterRatio: engine
        ? Math.max(0, Math.min(1, engine.waterModel.waterLevel / fullLevelMm)) : 0
    readonly property real speedRpm: engine ? engine.motorModel.speedRpm : 0

    property real drumAngle: 0
    property real camAzimuth: 38
    property real camElevation: 14
    property real camDistance: 1750
    property real lastX: 0
    property real lastY: 0

    /*!
        贴纸画布。sourceItem 贴到 #Rectangle 上会左右镜像，这里在 2D 侧预先做一次
        水平翻转把它抵消掉，于是贴纸内容按正常阅读方向书写即可。
    */
    component StickerCanvas: Item {
        default property alias content: mirror.data

        Item {
            id: mirror
            anchors.fill: parent
            transform: Scale { xScale: -1; origin.x: mirror.width / 2 }
        }
    }

    Timer {
        interval: 16
        running: root.visible && (root.engine ? root.engine.running : false)
        repeat: true
        onTriggered: {
            var deltaDeg = root.speedRpm * 6 * 0.016;
            root.drumAngle = (root.drumAngle
                              + Math.max(-24, Math.min(24, deltaDeg))) % 360;
        }
    }

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
        environment: env

        //! 毫米尺寸 → 图元缩放系数（内置图元原始边长为 100 单位）
        function dim(w, h, d) {
            return Qt.vector3d(w / 100, h / 100, d / 100);
        }

        // ---------- 机身尺寸（毫米）----------
        readonly property real bw: 600          // 箱体宽
        readonly property real bh: 850          // 箱体高
        readonly property real bd: 560          // 箱体深
        readonly property real wall: 20         // 板厚
        readonly property real frontZ: bd / 2   // 正面外表面
        readonly property real panelZ: frontZ - wall / 2
        readonly property real stickerZ: frontZ + 1   // 正面贴纸所在平面

        readonly property real holeCY: -30      // 门洞中心高度
        readonly property real holeHalf: 200    // 正面方形开口半边长
        readonly property real ringIn: 175      // 圆环内径 = 可见门洞半径
        readonly property real ringOut: 292     // 圆环外径，需盖住方口四角
        readonly property real doorR: 250       // 舱门外环半径，须小于机身半宽

        readonly property real drumR: 225       // 滚筒半径
        readonly property real drumLen: 380     // 滚筒轴向长度
        readonly property real drumCZ: 30       // 滚筒中心 Z

        // 控制面板：中心高度与前表面
        readonly property real ctrlCY: bh / 2 - 95
        readonly property real ctrlCZ: frontZ - 10
        readonly property real ctrlFace: 41     // 面板节点内贴纸的相对 Z

        // ---------- 机身漆面色（避开纯白，防高光过曝）----------
        readonly property color caseFront: "#e7ebef"
        readonly property color caseSide: "#dee4ea"
        readonly property color caseTop: "#eef1f5"
        readonly property color caseBack: "#d3d9e0"
        readonly property color caseBottom: "#c7ced6"
        readonly property color ctrlColor: "#e2e7ed"

        SceneEnvironment {
            id: env
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            backgroundMode: SceneEnvironment.Color
            clearColor: "#3b4450"
            // 胶片色调映射：把白色漆面的镜面高光压回可分辨的灰阶
            tonemapMode: SceneEnvironment.TonemapModeFilmic
        }

        // ---------------------------------------------------------
        // 贴纸纹理（QML 绘制 → 纹理）
        // ---------------------------------------------------------

        //! 显示屏：自发光，绑定实时温度 / 转速 / 状态
        Texture {
            id: displayTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 460
                height: 116

                Rectangle {
                    anchors.fill: parent
                    color: "#04090f"
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    color: "#071722"
                    border.width: 1
                    border.color: "#0f3546"
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 26

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.engine
                              ? root.engine.tempModel.temperature.toFixed(0) + "°"
                              : "--°"
                        color: "#35dcff"
                        font.family: "Consolas"
                        font.pixelSize: 56
                        font.bold: true
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: 56
                        color: "#12414f"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.abs(root.speedRpm).toFixed(0)
                        color: "#35dcff"
                        font.family: "Consolas"
                        font.pixelSize: 56
                        font.bold: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "rpm"
                        color: "#1c93b4"
                        font.family: "Consolas"
                        font.pixelSize: 26
                    }
                }
            }
        }

        //! 指示灯文字标签（面板底色，方形边界不可见）
        Texture {
            id: lampLabelTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 600
                height: 68

                Rectangle {
                    anchors.fill: parent
                    color: view3d.ctrlColor
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 34

                    Repeater {
                        model: [qsTr("运行"), qsTr("进水"),
                                qsTr("加热"), qsTr("门锁")]

                        Text {
                            width: 100
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData
                            color: "#4a545f"
                            font.pixelSize: 30
                            font.bold: true
                        }
                    }
                }
            }
        }

        //! 程序旋钮刻度盘（面板底色，中心被旋钮遮住）
        Texture {
            id: dialTex
            flipU: true
            sourceItem: StickerCanvas {
                id: dialItem
                width: 340
                height: 340

                Rectangle {
                    anchors.fill: parent
                    color: view3d.ctrlColor
                }

                // 旋钮外圈浅凹槽
                Rectangle {
                    anchors.centerIn: parent
                    width: 190
                    height: 190
                    radius: width / 2
                    color: "transparent"
                    border.width: 3
                    border.color: "#b9c2cb"
                }

                // 程序刻度与标签
                Repeater {
                    model: [qsTr("棉麻"), qsTr("混纺"), qsTr("羊毛"),
                            qsTr("快洗"), qsTr("漂洗"), qsTr("脱水")]

                    Item {
                        required property int index
                        required property string modelData
                        readonly property real ang: -125 + index * 50
                        readonly property real rad: ang * Math.PI / 180

                        x: dialItem.width / 2 - width / 2
                           + Math.sin(rad) * 132
                        y: dialItem.height / 2 - height / 2
                           - Math.cos(rad) * 132
                        width: 76
                        height: 30

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: "#4a545f"
                            font.pixelSize: 24
                            font.bold: true
                        }
                    }
                }

                // 刻度点
                Repeater {
                    model: 6

                    Rectangle {
                        required property int index
                        readonly property real rad:
                            (-125 + index * 50) * Math.PI / 180

                        x: dialItem.width / 2 - 4 + Math.sin(rad) * 104
                        y: dialItem.height / 2 - 4 - Math.cos(rad) * 104
                        width: 8
                        height: 8
                        radius: 4
                        color: "#78828d"
                    }
                }
            }
        }

        //! 控制面板品牌铭牌
        Texture {
            id: brandTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 340
                height: 100

                Rectangle {
                    anchors.fill: parent
                    color: view3d.ctrlColor
                }

                Text {
                    anchors.centerIn: parent
                    text: "Hisense"
                    color: "#3d4650"
                    font.pixelSize: 52
                    font.bold: true
                    font.letterSpacing: 2
                }
            }
        }

        //! 正面型号铭牌
        Texture {
            id: modelTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 420
                height: 92

                Rectangle {
                    anchors.fill: parent
                    color: view3d.caseFront
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "XQG100-U1"
                        color: "#525c67"
                        font.pixelSize: 38
                        font.bold: true
                        font.letterSpacing: 1
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("8.0 kg   1400 rpm")
                        color: "#7c8691"
                        font.pixelSize: 24
                    }
                }
            }
        }

        //! 正面左下能效标签
        Texture {
            id: energyTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 220
                height: 300

                Rectangle {
                    anchors.fill: parent
                    color: "#f7f9fb"
                    border.width: 2
                    border.color: "#c3ccd5"
                }

                Column {
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("能效等级")
                        color: "#2c3540"
                        font.pixelSize: 24
                        font.bold: true
                    }

                    Repeater {
                        model: [
                            { t: "A+++", c: "#0f8a3c" },
                            { t: "A++",  c: "#4caf50" },
                            { t: "A+",   c: "#8bc34a" },
                            { t: "A",    c: "#cddc39" },
                            { t: "B",    c: "#ffc107" },
                            { t: "C",    c: "#ff9800" }
                        ]

                        Rectangle {
                            required property var modelData
                            required property int index

                            width: 150 + index * 8
                            height: 30
                            color: modelData.c
                            radius: 3

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.t
                                color: "#ffffff"
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }
                    }
                }

                // 当前等级指针
                Rectangle {
                    x: 168
                    y: 46
                    width: 44
                    height: 30
                    color: "#0f8a3c"

                    Text {
                        anchors.centerIn: parent
                        text: "A+++"
                        color: "#ffffff"
                        font.pixelSize: 17
                        font.bold: true
                    }
                }
            }
        }

        //! 门圈下方容量提示贴纸
        Texture {
            id: capacityTex
            flipU: true
            sourceItem: StickerCanvas {
                width: 260
                height: 120

                Rectangle {
                    anchors.fill: parent
                    color: view3d.caseFront
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("变频直驱")
                        color: "#5b656f"
                        font.pixelSize: 30
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("蒸汽除菌")
                        color: "#8e98a3"
                        font.pixelSize: 24
                    }
                }
            }
        }

        // ---------------------------------------------------------
        // 灯光
        // ---------------------------------------------------------

        //! 主光：负责整体造型与投影
        DirectionalLight {
            eulerRotation: Qt.vector3d(-34, -30, 0)
            brightness: 1.05
            castsShadow: true
            shadowFactor: 62
            shadowMapQuality: Light.ShadowMapQualityHigh
        }
        //! 天光填充：压低顶面与正面的明暗反差
        DirectionalLight {
            eulerRotation: Qt.vector3d(-68, 24, 0)
            color: "#dae5f6"
            brightness: 0.38
        }
        //! 后侧轮廓光：勾出机身右侧边缘，避免与背景糊在一起
        DirectionalLight {
            eulerRotation: Qt.vector3d(12, 198, 0)
            color: "#e6edf8"
            brightness: 0.3
        }
        //! 滚筒内部照明：保证透过玻璃能看清筒壁与提升筋
        PointLight {
            position: Qt.vector3d(0, view3d.holeCY + 60, view3d.drumCZ + 130)
            color: "#eaf4ff"
            brightness: 14
            constantFade: 1.0
            linearFade: 0.28
            quadraticFade: 0.0
        }

        // ---------- 环绕相机 ----------
        readonly property vector3d pivot: Qt.vector3d(0, 0, 0)
        readonly property real azRad: root.camAzimuth * Math.PI / 180
        readonly property real elRad: root.camElevation * Math.PI / 180

        PerspectiveCamera {
            id: camera
            fieldOfView: 42
            clipNear: 10
            clipFar: 30000
            position: Qt.vector3d(
                view3d.pivot.x + root.camDistance * Math.cos(view3d.elRad)
                                * Math.sin(view3d.azRad),
                view3d.pivot.y + root.camDistance * Math.sin(view3d.elRad),
                view3d.pivot.z + root.camDistance * Math.cos(view3d.elRad)
                                * Math.cos(view3d.azRad))
            rotation: Quaternion.fromEulerAngles(
                Qt.vector3d(-root.camElevation, root.camAzimuth, 0))
        }

        Node {
            id: machine

            // ===== 地面 =====
            Model {
                source: "#Rectangle"
                scale: view3d.dim(4200, 4200, 100)
                position: Qt.vector3d(0, -view3d.bh / 2 - 42, 0)
                eulerRotation.x: -90
                materials: PrincipledMaterial {
                    baseColor: "#3f4750"
                    metalness: 0.0
                    roughness: 0.62
                }
            }

            // ===== 机身外壳：五块板围成前部开口的箱体 =====
            // 后板
            Model {
                source: "#Cube"
                scale: view3d.dim(view3d.bw, view3d.bh, view3d.wall)
                position: Qt.vector3d(0, 0, -view3d.bd / 2 + view3d.wall / 2)
                materials: PrincipledMaterial {
                    baseColor: view3d.caseBack
                    metalness: 0.08
                    roughness: 0.52
                }
            }

            // 顶板
            Model {
                source: "#Cube"
                scale: view3d.dim(view3d.bw, view3d.wall, view3d.bd)
                position: Qt.vector3d(0, view3d.bh / 2 - view3d.wall / 2, 0)
                materials: PrincipledMaterial {
                    baseColor: view3d.caseTop
                    metalness: 0.1
                    roughness: 0.3
                }
            }

            // 底板
            Model {
                source: "#Cube"
                scale: view3d.dim(view3d.bw, view3d.wall, view3d.bd)
                position: Qt.vector3d(0, -view3d.bh / 2 + view3d.wall / 2, 0)
                materials: PrincipledMaterial {
                    baseColor: view3d.caseBottom
                    metalness: 0.05
                    roughness: 0.6
                }
            }

            // 左右侧板
            Repeater3D {
                model: [-1, 1]

                Model {
                    required property int modelData

                    source: "#Cube"
                    scale: view3d.dim(view3d.wall, view3d.bh, view3d.bd)
                    position: Qt.vector3d(
                        modelData * (view3d.bw / 2 - view3d.wall / 2), 0, 0)
                    materials: PrincipledMaterial {
                        baseColor: view3d.caseSide
                        metalness: 0.1
                        roughness: 0.42
                    }
                }
            }

            // 顶板前沿深色饰条：切断顶面与正面的连续高光
            Model {
                source: "#Cube"
                scale: view3d.dim(view3d.bw + 4, 8, 10)
                position: Qt.vector3d(0, view3d.bh / 2 - 4,
                                      view3d.frontZ + 3)
                materials: PrincipledMaterial {
                    baseColor: "#aab3bd"
                    metalness: 0.4
                    roughness: 0.4
                }
            }

            // 正面与侧面转角接缝
            Repeater3D {
                model: [-1, 1]

                Model {
                    required property int modelData

                    source: "#Cube"
                    scale: view3d.dim(6, view3d.bh - 40, 6)
                    position: Qt.vector3d(
                        modelData * (view3d.bw / 2 - 2), 0,
                        view3d.frontZ - 2)
                    materials: PrincipledMaterial {
                        baseColor: "#b4bdc6"
                        metalness: 0.25
                        roughness: 0.5
                    }
                }
            }

            // ===== 正面板：四块围出方形开口 =====
            Node {
                position: Qt.vector3d(0, 0, view3d.panelZ)

                // 上板
                Model {
                    readonly property real top: view3d.bh / 2
                    readonly property real bottom: view3d.holeCY + view3d.holeHalf

                    source: "#Cube"
                    scale: view3d.dim(view3d.bw, top - bottom, view3d.wall)
                    position: Qt.vector3d(0, (top + bottom) / 2, 0)
                    materials: PrincipledMaterial {
                        baseColor: view3d.caseFront
                        metalness: 0.1
                        roughness: 0.38
                    }
                }

                // 下板
                Model {
                    readonly property real top: view3d.holeCY - view3d.holeHalf
                    readonly property real bottom: -view3d.bh / 2

                    source: "#Cube"
                    scale: view3d.dim(view3d.bw, top - bottom, view3d.wall)
                    position: Qt.vector3d(0, (top + bottom) / 2, 0)
                    materials: PrincipledMaterial {
                        baseColor: view3d.caseFront
                        metalness: 0.1
                        roughness: 0.38
                    }
                }

                // 左右板
                Repeater3D {
                    model: [-1, 1]

                    Model {
                        required property int modelData
                        readonly property real w: view3d.bw / 2 - view3d.holeHalf

                        source: "#Cube"
                        scale: view3d.dim(w, view3d.holeHalf * 2, view3d.wall)
                        position: Qt.vector3d(
                            modelData * (view3d.holeHalf + w / 2),
                            view3d.holeCY, 0)
                        materials: PrincipledMaterial {
                            baseColor: view3d.caseFront
                            metalness: 0.1
                            roughness: 0.38
                        }
                    }
                }

                // 圆环段：把方形开口修饰成圆形门洞（32 段径向块拼合）
                Repeater3D {
                    model: 32

                    Node {
                        required property int index

                        position: Qt.vector3d(0, view3d.holeCY, 0)
                        eulerRotation.z: index * 360 / 32

                        Model {
                            readonly property real rMid:
                                (view3d.ringIn + view3d.ringOut) / 2
                            readonly property real segW:
                                2 * rMid * Math.tan(Math.PI / 32) * 1.3

                            source: "#Cube"
                            scale: view3d.dim(segW,
                                              view3d.ringOut - view3d.ringIn,
                                              view3d.wall)
                            position: Qt.vector3d(0, rMid, 0)
                            materials: PrincipledMaterial {
                                baseColor: view3d.caseFront
                                metalness: 0.1
                                roughness: 0.38
                            }
                        }
                    }
                }

                // 门洞外沿金属装饰圈
                Model {
                    source: "#Cylinder"
                    scale: view3d.dim(view3d.ringIn * 2 + 26, view3d.wall + 8,
                                      view3d.ringIn * 2 + 26)
                    position: Qt.vector3d(0, view3d.holeCY, 4)
                    eulerRotation.x: 90
                    materials: PrincipledMaterial {
                        baseColor: "#8d959e"
                        metalness: 0.75
                        roughness: 0.35
                    }
                }
            }

            // ===== 正面贴纸 =====
            // 型号铭牌
            Model {
                source: "#Rectangle"
                scale: view3d.dim(210, 46, 1)
                position: Qt.vector3d(170, 235, view3d.stickerZ)
                materials: PrincipledMaterial {
                    baseColorMap: modelTex
                    metalness: 0.0
                    roughness: 0.55
                }
            }

            // 能效标签
            Model {
                source: "#Rectangle"
                scale: view3d.dim(110, 150, 1)
                position: Qt.vector3d(-215, -320, view3d.stickerZ)
                materials: PrincipledMaterial {
                    baseColorMap: energyTex
                    metalness: 0.0
                    roughness: 0.45
                }
            }

            // 卖点贴纸
            Model {
                source: "#Rectangle"
                scale: view3d.dim(130, 60, 1)
                position: Qt.vector3d(190, -330, view3d.stickerZ)
                materials: PrincipledMaterial {
                    baseColorMap: capacityTex
                    metalness: 0.0
                    roughness: 0.55
                }
            }

            // ===== 洗涤剂抽屉 =====
            Node {
                position: Qt.vector3d(-160, 217, view3d.frontZ + 15)

                Model {
                    source: "#Cube"
                    scale: view3d.dim(260, 88, 30)
                    materials: PrincipledMaterial {
                        baseColor: "#eaeef3"
                        metalness: 0.1
                        roughness: 0.34
                    }
                }

                // 把手凹槽
                Model {
                    source: "#Cube"
                    scale: view3d.dim(230, 16, 8)
                    position: Qt.vector3d(0, -22, 14)
                    materials: PrincipledMaterial {
                        baseColor: "#8e97a1"
                        metalness: 0.35
                        roughness: 0.55
                    }
                }

                // 上沿接缝
                Model {
                    source: "#Cube"
                    scale: view3d.dim(260, 4, 32)
                    position: Qt.vector3d(0, 44, 0)
                    materials: PrincipledMaterial {
                        baseColor: "#a9b2bc"
                        metalness: 0.2
                        roughness: 0.6
                    }
                }
            }

            // ===== 门洞密封套筒 =====
            // 用 24 段 Cube 拼成真正中空的管壁。实心 #Cylinder 自带封闭端面，
            // 一旦不透明就会把整个门洞糊成黑圆盘，滚筒完全不可见。
            Node {
                id: bootNode

                readonly property real len: view3d.frontZ - view3d.drumCZ
                                            - view3d.drumLen / 2 + 30

                position: Qt.vector3d(0, view3d.holeCY,
                                      view3d.frontZ - bootNode.len / 2)

                Repeater3D {
                    model: 24

                    Node {
                        required property int index

                        eulerRotation.z: index * 360 / 24

                        Model {
                            readonly property real rMid: view3d.ringIn + 6
                            readonly property real segW:
                                2 * rMid * Math.tan(Math.PI / 24) * 1.3

                            source: "#Cube"
                            scale: view3d.dim(segW, 12, bootNode.len)
                            position: Qt.vector3d(0, rMid, 0)
                            materials: PrincipledMaterial {
                                baseColor: "#20252c"
                                metalness: 0.0
                                roughness: 0.9
                            }
                        }
                    }
                }
            }

            // ===== 舱门 =====
            // DoorLockModel 只描述 PTC 门锁的上锁/解锁，并不含门体开合语义（Unlocked
            // 仅表示锁未锁上，不等于门敞开），因此门保持关闭，锁定状态由面板上的门锁
            // 指示灯呈现。铰链节点与动画保留，将来若引入真正的开门量，驱动 openAngle 即可。
            Node {
                id: doorPivot
                position: Qt.vector3d(-view3d.doorR - 8, view3d.holeCY,
                                      view3d.frontZ + 12)

                property real openAngle: 0
                Behavior on openAngle {
                    NumberAnimation { duration: 700; easing.type: Easing.InOutCubic }
                }
                eulerRotation.y: doorPivot.openAngle

                Node {
                    id: doorBody
                    position: Qt.vector3d(view3d.doorR + 8, 0, 0)

                    // 金属外环
                    Model {
                        source: "#Cylinder"
                        scale: view3d.dim(view3d.doorR * 2, 26,
                                          view3d.doorR * 2)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: "#aab2bb"
                            metalness: 0.88
                            roughness: 0.24
                        }
                    }

                    // 黑色密封圈
                    Model {
                        source: "#Cylinder"
                        scale: view3d.dim(view3d.ringIn * 2 + 40, 22,
                                          view3d.ringIn * 2 + 40)
                        position: Qt.vector3d(0, 0, 16)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: "#1b1f25"
                            metalness: 0.0
                            roughness: 0.88
                        }
                    }

                    // 玻璃观察窗（低不透明度，内部滚筒清晰可见）
                    Model {
                        source: "#Cylinder"
                        scale: view3d.dim(view3d.ringIn * 2 - 12, 14,
                                          view3d.ringIn * 2 - 12)
                        position: Qt.vector3d(0, 0, 24)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: Qt.rgba(0.72, 0.86, 0.98, 1.0)
                            roughness: 0.04
                            metalness: 0.0
                            opacity: 0.12
                            alphaMode: PrincipledMaterial.Blend
                        }
                    }

                    // 玻璃斜向高光条
                    Model {
                        source: "#Cube"
                        scale: view3d.dim(40, view3d.ringIn * 1.0, 3)
                        position: Qt.vector3d(-view3d.ringIn * 0.36,
                                              view3d.ringIn * 0.24, 33)
                        eulerRotation.z: -28
                        materials: PrincipledMaterial {
                            baseColor: "#ffffff"
                            roughness: 0.02
                            opacity: 0.22
                            alphaMode: PrincipledMaterial.Blend
                        }
                    }

                    // 玻璃边缘细高光
                    Model {
                        source: "#Cube"
                        scale: view3d.dim(16, view3d.ringIn * 0.6, 3)
                        position: Qt.vector3d(view3d.ringIn * 0.42,
                                              -view3d.ringIn * 0.3, 33)
                        eulerRotation.z: -20
                        materials: PrincipledMaterial {
                            baseColor: "#ffffff"
                            roughness: 0.02
                            opacity: 0.12
                            alphaMode: PrincipledMaterial.Blend
                        }
                    }

                    // 门把手（右侧）
                    Model {
                        source: "#Cube"
                        scale: view3d.dim(34, 130, 34)
                        position: Qt.vector3d(view3d.doorR + 6, 0, 18)
                        materials: PrincipledMaterial {
                            baseColor: "#9ca4ad"
                            metalness: 0.7
                            roughness: 0.3
                        }
                    }
                }
            }

            // ===== 滚筒 =====
            Node {
                id: drumNode
                position: Qt.vector3d(0, view3d.holeCY, view3d.drumCZ)

                Node {
                    id: drumSpin
                    eulerRotation.z: root.drumAngle

                    // 筒壁：32 段 Cube 拼成中空不锈钢圆筒。
                    // 实心 #Cylinder 自带前端面，会把门洞挡成一块背光的黑盘。
                    Repeater3D {
                        model: 32

                        Node {
                            required property int index

                            eulerRotation.z: index * 360 / 32

                            Model {
                                readonly property real rMid: view3d.drumR - 6
                                readonly property real segW:
                                    2 * rMid * Math.tan(Math.PI / 32) * 1.3

                                source: "#Cube"
                                scale: view3d.dim(segW, 12, view3d.drumLen)
                                position: Qt.vector3d(0, rMid, 0)
                                materials: PrincipledMaterial {
                                    baseColor: "#c6ced7"
                                    metalness: 0.75
                                    roughness: 0.25
                                }
                            }
                        }
                    }

                    // 后盖
                    Model {
                        source: "#Cylinder"
                        scale: view3d.dim(view3d.drumR * 2, 16,
                                          view3d.drumR * 2)
                        position: Qt.vector3d(0, 0, -view3d.drumLen / 2)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: "#69727c"
                            metalness: 0.7
                            roughness: 0.4
                        }
                    }

                    // 三根提升筋（120° 均布）
                    Repeater3D {
                        model: 3

                        Node {
                            required property int index

                            eulerRotation.z: index * 120

                            Model {
                                source: "#Cube"
                                scale: view3d.dim(52, 74, view3d.drumLen * 0.8)
                                position: Qt.vector3d(0, view3d.drumR - 40, 0)
                                materials: PrincipledMaterial {
                                    baseColor: "#dfe4e9"
                                    metalness: 0.55
                                    roughness: 0.3
                                }
                            }
                        }
                    }

                    // 排水孔（前后各一圈，每圈 24 个）
                    Repeater3D {
                        model: 48

                        Model {
                            required property int index

                            readonly property bool front: index < 24
                            readonly property real a: (index % 24) * Math.PI / 12
                            readonly property real r: view3d.drumR - 8

                            source: "#Cylinder"
                            scale: view3d.dim(16, 26, 16)
                            position: Qt.vector3d(
                                Math.cos(a) * r,
                                Math.sin(a) * r,
                                front ? view3d.drumLen / 2 - 40
                                      : -view3d.drumLen / 2 + 40)
                            eulerRotation.x: 90
                            materials: PrincipledMaterial {
                                baseColor: "#12151a"
                                metalness: 0.2
                                roughness: 0.8
                            }
                        }
                    }

                    // 后部轴承座
                    Model {
                        source: "#Cylinder"
                        scale: view3d.dim(70, 30, 70)
                        position: Qt.vector3d(0, 0, -view3d.drumLen / 2 - 18)
                        eulerRotation.x: 90
                        materials: PrincipledMaterial {
                            baseColor: "#868e97"
                            metalness: 0.85
                            roughness: 0.28
                        }
                    }
                }

                // 水体（不随滚筒旋转；绕 X 旋转后 scale.z 控制竖向高度）
                Model {
                    readonly property real maxH: view3d.drumR * 2 - 40
                    readonly property real h: Math.max(2, maxH * root.waterRatio)

                    source: "#Cylinder"
                    scale: view3d.dim(view3d.drumR * 2 - 26,
                                      view3d.drumLen - 50, h)
                    position: Qt.vector3d(0, -view3d.drumR + 20 + h / 2, 0)
                    eulerRotation.x: 90
                    materials: PrincipledMaterial {
                        baseColor: Qt.rgba(0.12, 0.5, 0.85, 1.0)
                        roughness: 0.06
                        metalness: 0.0
                        opacity: 0.68
                        alphaMode: PrincipledMaterial.Blend
                    }

                    Behavior on scale.z {
                        NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
                    }
                    Behavior on position.y {
                        NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
                    }
                }
            }

            // ===== 顶部控制面板（向前凸出）=====
            Node {
                position: Qt.vector3d(0, view3d.ctrlCY, view3d.ctrlCZ)

                // 面板主体
                Model {
                    source: "#Cube"
                    scale: view3d.dim(view3d.bw - 30, 130, 80)
                    materials: PrincipledMaterial {
                        baseColor: view3d.ctrlColor
                        metalness: 0.12
                        roughness: 0.3
                    }
                }

                // 面板下沿深色饰条
                Model {
                    source: "#Cube"
                    scale: view3d.dim(view3d.bw - 30, 6, 84)
                    position: Qt.vector3d(0, -65, 0)
                    materials: PrincipledMaterial {
                        baseColor: "#9fa8b2"
                        metalness: 0.3
                        roughness: 0.5
                    }
                }

                // 显示屏贴纸（自发光）
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(230, 58, 1)
                    position: Qt.vector3d(-140, 20, view3d.ctrlFace)
                    materials: PrincipledMaterial {
                        baseColorMap: displayTex
                        lighting: PrincipledMaterial.NoLighting
                    }
                }

                // 显示屏黑色边框
                Model {
                    source: "#Cube"
                    scale: view3d.dim(246, 74, 6)
                    position: Qt.vector3d(-140, 20, view3d.ctrlFace - 4)
                    materials: PrincipledMaterial {
                        baseColor: "#2b3138"
                        metalness: 0.2
                        roughness: 0.6
                    }
                }

                // 指示灯文字标签贴纸
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(300, 34, 1)
                    position: Qt.vector3d(-140, -42, view3d.ctrlFace)
                    materials: PrincipledMaterial {
                        baseColorMap: lampLabelTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }

                // 品牌铭牌贴纸
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(170, 50, 1)
                    position: Qt.vector3d(45, -20, view3d.ctrlFace)
                    materials: PrincipledMaterial {
                        baseColorMap: brandTex
                        metalness: 0.0
                        roughness: 0.45
                    }
                }

                // 旋钮刻度盘贴纸
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(120, 120, 1)
                    position: Qt.vector3d(185, 0, view3d.ctrlFace)
                    materials: PrincipledMaterial {
                        baseColorMap: dialTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }

                // 程序旋钮
                Model {
                    source: "#Cylinder"
                    scale: view3d.dim(76, 34, 76)
                    position: Qt.vector3d(185, 0, view3d.ctrlFace + 16)
                    eulerRotation.x: 90
                    materials: PrincipledMaterial {
                        baseColor: "#b9c1ca"
                        metalness: 0.65
                        roughness: 0.28
                    }
                }

                // 旋钮顶面浅色盖
                Model {
                    source: "#Cylinder"
                    scale: view3d.dim(58, 8, 58)
                    position: Qt.vector3d(185, 0, view3d.ctrlFace + 34)
                    eulerRotation.x: 90
                    materials: PrincipledMaterial {
                        baseColor: "#dfe4ea"
                        metalness: 0.3
                        roughness: 0.35
                    }
                }

                // 旋钮指示刻线
                Model {
                    source: "#Cube"
                    scale: view3d.dim(7, 26, 4)
                    position: Qt.vector3d(185, 18, view3d.ctrlFace + 38)
                    materials: PrincipledMaterial {
                        baseColor: "#2f353c"
                        metalness: 0.1
                        roughness: 0.6
                    }
                }

                // 指示灯（4 枚，点亮时自发光）
                Repeater3D {
                    model: [
                        { key: "run",   color: "#4caf50", x: -215 },
                        { key: "inlet", color: "#2196f3", x: -165 },
                        { key: "heat",  color: "#f44336", x: -115 },
                        { key: "lock",  color: "#ffc107", x:  -65 }
                    ]

                    Model {
                        required property var modelData
                        readonly property bool lit: root.lampOn(modelData.key)

                        source: "#Sphere"
                        scale: view3d.dim(24, 24, 12)
                        position: Qt.vector3d(modelData.x, -16,
                                              view3d.ctrlFace + 2)
                        materials: PrincipledMaterial {
                            baseColor: lit ? modelData.color : "#767f89"
                            metalness: 0.0
                            roughness: lit ? 0.2 : 0.55
                            lighting: lit ? PrincipledMaterial.NoLighting
                                          : PrincipledMaterial.FragmentLighting
                        }
                    }
                }
            }

            // ===== 底脚（4 个）=====
            Repeater3D {
                model: [
                    { x: -view3d.bw / 2 + 55, z:  view3d.bd / 2 - 55 },
                    { x:  view3d.bw / 2 - 55, z:  view3d.bd / 2 - 55 },
                    { x: -view3d.bw / 2 + 55, z: -view3d.bd / 2 + 55 },
                    { x:  view3d.bw / 2 - 55, z: -view3d.bd / 2 + 55 }
                ]

                Model {
                    required property var modelData

                    source: "#Cylinder"
                    scale: view3d.dim(76, 40, 76)
                    position: Qt.vector3d(modelData.x,
                                          -view3d.bh / 2 - 20, modelData.z)
                    materials: PrincipledMaterial {
                        baseColor: "#2b3037"
                        metalness: 0.2
                        roughness: 0.75
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // 鼠标交互
    // ---------------------------------------------------------------
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        z: -1

        onPositionChanged: (mouse) => {
            var dx = mouse.x - root.lastX;
            var dy = mouse.y - root.lastY;
            if (mouse.buttons & Qt.LeftButton) {
                root.camAzimuth -= dx * 0.5;
                root.camElevation = Math.max(-80, Math.min(80,
                                      root.camElevation + dy * 0.4));
            } else if (mouse.buttons & Qt.RightButton) {
                root.camDistance = Math.max(320, Math.min(9000,
                                     root.camDistance - dy * 12));
            }
            root.lastX = mouse.x;
            root.lastY = mouse.y;
        }

        onPressed: (mouse) => {
            root.lastX = mouse.x;
            root.lastY = mouse.y;
        }

        onWheel: (wheel) => {
            // 按当前距离比例缩放，远近都能保持顺滑手感
            var factor = Math.pow(0.88, wheel.angleDelta.y / 120);
            root.camDistance = Math.max(320, Math.min(9000,
                                 root.camDistance * factor));
        }

        onDoubleClicked: resetAnim.restart()
    }

    SequentialAnimation {
        id: resetAnim
        NumberAnimation { target: root; property: "camAzimuth";
                          to: 38; duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camElevation";
                          to: 14; duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camDistance";
                          to: 1750; duration: 400; easing.type: Easing.OutQuad }
    }

    // ---------------------------------------------------------------
    // 2D 信息叠层
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

        Column {
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
        text: qsTr("左键拖拽旋转   滚轮/右键拖拽缩放   双击复位")
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
