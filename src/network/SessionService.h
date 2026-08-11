#pragma once

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class AuthenticationService;
class HttpTransport;

/**
 * @brief Presents the active provider's remotely managed sessions.
 *
 * Jellyfin exposes playback sessions through /Sessions. Providers that expose
 * authentication sessions (currently Silo) are delegated to
 * AuthenticationService so this class never probes Jellyfin routes for them.
 */
class SessionService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged)
    Q_PROPERTY(bool authenticationSessionMode READ authenticationSessionMode NOTIFY sessionTypeChanged)
    Q_PROPERTY(QString sessionTypeLabel READ sessionTypeLabel NOTIFY sessionTypeChanged)
    Q_PROPERTY(QString sessionTypeDescription READ sessionTypeDescription NOTIFY sessionTypeChanged)

public:
    explicit SessionService(AuthenticationService *authService, QObject *parent = nullptr);

    QVariantList sessions() const { return m_sessions; }
    bool isLoading() const { return m_isLoading; }
    QString errorString() const { return m_errorString; }
    QString currentSessionId() const { return m_currentSessionId; }

    bool authenticationSessionMode() const;
    QString sessionTypeLabel() const;
    QString sessionTypeDescription() const;

    /**
     * @brief Fetch provider-supported authentication or playback sessions.
     */
    Q_INVOKABLE void fetchActiveSessions();

    /**
     * @brief Revoke a specific session by ID.
     * @param sessionId The session ID to revoke.
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
    void sessionTypeChanged();
    void sessionsLoaded();
    void sessionRevoked(QString sessionId);
    void allOtherSessionsRevoked(int count);
    void selfSessionRevoked();  // Current device was logged out
    void operationFailed(QString error);

private slots:
    void onFetchSessionsFinished(QNetworkReply *reply);
    void onRevokeSessionFinished(QNetworkReply *reply, QString sessionId, QString deviceId);
    void syncAuthenticationSessions();

private:
    AuthenticationService *m_authService;
    HttpTransport *m_transport = nullptr;
    QVariantList m_sessions;
    bool m_isLoading = false;
    QString m_errorString;
    QString m_currentSessionId;
    bool m_authLoadPending = false;
    QString m_pendingRevokeSessionId;
    bool m_pendingRevokeWasCurrent = false;
    QString m_deviceId;
    bool m_bulkRevokeRefreshPending = false;
    QSet<QString> m_pendingBulkRevokeDeviceIds;
    int m_successfulBulkRevocations = 0;

    void setIsLoading(bool loading);
    void setErrorString(const QString &error);
    void revokeLoadedOtherDevices();
    void finishDeviceRevocation(const QString &deviceId, bool successful);
    QString getDeviceId() const;
    QString deviceIdForSession(const QString &sessionId) const;
    QNetworkRequest createAuthenticatedRequest(const QString &endpoint) const;
};
