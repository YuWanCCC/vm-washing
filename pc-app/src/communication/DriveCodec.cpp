#include "DriveCodec.h"
#include "MatCRC8.h"

#include <array>

namespace whm {
namespace {
DriveField field(const char *key, const char *label, int byte, int width = 1,
                 bool editable = false, double scale = 1, const char *unit = "",
                 double offset = 0, int mask = -1, int shift = 0,
                 QStringList bits = {})
{
    return {QString::fromLatin1(key), QString::fromUtf8(label), byte, width,
            mask < 0 ? (width == 2 ? 65535 : 255) : mask, shift, scale, offset,
            QString::fromUtf8(unit), editable, bits};
}
const QStringList load1Bits = {"3D传感器供电", "排水泵", "风扇", "清洗阀/电三通",
                              "粒子发生器", "BLDC风机供电", "压缩机", "蒸汽发生器"};
const QStringList load2Bits = {"辅热", "电源PWM", "AC电机正转", "AC电机反转",
                              "循环水冷泵", "电机控制", "", "测试模式"};
const QStringList loadFaults = {"湿度传感器", "NTC1", "NTC2", "辅热传感器",
                               "门锁", "继电器", "压机过温", "单负载板通信"};
const QStringList motorFaults = {"低压", "高压", "失步", "IPM过温", "过电流",
                                "堵转", "缺相", "通信故障"};
const QStringList compressorFaults1 = {"", "PFC过流", "失步/失速", "过功率",
                                      "IPM过温", "IPM过流", "低压", "高压"};
const QStringList compressorFaults2 = {"", "通信故障", "", "", "PFC采样电路",
                                      "电机采样电路", "堵转", "缺相"};
const QStringList fanFaults = {"缺相", "堵转", "失步", "ADC异常", "过温", "过流", "低压", "高压"};

const std::array<QVector<DriveField>, 5> requests = {{
    {},
    {
        field("humidityPeriod", "湿度采样周期", 3, 2, false, 1, "ms"),
        field("charge", "湿度Charge", 5, 2, false, 1, "ms"),
        field("discharge", "湿度DisCharge", 7, 2, false, 1, "ms"),
        field("lockCommand", "门锁命令（0保持/1解锁/2上锁）", 9, 1, false, 1, "", 0, 15),
        field("lockMaxAttempts", "门锁最大尝试次数（0清累计）", 9, 1, false, 1, "", 0, 15, 4),
        field("lockPeriod", "门锁周期", 10, 2, false, 1, "ms"),
        field("lockHighTime", "门锁高电平时间", 12, 2, false, 1, "ms"),
        field("load1", "负载1", 14, 1, false, 1, "", 0, 255, 0, load1Bits),
        field("load2", "负载2", 15, 1, false, 1, "", 0, 191, 0, load2Bits)
    },
    {
        field("motorCode", "电机代号（V2.9）", 3), field("platform", "整机平台", 4),
        field("ratio", "传动比（0非法）", 5),
        field("direction", "设定方向（1右/2左）", 6),
        field("speed", "设定桶转速", 7, 2, false, 1, "rpm"),
        field("acceleration", "设定桶加速度", 9, 1, false, 1, "rpm/s"),
        field("weigh", "称重命令", 10)
    },
    {
        field("reset", "软件复位", 3, 1, false, 1, "", 0, 1, 7),
        field("clearFault", "清除故障", 3, 1, false, 1, "", 0, 1, 6),
        field("pfcOff", "强制关闭PFC", 3, 1, false, 1, "", 0, 1, 5),
        field("suppressionOff", "关闭低频振动抑制", 3, 1, false, 1, "", 0, 1, 4),
        field("mode", "模式（0/1正常 2AEA 3PCBA）", 3, 1, false, 1, "", 0, 3),
        field("acceleration", "设定加速度", 4, 2, false, 1, "rpm/s"),
        field("speed", "设定转速", 6, 2, false, 1, "rpm"),
        field("targetTemp", "目标出风口温度", 8, 1, false, 1, "℃"),
        field("lowVoltage", "低电压设定", 9, 1, false, 2, "V"),
        field("highVoltage", "高电压设定", 10, 1, false, 2, "V"),
        field("highCurrent", "高电流设定", 11, 1, false, 0.1, "A"),
        field("highTemp", "高温设定", 12, 1, false, 1, "℃"),
        field("model", "压缩机型号（0/1/2/4）", 13),
        field("suppressionRatio", "低频抑制电流比例（0=100%）", 14)
    },
    {
        field("testMode", "测试模式", 3, 1, false, 1, "", 0, 1, 7),
        field("direction", "设定方向（1顺/2逆）", 3, 1, false, 1, "", 0, 3),
        field("acceleration", "设定加速度", 4, 1, false, 40, "rpm/s"),
        field("speed", "设定转速", 5, 1, false, 100, "rpm"),
        field("lowVoltage", "低电压设定", 6, 1, false, 2, "V"),
        field("highVoltage", "高电压设定", 7, 1, false, 2, "V"),
        field("highCurrent", "高电流设定", 8, 1, false, 0.1, "A"),
        field("highTemp", "高温设定", 9, 1, false, 1, "℃")
    }
}};
const std::array<QVector<DriveField>, 5> responses = {{
    {},
    {
        field("version", "软件版本", 3, 1, true),
        field("ntc1", "NTC1采样原始值", 4, 2, true),
        field("ntc2", "NTC2采样原始值", 6, 2, true),
        field("auxNtc", "辅热NTC原始值", 8, 2, true),
        field("humidity", "湿度原始值", 10, 2, true),
        field("door", "门开关（0其他/1开/2关）", 14, 1, true, 1, "", 0, 3),
        field("floatSwitch", "浮子开关", 14, 1, true, 1, "", 0, 1, 2),
        field("waterEmpty", "蒸汽洗盒缺水", 14, 1, true, 1, "", 0, 1, 3),
        field("waterFull", "蒸汽洗盒水位满", 14, 1, true, 1, "", 0, 1, 4),
        field("load1", "实际负载1", 15, 1, false, 1, "", 0, 255, 0, load1Bits),
        field("lockPeriod", "门锁设定周期", 16, 2, false, 1, "ms"),
        field("lockAttempts", "门锁尝试次数", 18, 1, true),
        field("lockPulses", "500ms门锁检测脉冲数", 19, 1, true),
        field("lockState", "门锁状态（0其他/1开/2关/3动作中）", 20, 1, true, 1, "", 0, 3),
        field("fault", "负载板故障", 21, 1, true, 1, "", 0, 255, 0, loadFaults),
        field("frequency", "电源频率", 22, 1, true, 1, "Hz"),
        field("load2", "实际负载2", 23, 1, false, 1, "", 0, 191, 0, load2Bits)
    },
    {
        field("version", "软件版本", 3, 1, true), field("motorCode", "电机代号", 4),
        field("platform", "整机平台", 5), field("vendor", "变频板厂家", 6, 1, true),
        field("actualDirection", "实际方向（1右/2左）", 7),
        field("actualSpeed", "实际桶转速", 8, 2, false, 1, "rpm"),
        field("actualAcceleration", "实际桶加速度", 10, 1, false, 1, "rpm/s"),
        field("direction", "设定方向", 11),
        field("speed", "设定桶转速", 12, 2, false, 1, "rpm"),
        field("acceleration", "设定桶加速度", 14, 1, false, 1, "rpm/s"),
        field("weight", "称重值", 15, 1, true),
        field("fault", "电机故障", 16, 1, true, 1, "", 0, 255, 0, motorFaults),
        field("ipmTemp", "IPM温度编码", 17, 1, true, 1, "℃", -40),
        field("busVoltage", "母线电压编码", 18, 1, true, 0.5, "V"),
        field("dVoltage", "D轴电压编码", 19, 1, true, 0.5, "V"),
        field("qVoltage", "Q轴电压编码", 20, 1, true, 0.5, "V"),
        field("dCurrent", "D轴电流原始值（倍率待确认）", 21, 1, true),
        field("qCurrent", "Q轴电流原始值（倍率待确认）", 22, 1, true),
        field("power", "有功功率", 23, 2, true, 1, "W")
    },
    {
        field("version", "软件版本", 3, 1, true),
        field("pfcOff", "强制关闭PFC", 4, 1, false, 1, "", 0, 1, 5),
        field("suppression", "低频抑制档位", 4, 1, false, 1, "", 0, 7, 2),
        field("mode", "测试模式", 4, 1, false, 1, "", 0, 3),
        field("actualAcceleration", "实际加速度", 5, 2, false, 1, "rpm/s"),
        field("acceleration", "设定加速度", 7, 2, false, 1, "rpm/s"),
        field("actualSpeed", "实际转速", 9, 2, false, 1, "rpm"),
        field("speed", "设定转速", 11, 2, false, 1, "rpm"),
        field("outletTemp", "实际出风口温度", 13, 1, true, 1, "℃"),
        field("targetTemp", "设定出风口温度", 14, 1, false, 1, "℃"),
        field("busVoltage", "实际母线电压编码", 15, 1, true, 2, "V"),
        field("power", "实际电机功率编码", 16, 1, true, 10, "W"),
        field("ipmTemp", "IPM温度编码", 17, 1, true, 1, "℃", -40),
        field("fault1", "压机故障1", 18, 1, true, 1, "", 0, 254, 0, compressorFaults1),
        field("fault2", "压机故障2", 19, 1, true, 1, "", 0, 242, 0, compressorFaults2),
        field("limitReason", "限速原因", 20, 1, true, 1, "", 0, 240, 0,
              {"", "", "", "", "电压限制", "过温限制", "过流限制", "过功率限制"}),
        field("model", "压缩机型号", 21)
    },
    {
        field("version", "软件版本", 3, 1, true),
        field("testMode", "测试模式", 4, 1, false, 1, "", 0, 1, 7),
        field("actualDirection", "实际方向", 4, 1, false, 1, "", 0, 3, 2),
        field("direction", "设定方向", 4, 1, false, 1, "", 0, 3),
        field("actualAcceleration", "实际加速度编码", 5, 1, false, 40, "rpm/s"),
        field("acceleration", "设定加速度编码", 6, 1, false, 40, "rpm/s"),
        field("actualSpeed", "实际转速编码", 7, 1, false, 100, "rpm"),
        field("speed", "设定转速编码", 8, 1, false, 100, "rpm"),
        field("busVoltage", "实际母线电压编码", 9, 1, true, 2, "V"),
        field("current", "输出力矩电流编码", 10, 1, true, 0.1, "A"),
        field("power", "实际功率", 11, 1, true, 1, "W"),
        field("ipmTemp", "实际IPM温度", 12, 1, true, 1, "℃"),
        field("fault", "风机故障", 13, 1, true, 1, "", 0, 255, 0, fanFaults)
    }
}};
}

int DriveCodec::length(int address, bool request)
{
    static constexpr int tx[] = {0, 18, 14, 17, 13};
    static constexpr int rx[] = {0, 25, 28, 25, 17};
    return address >= 1 && address <= 4 ? (request ? tx[address] : rx[address]) : 0;
}

QString DriveCodec::boardName(int address)
{
    static const QStringList names = {"未知地址", "显示负载板", "变频电机", "变频压缩机", "变频风机"};
    return names.value(address, names[0]);
}

const QVector<DriveField> &DriveCodec::fields(int address, bool request)
{
    return (request ? requests : responses)[address >= 1 && address <= 4 ? address : 0];
}

QVariantList DriveCodec::fieldMetadata(int address, bool request)
{
    QVariantList list;
    for (const auto &f : fields(address, request)) {
        list.append(QVariantMap{{"key", f.key}, {"label", f.label}, {"byte", f.byte},
                    {"width", f.width}, {"max", f.mask}, {"scale", f.scale},
                    {"offset", f.offset}, {"unit", f.unit}, {"editable", f.editable}, {"bits", f.bits}});
    }
    return list;
}

bool DriveCodec::valid(const QByteArray &raw, bool request)
{
    if (raw.size() < 5 || quint8(raw[0]) != 0xfa || quint8(raw[1]) != 0xfa)
        return false;
    const int n = length(quint8(raw[2]), request);
    return n > 0 && quint8(raw[3]) == n && raw.size() == n + 4
        && MatCRC8::verify(reinterpret_cast<const uint8_t *>(raw.constData()),
                           raw.size() - 1, quint8(raw.back()));
}

QVariantMap DriveCodec::decode(const DriveFrame &frame)
{
    QVariantMap values;
    if (!valid(frame.raw, frame.request) || frame.address != quint8(frame.raw[2]))
        return values;
    for (const auto &f : fields(frame.address, frame.request)) {
        const int index = f.byte + 1;
        int raw = quint8(frame.raw[index]);
        if (f.width == 2) raw |= quint8(frame.raw[index + 1]) << 8;
        values.insert(f.key, (raw >> f.shift) & f.mask);
    }
    return values;
}

QByteArray DriveCodec::encode(int address, bool request, const QVariantMap &values)
{
    const int n = length(address, request);
    if (!n) return {};
    QByteArray raw(n + 4, char(0));
    raw[0] = raw[1] = char(0xfa);
    raw[2] = char(address);
    raw[3] = char(n);
    for (const auto &f : fields(address, request)) {
        const int value = (qBound(0, values.value(f.key).toInt(), f.width == 2 ? 65535 : 255) & f.mask) << f.shift;
        const int index = f.byte + 1;
        raw[index] = char(quint8(raw[index]) | (value & 255));
        if (f.width == 2) raw[index + 1] = char((value >> 8) & 255);
    }
    raw[raw.size() - 1] = char(MatCRC8::calculate(
        reinterpret_cast<const uint8_t *>(raw.constData()), raw.size() - 1));
    return raw;
}

void DriveStreamParser::reset()
{
    m_buffer.clear();
    m_lastByteMs = -1;
}

QVector<DriveParseEvent> DriveStreamParser::expire(qint64 nowMs)
{
    if (m_buffer.isEmpty() || m_lastByteMs < 0 || nowMs - m_lastByteMs <= 50)
        return {};
    const auto data = m_buffer;
    reset();
    return {{DriveParseEvent::GapTimeout, 0, data}};
}

QVector<DriveParseEvent> DriveStreamParser::feed(const QByteArray &data, qint64 nowMs)
{
    auto events = expire(nowMs);
    if (data.isEmpty()) return events;
    m_lastByteMs = nowMs;
    // 分块处理而不是截去有效的长串粘包。
    for (int offset = 0; offset < data.size(); offset += MaxBuffer / 2) {
        m_buffer.append(data.mid(offset, MaxBuffer / 2));
        while (!m_buffer.isEmpty()) {
            const int header = m_buffer.indexOf(QByteArray::fromHex("fafa"));
            if (header < 0) {
                const int discard = m_buffer.size() - (quint8(m_buffer.back()) == 0xfa ? 1 : 0);
                if (discard) events.append({DriveParseEvent::Noise, 0, m_buffer.left(discard)});
                m_buffer.remove(0, discard);
                break;
            }
            if (header > 0) {
                events.append({DriveParseEvent::Noise, 0, m_buffer.left(header)});
                m_buffer.remove(0, header);
            }
            if (m_buffer.size() < 4) break;
            const int address = quint8(m_buffer[2]);
            const int n = quint8(m_buffer[3]);
            const bool known = address >= 1 && address <= 4;
            const bool request = known && n == DriveCodec::length(address, true);
            const bool echo = known && n == DriveCodec::length(address, false);
            if (n < 1 || (known && !request && !echo)) {
                events.append({DriveParseEvent::LengthError, address, m_buffer.left(4)});
                m_buffer.remove(0, 1);
                continue;
            }
            if (m_buffer.size() < n + 4) break;
            const auto raw = m_buffer.left(n + 4);
            if (!MatCRC8::verify(reinterpret_cast<const uint8_t *>(raw.constData()),
                                 raw.size() - 1, quint8(raw.back()))) {
                events.append({DriveParseEvent::CrcError, address, raw});
                m_buffer.remove(0, 1);
                continue;
            }
            events.append({request ? DriveParseEvent::Request : echo ? DriveParseEvent::Echo
                                                                    : DriveParseEvent::Unknown,
                           address, raw});
            m_buffer.remove(0, raw.size());
        }
    }
    return events;
}

} // namespace whm
