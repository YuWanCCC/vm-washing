#ifndef WHM_INTEGRATIONBRIDGE_H
#define WHM_INTEGRATIONBRIDGE_H

#include <QObject>

#include "communication/SerialManager.h"
#include "core/SimulationEngine.h"

namespace whm {

class FaultInjector;
class DataRecorder;

/*!
 * \brief 闭环集成层：仿真引擎(SimulationEngine)与串口链路(SerialManager)之间的双向数据桥。
 *
 * 数据流向：
 *  - 下行(PC→MCU)：引擎每帧 ticked() 后，把各物理模型的传感器输出（水位频率、
 *    NTC 阻值→DAC、Hall 频率）与执行器/门锁状态打包为 BatchSetData 帧发送，
 *    模拟"真实传感器"喂给主控板；
 *  - 上行(MCU→PC)：收到 REPORT_INPUT 帧后，把主控实际发出的继电器指令与电机 PWM
 *    回灌给执行器模型与电机模型，使仿真按"主控真实行为"演化，形成硬件在环闭环。
 *
 * 桥接模式：
 *  - SimulationOnly  仅仿真，不与硬件交互（默认）；
 *  - OutputOnly      只把模型输出发送到 MCU（开环观测）；
 *  - ClosedLoop      发送输出 + 接收反馈 + 反馈驱动模型（完整闭环，
 *                    且仅在引擎处于 Automatic 联动模式时回灌反馈）。
 */
class IntegrationBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SimulationEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(SerialManager* serial READ serial WRITE setSerial NOTIFY serialChanged)
    //! 桥接是否激活（未激活时既不发送也不回灌）
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(BridgeMode bridgeMode READ bridgeMode WRITE setBridgeMode NOTIFY bridgeModeChanged)
    //! 下行发送节流周期(ms)：引擎 tick 周期(默认20ms)更细时按此限频
    Q_PROPERTY(int sendPeriodMs READ sendPeriodMs WRITE setSendPeriodMs NOTIFY sendPeriodChanged)
    //! 故障注入器：发送前对传感器输出应用故障效果（可为空，空则原样输出）
    Q_PROPERTY(FaultInjector* faultInjector READ faultInjector WRITE setFaultInjector NOTIFY faultInjectorChanged)
    //! 数据记录器：每帧采样关键通道（可为空，空则不记录）
    Q_PROPERTY(DataRecorder* recorder READ recorder WRITE setRecorder NOTIFY recorderChanged)

    // 最新上报的主控反馈（供 UI 显示）
    Q_PROPERTY(double feedbackMotorDuty READ feedbackMotorDuty NOTIFY feedbackUpdated)
    Q_PROPERTY(int feedbackRelayBitmap READ feedbackRelayBitmap NOTIFY feedbackUpdated)
    Q_PROPERTY(int feedbackGpioBitmap READ feedbackGpioBitmap NOTIFY feedbackUpdated)

public:
    //! 桥接模式
    enum BridgeMode {
        SimulationOnly,  //!< 仅仿真，不与硬件交互（默认）
        OutputOnly,      //!< 只发送模型输出到 MCU（开环）
        ClosedLoop       //!< 闭环：发输出 + 收反馈 + 反馈驱动模型
    };
    Q_ENUM(BridgeMode)

    explicit IntegrationBridge(QObject *parent = nullptr);

    SimulationEngine *engine() const { return m_engine; }
    SerialManager *serial() const { return m_serial; }
    FaultInjector *faultInjector() const { return m_faultInjector; }
    DataRecorder *recorder() const { return m_recorder; }

    void setEngine(SimulationEngine *e);
    void setSerial(SerialManager *s);
    void setFaultInjector(FaultInjector *f);
    void setRecorder(DataRecorder *r);

    bool isActive() const { return m_active; }
    Q_INVOKABLE void setActive(bool on);

    BridgeMode bridgeMode() const { return m_bridgeMode; }
    void setBridgeMode(BridgeMode mode);

    int sendPeriodMs() const { return m_sendPeriodMs; }
    void setSendPeriodMs(int ms);

    double feedbackMotorDuty() const { return m_feedbackMotorDuty; }
    int feedbackRelayBitmap() const { return m_feedbackRelayBitmap; }
    int feedbackGpioBitmap() const { return m_feedbackGpioBitmap; }

    //! 调整 NTC 阻值→DAC 的映射范围（rMin 对应 DAC=4095，rMax 对应 DAC=0）
    Q_INVOKABLE void setNtcRange(double rMinOhm, double rMaxOhm);

    //! 手动构造并发送一帧 BatchSet（调试用，不受节流/模式限制）
    Q_INVOKABLE void sendOnce();

signals:
    void engineChanged();
    void serialChanged();
    void faultInjectorChanged();
    void recorderChanged();
    void activeChanged();
    void bridgeModeChanged();
    void sendPeriodChanged();
    //! 收到并缓存了一帧主控反馈
    void feedbackUpdated();

private slots:
    //! 引擎每步回调 → 节流后构造 BatchSetData 发送
    void onEngineTicked();
    //! 收到主控反馈(REPORT_INPUT) → 缓存并回灌引擎
    void onSerialData(const ReportInputData &data);

private:
    BatchSetData buildBatchSet();                     //!< 从模型输出构造发送数据（含故障注入）
    void applyFeedback(const ReportInputData &data); //!< 反馈驱动模型（闭环）
    void recordChannels();                            //!< 采样关键通道到数据记录器
    uint16_t ntcToDac(double resistanceOhm) const;   //!< NTC 阻值(Ω)→DAC(0-4095)

    SimulationEngine *m_engine = nullptr;
    SerialManager *m_serial = nullptr;
    FaultInjector *m_faultInjector = nullptr;
    DataRecorder *m_recorder = nullptr;
    bool m_active = false;
    BridgeMode m_bridgeMode = SimulationOnly;
    int m_sendPeriodMs = 10;

    //! 发送节流：上次发送的时刻(ms 时间戳)
    qint64 m_lastSendTime = 0;

    // 最新反馈缓存
    double m_feedbackMotorDuty = 0.0;
    int m_feedbackRelayBitmap = 0;
    int m_feedbackGpioBitmap = 0;

    // NTC 映射参数：线性映射，阻值越大 DAC 越小
    double m_ntcRMin = 1000.0;    //!< 对应 DAC = 4095
    double m_ntcRMax = 100000.0;  //!< 对应 DAC = 0
};

} // namespace whm

#endif // WHM_INTEGRATIONBRIDGE_H
