#ifndef WHM_MOTORMODEL_H
#define WHM_MOTORMODEL_H

#include <QObject>

namespace whm {

/*!
 * \brief 滚筒电机模型：一阶转动惯量模型 + 霍尔测速信号输出。
 *
 * 物理方程：
 *   dw/dt        = (tau_motor - tau_friction - tau_load) / J
 *   tau_motor    = K_t * duty                     duty 为 PWM 占空比
 *   tau_friction = b * w                          粘性摩擦
 *   f_hall       = |w| * polePairs / (2*pi)       霍尔信号（电频率）
 *
 * 单位约定：speedRpm 为 r/min（负值表示反转），hallFrequency 为 Hz。
 * duty 允许 -1.0~1.0，负值用于洗涤正反转工况。
 */
class MotorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double speedRpm READ speedRpm NOTIFY speedChanged)
    Q_PROPERTY(double hallFrequency READ hallFrequency NOTIFY hallFrequencyChanged)
    Q_PROPERTY(double angularVelocity READ angularVelocity NOTIFY speedChanged)
    Q_PROPERTY(double motorPwmDuty READ motorPwmDuty WRITE setMotorPwmDuty NOTIFY pwmDutyChanged)
    Q_PROPERTY(double loadTorque READ loadTorque WRITE setLoadTorque NOTIFY loadTorqueChanged)
    Q_PROPERTY(double motorTorque READ motorTorque NOTIFY pwmDutyChanged)

public:
    explicit MotorModel(QObject *parent = nullptr);

    //! 推进模型 dt 秒
    void update(double dt);
    //! 停机复位（转速、占空比、负载归零）
    void reset();

    // --- 读取量 ---
    double speedRpm() const;          //!< 转速 (r/min)
    double angularVelocity() const;   //!< 角速度 (rad/s)
    double hallFrequency() const;     //!< 霍尔信号频率 (Hz)
    double motorTorque() const;       //!< 电磁转矩 (N·m)
    double frictionTorque() const;    //!< 摩擦转矩 (N·m)

    // --- 控制输入 ---
    double motorPwmDuty() const;
    void setMotorPwmDuty(double duty);   //!< -1.0~1.0
    double loadTorque() const;
    void setLoadTorque(double torque);   //!< 衣物负载转矩 (N·m)

    // --- 参数配置 ---
    void setInertia(double kgm2);
    void setFrictionCoeff(double nmsPerRad);
    void setTorqueConstant(double nmAtFullDuty);
    void setPolePairs(double polePairs);
    void setMaxSpeedRpm(double rpm);

    double inertia() const;
    double frictionCoeff() const;
    double torqueConstant() const;
    double polePairs() const;
    double maxSpeedRpm() const;

signals:
    void speedChanged();
    void hallFrequencyChanged();
    void pwmDutyChanged();
    void loadTorqueChanged();
    //! 转速达到限速上限（脱水堵转/超速保护）
    void speedLimitReached(double speedRpm);

private:
    double maxAngularVelocity() const;

    double m_angularVelocity = 0.0;      //!< 角速度 (rad/s)
    double m_inertia = 0.01;             //!< 转动惯量 (kg·m²)
    double m_frictionCoeff = 0.005;      //!< 粘性摩擦系数 (N·m·s)
    double m_motorTorqueConst = 2.0;     //!< 100% 占空比对应转矩 (N·m)
    double m_polePairs = 4;              //!< 极对数
    double m_maxSpeedRpm = 1600;         //!< 最高转速 (r/min)
    double m_pwmDuty = 0.0;
    double m_loadTorque = 0.0;
    bool m_speedLimitReported = false;
};

} // namespace whm

#endif // WHM_MOTORMODEL_H
