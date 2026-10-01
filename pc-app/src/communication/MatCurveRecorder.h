#ifndef WHM_MATCURVERECORDER_H
#define WHM_MATCURVERECORDER_H

#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace whm {

/*!
 * \brief MAT 曲线数据记录器：采样点按会话落盘为 CSV，支持历史会话列表与加载。
 *
 * 每次 startSession() 在可执行程序旁 mat_records/ 目录（自动创建）新建
 * mat_curve_yyyyMMdd_HHmmss.csv（首行表头）；append() 每点写一行并 flush，
 * 进程异常退出也不丢已写数据；loadSession() 读回整会话采样点数组供 QML
 * 曲线图回放显示。
 */
class MatCurveRecorder : public QObject
{
    Q_OBJECT
    //! 记录目录（exe 旁 mat_records/）
    Q_PROPERTY(QString recordsDir READ recordsDir CONSTANT)
    //! 当前会话文件名（无会话时为空）
    Q_PROPERTY(QString currentSession READ currentSession NOTIFY sessionChanged)
    //! 是否正在将采样点写入会话文件
    Q_PROPERTY(bool recording READ isRecording NOTIFY sessionChanged)
    //! 当前会话已写入的采样点数
    Q_PROPERTY(int recordedPoints READ recordedPoints NOTIFY recordedPointsChanged)

public:
    explicit MatCurveRecorder(QObject *parent = nullptr);
    ~MatCurveRecorder() override;

    //! 创建并打开新会话文件；已在录制时保持当前会话，成功返回 true
    Q_INVOKABLE bool startSession();
    //! 写入一个采样点（键同 QML 采样点：t/speed/ntc1/ntc2/hum/fan/comp/cpw/fpw/epw）
    Q_INVOKABLE void append(const QVariantMap &sample);
    //! 关闭当前会话
    Q_INVOKABLE void closeSession();
    //! 列出全部会话文件名（新→旧）
    Q_INVOKABLE QStringList listSessions() const;
    //! 加载指定会话，返回采样点数组（QVariantMap 集合），失败返回空列表
    Q_INVOKABLE QVariantList loadSession(const QString &fileName) const;
    //! 列出全部会话元信息（新→旧），每项含：
    //! name(文件名)/label(时间·点数·时长)/points/durationSec/sizeKb/isCurrent
    Q_INVOKABLE QVariantList sessionInfos() const;
    //! 删除指定会话文件；拒绝删除当前录制中的会话与非法路径，成功返回 true
    Q_INVOKABLE bool deleteSession(const QString &fileName);

    QString recordsDir() const;
    QString currentSession() const;
    bool isRecording() const;
    int recordedPoints() const;

signals:
    void sessionChanged();
    void recordedPointsChanged();

private:
    //! 惰性确保会话文件已打开（首帧数据到达时才真正创建，避免空会话）
    bool ensureOpen();

    QString m_dir;
    QFile m_file;
    QString m_session;
    int m_points = 0;
};

} // namespace whm

#endif // WHM_MATCURVERECORDER_H
