#ifndef WHM_DATARECORDER_H
#define WHM_DATARECORDER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace whm {

/*!
 * \brief 数据记录器：在仿真运行期间按帧缓存关键通道数据，支持导出 CSV。
 *
 * 由 IntegrationBridge 在引擎每帧 ticked 时调用 record() 采样；
 * 记录条数达到 MAX_RECORDS 上限后按"丢弃最旧记录"的环形策略滚动，
 * 避免长时间运行导致内存无限增长。
 */
class DataRecorder : public QObject
{
    Q_OBJECT
    //! 是否正在记录
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    //! 当前缓存记录条数
    Q_PROPERTY(int recordCount READ recordCount NOTIFY recordCountChanged)
    //! 默认导出路径（界面文件对话框的初始目录提示，可为空）
    Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY outputPathChanged)

public:
    //! 单条记录
    struct Record {
        qint64 timestampMs;  //!< 仿真时间(ms)
        QString channel;     //!< 通道名
        double value;        //!< 值
    };

    explicit DataRecorder(QObject *parent = nullptr);

    //! 开始记录（不清空已有数据，新数据追加在末尾）
    Q_INVOKABLE void startRecording();
    //! 停止记录
    Q_INVOKABLE void stopRecording();
    //! 清空全部记录
    Q_INVOKABLE void clearRecords();
    //! 导出全部记录为 CSV（列: timestamp_ms,channel,value），成功返回 true
    Q_INVOKABLE bool exportToCsv(const QString &filePath);

    //! 记录一个数据点（仅 recording 状态下生效）
    void record(const QString &channel, double value, qint64 timestampMs);

    //! 获取最近 count 条记录（QVariantMap: timestampMs/channel/value），供 UI 预览
    Q_INVOKABLE QVariantList recentRecords(int count) const;

    bool isRecording() const;
    int recordCount() const;
    QString outputPath() const;
    void setOutputPath(const QString &path);

signals:
    void recordingChanged();
    void recordCountChanged();
    void outputPathChanged();

private:
    QVector<Record> m_records;
    bool m_recording = false;
    QString m_outputPath;
    static constexpr int MAX_RECORDS = 100000; //!< 环形缓冲上限
};

} // namespace whm

#endif // WHM_DATARECORDER_H
