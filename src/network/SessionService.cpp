#include "SessionService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "../utils/ConfigManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include "../utils/BloomLogging.h"

SessionService::SessionService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_transport(authService ? authService->transport() : nullptr)
{
    if (authService) {
        m_deviceId = getDeviceId();
        connect(authService, &AuthenticationService::authSessionsChanged,
                this, &SessionService::syncAuthenticationSessions);
        connect(authService, &AuthenticationService::providerSelectionChanged,
                this, &SessionService::sessionTypeChanged);
        connect(authService, &AuthenticationService::authenticatedChanged,
                this, &SessionService::sessionTypeChanged);
        connect(authService, &AuthenticationService::loginError, this,
                [this](const QString &error) {
            if (!authenticationSessionMode()
                || (!m_authLoadPending && m_pendingRevokeSessionId.isEmpty())) {
                return;
            }
            m_authLoadPending = false;
            m_pendingRevokeSessionId.clear();
            m_pendingRevokeWasCurrent = false;
            setIsLoading(false);
            setErrorString(error);
        });
        connect(authService, &AuthenticationService::loggedOut, this, [this]() {
            const bool selfRevoked = m_pendingRevokeWasCurrent;
            m_authLoadPending = false;
            m_pendingRevokeSessionId.clear();
            m_pendingRevokeWasCurrent = false;
            m_sessions.clear();
            m_currentSessionId.clear();
            m_errorString.clear();
            m_isLoading = false;
            emit sessionsChanged();
            emit currentSessionIdChanged();
            emit errorStringChanged();
            emit isLoadingChanged();
            if (selfRevoked) {
                emit selfSessionRevoked();
            }
        });
    }
}

bool SessionService::authenticationSessionMode() const
{
    return m_authService
        && m_authService->activeProviderKind() == ProviderKind::Silo;
}

QString SessionService::sessionTypeLabel() const
{
    return authenticationSessionMode()
        ? tr("Authentication Sessions")
        : tr("Playback Sessions");
}

QString SessionService::sessionTypeDescription() const
{
    return authenticationSessionMode()
        ? tr("Devices signed in to this account")
        : tr("Devices currently connected for playback");
}

void SessionService::syncAuthenticationSessions()
{
    if (!authenticationSessionMode() || !m_authService) {
        return;
    }

    const QString previousCurrent = m_currentSessionId;
    m_sessions = m_authService->authSessions();
    m_currentSessionId.clear();
    for (const QVariant &value : m_sessions) {
        const QVariantMap session = value.toMap();
        if (session.value(QStringLiteral("isCurrent")).toBool()) {
            m_currentSessionId = session.value(QStringLiteral("id")).toString();
            break;
        }
    }

    emit sessionsChanged();
    if (previousCurrent != m_currentSessionId) {
        emit currentSessionIdChanged();
    }

    const QString revokedId = m_pendingRevokeSessionId;
    if (!revokedId.isEmpty()) {
        bool stillPresent = false;
        for (const QVariant &value : m_sessions) {
            if (value.toMap().value(QStringLiteral("id")).toString() == revokedId) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            const bool revokedCurrent = m_pendingRevokeWasCurrent;
            m_pendingRevokeSessionId.clear();
            setIsLoading(false);
            if (!revokedCurrent) {
                m_pendingRevokeWasCurrent = false;
                emit sessionRevoked(revokedId);
            }
        }
    }

    if (m_authLoadPending) {
        m_authLoadPending = false;
        setIsLoading(false);
        emit sessionsLoaded();
    }
}

void SessionService::fetchActiveSessions()
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }

    if (authenticationSessionMode() && m_isLoading) {
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

    if (!m_sessions.isEmpty()) {
        m_sessions.clear();
        emit sessionsChanged();
    }
    if (!m_currentSessionId.isEmpty()) {
        m_currentSessionId.clear();
        emit currentSessionIdChanged();
    }

    if (authenticationSessionMode()) {
        m_authLoadPending = true;
        m_authService->loadAuthSessions();
        return;
    }

    if (!m_transport) {
        setIsLoading(false);
        setErrorString("Network transport unavailable");
        emit operationFailed(m_errorString);
        return;
    }

    const QString endpoint = QStringLiteral("/Sessions");
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, endpoint]() {
            return m_authService->networkManager()->get(createAuthenticatedRequest(endpoint));
        },
        [this](QNetworkReply *reply) { onFetchSessionsFinished(reply); },
        [this](const NetworkError &error) {
            setIsLoading(false);
            setErrorString(error.userMessage);
            emit operationFailed(error.userMessage);
        },
        options);
}

void SessionService::revokeSession(const QString &sessionId)
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }

    if (sessionId.isEmpty()) {
        setErrorString("Session ID is required");
        emit operationFailed(m_errorString);
        return;
    }

    if (authenticationSessionMode() && m_isLoading) {
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

    if (authenticationSessionMode()) {
        m_pendingRevokeSessionId = sessionId;
        m_pendingRevokeWasCurrent = isCurrentSession(sessionId);
        m_authService->revokeAuthSession(sessionId);
        return;
    }

    if (!m_transport) {
        setIsLoading(false);
        setErrorString("Network transport unavailable");
        emit operationFailed(m_errorString);
        return;
    }

    // Jellyfin uses POST /Sessions/{id}/Logout to revoke a session.
    const QString endpoint = QString("/Sessions/%1/Logout").arg(sessionId);
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, endpoint]() {
            return m_authService->networkManager()->post(
                createAuthenticatedRequest(endpoint), QByteArray());
        },
        [this, sessionId](QNetworkReply *reply) {
            onRevokeSessionFinished(reply, sessionId);
        },
        [this](const NetworkError &error) {
            setIsLoading(false);
            setErrorString(error.userMessage);
            emit operationFailed(error.userMessage);
        },
        options);
}
void SessionService::revokeAllOtherSessions()
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }

    if (authenticationSessionMode()) {
        setErrorString(tr("Bulk authentication-session revocation is not supported."));
        emit operationFailed(m_errorString);
        return;
    }

    // Refresh the Jellyfin playback-session list before revoking every session
    // except the current device.
    connect(this, &SessionService::sessionsLoaded, this, [this]() {
        int revokedCount = 0;
        for (const QVariant &var : m_sessions) {
            const QVariantMap session = var.toMap();
            const QString sessionId = session.value(QStringLiteral("id")).toString();
            if (!sessionId.isEmpty() && sessionId != m_currentSessionId) {
                revokeSession(sessionId);
                ++revokedCount;
            }
        }
        emit allOtherSessionsRevoked(revokedCount);
    }, Qt::SingleShotConnection);

    fetchActiveSessions();
}

void SessionService::identifyCurrentSession()
{
    if (authenticationSessionMode()) {
        const QString previousCurrent = m_currentSessionId;
        m_currentSessionId.clear();
        for (const QVariant &value : m_sessions) {
            const QVariantMap session = value.toMap();
            if (session.value(QStringLiteral("isCurrent")).toBool()) {
                m_currentSessionId = session.value(QStringLiteral("id")).toString();
                break;
            }
        }
        if (previousCurrent != m_currentSessionId) {
            emit currentSessionIdChanged();
        }
        return;
    }

    if (m_deviceId.isEmpty()) {
        m_deviceId = getDeviceId();
    }

    if (m_deviceId.isEmpty() || m_sessions.isEmpty()) {
        return;
    }

    // Find the session matching our device ID.
    for (const QVariant &var : m_sessions) {
        const QVariantMap session = var.toMap();
        const QString sessionDeviceId = session.value(QStringLiteral("deviceId")).toString();
        if (sessionDeviceId == m_deviceId) {
            const QString newSessionId = session.value(QStringLiteral("id")).toString();
            if (newSessionId != m_currentSessionId) {
                m_currentSessionId = newSessionId;
                emit currentSessionIdChanged();
            }
            return;
        }
    }
}

void SessionService::setDeviceName(const QString &name)
{
    if (!m_authService || !m_authService->isAuthenticated() || name.isEmpty()) {
        return;
    }

    // Note: Jellyfin doesn't have a direct API to rename the current session's device name.
    // The device name is set during authentication. This method is a placeholder
    // for future server-side support or local tracking.
    
    // We could potentially send a Capabilities POST to update session info
    // For now, just emit that we attempted
    qCDebug(lcAuth) << "SessionService: Device name set to" << name;
}

bool SessionService::isCurrentSession(const QString &sessionId) const
{
    return sessionId == m_currentSessionId;
}

void SessionService::onFetchSessionsFinished(QNetworkReply *reply)
{
    setIsLoading(false);

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (!doc.isArray()) {
        QString error = "Invalid response format from server";
        setErrorString(error);
        emit operationFailed(error);
        return;
    }

    QString connectionId;
    if (ConfigManager *config = m_authService->configManager()) {
        const auto connection = config->getActiveConnection();
        if (connection.has_value()) {
            connectionId = connection->connectionId;
        }
    }
    m_sessions = m_authService->mapRemoteSessions(doc.array(), connectionId);

    // Identify our session
    identifyCurrentSession();

    emit sessionsChanged();
    emit sessionsLoaded();
    
    qCDebug(lcAuth) << "SessionService: Loaded" << m_sessions.size() << "sessions, current:" << m_currentSessionId;
}

void SessionService::onRevokeSessionFinished(QNetworkReply *, QString sessionId)
{
    setIsLoading(false);

    // Check if we revoked our own session
    if (sessionId == m_currentSessionId) {
        qCWarning(lcAuth) << "SessionService: Self-session was revoked";
        emit selfSessionRevoked();
        return;
    }

    // Remove from local list
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap session = m_sessions[i].toMap();
        if (session["id"].toString() == sessionId) {
            m_sessions.removeAt(i);
            break;
        }
    }

    emit sessionsChanged();
    emit sessionRevoked(sessionId);
    
    qCDebug(lcAuth) << "SessionService: Revoked session" << sessionId;
}

void SessionService::setIsLoading(bool loading)
{
    if (m_isLoading == loading) return;
    m_isLoading = loading;
    emit isLoadingChanged();
}

void SessionService::setErrorString(const QString &error)
{
    if (m_errorString == error) return;
    m_errorString = error;
    emit errorStringChanged();
}

QString SessionService::getDeviceId() const
{
    // Get device ID from the AuthenticationService's ConfigManager
    if (m_authService && m_authService->configManager()) {
        return m_authService->configManager()->getDeviceId();
    }
    return QString();
}

QNetworkRequest SessionService::createAuthenticatedRequest(const QString &endpoint) const
{
    QNetworkRequest request = m_authService->createRequest(endpoint);
    return request;
}
