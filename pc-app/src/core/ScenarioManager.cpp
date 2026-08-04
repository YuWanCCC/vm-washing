#include "core/ScenarioManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace whm {

ScenarioManager::ScenarioManager(QObject *parent)
    : QObject(parent)
{
    const QString configDir = defaultConfigDir();
    if (!configDir.isEmpty())
        m_scenariosPath = QDir(configDir).filePath(QStringLiteral("scenarios"));
    scanScenarios();
}

void ScenarioManager::loadScenario(const QString &name)
{
    if (!m_scenarios.contains(name)) {
        // 目录可能在运行期新增了场景文件，重扫一次再判断
        scanScenarios();
        if (!m_scenarios.contains(name)) {
            emit errorOccurred(tr("场景不存在: %1").arg(name));
            return;
        }
    }

    if (m_currentScenario != name) {
        m_currentScenario = name;
        emit currentScenarioChanged();
    }
    emit scenarioLoaded(name, m_scenarios.value(name));
}

QStringList ScenarioManager::availableScenarios() const
{
    return m_scenarios.keys();
}

QVariantMap ScenarioManager::getScenarioConfig(const QString &name) const
{
    return m_scenarios.value(name);
}

QString ScenarioManager::displayName(const QString &name) const
{
    const QVariantMap config = m_scenarios.value(name);
    const QString title = config.value(QStringLiteral("name")).toString();
    return title.isEmpty() ? name : title;
}

void ScenarioManager::refresh()
{
    scanScenarios();
}

QString ScenarioManager::currentScenario() const
{
    return m_currentScenario;
}

QString ScenarioManager::scenariosPath() const
{
    return m_scenariosPath;
}

void ScenarioManager::setScenariosPath(const QString &path)
{
    const QString localPath = resolveLocalPath(path);
    if (localPath == m_scenariosPath)
        return;
    m_scenariosPath = localPath;
    scanScenarios();
}

QString ScenarioManager::defaultConfigDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    if (!appDir.isEmpty()) {
        // 构建产物同级 config（POST_BUILD 拷贝）以及源码树中的 pc-app/config
        candidates << appDir + QStringLiteral("/config")
                   << appDir + QStringLiteral("/../config")
                   << appDir + QStringLiteral("/../../config")
                   << appDir + QStringLiteral("/../../pc-app/config")
                   << appDir + QStringLiteral("/../../../pc-app/config");
    }
    candidates << QDir::currentPath() + QStringLiteral("/config")
               << QDir::currentPath() + QStringLiteral("/pc-app/config");

    for (const QString &candidate : candidates) {
        const QDir dir(candidate);
        if (dir.exists())
            return dir.canonicalPath();
    }
    return QString();
}

QString ScenarioManager::resolveLocalPath(const QString &pathOrUrl)
{
    if (pathOrUrl.isEmpty())
        return QString();

    QString path = pathOrUrl;
    // QML 侧常以 file:/// 形式传入路径
    if (path.startsWith(QStringLiteral("file:"))) {
        path = QUrl(path).toLocalFile();
    } else if (path.startsWith(QStringLiteral("qrc:"))) {
        path = QStringLiteral(":") + QUrl(path).path();
    }

    const QFileInfo info(path);
    if (info.isRelative() && !path.startsWith(QLatin1Char(':'))) {
        const QString configDir = defaultConfigDir();
        if (!configDir.isEmpty()) {
            const QFileInfo underConfig(QDir(configDir), path);
            if (underConfig.exists())
                return underConfig.absoluteFilePath();
        }
        return info.absoluteFilePath();
    }
    return path;
}

void ScenarioManager::scanScenarios()
{
    m_scenarios.clear();

    if (!m_scenariosPath.isEmpty()) {
        QDir dir(m_scenariosPath);
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &fileName : files) {
            QFile file(dir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                emit errorOccurred(tr("无法打开场景文件: %1").arg(file.fileName()));
                continue;
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                emit errorOccurred(tr("场景文件解析失败: %1 (%2)")
                                       .arg(fileName, parseError.errorString()));
                continue;
            }

            m_scenarios.insert(QFileInfo(fileName).completeBaseName(), doc.object().toVariantMap());
        }
    }

    emit scenariosChanged();
}

} // namespace whm
