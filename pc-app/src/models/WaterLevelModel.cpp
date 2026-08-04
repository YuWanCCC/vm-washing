#include "models/WaterLevelModel.h"

#include <algorithm>
#include <cmath>

namespace whm {

namespace {
constexpr double kWaterDensity = 1000.0;   // kg/m³
constexpr double kGravity = 9.80665;       // m/s²
constexpr double kLitreToCubicMetre = 1.0e-3;
constexpr double kEpsilon = 1.0e-9;

//! 默认压力-频率标定表（x: kPa, y: Hz），典型压力式水位传感器特性
QVector<QPointF> defaultCalibration()
{
    return QVector<QPointF>{
        {0.0, 26000.0},
        {1.0, 25200.0},
        {2.0, 24500.0},
        {3.0, 23800.0},
        {4.0, 23200.0},
        {5.0, 22600.0},
        {6.0, 22100.0},
        {8.0, 21200.0},
        {10.0, 20500.0}};
}
} // namespace

WaterLevelModel::WaterLevelModel(QObject *parent)
    : QObject(parent)
    , m_calibration(defaultCalibration())
{
}

void WaterLevelModel::update(double dt)
{
    if (dt <= 0.0)
        return;

    const double previousVolume = m_volume;

    // dV/dt = Q_in - Q_out
    const double inflow = m_inletOpen ? m_inletFlow : 0.0;
    const double outflow = m_drainOn ? m_drainFlow : 0.0;
    m_volume += (inflow - outflow) * dt;

    if (m_volume >= m_maxCapacity) {
        m_volume = m_maxCapacity;
        if (previousVolume < m_maxCapacity)
            emit overflowDetected();
    }
    if (m_volume <= 0.0) {
        m_volume = 0.0;
        if (previousVolume > 0.0)
            emit emptyDetected();
    }

    if (std::abs(m_volume - previousVolume) > kEpsilon)
        emitStateChanged();
}

void WaterLevelModel::reset()
{
    const bool hadWater = m_volume > 0.0;
    m_volume = 0.0;
    setInletValveOpen(false);
    setDrainPumpOn(false);
    if (hadWater)
        emitStateChanged();
}

double WaterLevelModel::waterLevel() const
{
    return waterLevelMeters() * 1000.0;
}

double WaterLevelModel::waterLevelMeters() const
{
    if (m_crossArea <= kEpsilon)
        return 0.0;
    // h = V / A_cross
    return (m_volume * kLitreToCubicMetre) / m_crossArea;
}

double WaterLevelModel::pressure() const
{
    // P = rho * g * h，Pa 转 kPa
    return kWaterDensity * kGravity * waterLevelMeters() / 1000.0;
}

double WaterLevelModel::sensorFrequency() const
{
    return frequencyForPressure(pressure());
}

double WaterLevelModel::volume() const
{
    return m_volume;
}

double WaterLevelModel::fillPercent() const
{
    if (m_maxCapacity <= kEpsilon)
        return 0.0;
    return 100.0 * m_volume / m_maxCapacity;
}

double WaterLevelModel::waterMass() const
{
    // 1 L 水 ≈ 1 kg
    return m_volume;
}

bool WaterLevelModel::inletValveOpen() const
{
    return m_inletOpen;
}

bool WaterLevelModel::drainPumpOn() const
{
    return m_drainOn;
}

void WaterLevelModel::setInletValveOpen(bool open)
{
    if (m_inletOpen == open)
        return;
    m_inletOpen = open;
    emit inletValveOpenChanged();
}

void WaterLevelModel::setDrainPumpOn(bool on)
{
    if (m_drainOn == on)
        return;
    m_drainOn = on;
    emit drainPumpOnChanged();
}

void WaterLevelModel::setInletFlowRateLpm(double litrePerMinute)
{
    m_inletFlow = std::max(0.0, litrePerMinute) / 60.0;
}

void WaterLevelModel::setDrainFlowRateLpm(double litrePerMinute)
{
    m_drainFlow = std::max(0.0, litrePerMinute) / 60.0;
}

void WaterLevelModel::setMaxCapacity(double litre)
{
    m_maxCapacity = std::max(kEpsilon, litre);
    if (m_volume > m_maxCapacity) {
        m_volume = m_maxCapacity;
        emitStateChanged();
    }
}

void WaterLevelModel::setCrossSectionArea(double squareMetre)
{
    const double area = std::max(kEpsilon, squareMetre);
    if (std::abs(area - m_crossArea) <= kEpsilon)
        return;
    m_crossArea = area;
    emitStateChanged();
}

void WaterLevelModel::setCalibrationTable(const QVector<QPointF> &pressureToFrequency)
{
    if (pressureToFrequency.size() < 2)
        return;
    m_calibration = pressureToFrequency;
    std::sort(m_calibration.begin(), m_calibration.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    emit sensorFrequencyChanged();
}

double WaterLevelModel::inletFlowRateLpm() const
{
    return m_inletFlow * 60.0;
}

double WaterLevelModel::drainFlowRateLpm() const
{
    return m_drainFlow * 60.0;
}

double WaterLevelModel::maxCapacity() const
{
    return m_maxCapacity;
}

double WaterLevelModel::crossSectionArea() const
{
    return m_crossArea;
}

void WaterLevelModel::emitStateChanged()
{
    emit volumeChanged();
    emit waterLevelChanged();
    emit pressureChanged();
    emit sensorFrequencyChanged();
}

double WaterLevelModel::frequencyForPressure(double pressureKpa) const
{
    if (m_calibration.isEmpty())
        return 0.0;
    if (m_calibration.size() == 1)
        return m_calibration.first().y();

    // 低于标定下限：用首段斜率外推；高于上限：用末段斜率外推；区间内线性插值
    if (pressureKpa <= m_calibration.first().x()) {
        const QPointF &p0 = m_calibration.at(0);
        const QPointF &p1 = m_calibration.at(1);
        const double slope = (p1.y() - p0.y()) / (p1.x() - p0.x());
        return p0.y() + slope * (pressureKpa - p0.x());
    }

    for (int i = 1; i < m_calibration.size(); ++i) {
        const QPointF &p0 = m_calibration.at(i - 1);
        const QPointF &p1 = m_calibration.at(i);
        if (pressureKpa <= p1.x()) {
            const double span = p1.x() - p0.x();
            if (span <= kEpsilon)
                return p1.y();
            const double ratio = (pressureKpa - p0.x()) / span;
            return p0.y() + ratio * (p1.y() - p0.y());
        }
    }

    const QPointF &pn = m_calibration.at(m_calibration.size() - 1);
    const QPointF &pn1 = m_calibration.at(m_calibration.size() - 2);
    const double slope = (pn.y() - pn1.y()) / (pn.x() - pn1.x());
    return pn.y() + slope * (pressureKpa - pn.x());
}

} // namespace whm
