#ifndef WHM_TEMPERATUREMODEL_H
#define WHM_TEMPERATUREMODEL_H

#include <QObject>

namespace whm {

/*!
 * \brief 水温物理模型：加热管加热、自然散热与 NTC 温度传感器阻值输出。
 *
 * 物理方程：
 *   dT/dt   = (P_heat - Q_loss) / (m * Cp)          集中参数热容模型
 *   Q_loss  = h_conv * A * (T - T_env)              牛顿冷却，h_conv*A 合并为 heatLossCoeff (W/K)
 *   R_NTC   = R25 * exp(B * (1/T - 1/298.15))       B 值方程（Steinhart-Hart 简化式），T 为开尔文
 *
 * 单位约定：temperature 为 °C，ntcResistance 为 Ω。
 */
class TemperatureModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(double ntcResistance READ ntcResistance NOTIFY ntcResistanceChanged)
    Q_PROPERTY(bool heaterOn READ heaterOn WRITE setHeaterOn NOTIFY heaterOnChanged)
    Q_PROPERTY(double waterMass READ waterMass WRITE setWaterMass NOTIFY waterMassChanged)
    Q_PROPERTY(double environmentTemp READ environmentTemp WRITE setEnvironmentTemp NOTIFY environmentTempChanged)

public:
    explicit TemperatureModel(QObject *parent = nullptr);

    //! 推进模型 dt 秒
    void update(double dt);
    //! 复位到环境温度并关闭加热
    void reset();

    // --- 读取量 ---
    double temperature() const;      //!< 水温 (°C)
    double ntcResistance() const;    //!< NTC 阻值 (Ω)
    //! 指定温度对应的 NTC 阻值 (Ω)，供标定/查表使用
    double resistanceAt(double celsius) const;
    //! 指定 NTC 阻值反算温度 (°C)
    double temperatureFromResistance(double ohm) const;
    //! 当前净加热功率 (W)，负值表示散热大于加热
    double netHeatPower() const;

    // --- 执行器与工况输入 ---
    bool heaterOn() const;
    void setHeaterOn(bool on);
    void setWaterMass(double mass_kg);
    double waterMass() const;
    void setEnvironmentTemp(double celsius);
    double environmentTemp() const;
    //! 直接设定当前水温（如换水、初始化场景）
    void setTemperature(double celsius);

    // --- 参数配置 ---
    void setHeaterPower(double watt);
    void setSpecificHeat(double joulePerKgK);
    void setHeatLossCoeff(double wattPerKelvin);
    void setNtcParameters(double r25Ohm, double bValueKelvin);

    double heaterPower() const;
    double specificHeat() const;
    double heatLossCoeff() const;
    double ntcR25() const;
    double ntcB() const;

signals:
    void temperatureChanged();
    void ntcResistanceChanged();
    void heaterOnChanged();
    void waterMassChanged();
    void environmentTempChanged();
    //! 水温超过安全上限（干烧/过热保护）
    void overheatDetected(double temperature);

private:
    double effectiveMass() const;

    double m_temperature = 20.0;        //!< 当前水温 (°C)
    double m_heaterPower = 2000.0;      //!< 加热管功率 (W)
    double m_waterMass = 30.0;          //!< 水质量 (kg)
    double m_specificHeat = 4186.0;     //!< 水比热容 J/(kg·K)
    double m_heatLossCoeff = 5.0;       //!< 综合散热系数 (W/K)
    double m_envTemp = 25.0;            //!< 环境温度 (°C)
    double m_ntcR25 = 10000.0;          //!< 25°C 标称阻值 (Ω)
    double m_ntcB = 3950.0;             //!< NTC B 值 (K)
    bool m_heaterOn = false;
    bool m_overheatReported = false;
};

} // namespace whm

#endif // WHM_TEMPERATUREMODEL_H
