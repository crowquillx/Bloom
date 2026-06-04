#include "SystemPowerController.h"

#include "utils/ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>

SystemPowerController::SystemPowerController(ConfigManager* configManager,
                                             QObject* parent)
    : QObject(parent)
    , m_configManager(configManager)
{
}

bool SystemPowerController::quitApplication()
{
    if (m_configManager) {
        m_configManager->save();
    }
    QCoreApplication::quit();
    return true;
}

bool SystemPowerController::restartApplication()
{
    const QString program = QCoreApplication::applicationFilePath();
    if (program.isEmpty()) {
        setLastError(tr("Cannot restart Bloom because the executable path is unavailable."));
        return false;
    }

    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }

    if (!QProcess::startDetached(program, arguments, QDir::currentPath())) {
        setLastError(tr("Failed to launch a new Bloom process."));
        return false;
    }

    return quitApplication();
}

bool SystemPowerController::restartComputer()
{
#if defined(Q_OS_WIN)
    return startDetached(QStringLiteral("shutdown"), { QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0") });
#elif defined(Q_OS_MACOS)
    return startDetached(QStringLiteral("/sbin/shutdown"), { QStringLiteral("-r"), QStringLiteral("now") });
#else
    return startDetached(QStringLiteral("systemctl"), { QStringLiteral("reboot") });
#endif
}

bool SystemPowerController::shutdownComputer()
{
#if defined(Q_OS_WIN)
    return startDetached(QStringLiteral("shutdown"), { QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0") });
#elif defined(Q_OS_MACOS)
    return startDetached(QStringLiteral("/sbin/shutdown"), { QStringLiteral("-h"), QStringLiteral("now") });
#else
    return startDetached(QStringLiteral("systemctl"), { QStringLiteral("poweroff") });
#endif
}

bool SystemPowerController::startDetached(const QString& program, const QStringList& arguments)
{
    if (m_configManager) {
        m_configManager->save();
    }

    if (QProcess::startDetached(program, arguments)) {
        return true;
    }

    setLastError(tr("Failed to start %1.").arg(program));
    return false;
}

void SystemPowerController::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}
