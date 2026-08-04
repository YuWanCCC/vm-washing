#include "models/TemperatureModel.h"

#include <algorithm>
#include <cmath>

namespace whm {

namespace {
constexpr double kKelvinOffset = 273.15;
constexpr double kNtcNominalKelvin = 298.15;   // 25 °C
constexpr double kMinTemperature = 0.0;        // °C
constexpr double kMaxTemperature = 100.0;      // °C（常压沸点，超出后不再升温）
constexpr double kOverheatThreshold = 95.0;    // °C
constexpr double kMinEffectiveMass = 0.5;      // kg，避免空桶加热时除零
constexpr double kEpsilon = 1.0e-9;
} // namespace

TemperatureModel::TemperatureModel(QObject *parent)
    : QObject(parent)
{
}

void TemperatureModel::update(double dt)
{
    if (dt <= 0.0)
        return;

    const double previous = m_temperature;

    // dT/dt = (P_heat - Q_loss) / (m * Cp)
    const double heatInput = m_heaterOn ? m_heaterPower : 0.0;
    const double heatLoss = m_heatLossCoeff * (m_temperature - m_envTemp);
    const double thermalCapacity = effectiveMass() * m_specificHeat;
    if (thermalCapacity <= kEpsilon)
        return;

    m_temperature += (heatInput - heatLoss) / thermalCapacity * dt;
    m_temperature = std::clamp(m_temperature, kMinTemperature, kMaxTemperature);

    if (m_temperature >= kOverheatThreshold) {
        if (!m_overheatReported) {
            m_overheatReported = true;
            emit overheatDetected(m_temperature);
        }
    } else if (m_temperature < kOverheatThreshold - 2.0) {
        m_overheatReported = false;   // 迟滞，避免抖动重复告警
    }

    if (std::abs(m_temperature - previous) > kEpsilon) {
        emit temperatureChanged();
        emit ntcResistanceChanged();
    }
}

void TemperatureModel::reset()
{
    setHeaterOn(false);
    m_overheatReported = false;
    setTemperature(m_envTemp);
}

double TemperatureModel::temperature() const
{
    return m_temperature;
}

double TemperatureModel::ntcResistance() const
{
    return resistanceAt(m_temperature);
}

double TemperatureModel::resistanceAt(double celsius) const
{
    // R = R25 * exp(B * (1/T - 1/T25))
    const double kelvin = celsius + kKelvinOffset;
    if (kelvin <= kEpsilon)
        return 0.0;
    return m_ntcR25 * std::exp(m_ntcB * (1.0 / kelvin - 1.0 / kNtcNominalKelvin));
}

double TemperatureModel::temperatureFromResistance(double ohm) const
{
    // B 值方程反解：1/T = 1/T25 + ln(R/R25) / B
    if (ohm <= kEpsilon || m_ntcR25 <= kEpsilon || std::abs(m_ntcB) <= kEpsilon)
        return 0.0;
    const double invT = 1.0 / kNtcNominalKelvin + std::log(ohm / m_ntcR25) / m_ntcB;
    if (std::abs(invT) <= kEpsilon)
        return 0.0;
    return 1.0 / invT - kKelvinOffset;
}

double TemperatureModel::netHeatPower() const
{
    const double heatInput = m_heaterOn ? m_heaterPower : 0.0;
    return heatInput - m_heatLossCoeff * (m_temperature - m_envTemp);
}

bool TemperatureModel::heaterOn() const
{
    return m_heaterOn;
}

void TemperatureModel::setHeaterOn(bool on)
{
    if (m_heaterOn == on)
        return;
    m_heaterOn = on;
    emit heaterOnChanged();
}

void TemperatureModel::setWaterMass(double mass_kg)
{
    const double mass = std::max(0.0, mass_kg);
    if (std::abs(mass - m_waterMass) <= kEpsilon)
        return;
    m_waterMass = mass;
    emit waterMassChanged();
}

double TemperatureModel::waterMass() const
{
    return m_waterMass;
}

void TemperatureModel::setEnvironmentTemp(double celsius)
{
    if (std::abs(celsius - m_envTemp) <= kEpsilon)
        return;
    m_envTemp = celsius;
    emit environmentTempChanged();
}

double TemperatureModel::environmentTemp() const
{
    return m_envTemp;
}

void TemperatureModel::setTemperature(double celsius)
{
    const double value = std::clamp(celsius, kMinTemperature, kMaxTemperature);
    if (std::abs(value - m_temperature) <= kEpsilon)
        return;
    m_temperature = value;
    emit temperatureChanged();
    emit ntcResistanceChanged();
}

void TemperatureModel::setHeaterPower(double watt)
{
    m_heaterPower = std::max(0.0, watt);
}

void TemperatureModel::setSpecificHeat(double joulePerKgK)
{
    m_specificHeat = std::max(kEpsilon, joulePerKgK);
}

void TemperatureModel::setHeatLossCoeff(double wattPerKelvin)
{
    m_heatLossCoeff = std::max(0.0, wattPerKelvin);
}

void TemperatureModel::setNtcParameters(double r25Ohm, double bValueKelvin)
{
    m_ntcR25 = std::max(kEpsilon, r25Ohm);
    m_ntcB = bValueKelvin;
    emit ntcResistanceChanged();
}

double TemperatureModel::heaterPower() const
{
    return m_heaterPower;
}

double TemperatureModel::specificHeat() const
{
    return m_specificHeat;
}

double TemperatureModel::heatLossCoeff() const
{
    return m_heatLossCoeff;
}

double TemperatureModel::ntcR25() const
{
    return m_ntcR25;
}

double TemperatureModel::ntcB() const
{
    return m_ntcB;
}

double TemperatureModel::effectiveMass() const
{
    // 空桶时以内胆等效热容代替，防止除零并模拟干烧快速升温
    return std::max(kMinEffectiveMass, m_waterMass);
}

} // namespace whm
