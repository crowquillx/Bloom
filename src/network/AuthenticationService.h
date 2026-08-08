#pragma once

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>
#include <functional>
#include <memory>

#include "network/Types.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/ServerConnection.h"
#include "security/CredentialStore.h"

class ICatalogProvider;
class IPlaybackProvider;
class IProviderAdapter;
class ISecretStore;
class ConfigManager;
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

public:
    explicit AuthenticationService(ISecretStore *secretStore = nullptr, QObject *parent = nullptr);
    AuthenticationService(ISecretStore *secretStore,
                          HttpTransport *transport,
                          IProviderAdapter *providerAdapter,
                          QObject *parent = nullptr);
    AuthenticationService(ISecretStore *secretStore,
                          HttpTransport *transport,
                          const QList<IProviderAdapter *> &providerAdapters,
                          QObject *parent = nullptr);
    virtual ~AuthenticationService();

    virtual void initialize(ConfigManager *configManager);
    ConfigManager *configManager() const { return m_configManager; }

    Q_INVOKABLE virtual void authenticate(const QString &serverUrl,
                                          const QString &username,
                                          const QString &password);
    virtual void restoreSession(const QString &serverUrl,
                                const QString &userId,
                                const QString &accessToken,
                                const QString &username = QString());
    Q_INVOKABLE virtual void logout();
    Q_INVOKABLE void remoteLogout();
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
    Q_INVOKABLE void loadAuthSessions();
    Q_INVOKABLE void revokeAuthSession(const QString &sessionId);

    QString getServerUrl() const { return m_serverUrl; }
    QString getUserId() const { return m_userId; }
    QString getAccessToken() const { return m_accessToken; }
    QString getUsername() const { return m_username; }
    const IPlaybackProvider *playbackProvider() const;
    const ICatalogProvider *catalogProvider() const;
    ProviderKind activeProviderKind() const;
    PlaybackInfoResponse mapPlaybackInfo(const QJsonObject &wirePlaybackInfo) const;
    ParsedItemsResult parseItemsResponse(const QByteArray &wireResponse,
                                         const QString &parentId) const;
    std::function<ParsedItemsResult(const QByteArray &, const QString &)>
        itemsResponseParser() const;
    TrickplayTileInfoMap mapTrickplayInfo(const QJsonObject &wireItem) const;
    QList<MediaSegmentInfo> mapIntroSkipperSegments(
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

signals:
    void loginSuccess(const QString &userId, const QString &accessToken, const QString &username);
    void loginError(const QString &error);
    void loggedOut();
    void sessionExpired();
    void sessionExpiredAfterPlayback();

    void serverUrlChanged();
    void userIdChanged();
    void authenticatedChanged();
    void isRestoringSessionChanged();
    void providerSelectionChanged();
    void profilesChanged();
    void authSessionsChanged();
    void authenticationStepChanged();

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
    QString m_accessToken;
    QString m_refreshToken;
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
    ISecretStore *m_secretStore = nullptr;
    ServerConnection m_activeConnection;
    bool m_isRestoringSession = false;
    ConfigManager *m_configManager = nullptr;
    quint64 m_stateGeneration = 0;

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

    QFutureWatcher<RestorationResult> m_restorationWatcher;

    QString normalizeUrl(const QString &url) const;
    ProviderRequestContext requestContext(bool includeAuthentication = true) const;
    QNetworkRequest createUnauthenticatedRequest(const QString &endpoint) const;
    void configureTransport();
    IProviderAdapter *providerForKind(ProviderKind kind) const;
    bool activateProvider(IProviderAdapter *adapter);
    void probeProviderAndLogin(const QString &username,
                               const QString &password,
                               quint64 generation);
    void performLogin(const QString &username,
                      const QString &password,
                      quint64 generation);
    void handleAuthenticationResult(const ProviderAuthenticationResult &authentication,
                                    quint64 generation);
    void finishAuthentication();
    void loadProfiles(bool finishWhenUnavailable);
    void refreshAuthentication(std::function<void(bool)> completion);
    void validateAccessToken(std::function<void(bool)> callback);
    void persistConnection();
    void persistCredentials();
    void updateAuthenticationStep(const QString &step);
    void replaceProfiles(const QList<ProviderProfile> &profiles);
    void replaceAuthSessions(const QList<ProviderAuthSession> &sessions);
    void clearProfileStateInternal(bool persist);
    void clearAccountStateInternal(bool removeCredentials, bool emitLogout);
    void handleUnauthorized(bool deferLogout);
};
