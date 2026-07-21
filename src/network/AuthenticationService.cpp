#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "../security/ISecretStore.h"
#include "../utils/ConfigManager.h"
#include "providers/IProviderAdapter.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QPointer>
#include <QThreadPool>
#include "../utils/BloomLogging.h"

AuthenticationService::AuthenticationService(ISecretStore *secretStore, QObject *parent)
    : QObject(parent)
    , m_ownedTransport(std::make_unique<HttpTransport>())
    , m_ownedProviderAdapter(std::make_unique<JellyfinProviderAdapter>())
    , m_transport(m_ownedTransport.get())
    , m_providerAdapter(m_ownedProviderAdapter.get())
    , m_requestFactory(m_providerAdapter->requestFactory())
    , m_providerAuthenticator(m_providerAdapter->authenticator())
    , m_secretStore(secretStore)
{
    m_transport->setUrlRedactor([this](const QUrl &url) {
        return m_requestFactory->redactedUrl(url);
    });
    connect(m_transport, &HttpTransport::unauthorized,
            this, &AuthenticationService::handleUnauthorized);
}

AuthenticationService::AuthenticationService(ISecretStore *secretStore,
                                               HttpTransport *transport,
                                               IProviderAdapter *providerAdapter,
                                               QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_providerAdapter(providerAdapter)
    , m_requestFactory(providerAdapter ? providerAdapter->requestFactory() : nullptr)
    , m_providerAuthenticator(providerAdapter ? providerAdapter->authenticator() : nullptr)
    , m_secretStore(secretStore)
{
    Q_ASSERT(m_transport);
    Q_ASSERT(m_providerAdapter);
    Q_ASSERT(m_requestFactory);
    Q_ASSERT(m_providerAuthenticator);
    m_transport->setUrlRedactor([this](const QUrl &url) {
        return m_requestFactory->redactedUrl(url);
    });
    connect(m_transport, &HttpTransport::unauthorized,
            this, &AuthenticationService::handleUnauthorized);
}

AuthenticationService::~AuthenticationService()
{
    if (m_transport) {
        m_transport->setUrlRedactor({});
    }
}

const IPlaybackProvider *AuthenticationService::playbackProvider() const
{
    return m_providerAdapter ? m_providerAdapter->playbackProvider() : nullptr;
}

QVariantMap AuthenticationService::mapMediaItem(const QJsonObject &wireItem,
                                                 const QString &connectionId) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapMediaItem(wireItem, connectionId)
        : QVariantMap{};
}

QVariantList AuthenticationService::mapMediaItems(const QJsonArray &wireItems,
                                                   const QString &connectionId) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapMediaItems(wireItems, connectionId)
        : QVariantList{};
}

QNetworkAccessManager *AuthenticationService::networkManager() const
{
    return m_transport->networkManager();
}

void AuthenticationService::initialize(ConfigManager *configManager)
{
    if (!configManager) {
        qCWarning(lcAuth) << "AuthenticationService::initialize called with null ConfigManager";
        return;
    }

    m_configManager = configManager;
    m_isRestoringSession = true;
    emit isRestoringSessionChanged();

    const ConfigManager::SessionData session = configManager->getJellyfinSession();
    const ConfigManager::SessionData legacySession =
        configManager->getPendingLegacyJellyfinSession();
    const std::optional<ServerConnection> connection = configManager->getActiveConnection();
    const bool pendingLegacyMigration = configManager->hasPendingLegacyJellyfinMigration();
    const bool legacyMatchesConnection = connection.has_value()
        && ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection->baseUrl
        && legacySession.userId == connection->accountId;
    ISecretStore *store = m_secretStore;
    const QString deviceId = configManager->getDeviceId();

    QFuture<RestorationResult> future = QtConcurrent::run(
        [session, legacySession, connection, pendingLegacyMigration,
         legacyMatchesConnection, store, deviceId]() {
            RestorationResult result{};
            result.serverUrl = session.serverUrl;
            result.userId = session.userId;
            result.username = session.username;
            result.connection = connection.value_or(ServerConnection{});

            if (!store || !connection.has_value() || !connection->isValid()
                || connection->providerKind != ProviderKind::Jellyfin || !session.isValid()) {
                return result;
            }

            CredentialStore credentials(store);
            const CredentialReadResult credentialResult = credentials.readAccessToken(
                *connection,
                deviceId,
                legacyMatchesConnection ? legacySession.serverUrl : QString(),
                legacyMatchesConnection ? legacySession.username : QString(),
                session.accessToken);
            result.accessToken = credentialResult.secret;
            result.success = !result.accessToken.isEmpty();
            result.error = credentialResult.error;
            result.cleanupError = credentialResult.cleanupError;
            result.legacyMigrationComplete = pendingLegacyMigration
                && legacyMatchesConnection && result.success && result.error.isEmpty()
                && result.cleanupError.isEmpty();
            return result;
        });

    m_restorationWatcher.disconnect(this);
    connect(&m_restorationWatcher, &QFutureWatcher<RestorationResult>::finished,
            this, [this, configManager]() {
        const RestorationResult result = m_restorationWatcher.result();
        const auto currentConnection = configManager->getActiveConnection();
        const bool connectionChanged = result.connection.isValid()
            && (!currentConnection.has_value()
                || currentConnection->connectionId != result.connection.connectionId);

        if (connectionChanged) {
            qCInfo(lcAuth) << "Ignoring stale session restoration result after connection switch";
        } else {
            if (result.legacyMigrationComplete) {
                configManager->finalizeLegacyJellyfinMigration();
            }

            if (result.success) {
                m_activeConnection = result.connection;
                restoreSession(result.serverUrl,
                               result.userId,
                               result.accessToken,
                               result.username);
            } else if (!result.error.isEmpty()) {
                qCWarning(lcAuth) << "Session restoration failed:" << result.error;
            }
        }
        if (!result.cleanupError.isEmpty()) {
            qCWarning(lcAuth) << "Legacy credential cleanup failed:"
                              << result.cleanupError;
        }

        m_isRestoringSession = false;
        emit isRestoringSessionChanged();
    });

    m_restorationWatcher.setFuture(future);
}

QString AuthenticationService::normalizeUrl(const QString &url)
{
    QString normalized = url.trimmed();
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QNetworkRequest AuthenticationService::createRequest(const QString &endpoint) const
{
    ProviderRequestContext context;
    context.baseUrl = m_serverUrl;
    context.accessToken = m_accessToken;
    context.deviceId = m_configManager
        ? m_configManager->getDeviceId()
        : QStringLiteral("bloom-desktop-fallback");
    return m_requestFactory->createRequest(context, endpoint);
}

void AuthenticationService::authenticate(const QString &serverUrl, const QString &username, const QString &password)
{
    m_serverUrl = normalizeUrl(serverUrl);

    const ProviderAuthenticationRequest authenticationRequest =
        m_providerAuthenticator->createLoginRequest(username, password);
    const QNetworkRequest request = createRequest(authenticationRequest.endpoint);
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;

    m_transport->sendWithRetry(
        this,
        authenticationRequest.endpoint,
        [this, request, body = authenticationRequest.body]() {
            return networkManager()->post(request, body);
        },
        [this](QNetworkReply *reply) {
            onAuthenticateFinished(reply);
        },
        [this](const NetworkError &error) {
            if (error.code == 401) {
                emit loginError(tr("Invalid username or password"));
                return;
            }
            const QString detail = error.userMessage.isEmpty()
                ? tr("Could not connect to server. Please check the URL and your network connection.")
                : error.userMessage;
            emit loginError(tr("Authentication failed: %1").arg(detail));
        },
        options);
}

void AuthenticationService::onAuthenticateFinished(QNetworkReply *reply)
{
    const ProviderAuthenticationResult authentication =
        m_providerAuthenticator->parseLoginResponse(reply->readAll());
    if (!authentication.isValid()) {
        emit loginError(tr("Authentication response was incomplete."));
        return;
    }

    m_accessToken = authentication.accessToken;
    m_userId = authentication.accountId;
    m_username = authentication.username;
    
    qCDebug(lcAuth) << "Authentication successful. User ID:" << m_userId << "Username:" << m_username;

    if (m_configManager) {
        m_configManager->setJellyfinSession(m_serverUrl, m_userId, QString(), m_username);
        m_activeConnection = m_configManager->getActiveConnection().value_or(ServerConnection{});
    }

    if (m_secretStore && m_activeConnection.isValid()) {
        const QString token = m_accessToken;
        const ServerConnection connection = m_activeConnection;
        const QString deviceId = m_configManager ? m_configManager->getDeviceId() : QString();
        const ConfigManager::SessionData legacySession = m_configManager
            ? m_configManager->getPendingLegacyJellyfinSession()
            : ConfigManager::SessionData{};
        const bool legacyMatchesConnection =
            ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection.baseUrl
            && legacySession.userId == connection.accountId;
        ISecretStore *store = m_secretStore;
        QPointer<ConfigManager> configManager = m_configManager;

        QThreadPool::globalInstance()->start(
            [store, connection, token, deviceId, legacySession,
             legacyMatchesConnection, configManager]() {
                CredentialStore credentials(store);
                if (!credentials.write(connection, CredentialKind::AccessToken, token)) {
                    qCWarning(lcAuth) << "Failed to store token in keychain:" << store->lastError();
                    return;
                }
                if (credentials.read(connection, CredentialKind::AccessToken) != token) {
                    credentials.remove(connection, CredentialKind::AccessToken);
                    qCWarning(lcAuth) << "Stored token failed keychain verification";
                    return;
                }

                if (legacyMatchesConnection) {
                    const CredentialReadResult cleanup = credentials.readAccessToken(
                        connection,
                        deviceId,
                        legacySession.serverUrl,
                        legacySession.username);
                    if (!cleanup.error.isEmpty() || !cleanup.cleanupError.isEmpty()
                        || cleanup.secret != token) {
                        qCWarning(lcAuth) << "Legacy credential cleanup failed:"
                                          << (cleanup.error.isEmpty()
                                                  ? cleanup.cleanupError
                                                  : cleanup.error);
                        return;
                    }
                }

                qCDebug(lcAuth) << "Token stored in provider-neutral keychain entry";
                if (legacyMatchesConnection && configManager) {
                    QMetaObject::invokeMethod(configManager, [configManager]() {
                        if (configManager) {
                            configManager->finalizeLegacyJellyfinMigration();
                        }
                    }, Qt::QueuedConnection);
                }
            });
    }
    
    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
    qCCritical(lcAuth) << "=== AuthenticationService: EMITTING loginSuccess signal ===" << m_userId << m_username;
    emit loginSuccess(m_userId, m_accessToken, m_username);
}

void AuthenticationService::restoreSession(const QString &serverUrl,
                                           const QString &userId,
                                           const QString &accessToken,
                                           const QString &username)
{
    m_serverUrl = normalizeUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = username;
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    
    qCDebug(lcAuth) << "Restoring session for user:" << userId << "on server:" << serverUrl;
    
    // Validate the restored session
    validateAccessToken([this](bool valid) {
        if (valid) {
            qCDebug(lcAuth) << "Session restored successfully";
            emit serverUrlChanged();
            emit userIdChanged();
            emit authenticatedChanged();
            qCCritical(lcAuth) << "=== AuthenticationService: EMITTING loginSuccess from restoreSession ===" << m_userId;
            emit loginSuccess(m_userId, m_accessToken, m_username);
        } else {
            qCWarning(lcAuth) << "Stored session is invalid or expired";
            logout();
        }
    });
}

void AuthenticationService::seedSession(const QString &serverUrl,
                                        const QString &userId,
                                        const QString &accessToken,
                                        const QString &username)
{
    m_serverUrl = normalizeUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = username;
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;

    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
}

void AuthenticationService::logout()
{
    qCDebug(lcAuth) << "Logging out user:" << m_userId;
    m_transport->cancelAll();

    ServerConnection connection = m_activeConnection;
    if (!connection.isValid() && m_configManager) {
        connection = m_configManager->getActiveConnection().value_or(ServerConnection{});
    }
    if (m_secretStore && connection.isValid() && m_configManager) {
        const QString deviceId = m_configManager->getDeviceId();
        ConfigManager::SessionData legacySession =
            m_configManager->getPendingLegacyJellyfinSession();
        const bool legacyMatchesConnection =
            ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection.baseUrl
            && legacySession.userId == connection.accountId;
        if (!legacyMatchesConnection) {
            legacySession = {};
        }
        ISecretStore *store = m_secretStore;
        QThreadPool::globalInstance()->start(
            [store, connection, deviceId, legacySession]() {
                CredentialStore credentials(store);
                if (!credentials.removeAll(connection,
                                           deviceId,
                                           legacySession.serverUrl,
                                           legacySession.username)) {
                    qCWarning(lcAuth) << "Failed to remove one or more session credentials:"
                                      << store->lastError();
                } else {
                    qCDebug(lcAuth) << "Session credentials deleted from keychain";
                }
            });
    }

    m_activeConnection = {};
    m_accessToken.clear();
    m_userId.clear();
    m_username.clear();
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    
    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
    emit loggedOut();
}

void AuthenticationService::checkPendingSessionExpiry()
{
    if (m_sessionExpiredPending && !m_sessionExpiredEmitted) {
        m_sessionExpiredPending = false;
        m_sessionExpiredEmitted = true;
        emit sessionExpiredAfterPlayback();
    }
}

bool AuthenticationService::checkForSessionExpiry(QNetworkReply *reply, bool deferLogout)
{
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 401) {
        return false;
    }

    handleUnauthorized(deferLogout);
    return true;
}

void AuthenticationService::handleUnauthorized(bool deferLogout)
{
    qCWarning(lcAuth) << "Received 401 Unauthorized - session expired";
    if (deferLogout) {
        m_sessionExpiredPending = true;
    } else if (!m_sessionExpiredEmitted) {
        m_sessionExpiredEmitted = true;
        emit sessionExpired();
    }
}

void AuthenticationService::validateAccessToken(std::function<void(bool)> callback)
{
    if (m_accessToken.isEmpty() || m_userId.isEmpty()) {
        callback(false);
        return;
    }
    
    const QString endpoint = m_providerAuthenticator->sessionValidationEndpoint(m_userId);
    const QNetworkRequest request = createRequest(endpoint);
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;

    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, request]() {
            return networkManager()->get(request);
        },
        [callback](QNetworkReply *reply) {
            const int statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            callback(statusCode == 200);
        },
        [callback](const NetworkError &error) {
            qCDebug(lcAuth) << "Token validation failed. Status/error:" << error.code
                            << error.userMessage;
            callback(false);
        },
        options);
}
