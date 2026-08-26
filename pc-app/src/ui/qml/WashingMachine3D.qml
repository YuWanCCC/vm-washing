import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import HIL.Models 1.0

/*!
    洗衣机 3D 可视化视图（Qt Quick 3D）。

    模型来源
    --------
    两套方案并存，由 useExternalModel 切换（界面右上角按钮）：
      - 外部 GLB：RuntimeLoader 加载可执行目录下的 models/washing-machine.glb（由 CMake
        拷过去）。该类不支持 qrc，只能读真实文件，路径由 appDirUrl 拼出。GLB 的
        单位、原点、up 轴与材质都不能假定：按 bounds 自动等比缩放到与机身等高并
        对齐地面中心，Z-up 模型由 externalZUp 绕 X 轴 -90° 转正，整体朝向再由
        externalYaw 按 90° 步进修正；若文件不带材质，加载完成后遍历子 Model 统一
        赋上漆面材质，并在包围盒表面叠加一组贴纸。若模型里门体是独立节点
        （名字含 externalDoorNodeHint），加载后会被定位出来，按 externalDoorHinge /
        externalDoorAxis 摆动，于是与内置模型一样支持手动/自动开合；找不到则
        退化为不可动并在按钮上标出
      - 程序化模型：下方由内置图元拼装，与物理量联动（水位/转速/指示灯/显示屏）
    程序化模型的完整实现全部保留，切回后联动照常工作。

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
      - 舱门（含玻璃观察窗）可绕左侧铰链开合，由 engine.doorOpenState 控制开合
      - 滚筒随 motorModel.speedRpm 旋转
      - 顶部凸出控制面板（显示屏 / 旋钮 / 四枚指示灯）+ 洗涤剂抽屉

    视角：左键拖拽环绕 / 滚轮或右键拖拽缩放 / 双击复位。
    初始与复位的进退不写死，由 fitDistanceFor() 根据机身尺寸、复位角度与当前
    视口宽高比算出，因此换模型、改窗口大小都不需要重新调参。
*/
Item {
    id: root

    required property var engine

    readonly property color colorMuted: "#9e9e9e"

    readonly property real fullLevelMm: 500
    readonly property real waterRatio: engine
        ? Math.max(0, Math.min(1, engine.waterModel.waterLevel / fullLevelMm)) : 0
    readonly property real speedRpm: engine ? engine.motorModel.speedRpm : 0

    //! 模型来源：true 加载外部 GLB，false 使用下方代码生成的程序化模型
    property bool useExternalModel: true
    //! 外部模型绕竖轴的朝向修正（GLB 正面朝向不确定时按 90° 步进调整）。
    //! 当前模型实测正面朝向与相机相反，需横向转 180° 才能把机门朝向观察者。
    property real externalYaw: 180
    //! 外部模型是否为 Z-up（Blender 默认导出为 Z-up，需绕 X 轴 -90° 转成 Y-up，
    //! 否则模型会躺倒）。当前模型实测包围盒 600×625×840，最大维度在 Z 上，故为真。
    property bool externalZUp: true
    //! 贴纸组的换面偏移（按 90° 步进换到另一个面）
    property real stickerFaceOffset: 0
    //! 贴纸组绕模型竖轴的最终角度。贴纸组是 externalMachine 的子节点，会跟着
    //! externalYaw 一起转，所以先减去整体朝向作补偿（使贴纸总是留在朝向观察者
    //! 的正面），再叠上手动换面偏移。
    readonly property real stickerYaw: root.stickerFaceOffset - root.externalYaw
    //! 是否在外部模型上叠加贴纸
    property bool showExternalStickers: true
    //! 外部模型文件 URL。RuntimeLoader 内部经 QUrl::toLocalFile() 交由 assimp 读取，
    //! 不支持 qrc 资源，故拼接 main.cpp 注入的可执行目录前缀（CMake 已把 GLB
    //! 拷到该目录下的 models/）。
    readonly property url externalModelSource:
        appDirUrl + "models/washing-machine.glb"

    //! GLB 门体节点名关键字（不区分大小写的子串匹配，空串=不查找）。RuntimeLoader
    //! 会把 glTF 节点名写进 objectName，因此可按名字定位。前提是门体在模型里是
    //! 独立的 node；当前默认模型整台机器融成一个网格，找不到就退化为不可动。
    property string externalDoorNodeHint: "door"
    //! 铰链位置，写在门体节点自身的局部坐标系里、用 GLB 的原始单位。赋给
    //! Node.pivot 作为旋转中心；留零则绕门体节点自己的原点转。
    property vector3d externalDoorHinge: Qt.vector3d(0, 0, 0)
    //! 门体摆动轴。门体节点位于 up 轴修正层之内，局部轴仍是 GLB 的原始轴向，
    //! 所以 Z-up 模型的竖轴是 z。空串表示按 externalZUp 自动取竖轴。
    property string externalDoorAxis: ""
    //! 开门角。符号决定往哪边摆，装上真模型后若开反了取负即可。
    property real externalDoorOpenMax: 105

    readonly property string doorAxisEffective: root.externalDoorAxis.length > 0
        ? root.externalDoorAxis : (root.externalZUp ? "z" : "y")
    //! 当前模型来源下门体能否摆动（GLB 需成功定位到独立门体节点）
    readonly property bool doorAnimatable: root.useExternalModel
        ? externalMachine.doorNode !== null : true

    //! 舱门最终是否敞开。由 engine.doorOpenState 驱动（与 DOOR_1/DOOR_2 信号一致）。
    readonly property bool doorOpen: engine ? engine.doorOpenState : false

    //! 初始与复位视角。原来的 38°/14° 偏斜得过多，正面看上去是歪的；收到接近
    //! 正视，只保留一点立体感。
    readonly property real homeAzimuth: 20
    readonly property real homeElevation: 8

    property real drumAngle: 0
    property real camAzimuth: root.homeAzimuth
    property real camElevation: root.homeElevation
    //! 初始相机距离：内置模型 fitDistance 的 1.3 倍，确保外部模型加载前也能看到整体
    property real camDistance: view3d.fitDistanceBuiltin * 1.3
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

    //! 叠层上的小工具按钮（宽度随文字自适应）
    component ToolButton: Rectangle {
        id: btn

        property string label: ""
        property color labelColor: "#8ab4c8"
        signal activated()

        width: btnText.implicitWidth + 22
        height: 26
        radius: 6
        color: btnArea.containsMouse ? "#2b3a4a" : "#1b2430"
        opacity: 0.9
        border.width: 1
        border.color: "#2d3844"

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: btn.labelColor
            font.pixelSize: 11
        }

        MouseArea {
            id: btnArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: btn.activated()
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
        case "lock":  return engine.doorOpenState;  // 门锁指示灯改为门体开合状态
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

        //! 画面充满度余量（透视下近处会比正交投影略大，留三成余量防裁切并确保整体可见）
        readonly property real fitMargin: 1.5

        /*!
            把尺寸为 w×h×d 的机身完整装进画面所需的相机进退。

            不能用包围球算：600×850×625 的箱体包围球半径比半高大四成，模型只能占到
            七成画面，看起来又小又远。这里改成求箱体在屏幕上的实际铺开范围：绕竖轴
            转 az 后，宽与深各自投影出一部分到屏幕横向；俯视角 el 又把沿视线的纵深
            折进屏幕竖向。横、竖两个方向分别取所需进退，取其大者。
            fieldOfView 默认是垂直方向，横向半角需经宽高比换算。
        */
        function fitDistanceFor(w, h, d) {
            var az = Math.abs(root.homeAzimuth) * Math.PI / 180;
            var el = Math.abs(root.homeElevation) * Math.PI / 180;
            var screenW = Math.abs(w * Math.cos(az)) + Math.abs(d * Math.sin(az));
            var alongView = Math.abs(d * Math.cos(az)) + Math.abs(w * Math.sin(az));
            var screenH = h * Math.cos(el) + alongView * Math.sin(el);

            var halfV = camera.fieldOfView / 2 * Math.PI / 180;
            var aspect = view3d.height > 0 ? view3d.width / view3d.height : 1;
            var halfH = Math.atan(Math.tan(halfV) * aspect);
            return Math.max(screenH / 2 / Math.tan(halfV),
                            screenW / 2 / Math.tan(halfH)) * view3d.fitMargin;
        }

        //! 内置模型的复位进退。舱门摆开后会向左伸出机身之外，但竖向才是约束
        //! 方向，横向余量足够装下开门行程，所以直接按箱体尺寸求即可。
        readonly property real fitDistanceBuiltin:
            view3d.fitDistanceFor(view3d.bw, view3d.bh, view3d.bd)

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
        readonly property real doorOpenMax: 112 // 舱门最大开启角

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
        // 贴纸纹理（QML 绘制 → 纹理）—— 暂时屏蔽
        // ---------------------------------------------------------

        /*
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
                width: 200
                height: 100

                Rectangle {
                    anchors.fill: parent
                    color: view3d.ctrlColor
                }

                Text {
                    anchors.centerIn: parent
                    text: "Hisense"
                    color: '#1edc2b'
                    font.pixelSize: 45
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
        */

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
        //! 滚筒内部照明：保证透过玻璃能看清筒壁与提升筋（仅程序化模型需要）
        PointLight {
            position: Qt.vector3d(0, view3d.holeCY + 60, view3d.drumCZ + 130)
            color: "#eaf4ff"
            brightness: 14
            constantFade: 1.0
            linearFade: 0.28
            quadraticFade: 0.0
            visible: !root.useExternalModel
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

        // ===== 地面（两种模型方案共用）=====
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

        //! GLB 不包含任何材质（实测 materials=0 images=0），统一套一层家电烤漆，
        //! 否则会回退到默认材质而渲染成一片死黑。
        PrincipledMaterial {
            id: glbShell
            baseColor: "#e9edf1"
            metalness: 0.08
            roughness: 0.35
        }

        // ===== 方案二：加载外部 GLB 模型 =====
        // 三层嵌套，自内向外依次是：up 轴修正 → 缩放与居中 → 整体朝向旋转。
        // 必须分层：同一 Node 的变换顺序为 scale→rotate→translate，若挤在一起，
        // 旋转会作用在尚未居中的坐标上把模型甩出去。
        Node {
            id: externalMachine
            visible: root.useExternalModel
            eulerRotation.y: root.externalYaw

            readonly property vector3d bmin: glbLoader.bounds.minimum
            readonly property vector3d bmax: glbLoader.bounds.maximum

            // up 轴修正后的包围盒。绕 X 轴 -90° 把 (x,y,z) 映射为 (x,z,-y)，
            // 因此原 Z 区间成为高度，原 Y 区间取反后成为深度。
            readonly property vector3d rmin: root.externalZUp
                ? Qt.vector3d(externalMachine.bmin.x, externalMachine.bmin.z,
                              -externalMachine.bmax.y)
                : externalMachine.bmin
            readonly property vector3d rmax: root.externalZUp
                ? Qt.vector3d(externalMachine.bmax.x, externalMachine.bmax.z,
                              -externalMachine.bmin.y)
                : externalMachine.bmax

            readonly property real srcH: Math.max(0.0001,
                                                  externalMachine.rmax.y
                                                  - externalMachine.rmin.y)
            readonly property real fit: view3d.bh / externalMachine.srcH

            // 缩放后的实际外形尺寸，供相机进退与贴纸定位使用
            readonly property real fitW: (externalMachine.rmax.x
                                         - externalMachine.rmin.x)
                                        * externalMachine.fit
            readonly property real fitD: (externalMachine.rmax.z
                                         - externalMachine.rmin.z)
                                        * externalMachine.fit

            //! 刚好完整装进画面所需的相机进退（按复位视角下的屏幕投影范围求）
            readonly property real fitDistance:
                view3d.fitDistanceFor(externalMachine.fitW, view3d.bh,
                                      externalMachine.fitD)

            //! 按名字找到的门体节点（未找到为 null）
            property var doorNode: null
            //! 门体节点自带的初始欧拉角，摆动量叠加在它之上
            property vector3d doorHomeRotation: Qt.vector3d(0, 0, 0)
            //! 门体摆动量，缓动时长与内置模型保持一致
            property real doorAngle: root.doorOpen ? root.externalDoorOpenMax : 0
            Behavior on doorAngle {
                NumberAnimation { duration: 700; easing.type: Easing.InOutCubic }
            }

            /*!
                递归扫描加载出来的子树：给所有 Model 赋上漆面材质，同时按名字找出门体
                节点。RuntimeLoader 不提供覆盖材质与取子节点的接口，只能在加载完成后自己
                遍历；非 Model 节点没有 materials 属性。
                先写 doorHomeRotation 再写 doorNode：下方的 Binding 以 doorNode 为开关，
                反序会让它先拿到旧的初始角。
            */
            function scanLoaded(node) {
                for (var i = 0; i < node.children.length; ++i) {
                    var child = node.children[i];
                    if (child.materials !== undefined)
                        child.materials = [glbShell];
                    if (externalMachine.doorNode === null
                            && root.externalDoorNodeHint.length > 0
                            && child.objectName.toLowerCase().indexOf(
                                root.externalDoorNodeHint.toLowerCase()) >= 0) {
                        externalMachine.doorHomeRotation = child.eulerRotation;
                        externalMachine.doorNode = child;
                    }
                    externalMachine.scanLoaded(child);
                }
            }

            // 把铰链位置设为门体节点的旋转中心。pivot 在旋转前先把节点平移 -pivot，
            // 因此零角时不会改变外观，只改变绕什么转。
            Binding {
                target: externalMachine.doorNode
                when: externalMachine.doorNode !== null
                property: "pivot"
                value: root.externalDoorHinge
                restoreMode: Binding.RestoreNone
            }

            // 摆动量只加在选定的那根轴上。这是相加而不是真正的旋转叠加，仅当门体
            // 节点自带旋转为零、或本就绕同一轴时等价；对正常导出的门体这一前提成立。
            Binding {
                target: externalMachine.doorNode
                when: externalMachine.doorNode !== null
                property: "eulerRotation"
                value: Qt.vector3d(
                    externalMachine.doorHomeRotation.x
                        + (root.doorAxisEffective === "x"
                           ? externalMachine.doorAngle : 0),
                    externalMachine.doorHomeRotation.y
                        + (root.doorAxisEffective === "y"
                           ? externalMachine.doorAngle : 0),
                    externalMachine.doorHomeRotation.z
                        + (root.doorAxisEffective === "z"
                           ? externalMachine.doorAngle : 0))
                restoreMode: Binding.RestoreNone
            }

            Node {
                scale: Qt.vector3d(externalMachine.fit, externalMachine.fit,
                                   externalMachine.fit)
                position: Qt.vector3d(
                    -(externalMachine.rmin.x + externalMachine.rmax.x) / 2
                        * externalMachine.fit,
                    -view3d.bh / 2
                        - externalMachine.rmin.y * externalMachine.fit,
                    -(externalMachine.rmin.z + externalMachine.rmax.z) / 2
                        * externalMachine.fit)

                Node {
                    eulerRotation.x: root.externalZUp ? -90 : 0

                    RuntimeLoader {
                        id: glbLoader
                        source: root.externalModelSource

                        onStatusChanged: {
                            if (glbLoader.status !== RuntimeLoader.Success)
                                return;
                            // 重新加载会换掉整棵子树，旧的门体节点引用必须先作废
                            externalMachine.doorNode = null;
                            externalMachine.scanLoaded(glbLoader);
                            // 模型比例因文件而异，加载完成后把相机退到能看全的进退（留额外 20% 余量）
                            root.camDistance = externalMachine.fitDistance * 1.2;
                        }
                    }
                }
            }

            // ===== 外部模型上的贴纸 —— 暂时屏蔽 =====
            /*
            Node {
                id: glbStickers
                visible: root.showExternalStickers
                eulerRotation.y: root.stickerYaw

                readonly property real dist:
                    (Math.abs(root.stickerYaw % 180) < 1
                     ? externalMachine.fitD : externalMachine.fitW) / 2 + 3

                //! 品牌铭牌
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(200, 48, 1)
                    position: Qt.vector3d(0, 290, glbStickers.dist)
                    materials: PrincipledMaterial {
                        baseColorMap: brandTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }
                //! 型号铭牌
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(200, 44, 1)
                    position: Qt.vector3d(140, -300, glbStickers.dist)
                    materials: PrincipledMaterial {
                        baseColorMap: modelTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }
                //! 能效标签
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(110, 150, 1)
                    position: Qt.vector3d(-185, -250, glbStickers.dist)
                    materials: PrincipledMaterial {
                        baseColorMap: energyTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }
                //! 卖点贴纸
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(130, 60, 1)
                    position: Qt.vector3d(165, 225, glbStickers.dist)
                    materials: PrincipledMaterial {
                        baseColorMap: capacityTex
                        metalness: 0.0
                        roughness: 0.5
                    }
                }
            }
            */
        }

        // ===== 方案一：代码生成的程序化模型（保留，可随时切回）=====
        Node {
            id: machine
            visible: !root.useExternalModel

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

            // ===== 正面贴纸 —— 暂时屏蔽 =====
            /*
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
            */

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
            // 铰链在左侧，门体（金属外环 / 密封圈 / 玻璃观察窗 / 把手）全部挂在
            // doorBody 下，因此只需转动铰链节点就会整体摆出。绕 Y 轴正向旋转会把门的
            // 自由边推向 -Z（机内），所以向外开必须取负角。
            // 门锁只负责闩住与否，开合量由 root.doorOpen 给出。
            Node {
                id: doorPivot
                position: Qt.vector3d(-view3d.doorR - 8, view3d.holeCY,
                                      view3d.frontZ + 12)

                property real openAngle: root.doorOpen ? -view3d.doorOpenMax : 0
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

                // 显示屏贴纸 —— 暂时屏蔽
                /*
                Model {
                    source: "#Rectangle"
                    scale: view3d.dim(230, 58, 1)
                    position: Qt.vector3d(-140, 20, view3d.ctrlFace)
                    materials: PrincipledMaterial {
                        baseColorMap: displayTex
                        lighting: PrincipledMaterial.NoLighting
                    }
                }
                */

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

                // 指示灯文字标签贴纸 —— 暂时屏蔽
                /*
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
                */

                // 品牌铭牌贴纸 —— 暂时屏蔽
                /*
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
                */

                // 旋钮刻度盘贴纸 —— 暂时屏蔽
                /*
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
                */

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
                          to: root.homeAzimuth;
                          duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camElevation";
                          to: root.homeElevation;
                          duration: 400; easing.type: Easing.OutQuad }
        NumberAnimation { target: root; property: "camDistance";
                          to: root.useExternalModel
                              ? externalMachine.fitDistance
                              : view3d.fitDistanceBuiltin;
                          duration: 400; easing.type: Easing.OutQuad }
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

    // ---------------------------------------------------------------
    // 模型来源 / 舱门开合 / 外部模型姿态调整
    // ---------------------------------------------------------------
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        ToolButton {
            label: root.useExternalModel ? qsTr("GLB 模型") : qsTr("内置模型")
            labelColor: "#7fdcff"
            onActivated: root.useExternalModel = !root.useExternalModel
        }

        //! 舱门开合。内置模型天然可动；GLB 需要门体是独立节点，定位不到时
        //! 直接把原因标在按钮上，而不是默默隐起来。
        ToolButton {
            visible: !root.doorAnimatable
            label: qsTr("门:无独立节点")
            labelColor: root.colorMuted
        }

        ToolButton {
            visible: root.doorAnimatable
            label: root.doorOpen ? qsTr("关门") : qsTr("开门")
            onActivated: {
                if (root.engine) {
                    if (root.doorOpen)
                        root.engine.requestDoorClose();
                    else
                        root.engine.requestDoorOpen();
                }
            }
        }

        ToolButton {
            visible: root.useExternalModel
            label: qsTr("转 90°")
            onActivated: root.externalYaw = (root.externalYaw + 90) % 360
        }

        ToolButton {
            visible: root.useExternalModel
            label: root.externalZUp ? qsTr("Z 轴向上") : qsTr("Y 轴向上")
            onActivated: root.externalZUp = !root.externalZUp
        }
    }

    //! GLB 加载中 / 失败时的提示（加载成功后自动隐藏）
    Rectangle {
        anchors.centerIn: parent
        visible: root.useExternalModel
                 && glbLoader.status !== RuntimeLoader.Success
        width: loadText.implicitWidth + 24
        height: loadText.implicitHeight + 16
        radius: 6
        color: "#1b2430"
        opacity: 0.92
        border.width: 1
        border.color: "#2d3844"

        Text {
            id: loadText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - 80)
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: glbLoader.status === RuntimeLoader.Error ? "#ff8a80"
                                                           : "#7fdcff"
            text: {
                switch (glbLoader.status) {
                case RuntimeLoader.Loading:
                    return qsTr("正在加载 GLB 模型…");
                case RuntimeLoader.Error:
                    return qsTr("GLB 加载失败：") + glbLoader.errorString
                           + "\n" + root.externalModelSource;
                case RuntimeLoader.Empty:
                    return qsTr("未指定 GLB 模型");
                default:
                    return "";
                }
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
        text: root.useExternalModel ? qsTr("洗衣机 3D 运行视图（GLB 模型）")
                                    : qsTr("洗衣机 3D 运行视图（内置模型）")
        color: root.colorMuted
        font.pixelSize: 12
    }
}
