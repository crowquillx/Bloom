#pragma once

#include <QObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>

class AuthenticationService;

/**
 * @brief Session information structure
 */
struct SessionInfo {
    QString id;
    QString deviceId;
    QString deviceName;
    QString client;
    QString clientVersion;
    QString userId;
    QString userName;
    QDateTime lastActivityDate;
    QDateTime lastPlaybackCheckIn;
    bool isRemoteSession = false;
    bool supportsRemoteControl = false;
    QString playState;
    bool hasCustomDeviceName = false;

    QVariantMap toVariantMap() const;
};

/**
 * @brief Wraps Jellyfin /Sessions API for session management.
 *
 * Provides:
 * - Fetch active sessions from Jellyfin server
 * - Revoke specific sessions
 * - Revoke all other sessions (logout everywhere else)
 * - Detect self-session revocation
 */
class SessionService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged)

public:
    explicit SessionService(AuthenticationService *authService, QObject *parent = nullptr);

    QVariantList sessions() const { return m_sessions; }
    bool isLoading() const { return m_isLoading; }
    QString errorString() const { return m_errorString; }
    QString currentSessionId() const { return m_currentSessionId; }

    /**
     * @brief Fetch all active sessions from the Jellyfin server
     */
    Q_INVOKABLE void fetchActiveSessions();

    /**
     * @brief Revoke a specific session by ID
     * @param sessionId The session ID to revoke
     */
    Q_INVOKABLE void revokeSession(const QString &sessionId);

    /**
     * @brief Revoke all sessions except the current one
     */
    Q_INVOKABLE void revokeAllOtherSessions();

    /**
     * @brief Identify the current session among server sessions
     * Call this after fetchActiveSessions() to identify which session is "us"
     */
    Q_INVOKABLE void identifyCurrentSession();

    /**
     * @brief Set a custom device name for the current session
     * @param name The device name to display
     */
    Q_INVOKABLE void setDeviceName(const QString &name);

    /**
     * @brief Check if a given session ID is the current session
     */
    Q_INVOKABLE bool isCurrentSession(const QString &sessionId) const;

signals:
    void sessionsChanged();
    void isLoadingChanged();
    void errorStringChanged();
    void currentSessionIdChanged();
    void sessionsLoaded();
    void sessionRevoked(QString sessionId);
    void allOtherSessionsRevoked(int count);
    void selfSessionRevoked();  // Current device was logged out
    void operationFailed(QString error);

private slots:
    void onFetchSessionsFinished(QNetworkReply *reply);
    void onRevokeSessionFinished(QNetworkReply *reply, QString sessionId);

private:
    AuthenticationService *m_authService;
    QNetworkAccessManager *m_nam;
    QVariantList m_sessions;
    bool m_isLoading = false;
    QString m_errorString;
    QString m_currentSessionId;
    QString m_deviceId;

    void setIsLoading(bool loading);
    void setErrorString(const QString &error);
    QString getDeviceId() const;
    QNetworkRequest createAuthenticatedRequest(const QString &endpoint) const;
};
