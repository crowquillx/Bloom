#include "SessionService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "../utils/ConfigManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
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
            m_bulkRevokeRefreshPending = false;
            m_pendingBulkRevokeDeviceIds.clear();
            m_successfulBulkRevocations = 0;
            setIsLoading(false);
            setErrorString(error);
        });
        connect(authService, &AuthenticationService::loggedOut, this, [this]() {
            const bool selfRevoked = m_pendingRevokeWasCurrent;
            m_authLoadPending = false;
            m_pendingRevokeSessionId.clear();
            m_pendingRevokeWasCurrent = false;
            m_bulkRevokeRefreshPending = false;
            m_pendingBulkRevokeDeviceIds.clear();
            m_successfulBulkRevocations = 0;
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
        m_bulkRevokeRefreshPending = false;
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }

    if (authenticationSessionMode() && m_isLoading) {
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

    if (authenticationSessionMode()) {
        m_authLoadPending = true;
        m_authService->loadAuthSessions();
        return;
    }

    if (!m_sessions.isEmpty()) {
        m_sessions.clear();
        emit sessionsChanged();
    }
    if (!m_currentSessionId.isEmpty()) {
        m_currentSessionId.clear();
        emit currentSessionIdChanged();
    }

    if (!m_transport) {
        m_bulkRevokeRefreshPending = false;
        setIsLoading(false);
        setErrorString("Network transport unavailable");
        emit operationFailed(m_errorString);
        return;
    }

    const QString endpoint = QStringLiteral("/Sessions");
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Idempotent;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, endpoint]() {
            return m_authService->networkManager()->get(createAuthenticatedRequest(endpoint));
        },
        [this](QNetworkReply *reply) { onFetchSessionsFinished(reply); },
        [this](const NetworkError &error) {
            m_bulkRevokeRefreshPending = false;
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

    // Jellyfin 12 no longer exposes the legacy per-session Logout route in
    // OpenAPI. Delete the device associated with the selected playback
    // session using the supported device-management endpoint instead.
    const QString deviceId = deviceIdForSession(sessionId);
    if (deviceId.isEmpty()) {
        setIsLoading(false);
        setErrorString(tr("The selected session has no device identifier."));
        emit operationFailed(m_errorString);
        return;
    }
    QUrl endpointUrl(QStringLiteral("/Devices"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), deviceId);
    endpointUrl.setQuery(query);
    const QString endpoint = endpointUrl.toString(QUrl::FullyEncoded);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, endpoint]() {
            return m_authService->networkManager()->deleteResource(
                createAuthenticatedRequest(endpoint));
        },
        [this, sessionId, deviceId](QNetworkReply *reply) {
            onRevokeSessionFinished(reply, sessionId, deviceId);
        },
        [this, deviceId](const NetworkError &error) {
            finishDeviceRevocation(deviceId, false);
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
    if (m_isLoading) {
        return;
    }

    // The explicit pending flag is cleared on every fetch failure, so a later
    // unrelated refresh can never inherit this destructive operation.
    m_bulkRevokeRefreshPending = true;
    fetchActiveSessions();
}

void SessionService::revokeLoadedOtherDevices()
{
    QStringList revocations;
    QSet<QString> revokedDeviceIds;
    const QString sessionDeviceId = deviceIdForSession(m_currentSessionId);
    const QString currentDeviceId = sessionDeviceId.isEmpty()
        ? m_deviceId
        : sessionDeviceId;
    if (currentDeviceId.isEmpty()) {
        setErrorString(tr("The current Jellyfin device could not be identified."));
        emit operationFailed(m_errorString);
        return;
    }
    for (const QVariant &var : m_sessions) {
        const QVariantMap session = var.toMap();
        const QString sessionId = session.value(QStringLiteral("id")).toString();
        const QString deviceId = session.value(QStringLiteral("deviceId")).toString();
        if (!sessionId.isEmpty() && !deviceId.isEmpty()
            && deviceId != currentDeviceId && !revokedDeviceIds.contains(deviceId)) {
            revokedDeviceIds.insert(deviceId);
            revocations.append(sessionId);
        }
    }
    if (revocations.isEmpty()) {
        emit allOtherSessionsRevoked(0);
        return;
    }

    m_pendingBulkRevokeDeviceIds = revokedDeviceIds;
    m_successfulBulkRevocations = 0;
    setIsLoading(true);
    for (const QString &sessionId : revocations) {
        revokeSession(sessionId);
    }
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
        m_bulkRevokeRefreshPending = false;
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

    if (m_bulkRevokeRefreshPending) {
        m_bulkRevokeRefreshPending = false;
        revokeLoadedOtherDevices();
    }

    emit sessionsChanged();
    emit sessionsLoaded();
    
    qCDebug(lcAuth) << "SessionService: Loaded" << m_sessions.size() << "sessions, current:" << m_currentSessionId;
}

void SessionService::onRevokeSessionFinished(QNetworkReply *, QString sessionId, QString deviceId)
{
    // Jellyfin 12 revocation deletes a device rather than one playback
    // session. Another session row for this device therefore revokes the
    // current session as well.
    const QString sessionCurrentDeviceId = deviceIdForSession(m_currentSessionId);
    const QString currentDeviceId = sessionCurrentDeviceId.isEmpty()
        ? m_deviceId
        : sessionCurrentDeviceId;
    if (sessionId == m_currentSessionId
        || (!deviceId.isEmpty() && deviceId == currentDeviceId)) {
        qCWarning(lcAuth) << "SessionService: Current device was revoked";
        finishDeviceRevocation(deviceId, true);
        emit selfSessionRevoked();
        return;
    }

    // Device deletion can remove more than one server session for the same
    // device, so keep the local projection consistent with that operation.
    for (int i = m_sessions.size() - 1; i >= 0; --i) {
        const QVariantMap session = m_sessions[i].toMap();
        if (session.value(QStringLiteral("deviceId")).toString() == deviceId) {
            m_sessions.removeAt(i);
        }
    }

    emit sessionsChanged();
    emit sessionRevoked(sessionId);
    finishDeviceRevocation(deviceId, true);
    
    qCDebug(lcAuth) << "SessionService: Revoked session" << sessionId;
}

void SessionService::finishDeviceRevocation(const QString &deviceId, bool successful)
{
    if (!m_pendingBulkRevokeDeviceIds.remove(deviceId)) {
        setIsLoading(false);
        return;
    }

    if (successful) {
        ++m_successfulBulkRevocations;
    }
    if (!m_pendingBulkRevokeDeviceIds.isEmpty()) {
        return;
    }

    setIsLoading(false);
    const int revokedCount = m_successfulBulkRevocations;
    m_successfulBulkRevocations = 0;
    emit allOtherSessionsRevoked(revokedCount);
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

QString SessionService::deviceIdForSession(const QString &sessionId) const
{
    for (const QVariant &value : m_sessions) {
        const QVariantMap session = value.toMap();
        if (session.value(QStringLiteral("id")).toString() == sessionId) {
            return session.value(QStringLiteral("deviceId")).toString();
        }
    }
    return {};
}

QNetworkRequest SessionService::createAuthenticatedRequest(const QString &endpoint) const
{
    QNetworkRequest request = m_authService->createRequest(endpoint);
    return request;
}
