#include "MatCurveRecorder.h"

#include <algorithm>
#include <functional>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

namespace whm {

namespace {

//! CSV 表头与列顺序（首列 t 为墙钟秒，其余为采样字段）
constexpr const char *kHeader = "t,speed,ntc1,ntc2,hum,fan,comp,cpw,fpw,epw";
const QStringList kColumns = { QStringLiteral("t"),    QStringLiteral("speed"),
                               QStringLiteral("ntc1"), QStringLiteral("ntc2"),
                               QStringLiteral("hum"),  QStringLiteral("fan"),
                               QStringLiteral("comp"), QStringLiteral("cpw"),
                               QStringLiteral("fpw"),  QStringLiteral("epw") };

//! 从文件名 mat_curve_yyyyMMdd_HHmmss.csv 解析出 "MM-dd HH:mm:ss"，失败返回原名
QString labelFromFileName(const QString &fileName)
{
    const QString base = QFileInfo(fileName).baseName(); // mat_curve_20260930_161244
    const QStringList p = base.split(QLatin1Char('_'));
    if (p.size() >= 4 && p[2].size() == 8 && p[3].size() == 6) {
        const QString &d = p[2];
        const QString &t = p[3];
        return QStringLiteral("%1-%2 %3:%4:%5")
            .arg(d.mid(4, 2), d.mid(6, 2), t.mid(0, 2), t.mid(2, 2), t.mid(4, 2));
    }
    return fileName;
}

//! 秒数格式化为紧凑时长文本："<1h → mm:ss"，">=1h → h:mm:ss"
QString formatDuration(double sec)
{
    if (sec < 0)
        sec = 0;
    const qint64 s = static_cast<qint64>(sec + 0.5);
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    const qint64 ss = s % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
            .arg(ss, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(ss, 2, 10, QLatin1Char('0'));
}

} // namespace

MatCurveRecorder::MatCurveRecorder(QObject *parent)
    : QObject(parent)
    , m_dir(QCoreApplication::applicationDirPath() + QStringLiteral("/mat_records"))
{
    QDir().mkpath(m_dir);
}

MatCurveRecorder::~MatCurveRecorder()
{
    if (m_file.isOpen())
        m_file.close();
}

bool MatCurveRecorder::startSession()
{
    // 已在录制中则保持当前会话，避免误触重复创建并覆盖正在写入的文件
    if (!m_session.isEmpty())
        return true;

    m_session = QStringLiteral("mat_curve_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_points = 0;
    // 惰性建文件：仅登记会话名，首帧数据到达时才真正创建，避免生成只有表头的空会话
    emit sessionChanged();
    emit recordedPointsChanged();
    return true;
}

bool MatCurveRecorder::ensureOpen()
{
    if (m_file.isOpen())
        return true;
    if (m_session.isEmpty())
        return false;

    m_file.setFileName(m_dir + QLatin1Char('/') + m_session);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_session.clear();          // 打开失败：撤销录制状态
        emit sessionChanged();
        return false;
    }
    QTextStream out(&m_file);
    out << kHeader << Qt::endl;     // Qt::endl 带 flush
    return true;
}

void MatCurveRecorder::append(const QVariantMap &sample)
{
    if (m_session.isEmpty())        // 未录制：显式丢弃，不写入任何已停止的会话
        return;
    if (!ensureOpen())
        return;

    QTextStream out(&m_file);
    for (int i = 0; i < kColumns.size(); ++i) {
        if (i > 0)
            out << ',';
        const double v = sample.value(kColumns[i]).toDouble();
        // 时间列保留毫秒精度，数值列 2 位小数足够回放显示
        out << QString::number(v, 'f', i == 0 ? 3 : 2);
    }
    out << Qt::endl;                // 每点 flush，进程异常退出不丢已写数据
    ++m_points;
    emit recordedPointsChanged();
}

void MatCurveRecorder::closeSession()
{
    if (m_session.isEmpty() && !m_file.isOpen())
        return;
    if (m_file.isOpen())
        m_file.close();             // 无数据时会话文件从未创建，天然不产生空文件
    m_session.clear();
    m_points = 0;
    emit sessionChanged();
    emit recordedPointsChanged();
}

QStringList MatCurveRecorder::listSessions() const
{
    QDir dir(m_dir);
    QStringList names = dir.entryList({ QStringLiteral("mat_curve_*.csv") },
                                      QDir::Files, QDir::Name);
    // 文件名内嵌时间戳，字典序即时间序；反转为新→旧
    std::sort(names.begin(), names.end(), std::greater<QString>());
    return names;
}

QVariantList MatCurveRecorder::loadSession(const QString &fileName) const
{
    QVariantList result;

    // 仅接受目录内文件名，避免路径穿越
    if (fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
        || fileName.contains(QStringLiteral("..")))
        return result;

    QFile file(m_dir + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&file);
    bool firstLine = true;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (firstLine) {  // 跳过表头
            firstLine = false;
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < kColumns.size())
            continue;
        QVariantMap point;
        for (int i = 0; i < kColumns.size(); ++i)
            point.insert(kColumns[i], parts[i].toDouble());
        result.append(point);
    }
    return result;
}

QVariantList MatCurveRecorder::sessionInfos() const
{
    QVariantList result;
    const QStringList names = listSessions(); // 已按新→旧排序
    for (const QString &name : names) {
        const QString path = m_dir + QLatin1Char('/') + name;
        QFileInfo fi(path);

        int points = 0;
        double t0 = 0.0;
        double t1 = 0.0;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            bool firstLine = true;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty())
                    continue;
                if (firstLine) { // 跳过表头
                    firstLine = false;
                    continue;
                }
                const double t = line.section(QLatin1Char(','), 0, 0).toDouble();
                if (points == 0)
                    t0 = t;
                t1 = t;
                ++points;
            }
        }

        const double duration = (points > 1) ? (t1 - t0) : 0.0;
        const bool isCurrent = (name == m_session);
        QString label = QStringLiteral("%1  ·  %2点  ·  %3")
                            .arg(labelFromFileName(name))
                            .arg(points)
                            .arg(formatDuration(duration));
        if (isCurrent)
            label += QStringLiteral("  ·  录制中");

        QVariantMap info;
        info.insert(QStringLiteral("name"), name);
        info.insert(QStringLiteral("label"), label);
        info.insert(QStringLiteral("points"), points);
        info.insert(QStringLiteral("durationSec"), duration);
        info.insert(QStringLiteral("sizeKb"), static_cast<double>(fi.size()) / 1024.0);
        info.insert(QStringLiteral("isCurrent"), isCurrent);
        result.append(info);
    }
    return result;
}

bool MatCurveRecorder::deleteSession(const QString &fileName)
{
    // 拒绝路径穿越与删除正在录制的会话
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/'))
        || fileName.contains(QLatin1Char('\\')) || fileName.contains(QStringLiteral("..")))
        return false;
    if (fileName == m_session)
        return false;
    return QFile::remove(m_dir + QLatin1Char('/') + fileName);
}

QString MatCurveRecorder::recordsDir() const
{
    return m_dir;
}

QString MatCurveRecorder::currentSession() const
{
    return m_session;
}

bool MatCurveRecorder::isRecording() const
{
    return !m_session.isEmpty();
}

int MatCurveRecorder::recordedPoints() const
{
    return m_points;
}

} // namespace whm
