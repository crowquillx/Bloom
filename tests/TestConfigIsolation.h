#pragma once

#include <QByteArray>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace {

// Redirects config-related paths into a temporary directory and restores the
// previous environment on destruction. Shared by tests that exercise
// ConfigManager, CredentialStore, and SessionManager so each test runs against
// an isolated, throwaway configuration tree.
class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
        , m_hadPreviousConfigHome(!m_previousConfigHome.isNull())
        , m_hadPreviousAppData(!m_previousAppData.isNull())
        , m_hadPreviousHome(!m_previousHome.isNull())
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        restore("XDG_CONFIG_HOME", m_previousConfigHome, m_hadPreviousConfigHome);
        restore("APPDATA", m_previousAppData, m_hadPreviousAppData);
        restore("HOME", m_previousHome, m_hadPreviousHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    static void restore(const char *name, const QByteArray &value, bool hadPrevious)
    {
        if (hadPrevious) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
    bool m_hadPreviousConfigHome;
    bool m_hadPreviousAppData;
    bool m_hadPreviousHome;
};

} // namespace
