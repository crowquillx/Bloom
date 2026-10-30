#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QWaitCondition>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>
#include <functional>
#include <utility>
#include <memory>

#include <optional>
#include "network/ConnectionSessionPool.h"
#include "network/HttpTransport.h"
#include "network/Types.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/IProviderAdapter.h"
#include "providers/ServerConnection.h"
#include "security/CredentialStore.h"

class DeviceSignInSession;
class ICatalogProvider;
class ISecretStore;
class ConfigManager;
class ConnectionSessionPool;
class HttpTransport;

/**
 * @brief Stable QML-facing authentication and provider-session facade.
 *
 * Provider adapters own wire formats. This service owns provider selection,
 * persisted connection identity, credentials, profile state, and session expiry.
 */
class AuthenticationService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ getServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString userId READ getUserId NOTIFY userIdChanged)
    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool isRestoringSession READ isRestoringSession NOTIFY isRestoringSessionChanged)
    Q_PROPERTY(QString providerSelection READ providerSelection WRITE setProviderSelection NOTIFY providerSelectionChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList authSessions READ authSessions NOTIFY authSessionsChanged)
    Q_PROPERTY(QString authenticationStep READ authenticationStep NOTIFY authenticationStepChanged)
    // Sign-in methods the typed server offers: server, state (idle, checking,
    // ready, unreachable), silo, passwordSignIn, deviceSignIn.
    Q_PROPERTY(QVariantMap signInOptions READ signInOptions NOTIFY signInOptionsChanged)
    // "Sign in with a code": see DeviceSignInSession::toVariantMap.
    Q_PROPERTY(QVariantMap deviceSignIn READ deviceSignIn NOTIFY deviceSignInChanged)

public:
    explicit AuthenticationService(std::shared_ptr<ISecretStore> secretStore = nullptr,
                                     QObject *parent = nullptr);
    AuthenticationService(std::shared_ptr<ISecretStore> secretStore,
                          HttpTransport *transport,
                          IProviderAdapter *providerAdapter,
                          QObject *parent = nullptr);
    AuthenticationService(std::shared_ptr<ISecretStore> secretStore,
                          HttpTransport *transport,
                          const QList<IProviderAdapter *> &providerAdapters,
                          QObject *parent = nullptr);
    virtual ~AuthenticationService();

    virtual void initialize(ConfigManager *configManager);
    ProviderRequestContext requestContext(bool includeAuthentication = true) const;
    ConfigManager *configManager() const { return m_configManager; }

    Q_INVOKABLE virtual void authenticate(const QString &serverUrl,
                                          const QString &username,
                                          const QString &password);
    virtual void restoreSession(const QString &serverUrl,
                                const QString &userId,
                                const QString &accessToken,
                                const QString &username = QString());
    Q_INVOKABLE virtual void logout();
    Q_INVOKABLE void clearAccountState();
    Q_INVOKABLE void clearProfileState();
    Q_INVOKABLE void switchProfile();
    Q_INVOKABLE virtual void checkPendingSessionExpiry();

    QString providerSelection() const { return m_providerSelection; }
    QVariantList profiles() const { return m_profiles; }
    QVariantList authSessions() const { return m_authSessions; }
    QString authenticationStep() const { return m_authenticationStep; }

    Q_INVOKABLE void setProviderSelection(const QString &selection);
    Q_INVOKABLE void selectProfile(const QString &profileId);
    Q_INVOKABLE void verifyProfilePin(const QString &profileId, const QString &pin);
    /// Reads which sign-in methods a server offers, before anyone signs in.
    /// Repeating the same address while a check is current does nothing.
    Q_INVOKABLE void checkSignInOptions(const QString &serverAddress);
    /// Starts device sign-in on a server whose options offer it. Approved
    /// tokens continue exactly like a password login (profiles, PIN, storage).
    Q_INVOKABLE void startDeviceSignIn(const QString &serverAddress);
    /// Abandons device sign-in and withdraws its code on the server.
    Q_INVOKABLE void cancelDeviceSignIn();
    QVariantMap signInOptions() const;
    QVariantMap deviceSignIn() const;
    DeviceSignInSession *deviceSignInSession() const { return m_deviceSignIn; }
    Q_INVOKABLE void loadAuthSessions();
    Q_INVOKABLE void revokeAuthSession(const QString &sessionId);

    QString getServerUrl() const { return m_serverUrl; }
    QString getUserId() const { return m_userId; }
    QString getAccessToken() const { return m_accessToken; }
    QString getUsername() const { return m_username; }
    const IPlaybackProvider *playbackProvider() const;
    const ICatalogProvider *catalogProvider() const;
    ProviderKind activeProviderKind() const;
    bool supportsCapability(ProviderCapability capability) const;
    std::optional<QString> endpointFor(ProviderRoute route,
                                       const ProviderRouteContext &context = {}) const;
    PlaybackInfoResponse mapPlaybackInfo(const QJsonObject &wirePlaybackInfo) const;
    ParsedItemsResult parseItemsResponse(const QByteArray &wireResponse,
                                         const QString &parentId) const;
    std::function<ParsedItemsResult(const QByteArray &, const QString &)>
        itemsResponseParser() const;
    TrickplayTileInfoMap mapTrickplayInfo(const QJsonObject &wireItem) const;
    QList<MediaSegmentInfo> mapIntroSkipperSegments(
        const QString &itemId, const QJsonObject &wireSegments) const;
    QList<MediaSegmentInfo> mapMediaSegments(
        const QString &itemId, const QJsonObject &wireSegments) const;
    QVariantList mapRemoteSessions(const QJsonArray &wireSessions,
                                   const QString &connectionId) const;
    QString mapLibraryIdFromAncestors(const QJsonArray &wireAncestors) const;
    QVariantMap mapFilterOptions(const QJsonObject &wireFilters) const;
    QStringList mapNamedItems(const QJsonObject &wireItems) const;
    QVariantMap mapMediaItem(const QJsonObject &wireItem,
                             const QString &connectionId) const;
    QVariantList mapMediaItems(const QJsonArray &wireItems,
                               const QString &connectionId) const;
    QVariantList mapChaptersFromItem(const QJsonObject &wireItem,
                                     const QString &connectionId,
                                     const QString &itemId) const;
    bool isAuthenticated() const
    {
        return !m_accessToken.isEmpty() && !m_userId.isEmpty()
            && m_authenticationStep == QStringLiteral("authenticated");
    }
    bool isRestoringSession() const { return m_isRestoringSession; }

    QNetworkAccessManager *networkManager() const;
    HttpTransport *transport() const { return m_transport; }
    QNetworkRequest createRequest(const QString &endpoint) const;
    bool checkForSessionExpiry(QNetworkReply *reply, bool deferLogout = false);

    // Bloom profile routing (docs/profiles.md). Other members of a merged
    // profile are served from credentials held by ConnectionSessionPool and
    // renewed through renewConnection().
    QString activeConnectionId() const;
    ConnectionSessionPool *sessionPool() const { return m_sessionPool; }
    /// Saved connections whose credentials stay loaded for merged reads.
    void setMemberConnections(const QStringList &connectionIds);
    bool hasSessionForConnection(const QString &connectionId) const;
    std::optional<ProviderRequestContext> requestContextForConnection(
        const QString &connectionId) const;
    IProviderAdapter *providerForConnection(const QString &connectionId) const;
    std::optional<QNetworkRequest> createRequestForConnection(
        const QString &connectionId, const QString &endpoint) const;
    /// Makes a saved connection the active session without deleting the
    /// previous connection's credentials. On failure the previous session is
    /// restored when possible.
    void activateConnection(const QString &connectionId);
    bool isActivatingConnection() const { return !m_activatingConnectionId.isEmpty(); }
    /// Leaves the active session without deleting its credentials (used to
    /// sign in to an additional server). Emits loggedOut.
    void suspendSession();
    /// Deletes stored credentials for a connection that is not active.
    void removeStoredCredentials(const ServerConnection &connection);
    /// Renews a connection's access token with its refresh token: the live
    /// session's, or a pooled member's. Concurrent calls for one connection
    /// share a single attempt. New tokens are saved; Rejected (the server
    /// refused the refresh token) is the only outcome that needs a sign-in.
    void renewConnection(const QString &connectionId,
                         std::function<void(TokenRenewal)> completion);
    /// Renews the live access token first when it expires within the given
    /// time (capped at half the token's issued lifetime, so short-lived tokens
    /// are not renewed on every call), or when its lifetime is unknown. Sessions without a refresh token
    /// (Jellyfin) continue immediately. done runs whatever the outcome.
    void ensureAccessTokenValidFor(qint64 seconds, std::function<void()> done);

signals:
    void loginSuccess(const QString &userId, const QString &accessToken, const QString &username);
    void loginError(const QString &error);
    void loggedOut();
    void sessionExpired();
    void sessionExpiredAfterPlayback();
    void connectionActivated(const QString &connectionId);
    void connectionActivationFailed(const QString &connectionId, const QString &error);
    /// An activated connection needs interactive input (provider profile or
    /// PIN); the login screen continues from the current step.
    void signInRequired(const QString &connectionId);

    void serverUrlChanged();
    void userIdChanged();
    void authenticatedChanged();
    void isRestoringSessionChanged();
    void providerSelectionChanged();
    void profilesChanged();
    void authSessionsChanged();
    void authenticationStepChanged();
    void signInOptionsChanged();
    void deviceSignInChanged();

protected:
    void seedSession(const QString &serverUrl,
                     const QString &userId,
                     const QString &accessToken,
                     const QString &username = QString());

private:
    std::unique_ptr<HttpTransport> m_ownedTransport;
    std::unique_ptr<IProviderAdapter> m_ownedProviderAdapter;
    QList<IProviderAdapter *> m_providerAdapters;
    HttpTransport *m_transport = nullptr;
    IProviderAdapter *m_providerAdapter = nullptr;
    const IProviderRequestFactory *m_requestFactory = nullptr;
    const IProviderAuthenticator *m_providerAuthenticator = nullptr;
    QString m_serverUrl;
    // HTTP form of a scheme-less server URL, tried if HTTPS gets no response.
    QString m_schemeFallbackUrl;
    QString m_accessToken;
    QString m_refreshToken;
    // Invalid when the provider sent no lifetime or the token was restored.
    QDateTime m_accessTokenExpiresAt;
    qint64 m_accessTokenLifetimeSeconds = 0;
    void setAccessTokenLifetime(qint64 expiresInSeconds);
    QString m_profileToken;
    QString m_userId;
    QString m_username;
    QString m_providerSelection = QStringLiteral("auto");
    QString m_authenticationStep = QStringLiteral("credentials");
    QString m_pendingProfileId;
    QVariantList m_profiles;
    QVariantList m_authSessions;
    QList<ProviderProfile> m_providerProfiles;
    ProviderDetectionResult m_detectionResult;
    bool m_sessionExpiredPending = false;
    bool m_sessionExpiredEmitted = false;
    // Shared ownership: background credential tasks hold a copy, so the
    // store outlives a stalled task even if shutdown stops waiting for it.
    std::shared_ptr<ISecretStore> m_secretStore;
    ServerConnection m_activeConnection;
    bool m_isRestoringSession = false;
    // Identifies the newest restoration; a superseded one must not clear
    // m_isRestoringSession while its replacement is still running.
    quint64 m_restorationGeneration = 0;
    ConfigManager *m_configManager = nullptr;
    quint64 m_stateGeneration = 0;
    ConnectionSessionPool *m_sessionPool = nullptr;
    QStringList m_memberConnectionIds;
    QString m_activatingConnectionId;
    QString m_activationFallbackConnectionId;
    // Callers waiting on a token renewal, by connection.
    QHash<QString, QList<std::function<void(TokenRenewal)>>> m_pendingRenewals;
    // Pooled renewals use their own transport: session switches cancel
    // m_transport, which must not abort another member's renewal.
    std::unique_ptr<HttpTransport> m_renewalTransport;
    // Connection whose live-session refresh is in flight on m_transport.
    QString m_liveRenewalConnectionId;
    // Bumped when a connection's sign-in is replaced (new login) or deleted;
    // a renewal that started before must not apply its result.
    QHash<QString, quint64> m_credentialEpochs;
    // Renewed tokens whose keyring write has not finished; activation reads
    // storage and must not miss them.
    QHash<QString, ConnectionCredentials> m_unsavedRenewals;

    // Snapshot of everything a background keyring write needs. Captured on
    // the UI thread because ConfigManager is not thread-safe.
    struct CredentialPersistSnapshot {
        ServerConnection connection;
        QString accessToken;
        QString refreshToken;
        QString profileToken;
        QString deviceId;
        QString legacyServerUrl;
        QString legacyUsername;
        QString legacyUserId;
    };
    struct CredentialPersistOutcome {
        QString warning;
        bool legacyVerified = false;
    };
    // Shared FIFO state for background keyring tasks. Reference-counted so an
    // in-flight worker never touches destroyed service members at shutdown.
    // The destructor drains the queue (see ~AuthenticationService) while the
    // secret store is still alive — ApplicationInitializer destroys this
    // service before the store — so no task can race store destruction.
    struct CredentialTaskState {
        QMutex mutex;
        QWaitCondition idle;
        QQueue<std::function<void()>> tasks;
        bool pumpRunning = false;
    };

    struct RestorationResult {
        bool success = false;
        QString serverUrl;
        QString userId;
        QString accessToken;
        QString refreshToken;
        QString profileToken;
        QString username;
        QString error;
        QString cleanupError;
        ServerConnection connection;
        bool legacyMigrationComplete = false;
    };

    // Sign-in discovery for the address typed on the login screen. It has its
    // own transport: a session change on m_transport must not drop it.
    struct SignInDiscovery {
        QString address;
        QString baseUrl;
        QString state = QStringLiteral("idle");
        bool silo = false;
        bool passwordSignIn = true;
        bool deviceSignIn = false;
        bool deviceCanWithdraw = false;
        ProviderDetectionResult detection;
    };
    SignInDiscovery m_signInDiscovery;
    quint64 m_signInDiscoveryGeneration = 0;
    std::unique_ptr<HttpTransport> m_discoveryTransport;
    DeviceSignInSession *m_deviceSignIn = nullptr;

    QFutureWatcher<RestorationResult> m_restorationWatcher;
    std::shared_ptr<CredentialTaskState> m_credentialTasks =
        std::make_shared<CredentialTaskState>();

    QString normalizeUrl(const QString &url) const;
    // Typed address -> {URL to try, HTTP fallback when no scheme was typed}.
    std::pair<QString, QString> resolveServerAddress(const QString &address) const;
    void initializeDeviceSignIn();
    void discoverSilo(const QString &baseUrl, const QString &fallbackUrl, quint64 generation);
    void discoverDeviceSignIn(quint64 generation);
    void discoverSignInMethods(quint64 generation);
    void finishSignInDiscovery(const QString &state, quint64 generation);
    HttpRequestOptions discoveryRequestOptions(int maxAttempts) const;
    void startRestoration();
    // keepCredentials: the server could not be reached, so the saved sign-in
    // may still be good.
    void failRestoration(const QString &error, bool keepCredentials = false);
    void finishActivation();
    QNetworkRequest createUnauthenticatedRequest(const QString &endpoint) const;
    void configureTransport();
    IProviderAdapter *providerForKind(ProviderKind kind) const;
    bool activateProvider(IProviderAdapter *adapter);
    void probeProviderAndLogin(
        const QString &username, const QString &password, quint64 generation, int probeStep = 0);
    void startLogin(const QString &username, const QString &password, quint64 generation);
    bool retryWithSchemeFallback(const NetworkError &error,
                                 const QString &username,
                                 const QString &password,
                                 quint64 generation);
    void performLogin(const QString &username,
                      const QString &password,
                      quint64 generation);
    void handleAuthenticationResult(const ProviderAuthenticationResult &authentication,
                                    quint64 generation);
    void finishAuthentication();
    void loadProfiles(bool finishWhenUnavailable);
    enum class SessionValidity { Valid, Invalid, Unreachable };
    void refreshLiveSession();
    void renewPooledConnection(const QString &connectionId);
    void finishRenewal(const QString &connectionId, TokenRenewal outcome);
    // A session change cancels m_transport without callbacks: release callers
    // waiting on the live refresh it carried.
    void abandonLiveRenewal();
    void bumpCredentialEpoch(const QString &connectionId);
    void validateAccessToken(std::function<void(SessionValidity)> callback);
    void persistConnection();
    // Keyring I/O can block for seconds (locked keyring, unlock prompts), so
    // every mutation runs on a shared FIFO background queue. Tasks execute in
    // issue order, which keeps write-then-delete sequences (e.g. login
    // followed by logout) consistent without ever blocking the UI thread.
    void enqueueCredentialTask(std::function<void()> task);
    // Persists the in-memory tokens captured at call time, then invokes the
    // continuation on the UI thread. The continuation always runs, even when
    // the keyring write fails, mirroring the previous synchronous behavior.
    void persistCredentialsAsync(std::function<void()> continuation = {});
    void finishCredentialPersist(const CredentialPersistOutcome &outcome,
                                 const std::function<void()> &continuation);
    void removeCredentialAsync(const ServerConnection &connection, CredentialKind kind);
    void removeAllCredentialsAsync(const ServerConnection &connection,
                                    const QString &deviceId);
    void updateAuthenticationStep(const QString &step);
    void replaceProfiles(const QList<ProviderProfile> &profiles);
    void replaceAuthSessions(const QList<ProviderAuthSession> &sessions);
    void clearProfileStateInternal(bool persist);
    void clearAccountStateInternal(bool removeCredentials, bool emitLogout);
    void handleUnauthorized(bool deferLogout);
};
