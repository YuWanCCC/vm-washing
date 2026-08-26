#ifndef WHM_SIMULATIONENGINE_H
#define WHM_SIMULATIONENGINE_H

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "core/ScenarioManager.h"
#include "models/ActuatorModels.h"
#include "models/DoorLockModel.h"
#include "models/MotorModel.h"
#include "models/TemperatureModel.h"
#include "models/WaterLevelModel.h"

class QTimer;

namespace whm {

/*!
 * \brief 仿真引擎：统一的定步长更新循环 + 洗衣周期状态机。
 *
 * 职责划分：
 *  - 时基：QTimer 以 config 中 simulation.timeStepMs 为周期（默认 20 ms）驱动 tick()，
 *    每帧推进各物理模型 dt = timeStepMs/1000 * timeScale 秒；
 *  - 数据流：执行器状态（ActuatorModels）为唯一输入源，每帧注入水位/温度模型，
 *    并把水位模型算出的水质量回灌给温度模型（水多升温慢）；
 *  - 模式：Manual 由界面直接操作执行器与电机；Automatic 由洗衣状态机自动编排。
 *
 * 状态机：Idle → Fill → Heat → Wash → Drain → Rinse(×N，内部含注水/洗涤，排水复用 Drain)
 *         → Spin → Complete。
 */
class SimulationEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SimState state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(SimMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(double elapsedTime READ elapsedTime NOTIFY elapsedTimeChanged)
    Q_PROPERTY(double stateElapsedTime READ stateElapsedTime NOTIFY elapsedTimeChanged)
    Q_PROPERTY(double timeStepMs READ timeStepMs NOTIFY timeStepChanged)
    Q_PROPERTY(double timeScale READ timeScale WRITE setTimeScale NOTIFY timeScaleChanged)
    Q_PROPERTY(QString programName READ programName NOTIFY programChanged)
    Q_PROPERTY(int rinseIndex READ rinseIndex NOTIFY stateChanged)

    // 暴露子模型给QML
    Q_PROPERTY(WaterLevelModel* waterModel READ waterModel CONSTANT)
    Q_PROPERTY(TemperatureModel* tempModel READ tempModel CONSTANT)
    Q_PROPERTY(MotorModel* motorModel READ motorModel CONSTANT)
    Q_PROPERTY(DoorLockModel* doorModel READ doorModel CONSTANT)
    Q_PROPERTY(ActuatorModels* actuators READ actuators CONSTANT)
    Q_PROPERTY(ScenarioManager* scenarios READ scenarioManager CONSTANT)

    //! 门体开合状态（独立于门锁，描述门体物理开/关）
    Q_PROPERTY(bool doorOpenState READ doorOpenState NOTIFY doorStateChanged)
    Q_PROPERTY(QString doorStateText READ doorStateText NOTIFY doorStateChanged)

public:
    //! 仿真状态（洗衣周期）
    enum SimState {
        Idle,       //!< 待机
        Fill,       //!< 注水
        Heat,       //!< 加热
        Wash,       //!< 洗涤
        Rinse,      //!< 漂洗
        Spin,       //!< 脱水
        Drain,      //!< 排水
        Complete    //!< 完成
    };
    Q_ENUM(SimState)

    //! 仿真模式
    enum SimMode {
        Manual,     //!< 手动模式：直接设置参数
        Automatic   //!< 联动模式：洗衣状态机自动驱动
    };
    Q_ENUM(SimMode)

    explicit SimulationEngine(QObject *parent = nullptr);

    Q_INVOKABLE void start();      //!< 开始仿真
    Q_INVOKABLE void stop();       //!< 停止仿真
    Q_INVOKABLE void reset();      //!< 重置所有状态
    Q_INVOKABLE void stepOnce();   //!< 单步执行（调试用）

    //! 联动模式：启动自动洗衣程序（name 为 config/scenarios 下的文件基名，未找到则用内置默认参数）
    Q_INVOKABLE void startWashProgram(const QString &programName);
    //! 中止自动洗衣程序，回到待机（执行器全断、门锁解锁）
    Q_INVOKABLE void abortWashProgram();

    //! 手动开门（门体打开，DOOR_1 输出高 / DOOR_2 输出低）
    Q_INVOKABLE void requestDoorOpen();
    //! 手动物关门（门体关闭，DOOR_1 输出低 / DOOR_2 输出高）
    Q_INVOKABLE void requestDoorClose();

    //! 加载配置参数（default_params.json 格式），空路径表示使用默认 config 目录
    Q_INVOKABLE void loadConfig(const QString &configPath);

    // --- 状态读取 ---
    SimState state() const;
    QString stateText() const;
    SimMode mode() const;
    void setMode(SimMode mode);
    bool isRunning() const;
    double elapsedTime() const;
    double stateElapsedTime() const;
    double timeStepMs() const;
    double timeScale() const;
    void setTimeScale(double scale);
    QString programName() const;
    //! 门体是否敞开
    bool doorOpenState() const;
    //! 门状态文本
    QString doorStateText() const;
    //! 当前处于第几次漂洗（从 1 开始，未漂洗时为 0）
    int rinseIndex() const;

    // --- 子模型访问 ---
    WaterLevelModel *waterModel() const;
    TemperatureModel *tempModel() const;
    MotorModel *motorModel() const;
    DoorLockModel *doorModel() const;
    ActuatorModels *actuators() const;
    ScenarioManager *scenarioManager() const;

signals:
    void stateChanged();
    void modeChanged();
    void runningChanged();
    void elapsedTimeChanged();
    void timeStepChanged();
    void timeScaleChanged();
    void programChanged();
    //! 每帧更新完成，供界面/曲线统一刷新
    void ticked(double elapsedTime);
    //! 状态迁移完成（便于日志记录）
    void stateEntered(SimState state);
    //! 自动洗衣程序结束（进入 Complete）
    void washProgramFinished();
    //! 保护动作/异常提示（溢流、加热超时等）
    void warning(const QString &message);
    void configLoaded(const QString &configPath);
    //! 门体开合状态变化
    void doorStateChanged();

private:
    //! 自动模式参数
    struct WashProgram {
        double targetWaterLevel = 300.0;    //!< 目标水位(mm)
        double targetTemperature = 40.0;    //!< 目标温度(°C)
        double washDuration = 900.0;        //!< 洗涤时间(s)
        double rinseCycles = 2.0;           //!< 漂洗次数
        double spinDuration = 300.0;        //!< 脱水时间(s)
        double spinSpeed = 1200.0;          //!< 脱水转速(RPM)
        double washSpeed = 50.0;            //!< 洗涤转速(RPM)
        double loadTorque = 0.6;            //!< 衣物负载转矩(N·m)
    };

    void tick();                            //!< 定时器回调，核心更新循环
    void updateAutomaticMode();             //!< 联动模式状态机
    void transitionTo(SimState newState);   //!< 状态转换（含进入动作）

    // 各状态每帧逻辑
    void updateFillState();
    void updateHeatState();
    void updateWashState();
    void updateDrainState();
    void updateRinseState();
    void updateSpinState();

    //! 简易转速闭环：按误差调整 PWM 占空比逼近目标转速（负值为反转）
    void driveMotorToSpeed(double targetRpm);
    //! 电机停机（占空比归零，依靠摩擦自由减速）
    void stopMotor();
    //! 洗涤/漂洗共用的正-停-反-停搅拌节拍
    void runAgitation(double speedRpm);
    //! 桶内是否已排空
    bool isDrained() const;
    //! 从场景 JSON（standard_wash.json 结构）解析洗衣程序参数
    bool parseProgramFromScenario(const QVariantMap &scenario, WashProgram *program) const;

    QTimer *m_timer = nullptr;      //!< 定步长定时器（周期取自配置，默认 20 ms）
    double m_dt = 0.02;             //!< 时间步长(s)
    double m_timeStepMs = 20.0;     //!< 定时器周期(ms)
    double m_timeScale = 1.0;       //!< 仿真倍速
    double m_elapsedTime = 0;       //!< 仿真累计时间

    // 子模型
    WaterLevelModel *m_waterModel = nullptr;
    TemperatureModel *m_tempModel = nullptr;
    MotorModel *m_motorModel = nullptr;
    DoorLockModel *m_doorModel = nullptr;
    ActuatorModels *m_actuators = nullptr;
    ScenarioManager *m_scenarios = nullptr;

    SimState m_state = Idle;
    SimMode m_mode = Manual;
    bool m_running = false;

    WashProgram m_currentProgram;
    QString m_programName;          //!< 当前程序名（场景文件基名或内置名）
    double m_stateTimer = 0;        //!< 当前状态计时(s)
    double m_agitationTimer = 0;    //!< 搅拌节拍计时(s)
    int m_rinseDone = 0;            //!< 已完成的漂洗次数
    SimState m_drainNext = Idle;    //!< 排水完成后进入的状态
    bool m_rinseFilling = false;    //!< 漂洗子阶段：true 注水中，false 搅拌中

    bool m_doorOpenState = false;   //!< 门体开合状态：true=开, false=关
};

} // namespace whm

#endif // WHM_SIMULATIONENGINE_H
