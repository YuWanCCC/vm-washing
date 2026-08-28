#ifndef WHM_MATCODEC_H
#define WHM_MATCODEC_H

#include <QByteArray>
#include <QObject>
#include <QVector>

namespace whm {

/*!
 * \brief MAT 协议帧结构体（滚筒洗衣机，规范版本 2.0）
 *
 * 帧格式（LSB-first，8N1）：
 *   0xFA | Address(1B) | Length(1B) | Command(1B) | SDU(Length-1 B) | FCS(1B)
 *
 * - Header  = 0xFA（固定）
 * - Address = { Bit6-4: 目标地址, Bit2-0: 源地址 }
 *             监控软件地址 = 011b；主控板地址 = 001b
 *             PC→主控: 0x13（目标=001, 源=011）
 *             主控→PC: 0x31（目标=011, 源=001）
 * - Length  = Command 到 SDU 末尾的字节数（**不含 FCS**）
 * - Command = 指令码（见 MatCommand）
 * - SDU     = 数据载荷（Length-1 字节）
 * - FCS     = CRC-8（poly=0x9B），覆盖 Header(0xFA)+Address+Length+Command+SDU
 */
struct MatFrame
{
    //! MAT 帧头
    static constexpr uint8_t Header = 0xFA;

    //! 地址：PC（监控软件）发给主控
    static constexpr uint8_t AddrPcToMcu = 0x13;
    //! 地址：主控回复 PC
    static constexpr uint8_t AddrMcuToPc = 0x31;

    uint8_t address = AddrPcToMcu;
    uint8_t command = 0x00;
    QByteArray sdu;             //!< 数据载荷（不含 FCS）

    bool isValid = false;       //!< 解析结果标记
};

/*!
 * \brief MAT 协议指令码
 */
enum class MatCommand : uint8_t
{
    Query     = 0x01,  //!< 查询主控状态（SDU=[0x01]，回复 147B 状态数据）
    Control   = 0x08,  //!< 控制执行器（SDU=6B 位图）
};

/*!
 * \brief MAT 控制帧的 6 字节执行器位图
 *
 * 各 bit 为 1 时打开对应执行器，为 0 时关闭。
 * 参考协议文档 Sheet2 控制命令（Command=0x08）。
 */
struct MatControlBits
{
    // BYTE1
    bool preWashValve    : 1;  //!< bit7 预洗进水阀
    bool mainWashValve   : 1;  //!< bit6 主洗进水阀
    bool rinseValve      : 1;  //!< bit5 冲洗进水阀
    bool condenserValve  : 1;  //!< bit4 冷凝进水阀
    bool drainPump       : 1;  //!< bit3 排水泵
    bool waterHeater     : 1;  //!< bit2 水加热器
    bool airHeatDry      : 1;  //!< bit1 风干加热
    bool fan             : 1;  //!< bit0 风机

    // BYTE2
    bool doorLock        : 1;  //!< bit7 门锁
    bool detergent       : 1;  //!< bit6 洗涤剂投入
    bool softener        : 1;  //!< bit5 柔顺剂投入
    bool uvLed           : 1;  //!< bit4 UV杀菌灯
    bool reserved2b3     : 1;
    bool reserved2b2     : 1;
    bool reserved2b1     : 1;
    bool reserved2b0     : 1;

    // BYTE3~6 保留，全为 0
    uint8_t reserved[4] = {0, 0, 0, 0};

    MatControlBits() { memset(this, 0, sizeof(*this)); }
};

/*!
 * \brief MAT 主控状态回复数据（Command=0x01 的回复）
 *
 * 协议数据长度不固定，最大扩展到 67 字节。
 * decodeStatus() 按实际长度自适应解析，sduLen 记录实际 SDU 字节数。
 * rawBytes 保存原始 SDU，可按需访问任意字段。
 */
struct MatStatus
{
    QByteArray rawBytes;        //!< 原始 SDU 字节
    int sduLen = 0;             //!< 实际 SDU 长度（0~67）
    bool isValid = false;

    // ---- BYTE 1-2: 协议基本信息 ----
    uint8_t protocolVersion = 0;  //!< 偏移 0: 协议版本（2=新协议）
    uint8_t machineType     = 0;  //!< 偏移 1: 机器类型（1=滚筒, 2=波轮, 3=干衣机）

    // ---- BYTE 3 (偏移 2): 开关状态 1 ----
    bool antiCrease       = false; //!< bit7 防皱
    bool doorSwitch       = false; //!< bit6 门状态
    bool drumLight        = false; //!< bit5 筒灯
    bool blueLight        = false; //!< bit4 蓝光灯
    bool uvLight          = false; //!< bit3 UV灯
    bool fan              = false; //!< bit2 冷却风扇
    bool floatSwitch      = false; //!< bit1 浮子开关
    bool steamLowWater    = false; //!< bit0 蒸汽发生器缺水

    // ---- BYTE 4 (偏移 3): 开关状态 2 ----
    bool compressorSwitch = false; //!< bit7 压缩机电源
    bool drainPump        = false; //!< bit6 排水泵
    bool electricTee      = false; //!< bit5 电三通
    bool steamGenerator   = false; //!< bit4 蒸汽发生器
    bool anion            = false; //!< bit3 负离子
    bool rotateLeft       = false; //!< bit2 电机左转
    bool rotateRight      = false; //!< bit1 电机右转
    bool motorStop        = false; //!< bit0 电机停

    // ---- BYTE 5 (偏移 4): 开关状态 3 ----
    bool compressor       = false; //!< bit2 变频压缩机
    bool heater           = false; //!< bit1 加热管
    bool ctrlFan          = false; //!< bit0 变频风机

    // ---- 传感器数据 ----
    uint16_t humidity         = 0; //!< 偏移 5-6: 湿度（16-bit, LSB first）
    uint16_t humidityRaw      = 0; //!< 偏移 7-8: 原始湿度
    uint8_t  dryStep          = 0; //!< 偏移 9: 烘干进程
    uint8_t  load             = 0; //!< 偏移 10: 负载量
    int16_t  motorSpeed       = 0; //!< 偏移 11-12: 电机转速（有符号 16-bit）
    uint16_t ntc1Raw          = 0; //!< 偏移 13-14: NTC1 原始值
    uint8_t  ntc1Int          = 0; //!< 偏移 15: NTC1 整数位
    uint8_t  ntc1Dec          = 0; //!< 偏移 16: NTC1 小数位
    uint16_t ntc2Raw          = 0; //!< 偏移 17-18: NTC2 原始值
    uint8_t  ntc2Int          = 0; //!< 偏移 19: NTC2 整数位
    uint8_t  ntc2Dec          = 0; //!< 偏移 20: NTC2 小数位

    // ---- 程序与时间 ----
    uint8_t  programId        = 0; //!< 偏移 21: 程序 ID
    uint16_t programTime      = 0; //!< 偏移 22-23: 程序设定时间
    uint16_t timeToEnd        = 0; //!< 偏移 24-25: 运行剩余时间

    // ---- 变频数据 ----
    uint16_t fanSpeed         = 0; //!< 偏移 26-27: 变频风机转速
    uint16_t compressorSpeed  = 0; //!< 偏移 28-29: 变频压缩机转速
    uint16_t compressorPower  = 0; //!< 偏移 30-31: 变频压缩机功率
    int8_t   paramP           = 0; //!< 偏移 32: 调温参数 P
    int8_t   paramI           = 0; //!< 偏移 33: 调温参数 I
    int8_t   paramD           = 0; //!< 偏移 34: 调温参数 D

    // ---- 3D 传感器 ----
    uint16_t sensor3dRef      = 0; //!< 偏移 35-36: 参考 sensor 信号量
    uint16_t sensor3dSense    = 0; //!< 偏移 37-38: 感应 sensor 信号量
    uint16_t sensor3dMoisture = 0; //!< 偏移 39-40: 3D 含水率 / 软件版本
    uint16_t sensor3dStdDev   = 0; //!< 偏移 41-42: 初值差值标准差
    int16_t  sensor3dFitting  = 0; //!< 偏移 43-44: 差值拟合（最高位 1=负数）
    uint16_t sensor3dFitStd   = 0; //!< 偏移 45-46: 拟合标准差

    // ---- 故障码 ----
    uint8_t errorCode         = 0; //!< 偏移 47: 整机报警代码
    uint8_t motorErrorCode    = 0; //!< 偏移 48: 变频电机故障代码
    uint8_t fanErrorCode      = 0; //!< 偏移 49: 变频风机故障代码
    uint8_t compErrorCode     = 0; //!< 偏移 50: 变频压缩机故障代码

    // ---- 版本信息（偏移 51-60）----
    uint8_t versionA = 0;  //!< A 显示（显示+主控）
    uint8_t versionB = 0;  //!< B 主控（负载板）
    uint8_t versionC = 0;  //!< C 变频主电机
    uint8_t versionE = 0;  //!< E 变频压缩机
    uint8_t versionF = 0;  //!< F 变频风机
    uint8_t versionP = 0;  //!< P 电机平台号
    uint8_t versionT = 0;  //!< T 触摸按键
    uint8_t versionD = 0;  //!< D 变频泵
    uint8_t versionG = 0;  //!< G 3D 传感器
    uint8_t versionO = 0;  //!< O 单板版本

    // ---- 功率与湿度 ----
    uint16_t fanPower         = 0; //!< 偏移 61-62: 风机功率
    uint16_t electricPower    = 0; //!< 偏移 63-64: 电机功率
    uint8_t  humidityRatio   = 0; //!< 偏移 65: 湿度占比
    uint8_t  temp3dFitting   = 0; //!< 偏移 66: 3D 温度拟合
};

/*!
 * \brief MAT 协议编解码器
 *
 * - encode*() 将参数打包为完整帧字节流（含 0xFA 头和 FCS）
 * - parseFrame() 从原始字节流中提取并验证 MatFrame
 * - decodeStatus() 将 MatFrame.sdu 解析为 MatStatus
 *
 * 线程安全：无状态，所有方法为纯函数，可跨线程使用。
 */
class MatCodec
{
public:
    MatCodec() = delete;

    // ---- 编码 -------------------------------------------------------

    //! 编码查询帧（Command=0x01，SDU=[0x01]）
    static QByteArray encodeQuery();

    //! 编码控制帧（Command=0x08，SDU=6B 位图）
    static QByteArray encodeControl(uint8_t b1, uint8_t b2,
                                    uint8_t b3 = 0, uint8_t b4 = 0,
                                    uint8_t b5 = 0, uint8_t b6 = 0);

    // ---- 解码 -------------------------------------------------------

    /*!
     * \brief 从字节流中解析一帧
     * \param buf   输入字节流
     * \param[out] consumed  成功时置为消耗的字节数；失败时置为跳过的字节数（1）
     * \return 解析结果帧（isValid=true 表示成功）
     */
    static MatFrame parseFrame(const QByteArray &buf, int *consumed);

    //! 从 MatFrame.sdu 解析状态结构（自适应长度，最大 67 字节）
    static MatStatus decodeStatus(const MatFrame &frame);

private:
    //! 组装帧：计算 Length = 1(cmd) + sdu.size()，计算 FCS，返回完整字节流
    static QByteArray buildFrame(uint8_t address, uint8_t command,
                                 const QByteArray &sdu);
};

} // namespace whm

#endif // WHM_MATCODEC_H
