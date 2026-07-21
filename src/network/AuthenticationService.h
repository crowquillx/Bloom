#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "providers/ServerConnection.h"
#include "security/CredentialStore.h"

class IPlaybackProvider;
class IProviderAdapter;
class IProviderAuthenticator;
class IProviderRequestFactory;
class ISecretStore;
class ConfigManager;
class HttpTransport;

/**
 * @brief Handles user authentication, session management, and token validation.
 * 
 * This service manages:
 * - Login/logout flows
 * - Session persistence and restoration
 * - Access token validation
 * - Session expiry detection
 *
 * Part of the service decomposition formerly handled by the legacy client (Roadmap 1.1).
 */
class AuthenticationService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ getServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString userId READ getUserId NOTIFY userIdChanged)
    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool isRestoringSession READ isRestoringSession NOTIFY isRestoringSessionChanged)

public:
    explicit AuthenticationService(ISecretStore *secretStore = nullptr, QObject *parent = nullptr);
    AuthenticationService(ISecretStore *secretStore,
                          HttpTransport *transport,
                          IProviderAdapter *providerAdapter,
                          QObject *parent = nullptr);
    virtual ~AuthenticationService();
    
    /**
     * @brief Initialize service and attempt to restore session asynchronously
     * @param configManager Pointer to ConfigManager for reading/migrating session data
     */
    virtual void initialize(ConfigManager *configManager);
    
    /**
     * @brief Get the ConfigManager instance
     * @return Pointer to ConfigManager (not owned)
     */
    ConfigManager* configManager() const { return m_configManager; }

    /**
     * @brief Authenticate with username and password
     * @param serverUrl Jellyfin server URL
     * @param username User's username
     * @param password User's password
     */
    Q_INVOKABLE virtual void authenticate(const QString &serverUrl, const QString &username, const QString &password);
    
    /**
     * @brief Restore a previously saved session
     * @param serverUrl Jellyfin server URL
     * @param userId User ID from previous session
     * @param accessToken Access token from previous session
     */
    virtual void restoreSession(const QString &serverUrl,
                                const QString &userId,
                                const QString &accessToken,
                                const QString &username = QString());
    
    /**
     * @brief Log out and clear session data
     */
    Q_INVOKABLE virtual void logout();
    
    /**
     * @brief Check and emit pending session expiry (call after playback ends)
     */
    Q_INVOKABLE virtual void checkPendingSessionExpiry();
    
    // Accessors
    QString getServerUrl() const { return m_serverUrl; }
    QString getUserId() const { return m_userId; }
    QString getAccessToken() const { return m_accessToken; }
    QString getUsername() const { return m_username; }
    const IPlaybackProvider *playbackProvider() const;
    QVariantMap mapMediaItem(const QJsonObject &wireItem,
                             const QString &connectionId) const;
    QVariantList mapMediaItems(const QJsonArray &wireItems,
                               const QString &connectionId) const;
    QVariantList mapChapters(const QJsonArray &wireChapters,
                             const QString &connectionId,
                             const QString &itemId) const;
    bool isAuthenticated() const { return !m_accessToken.isEmpty() && !m_userId.isEmpty(); }
    bool isRestoringSession() const { return m_isRestoringSession; }
    
    /**
     * @brief Get the shared network access manager
     * @return Pointer to QNetworkAccessManager (owned by this service)
     */
    QNetworkAccessManager* networkManager() const;
    HttpTransport* transport() const { return m_transport; }
    
    /**
     * @brief Create a network request with authentication headers
     * @param endpoint API endpoint (will be appended to server URL)
     * @return Configured QNetworkRequest
     */
    QNetworkRequest createRequest(const QString &endpoint) const;
    
    /**
     * @brief Check HTTP response for 401 and handle session expiry
     * @param reply The network reply to check
     * @param deferLogout If true, defer sessionExpired signal until checkPendingSessionExpiry()
     * @return true if response was 401 (session expired)
     */
    bool checkForSessionExpiry(QNetworkReply *reply, bool deferLogout = false);

signals:
    void loginSuccess(const QString &userId, const QString &accessToken, const QString &username);
    void loginError(const QString &error);
    void loggedOut();
    void sessionExpired();  // Emitted when server returns 401 (token invalid/expired)
    void sessionExpiredAfterPlayback();  // Emitted after playback ends if session expired during playback
    
    // Property change signals
    void serverUrlChanged();
    void userIdChanged();
    void authenticatedChanged();
    void isRestoringSessionChanged();

protected:
    void seedSession(const QString &serverUrl,
                     const QString &userId,
                     const QString &accessToken,
                     const QString &username = QString());

private slots:
    void onAuthenticateFinished(QNetworkReply *reply);

private:
    std::unique_ptr<HttpTransport> m_ownedTransport;
    std::unique_ptr<IProviderAdapter> m_ownedProviderAdapter;
    HttpTransport *m_transport = nullptr;
    IProviderAdapter *m_providerAdapter = nullptr;
    const IProviderRequestFactory *m_requestFactory = nullptr;
    const IProviderAuthenticator *m_providerAuthenticator = nullptr;
    QString m_serverUrl;
    QString m_accessToken;
    QString m_userId;
    QString m_username;
    bool m_sessionExpiredPending = false;  // Set when 401 occurs during playback
    bool m_sessionExpiredEmitted = false;  // Guard against duplicate emissions
    ISecretStore *m_secretStore;  // Not owned (provided by main)
    ServerConnection m_activeConnection;
    bool m_isRestoringSession = false;
    ConfigManager* m_configManager = nullptr;
    
    // Helper struct for async session restoration results
    struct RestorationResult {
        bool success = false;
        QString serverUrl;
        QString userId;
        QString accessToken;
        QString username;
        QString error;
        QString cleanupError;
        ServerConnection connection;
        bool legacyMigrationComplete = false;
    };
    
    QFutureWatcher<RestorationResult> m_restorationWatcher;
    
    QString normalizeUrl(const QString &url);
    void handleUnauthorized(bool deferLogout);
    
    /**
     * @brief Validate the current access token by making a test API call
     * @param callback Called with true if valid, false if invalid/expired
     */
    void validateAccessToken(std::function<void(bool)> callback);
};
