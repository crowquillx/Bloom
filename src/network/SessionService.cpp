#include "SessionService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "../utils/ConfigManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include "../utils/BloomLogging.h"

QVariantMap SessionInfo::toVariantMap() const
{
    QVariantMap map;
    map["id"] = id;
    map["deviceId"] = deviceId;
    map["deviceName"] = deviceName;
    map["client"] = client;
    map["clientVersion"] = clientVersion;
    map["userId"] = userId;
    map["userName"] = userName;
    map["lastActivityDate"] = lastActivityDate;
    map["lastPlaybackCheckIn"] = lastPlaybackCheckIn;
    map["isRemoteSession"] = isRemoteSession;
    map["supportsRemoteControl"] = supportsRemoteControl;
    map["playState"] = playState;
    map["hasCustomDeviceName"] = hasCustomDeviceName;
    return map;
}

SessionService::SessionService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_transport(authService ? authService->transport() : nullptr)
{
    if (authService) {
        m_deviceId = getDeviceId();
        connect(authService, &AuthenticationService::loggedOut, this, [this]() {
            m_sessions.clear();
            m_currentSessionId.clear();
            m_errorString.clear();
            m_isLoading = false;
            emit sessionsChanged();
            emit currentSessionIdChanged();
            emit errorStringChanged();
            emit isLoadingChanged();
        });
    }
}

void SessionService::fetchActiveSessions()
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }
    if (!m_transport) {
        setErrorString("Network transport unavailable");
        emit operationFailed(m_errorString);
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

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
    if (!m_transport) {
        setErrorString("Network transport unavailable");
        emit operationFailed(m_errorString);
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

    // Jellyfin uses POST /Sessions/{id}/Logout to revoke a session
    QString endpoint = QString("/Sessions/%1/Logout").arg(sessionId);
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

    // First refresh the session list
    fetchActiveSessions();

    // Wait for sessions to load, then revoke all except current
    connect(this, &SessionService::sessionsLoaded, this, [this]() {
        int revokedCount = 0;
        for (const QVariant &var : m_sessions) {
            QVariantMap session = var.toMap();
            QString sessionId = session["id"].toString();
            
            if (!sessionId.isEmpty() && sessionId != m_currentSessionId) {
                revokeSession(sessionId);
                revokedCount++;
            }
        }
        emit allOtherSessionsRevoked(revokedCount);
    }, Qt::SingleShotConnection);
}

void SessionService::identifyCurrentSession()
{
    if (m_deviceId.isEmpty()) {
        m_deviceId = getDeviceId();
    }

    if (m_deviceId.isEmpty() || m_sessions.isEmpty()) {
        return;
    }

    // Find the session matching our device ID
    for (const QVariant &var : m_sessions) {
        QVariantMap session = var.toMap();
        QString sessionDeviceId = session["deviceId"].toString();
        
        if (sessionDeviceId == m_deviceId) {
            QString newSessionId = session["id"].toString();
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

    QJsonArray sessionsArray = doc.array();
    m_sessions.clear();

    for (const QJsonValue &value : sessionsArray) {
        if (!value.isObject()) continue;

        QJsonObject obj = value.toObject();
        SessionInfo info;
        
        info.id = obj["Id"].toString();
        info.deviceId = obj["DeviceId"].toString();
        info.deviceName = obj["DeviceName"].toString();
        info.client = obj["Client"].toString();
        info.clientVersion = obj["ApplicationVersion"].toString();
        info.userId = obj["UserId"].toString();
        info.userName = obj["UserName"].toString();
        info.isRemoteSession = obj["IsRemoteSession"].toBool();
        info.supportsRemoteControl = obj["SupportsRemoteControl"].toBool();
        info.hasCustomDeviceName = obj["HasCustomDeviceName"].toBool();

        // Parse dates
        QString lastActivity = obj["LastActivityDate"].toString();
        if (!lastActivity.isEmpty()) {
            info.lastActivityDate = QDateTime::fromString(lastActivity, Qt::ISODate);
        }

        QString lastPlayback = obj["LastPlaybackCheckIn"].toString();
        if (!lastPlayback.isEmpty()) {
            info.lastPlaybackCheckIn = QDateTime::fromString(lastPlayback, Qt::ISODate);
        }

        // Play state (if present)
        QJsonObject playStateObj = obj["PlayState"].toObject();
        if (!playStateObj.isEmpty()) {
            info.playState = playStateObj["PlayMethod"].toString();
        }

        m_sessions.append(info.toVariantMap());
    }

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
