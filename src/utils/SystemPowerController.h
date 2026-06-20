#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager;

class SystemPowerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool hostPowerActionsAvailable READ hostPowerActionsAvailable CONSTANT)

public:
    explicit SystemPowerController(ConfigManager* configManager, QObject* parent = nullptr);

    QString lastError() const { return m_lastError; }
    bool hostPowerActionsAvailable() const;

    Q_INVOKABLE bool quitApplication();
    Q_INVOKABLE bool restartApplication();
    Q_INVOKABLE bool restartComputer();
    Q_INVOKABLE bool shutdownComputer();

signals:
    void lastErrorChanged();

private:
    bool runCheckedPowerCommand(const QString& program, const QStringList& arguments);
    void setLastError(const QString& error);

    ConfigManager* m_configManager = nullptr;
    QString m_lastError;
};
