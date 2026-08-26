#ifndef WHM_FAULTINJECTOR_H
#define WHM_FAULTINJECTOR_H

#include <QMap>
#include <QObject>
#include <QRandomGenerator>
#include <QString>

namespace whm {

/*!
 * \brief 故障注入器：对 PC→MCU 下行传感器输出通道注入典型线束/传感器故障。
 *
 * 使用方式：
 *  - 界面调用 injectFault() 在指定通道注入指定类型故障（可设持续时间与幅度系数）；
 *  - IntegrationBridge 在构造 BatchSet 帧前，对每个传感器原始输出调用
 *    applyFault() 获得"带故障"的输出值再发送给主控板，模拟真实故障工况。
 *
 * 故障类型效果：
 *  - Disconnected  断线：频率通道输出 0Hz，NTC 开路(极大阻值)，门锁固定解锁；
 *  - OutOfRange    超范围：输出值 × magnitude，超出主控标定范围；
 *  - Jitter        抖动：原值叠加随机噪声，幅度 = magnitude × |原值| × 0.1；
 *  - Stuck         卡死：输出冻结在故障生效后首次采样到的值；
 *  - ShortCircuit  短路：输出拉满（频率→100kHz，阻值→0Ω，门锁→锁定）。
 */
class FaultInjector : public QObject
{
    Q_OBJECT
    //! 是否有任意通道故障处于激活状态
    Q_PROPERTY(bool anyFaultActive READ anyFaultActive NOTIFY faultsChanged)

public:
    //! 故障目标通道
    enum Channel {
        WaterLevelSensor,  //!< 水位传感器（频率输出）
        TempSensor,        //!< NTC 温度传感器（阻值输出）
        MotorHall          //!< Hall 转速传感器（频率输出）
    };
    Q_ENUM(Channel)

    //! 故障类型
    enum FaultType {
        None,           //!< 无故障
        Disconnected,   //!< 断线（输出关闭/开路）
        OutOfRange,     //!< 超范围（输出超出标定范围）
        Jitter,         //!< 抖动（叠加随机噪声）
        Stuck,          //!< 卡死（输出冻结在故障注入时刻的值）
        ShortCircuit    //!< 短路（输出拉满）
    };
    Q_ENUM(FaultType)

    //! 单通道故障配置与运行状态
    struct FaultConfig {
        FaultType type = None;
        double durationSec = 0;   //!< 持续时间(s)，0 = 持续直到手动清除
        double magnitude = 1.0;   //!< 故障幅度系数（抖动幅度/超范围倍数）
        double elapsed = 0;       //!< 已持续时间(s)
        bool active = false;      //!< 是否激活
        double stuckValue = 0;    //!< Stuck 时冻结的值
        bool stuckCaptured = false; //!< Stuck 值是否已捕获（生效首帧采样）
    };

    explicit FaultInjector(QObject *parent = nullptr);

    //! 注入故障（channel/faultType 为枚举整数值，供 QML 调用）
    Q_INVOKABLE void injectFault(int channel, int faultType,
                                 double durationSec = 0, double magnitude = 1.0);
    //! 清除指定通道故障
    Q_INVOKABLE void clearFault(int channel);
    //! 清除全部故障
    Q_INVOKABLE void clearAllFaults();

    //! 应用故障到传感器输出值（IntegrationBridge 发送前逐帧调用），dt 为仿真步长(s)
    double applyFault(Channel ch, double originalValue, double dt);

    //! 查询指定通道是否有激活故障（channel 为枚举整数值）
    Q_INVOKABLE bool isFaultActive(int channel) const;
    //! 查询指定通道故障的中文描述（无故障时返回空串）
    Q_INVOKABLE QString faultDescription(int channel) const;

    bool anyFaultActive() const;

signals:
    void faultsChanged();
    void faultInjected(int channel, int faultType);
    void faultCleared(int channel);

private:
    double applyDisconnected(Channel ch, double value);
    double applyOutOfRange(Channel ch, double value, double mag);
    double applyJitter(Channel ch, double value, double mag);
    double applyStuck(Channel ch, double value);
    double applyShortCircuit(Channel ch);

    //! 通道枚举 → 中文名称
    static QString channelName(Channel ch);
    //! 故障类型枚举 → 中文名称
    static QString faultTypeName(FaultType type);

    QMap<Channel, FaultConfig> m_faults;
    QRandomGenerator m_rng;
};

} // namespace whm

#endif // WHM_FAULTINJECTOR_H
