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
 *             PC→主控: 0x31（目标=001, 源=011）
 *             主控→PC: 0x13（目标=011, 源=001）
 * - Length  = Command 到 SDU 末尾的字节数（**不含 FCS**）
 * - Command = 指令码（见 MatCommand）
 * - SDU     = 数据载荷（Length-1 字节）
 * - FCS     = CRC-8/MAXIM，覆盖 Address+Length+Command+SDU
 */
struct MatFrame
{
    //! MAT 帧头
    static constexpr uint8_t Header = 0xFA;

    //! 地址：PC（监控软件）发给主控
    static constexpr uint8_t AddrPcToMcu = 0x31;
    //! 地址：主控回复 PC
    static constexpr uint8_t AddrMcuToPc = 0x13;

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
 * \brief MAT 主控状态回复数据（Command=0x01 的回复，SDU=147B）
 *
 * 只解析项目中实际关心的字段；其余字段通过 rawBytes 访问。
 */
struct MatStatus
{
    QByteArray rawBytes;        //!< 完整 147 字节原始 SDU

    // 以下为常用字段（偏移参考协议 Sheet3）
    uint8_t  workStatus    = 0;  //!< 工作状态（偏移 0）
    uint8_t  faultCode     = 0;  //!< 故障码（偏移 1）
    uint8_t  childLock     = 0;  //!< 童锁
    uint16_t motorRpm      = 0;  //!< 电机转速（偏移 4~5, LSB first）
    uint8_t  waterLevel    = 0;  //!< 水位（偏移 6）
    uint8_t  waterTemp     = 0;  //!< 水温（偏移 7，单位 °C，偏移 = 40°C offset？）
    uint8_t  remainMin     = 0;  //!< 剩余时间分钟（偏移 8）
    uint8_t  remainSec     = 0;  //!< 剩余时间秒（偏移 9）

    bool isValid = false;
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

    //! 从 MatFrame.sdu 解析 147 字节状态结构
    static MatStatus decodeStatus(const MatFrame &frame);

private:
    //! 组装帧：计算 Length = 1(cmd) + sdu.size()，计算 FCS，返回完整字节流
    static QByteArray buildFrame(uint8_t address, uint8_t command,
                                 const QByteArray &sdu);
};

} // namespace whm

#endif // WHM_MATCODEC_H
