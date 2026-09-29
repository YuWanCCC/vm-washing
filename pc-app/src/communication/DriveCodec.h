#ifndef WHM_DRIVECODEC_H
#define WHM_DRIVECODEC_H

#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace whm {

//! V2.9 的 BYTE 编号从地址开始；双帧头不计入编号。
struct DriveField {
    QString key;
    QString label;
    int byte;
    int width = 1;
    int mask = 255;
    int shift = 0;
    double scale = 1.0;
    double offset = 0.0;
    QString unit;
    bool editable = false;
    QStringList bits;
};

struct DriveFrame {
    int address = 0;
    QByteArray raw;
    bool request = false;
};

struct DriveParseEvent {
    enum Kind { Request, Echo, Unknown, CrcError, LengthError, Noise, GapTimeout };
    Kind kind = Noise;
    int address = 0;
    QByteArray raw;
};

class DriveCodec {
public:
    static int length(int address, bool request);
    static QString boardName(int address);
    static const QVector<DriveField> &fields(int address, bool request);
    static QVariantList fieldMetadata(int address, bool request);
    static QVariantMap decode(const DriveFrame &frame);
    static QByteArray encode(int address, bool request, const QVariantMap &values);
    static bool valid(const QByteArray &raw, bool request);
};

//! 有界流式解析器。时间由调用者传入，便于确定性测试。
class DriveStreamParser {
public:
    QVector<DriveParseEvent> feed(const QByteArray &data, qint64 nowMs);
    QVector<DriveParseEvent> expire(qint64 nowMs);
    void reset();
    int bufferedSize() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
    qint64 m_lastByteMs = -1;
    static constexpr int MaxBuffer = 4096;
};

} // namespace whm
#endif
