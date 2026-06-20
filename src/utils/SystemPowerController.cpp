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

bool SystemPowerController::hostPowerActionsAvailable() const
{
    return qEnvironmentVariableIsEmpty("FLATPAK_ID")
        && qEnvironmentVariableIsEmpty("BLOOM_FLATPAK");
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

    if (m_configManager) {
        m_configManager->save();
    }

    if (!QProcess::startDetached(program, arguments, QDir::currentPath())) {
        setLastError(tr("Failed to launch a new Bloom process."));
        return false;
    }

    QCoreApplication::quit();
    return true;
}

bool SystemPowerController::restartComputer()
{
    if (!hostPowerActionsAvailable()) {
        setLastError(tr("Restarting the computer is unavailable in the Flatpak build."));
        return false;
    }
#if defined(Q_OS_WIN)
    return runCheckedPowerCommand(QStringLiteral("shutdown"), { QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0") });
#elif defined(Q_OS_MACOS)
    return runCheckedPowerCommand(QStringLiteral("/sbin/shutdown"), { QStringLiteral("-r"), QStringLiteral("now") });
#else
    return runCheckedPowerCommand(QStringLiteral("systemctl"), { QStringLiteral("reboot") });
#endif
}

bool SystemPowerController::shutdownComputer()
{
    if (!hostPowerActionsAvailable()) {
        setLastError(tr("Shutting down the computer is unavailable in the Flatpak build."));
        return false;
    }
#if defined(Q_OS_WIN)
    return runCheckedPowerCommand(QStringLiteral("shutdown"), { QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0") });
#elif defined(Q_OS_MACOS)
    return runCheckedPowerCommand(QStringLiteral("/sbin/shutdown"), { QStringLiteral("-h"), QStringLiteral("now") });
#else
    return runCheckedPowerCommand(QStringLiteral("systemctl"), { QStringLiteral("poweroff") });
#endif
}

bool SystemPowerController::runCheckedPowerCommand(const QString& program, const QStringList& arguments)
{
    if (m_configManager) {
        m_configManager->save();
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        setLastError(tr("Failed to start %1: %2").arg(program, process.errorString()));
        return false;
    }

    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        setLastError(tr("%1 did not finish. Check system power permissions.").arg(program));
        return false;
    }

    if (process.exitStatus() == QProcess::CrashExit) {
        setLastError(tr("%1 crashed before completing the power request.").arg(program));
        return false;
    }

    if (process.exitCode() != 0) {
        QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (detail.isEmpty()) {
            detail = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        }
        setLastError(detail.isEmpty()
                         ? tr("%1 failed with exit code %2.").arg(program).arg(process.exitCode())
                         : detail);
        return false;
    }

    return true;
}

void SystemPowerController::setLastError(const QString& error)
{
    m_lastError = error;
    emit lastErrorChanged();
}
