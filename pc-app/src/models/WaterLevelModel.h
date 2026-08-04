#ifndef WHM_WATERLEVELMODEL_H
#define WHM_WATERLEVELMODEL_H

#include <QObject>
#include <QVector>
#include <QPointF>

namespace whm {

/*!
 * \brief 水位物理模型：进水/排水引起的水量变化、静压与压力式水位传感器输出频率。
 *
 * 物理方程：
 *   dV/dt = Q_in - Q_out              水量守恒 (L/s)
 *   h     = V / A_cross               水位高度 (m)，V 换算为 m³
 *   P     = rho * g * h               液柱静压 (Pa)
 *   f     = f(P)                      压力-频率标定表线性插值 (Hz)
 *
 * 单位约定：waterLevel 以 mm 暴露给界面，pressure 以 kPa 暴露，
 * sensorFrequency 以 Hz 暴露（真实洗衣机水位传感器空桶约 26 kHz，随水位升高频率下降）。
 */
class WaterLevelModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double waterLevel READ waterLevel NOTIFY waterLevelChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY pressureChanged)
    Q_PROPERTY(double sensorFrequency READ sensorFrequency NOTIFY sensorFrequencyChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(double fillPercent READ fillPercent NOTIFY volumeChanged)
    Q_PROPERTY(bool inletValveOpen READ inletValveOpen WRITE setInletValveOpen NOTIFY inletValveOpenChanged)
    Q_PROPERTY(bool drainPumpOn READ drainPumpOn WRITE setDrainPumpOn NOTIFY drainPumpOnChanged)

public:
    explicit WaterLevelModel(QObject *parent = nullptr);

    //! 推进模型 dt 秒
    void update(double dt);
    //! 复位到空桶状态（阀门/水泵关闭）
    void reset();

    // --- 读取量 ---
    double waterLevel() const;        //!< 水位 (mm)
    double waterLevelMeters() const;  //!< 水位 (m)
    double pressure() const;          //!< 液柱静压 (kPa)
    double sensorFrequency() const;   //!< 水位传感器输出频率 (Hz)
    double volume() const;            //!< 当前水量 (L)
    double fillPercent() const;       //!< 水量占最大容量百分比 (0-100)
    double waterMass() const;         //!< 水质量 (kg)，供温度模型使用

    // --- 执行器输入 ---
    bool inletValveOpen() const;
    bool drainPumpOn() const;
    void setInletValveOpen(bool open);
    void setDrainPumpOn(bool on);

    // --- 参数配置 ---
    void setInletFlowRateLpm(double litrePerMinute);
    void setDrainFlowRateLpm(double litrePerMinute);
    void setMaxCapacity(double litre);
    void setCrossSectionArea(double squareMetre);
    //! 自定义压力(kPa)-频率(Hz)标定表，按压力升序给出
    void setCalibrationTable(const QVector<QPointF> &pressureToFrequency);

    double inletFlowRateLpm() const;
    double drainFlowRateLpm() const;
    double maxCapacity() const;
    double crossSectionArea() const;

signals:
    void waterLevelChanged();
    void pressureChanged();
    void sensorFrequencyChanged();
    void volumeChanged();
    void inletValveOpenChanged();
    void drainPumpOnChanged();
    //! 水量达到最大容量（溢出保护）
    void overflowDetected();
    //! 桶内排空
    void emptyDetected();

private:
    void emitStateChanged();
    double frequencyForPressure(double pressureKpa) const;

    double m_volume = 0.0;              //!< 当前水量 (L)
    double m_inletFlow = 2.0 / 60.0;    //!< 进水流量 (L/s)
    double m_drainFlow = 1.5 / 60.0;    //!< 排水流量 (L/s)
    double m_maxCapacity = 50.0;        //!< 最大容量 (L)
    double m_crossArea = 0.1;           //!< 桶体横截面积 (m²)
    bool m_inletOpen = false;
    bool m_drainOn = false;

    QVector<QPointF> m_calibration;     //!< x: 压力(kPa), y: 频率(Hz)
};

} // namespace whm

#endif // WHM_WATERLEVELMODEL_H
