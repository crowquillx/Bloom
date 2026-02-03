#include "SessionService.h"
#include "AuthenticationService.h"
#include "../utils/ConfigManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

/**
 * @brief Serializes the session fields into a QVariantMap.
 *
 * The returned map contains the session properties using the following keys:
 * `id`, `deviceId`, `deviceName`, `client`, `clientVersion`, `userId`, `userName`,
 * `lastActivityDate`, `lastPlaybackCheckIn`, `isRemoteSession`, `supportsRemoteControl`,
 * `playState`, and `hasCustomDeviceName`.
 *
 * @return QVariantMap Mapping of session property names to their values.
 */
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

/**
 * @brief Constructs a SessionService and prepares network/auth state.
 *
 * Stores the provided AuthenticationService pointer, creates a QNetworkAccessManager
 * owned by this object, and initializes the local device ID when an
 * AuthenticationService is supplied.
 *
 * @param authService Optional authentication service used for creating
 *                    authenticated requests and deriving the device ID.
 * @param parent QObject parent for ownership and lifetime management.
 */
SessionService::SessionService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_nam(new QNetworkAccessManager(this))
{
    if (authService) {
        m_deviceId = getDeviceId();
    }
}

/**
 * @brief Requests the server for the list of active sessions and updates service state.
 *
 * If the service is not authenticated, sets the error string and emits `operationFailed`.
 * Otherwise clears any existing error, marks the service as loading, and issues an authenticated
 * GET request to the "/Sessions" endpoint. The network response is handled asynchronously by
 * `onFetchSessionsFinished`, which updates the internal session list and emits the appropriate
 * signals (e.g., `sessionsChanged`, `sessionsLoaded`, or error signals).
 */
void SessionService::fetchActiveSessions()
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        setErrorString("Not authenticated");
        emit operationFailed(m_errorString);
        return;
    }

    setIsLoading(true);
    setErrorString(QString());

    QNetworkRequest request = createAuthenticatedRequest("/Sessions");
    QNetworkReply *reply = m_nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchSessionsFinished(reply);
    });
}

/**
 * @brief Revokes (logs out) the session with the given session ID on the server.
 *
 * Validates authentication and the presence of a session ID, sets the loading state,
 * and sends a logout request for the specified session. On validation failure emits
 * `operationFailed`. While the network request is pending the service's loading state
 * is set; completion will result in either `sessionRevoked` for a remote session
 * or `selfSessionRevoked` if the current session was revoked, or `operationFailed`
 * on error.
 *
 * @param sessionId ID of the session to revoke; must not be empty.
 */
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

    setIsLoading(true);
    setErrorString(QString());

    // Jellyfin uses POST /Sessions/{id}/Logout to revoke a session
    QString endpoint = QString("/Sessions/%1/Logout").arg(sessionId);
    QNetworkRequest request = createAuthenticatedRequest(endpoint);
    QNetworkReply *reply = m_nam->post(request, QByteArray());

    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        onRevokeSessionFinished(reply, sessionId);
    });
}

/**
 * @brief Revokes all active sessions except the current one.
 *
 * If not authenticated, sets an error string and emits operationFailed. Otherwise
 * refreshes the server session list, revokes each session whose id differs
 * from the current session id, and emits `allOtherSessionsRevoked` with the
 * number of sessions revoked.
 *
 * Signals:
 * - Emits `operationFailed(const QString &)` when authentication is missing.
 * - Emits `allOtherSessionsRevoked(int)` after revocation attempts complete.
 */
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

/**
 * @brief Locate and mark the session that corresponds to this device.
 *
 * Ensures the local deviceId is available, then searches the cached sessions for a session
 * whose deviceId matches it. If a matching session is found and its id differs from the
 * stored current session id, updates m_currentSessionId and emits currentSessionIdChanged().
 * Does nothing if deviceId is unavailable or no matching session is present.
 */
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

/**
 * @brief Attempts to set a human-readable name for the current device session.
 *
 * If the client is not authenticated or `name` is empty, this function does nothing.
 * Currently there is no server-side API to rename an existing session; this method
 * is a placeholder and does not modify the session name on the server. It will
 * record the attempt locally (debug output) for future/diagnostic purposes.
 *
 * @param name Desired device name to apply to the current session.
 */
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
    qDebug() << "SessionService: Device name set to" << name;
}

/**
 * @brief Checks whether the given session ID corresponds to the currently identified session.
 *
 * @param sessionId The session ID to check.
 * @return `true` if the provided `sessionId` equals the current session ID, `false` otherwise.
 */
bool SessionService::isCurrentSession(const QString &sessionId) const
{
    return sessionId == m_currentSessionId;
}

/**
 * @brief Processes the completed /Sessions network reply, updates the cached session list, and emits state signals.
 *
 * Parses the JSON array returned by the server into SessionInfo entries, replaces the internal session list,
 * calls identifyCurrentSession(), and emits sessionsChanged() and sessionsLoaded().
 *
 * If the reply contains a network error or an unexpected response format, sets the error string and emits operationFailed().
 *
 * @param reply QNetworkReply produced by the request to fetch active sessions.
 */
void SessionService::onFetchSessionsFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    setIsLoading(false);

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = QString("Failed to fetch sessions: %1 (HTTP %2)")
                            .arg(reply->errorString())
                            .arg(statusCode);
        setErrorString(error);
        emit operationFailed(error);
        return;
    }

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
    
    qDebug() << "SessionService: Loaded" << m_sessions.size() << "sessions, current:" << m_currentSessionId;
}

/**
 * @brief Handle the network reply for a session revocation request and update local state.
 *
 * Processes the finished revoke reply: clears the loading state, reports an error and emits
 * operationFailed() when the request failed, emits selfSessionRevoked() if the revoked session
 * matches the current session, or removes the session from the local list and emits
 * sessionsChanged() and sessionRevoked(sessionId) on success.
 *
 * @param reply The finished QNetworkReply for the revoke request.
 * @param sessionId The identifier of the session that was requested to be revoked.
 */
void SessionService::onRevokeSessionFinished(QNetworkReply *reply, QString sessionId)
{
    reply->deleteLater();
    setIsLoading(false);

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError && statusCode != 204) {
        QString error = QString("Failed to revoke session: %1 (HTTP %2)")
                            .arg(reply->errorString())
                            .arg(statusCode);
        setErrorString(error);
        emit operationFailed(error);
        return;
    }

    // Check if we revoked our own session
    if (sessionId == m_currentSessionId) {
        qWarning() << "SessionService: Self-session was revoked";
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
    
    qDebug() << "SessionService: Revoked session" << sessionId;
}

/**
 * @brief Updates the service's loading state and notifies listeners when it changes.
 *
 * Emits isLoadingChanged() if the provided value differs from the current state.
 *
 * @param loading true to mark the service as loading, false otherwise.
 */
void SessionService::setIsLoading(bool loading)
{
    if (m_isLoading == loading) return;
    m_isLoading = loading;
    emit isLoadingChanged();
}

/**
 * @brief Update the service's error message and notify observers when it changes.
 *
 * Sets the internal error string to the provided value and emits errorStringChanged()
 * if the new value differs from the current one.
 *
 * @param error New error message; an empty string clears any existing error.
 */
void SessionService::setErrorString(const QString &error)
{
    if (m_errorString == error) return;
    m_errorString = error;
    emit errorStringChanged();
}

/**
 * @brief Retrieves the device identifier from the authentication configuration.
 *
 * Queries the associated AuthenticationService's ConfigManager for the current device ID.
 *
 * @return QString The device identifier, or an empty QString if no AuthenticationService
 *         or ConfigManager is available or the device ID is not set.
 */
QString SessionService::getDeviceId() const
{
    // Get device ID from the AuthenticationService's ConfigManager
    if (m_authService && m_authService->configManager()) {
        return m_authService->configManager()->getDeviceId();
    }
    return QString();
}

/**
 * @brief Create an authenticated network request for the specified API endpoint.
 *
 * Builds a QNetworkRequest for the given endpoint and applies the current authentication
 * configuration (headers, tokens, etc.) so it can be used for authenticated server calls.
 *
 * @param endpoint Endpoint path or URL to request (for example, "/Sessions").
 * @return QNetworkRequest Configured request with authentication applied.
 */
QNetworkRequest SessionService::createAuthenticatedRequest(const QString &endpoint) const
{
    QNetworkRequest request = m_authService->createRequest(endpoint);
    return request;
}