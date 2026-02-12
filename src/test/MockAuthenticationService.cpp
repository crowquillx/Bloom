#include "MockAuthenticationService.h"
#include <QDebug>

MockAuthenticationService::MockAuthenticationService(ISecretStore *secretStore, QObject *parent)
    : AuthenticationService(secretStore, parent)
{
}

void MockAuthenticationService::initialize(ConfigManager *configManager)
{
    Q_UNUSED(configManager)
    
    // In test mode, we start with a pre-authenticated session
    // Set the internal state directly via the parent class methods
    // We need to call the parent's initialize to set up the config manager
    // but then override the authentication state
    
    qDebug() << "MockAuthenticationService: Initialized with pre-authenticated session";
    
    // Emit login success to simulate authenticated state
    emit loginSuccess("test-user-001", "test-access-token-001", "TestUser");
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
