#ifndef WHM_SCENARIOMANAGER_H
#define WHM_SCENARIOMANAGER_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace whm {

/*!
 * \brief 测试场景管理器：扫描 config/scenarios 目录下的场景 JSON 并提供查询。
 *
 * 场景文件结构（见 standard_wash.json）：
 *   name / description / initialState / parameters / steps[]
 * 其中 steps 内每一步可含 actuators、motor、action、waitUntil、durationSec、timeoutSec。
 * 本类只负责“读取与索引”，具体如何驱动仿真由 SimulationEngine 解释。
 */
class ScenarioManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList availableScenarios READ availableScenarios NOTIFY scenariosChanged)
    Q_PROPERTY(QString currentScenario READ currentScenario NOTIFY currentScenarioChanged)
    Q_PROPERTY(QString scenariosPath READ scenariosPath NOTIFY scenariosChanged)

public:
    explicit ScenarioManager(QObject *parent = nullptr);

    //! 载入指定场景（name 为文件基名，如 standard_wash），成功后发射 scenarioLoaded
    Q_INVOKABLE void loadScenario(const QString &name);
    //! 可用场景的文件基名列表（升序）
    Q_INVOKABLE QStringList availableScenarios() const;
    //! 场景完整配置，未找到时返回空 map
    Q_INVOKABLE QVariantMap getScenarioConfig(const QString &name) const;
    //! 场景中文显示名（JSON 的 name 字段），缺失时回退为文件基名
    Q_INVOKABLE QString displayName(const QString &name) const;
    //! 重新扫描场景目录
    Q_INVOKABLE void refresh();

    QString currentScenario() const;
    QString scenariosPath() const;
    //! 设置场景目录并立即重新扫描；支持 file:// URL
    void setScenariosPath(const QString &path);

    //! 定位随程序发布的 config 目录（依次尝试可执行目录及其上层、当前工作目录）
    static QString defaultConfigDir();
    //! 把 QML 传入的 file:/// URL 或相对路径统一转换为本地绝对路径
    static QString resolveLocalPath(const QString &pathOrUrl);

signals:
    void scenariosChanged();
    void currentScenarioChanged();
    //! 场景载入成功，config 为完整场景内容
    void scenarioLoaded(const QString &name, const QVariantMap &config);
    void errorOccurred(const QString &message);

private:
    void scanScenarios();

    QString m_scenariosPath;
    QMap<QString, QVariantMap> m_scenarios;
    QString m_currentScenario;
};

} // namespace whm

#endif // WHM_SCENARIOMANAGER_H
