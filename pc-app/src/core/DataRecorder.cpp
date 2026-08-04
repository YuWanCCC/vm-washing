#include "core/DataRecorder.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

namespace whm {

namespace {
//! recordCountChanged 信号节流间隔(ms)：引擎按帧记录时避免 UI 高频刷新
constexpr qint64 kCountNotifyIntervalMs = 200;
} // namespace

DataRecorder::DataRecorder(QObject *parent)
    : QObject(parent)
{
    m_records.reserve(4096);
}

void DataRecorder::startRecording()
{
    if (m_recording)
        return;
    m_recording = true;
    emit recordingChanged();
}

void DataRecorder::stopRecording()
{
    if (!m_recording)
        return;
    m_recording = false;
    emit recordingChanged();
    emit recordCountChanged(); //!< 停止时强制同步一次计数
}

void DataRecorder::clearRecords()
{
    if (m_records.isEmpty())
        return;
    m_records.clear();
    emit recordCountChanged();
}

bool DataRecorder::exportToCsv(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << QStringLiteral("timestamp_ms,channel,value\n");
    for (const Record &r : m_records)
        out << r.timestampMs << ',' << r.channel << ','
            << QString::number(r.value, 'g', 10) << '\n';

    file.close();
    return true;
}

void DataRecorder::record(const QString &channel, double value, qint64 timestampMs)
{
    if (!m_recording)
        return;

    // 环形缓冲：超过上限时丢弃最旧记录
    if (m_records.size() >= MAX_RECORDS)
        m_records.erase(m_records.begin(), m_records.begin() + (m_records.size() / 10));

    m_records.append(Record{timestampMs, channel, value});

    // 计数信号节流，避免每帧多次触发 UI 绑定重算
    static thread_local qint64 lastNotifyMs = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastNotifyMs >= kCountNotifyIntervalMs) {
        lastNotifyMs = now;
        emit recordCountChanged();
    }
}

QVariantList DataRecorder::recentRecords(int count) const
{
    QVariantList list;
    if (count <= 0 || m_records.isEmpty())
        return list;

    const int n = qMin(count, m_records.size());
    const int start = m_records.size() - n;
    list.reserve(n);
    for (int i = start; i < m_records.size(); ++i) {
        const Record &r = m_records.at(i);
        list.append(QVariantMap{
            {QStringLiteral("timestampMs"), r.timestampMs},
            {QStringLiteral("channel"), r.channel},
            {QStringLiteral("value"), r.value},
        });
    }
    return list;
}

bool DataRecorder::isRecording() const
{
    return m_recording;
}

int DataRecorder::recordCount() const
{
    return m_records.size();
}

QString DataRecorder::outputPath() const
{
    return m_outputPath;
}

void DataRecorder::setOutputPath(const QString &path)
{
    if (m_outputPath == path)
        return;
    m_outputPath = path;
    emit outputPathChanged();
}

} // namespace whm
