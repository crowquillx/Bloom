#include "MockAuthenticationService.h"
#include <QDebug>

MockAuthenticationService::MockAuthenticationService(ISecretStore *secretStore, QObject *parent)
    : AuthenticationService(secretStore, parent)
{
}

void MockAuthenticationService::initialize(ConfigManager *configManager)
{
    // Use restoreSession to set server URL and internal state consistently
    // Set this BEFORE base initialization so that any base class logic that relies on
    // established session state sees the mock values immediately (avoiding network restoration).
    restoreSession(QStringLiteral("test://mock"), QStringLiteral("test-user-001"), QStringLiteral("test-access-token-001"));

    // Initialize the base class to set up config manager and secret store members
    AuthenticationService::initialize(configManager);
    
    qDebug() << "MockAuthenticationService: Initialized with pre-authenticated session";
}

void MockAuthenticationService::authenticate(const QString &serverUrl, const QString &username, const QString &password)
{
    Q_UNUSED(password)
    
    qDebug() << "MockAuthenticationService::authenticate(" << serverUrl << "," << username << ")";
    
    // Immediately emit success
    emit loginSuccess("test-user-001", "test-access-token-001", username);
}

void MockAuthenticationService::restoreSession(const QString &serverUrl, const QString &userId, const QString &accessToken)
{
    qDebug() << "MockAuthenticationService::restoreSession(" << serverUrl << "," << userId << ")";
    
    // Immediately emit success
    emit loginSuccess(userId, accessToken, QString());
}

void MockAuthenticationService::logout()
{
    qDebug() << "MockAuthenticationService::logout()";
    
    emit loggedOut();
}

void MockAuthenticationService::checkPendingSessionExpiry()
{
    // No-op in mock - sessions don't expire
}
