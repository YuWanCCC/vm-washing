#ifndef WHM_ACTUATORMODELS_H
#define WHM_ACTUATORMODELS_H

#include <QObject>

namespace whm {

/*!
 * \brief 执行器状态集合：进水阀、排水泵、加热器的开关量包装。
 *
 * 作为下位机数字输出（或界面手动控制）与各物理模型之间的输入接口，
 * 仿真引擎每帧读取这些状态并注入 WaterLevelModel / TemperatureModel。
 */
class ActuatorModels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool inletValveOpen READ inletValveOpen WRITE setInletValveOpen NOTIFY inletValveChanged)
    Q_PROPERTY(bool drainPumpOn READ drainPumpOn WRITE setDrainPumpOn NOTIFY drainPumpChanged)
    Q_PROPERTY(bool heaterOn READ heaterOn WRITE setHeaterOn NOTIFY heaterChanged)
    Q_PROPERTY(bool anyActive READ anyActive NOTIFY anyActiveChanged)

public:
    explicit ActuatorModels(QObject *parent = nullptr);

    bool inletValveOpen() const;
    bool drainPumpOn() const;
    bool heaterOn() const;
    //! 任一执行器处于工作状态
    bool anyActive() const;

    void setInletValveOpen(bool open);
    void setDrainPumpOn(bool on);
    void setHeaterOn(bool on);

    //! 全部执行器断开（急停 / 场景复位）
    Q_INVOKABLE void reset();
    //! 按位读写下位机数字输出字节：bit0 进水阀, bit1 排水泵, bit2 加热器
    quint8 toBitmask() const;
    void applyBitmask(quint8 mask);

signals:
    void inletValveChanged();
    void drainPumpChanged();
    void heaterChanged();
    void anyActiveChanged();
    //! 任一执行器状态变化（供仿真引擎/日志统一挂钩）
    void actuatorStateChanged();

private:
    void notifyChanged();

    bool m_inletValveOpen = false;
    bool m_drainPumpOn = false;
    bool m_heaterOn = false;
};

} // namespace whm

#endif // WHM_ACTUATORMODELS_H
