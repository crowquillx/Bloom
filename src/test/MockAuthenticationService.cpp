#include "MockAuthenticationService.h"
#include <QDebug>
#include "../utils/BloomLogging.h"

MockAuthenticationService::MockAuthenticationService(ISecretStore *secretStore, QObject *parent)
    : AuthenticationService(secretStore, parent)
{
}

void MockAuthenticationService::initialize(ConfigManager *configManager)
{
    Q_UNUSED(configManager)

    seedSession(QStringLiteral("test://mock"),
                QStringLiteral("test-user-001"),
                QStringLiteral("test-access-token-001"),
                QStringLiteral("Test User"));
    emit loginSuccess(QStringLiteral("test-user-001"),
                      QStringLiteral("test-access-token-001"),
                      QStringLiteral("Test User"));
    
    qCDebug(lcTest) << "MockAuthenticationService: Initialized with pre-authenticated session";
}

void MockAuthenticationService::authenticate(const QString &serverUrl, const QString &username, const QString &password)
{
    Q_UNUSED(password)
    
    qCDebug(lcTest) << "MockAuthenticationService::authenticate(" << serverUrl << "," << username << ")";
    
    seedSession(serverUrl, QStringLiteral("test-user-001"), QStringLiteral("test-access-token-001"), username);
    emit loginSuccess(QStringLiteral("test-user-001"), QStringLiteral("test-access-token-001"), username);
}

void MockAuthenticationService::restoreSession(const QString &serverUrl,
                                               const QString &userId,
                                               const QString &accessToken,
                                               const QString &username)
{
    qCDebug(lcTest) << "MockAuthenticationService::restoreSession(" << serverUrl << "," << userId << ")";

    seedSession(serverUrl, userId, accessToken, username);
    emit loginSuccess(userId, accessToken, username);
}

void MockAuthenticationService::logout()
{
    qCDebug(lcTest) << "MockAuthenticationService::logout()";
    seedSession(QString(), QString(), QString());
    emit loggedOut();
}

void MockAuthenticationService::checkPendingSessionExpiry()
{
    // No-op in mock - sessions don't expire
}
