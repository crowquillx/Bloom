#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "../security/ISecretStore.h"
#include "../utils/ConfigManager.h"
#include "providers/IProviderAdapter.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include "../utils/BloomLogging.h"

namespace {
QJsonArray responseArray(const QByteArray &body, const QString &member)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isArray()) {
        return document.array();
    }
    if (document.isObject()) {
        return document.object().value(member).toArray();
    }
    return {};
}

QJsonObject capabilitiesObject(ProviderCapabilities capabilities)
{
    QJsonObject result;
    const auto add = [&result, capabilities](ProviderCapability capability, const char *name) {
        result[QString::fromLatin1(name)] = capabilities.testFlag(capability);
    };
    add(ProviderCapability::RefreshAuthentication, "refresh_authentication");
    add(ProviderCapability::Profiles, "profiles");
    add(ProviderCapability::ProfilePin, "profile_pin");
    add(ProviderCapability::AuthSessions, "auth_sessions");
    add(ProviderCapability::Catalog, "catalog");
    add(ProviderCapability::NativeState, "native_state");
    add(ProviderCapability::MediaSegments, "media_segments");
    add(ProviderCapability::Playback, "playback");
    add(ProviderCapability::PlaybackReporting, "playback_reporting");
    return result;
}
}

AuthenticationService::AuthenticationService(ISecretStore *secretStore, QObject *parent)
    : QObject(parent)
    , m_ownedTransport(std::make_unique<HttpTransport>())
    , m_ownedProviderAdapter(std::make_unique<JellyfinProviderAdapter>())
    , m_providerAdapters{m_ownedProviderAdapter.get()}
    , m_transport(m_ownedTransport.get())
    , m_providerAdapter(m_ownedProviderAdapter.get())
    , m_requestFactory(m_providerAdapter->requestFactory())
    , m_providerAuthenticator(m_providerAdapter->authenticator())
    , m_secretStore(secretStore)
{
    configureTransport();
}

AuthenticationService::AuthenticationService(ISecretStore *secretStore,
                                               HttpTransport *transport,
                                               IProviderAdapter *providerAdapter,
                                               QObject *parent)
    : QObject(parent)
    , m_providerAdapters{providerAdapter}
    , m_transport(transport)
    , m_providerAdapter(providerAdapter)
    , m_requestFactory(providerAdapter ? providerAdapter->requestFactory() : nullptr)
    , m_providerAuthenticator(providerAdapter ? providerAdapter->authenticator() : nullptr)
    , m_secretStore(secretStore)
{
    configureTransport();
}

AuthenticationService::AuthenticationService(
    ISecretStore *secretStore,
    HttpTransport *transport,
    const QList<IProviderAdapter *> &providerAdapters,
    QObject *parent)
    : QObject(parent)
    , m_providerAdapters(providerAdapters)
    , m_transport(transport)
    , m_secretStore(secretStore)
{
    IProviderAdapter *initial = providerForKind(ProviderKind::Jellyfin);
    if (!initial && !m_providerAdapters.isEmpty()) {
        initial = m_providerAdapters.constFirst();
    }
    activateProvider(initial);
    configureTransport();
}

AuthenticationService::~AuthenticationService()
{
    if (m_transport) {
        disconnect(m_transport, nullptr, this, nullptr);
        m_transport->setUrlRedactor({});
        m_transport->setUnauthorizedRecovery({});
    }
}

void AuthenticationService::configureTransport()
{
    if (!m_transport) {
        return;
    }
    Q_ASSERT(m_providerAdapter);
    Q_ASSERT(m_requestFactory);
    Q_ASSERT(m_providerAuthenticator);

    m_transport->setUrlRedactor([this](const QUrl &url) {
        return m_requestFactory ? m_requestFactory->redactedUrl(url) : url.toString();
    });
    m_transport->setUnauthorizedRecovery(
        [this](std::function<void(bool)> completion) {
            refreshAuthentication(std::move(completion));
        });
    connect(m_transport, &HttpTransport::unauthorized,
            this, &AuthenticationService::handleUnauthorized, Qt::UniqueConnection);
}

IProviderAdapter *AuthenticationService::providerForKind(ProviderKind kind) const
{
    for (IProviderAdapter *adapter : m_providerAdapters) {
        if (adapter && adapter->providerKind() == kind) {
            return adapter;
        }
    }
    return nullptr;
}

bool AuthenticationService::activateProvider(IProviderAdapter *adapter)
{
    if (!adapter || !adapter->requestFactory() || !adapter->authenticator()) {
        return false;
    }
    m_providerAdapter = adapter;
    m_requestFactory = adapter->requestFactory();
    m_providerAuthenticator = adapter->authenticator();
    return true;
}

const IPlaybackProvider *AuthenticationService::playbackProvider() const
{
    return m_providerAdapter ? m_providerAdapter->playbackProvider() : nullptr;
}

const ICatalogProvider *AuthenticationService::catalogProvider() const
{
    return m_providerAdapter ? m_providerAdapter->catalogProvider() : nullptr;
}

ProviderKind AuthenticationService::activeProviderKind() const
{
    return m_providerAdapter
        ? m_providerAdapter->providerKind()
        : m_activeConnection.providerKind;
}

PlaybackInfoResponse AuthenticationService::mapPlaybackInfo(
    const QJsonObject &wirePlaybackInfo) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapPlaybackInfo(wirePlaybackInfo)
        : PlaybackInfoResponse{};
}

ParsedItemsResult AuthenticationService::parseItemsResponse(
    const QByteArray &wireResponse, const QString &parentId) const
{
    const auto parser = itemsResponseParser();
    return parser ? parser(wireResponse, parentId) : ParsedItemsResult{};
}

std::function<ParsedItemsResult(const QByteArray &, const QString &)>
AuthenticationService::itemsResponseParser() const
{
    return m_providerAdapter
        ? m_providerAdapter->itemsResponseParser()
        : std::function<ParsedItemsResult(const QByteArray &, const QString &)>{};
}

TrickplayTileInfoMap AuthenticationService::mapTrickplayInfo(
    const QJsonObject &wireItem) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapTrickplayInfo(wireItem)
        : TrickplayTileInfoMap{};
}

QList<MediaSegmentInfo> AuthenticationService::mapIntroSkipperSegments(
    const QString &itemId, const QJsonObject &wireSegments) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapIntroSkipperSegments(itemId, wireSegments)
        : QList<MediaSegmentInfo>{};
}

QVariantList AuthenticationService::mapRemoteSessions(
    const QJsonArray &wireSessions, const QString &connectionId) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapRemoteSessions(wireSessions, connectionId)
        : QVariantList{};
}

QString AuthenticationService::mapLibraryIdFromAncestors(
    const QJsonArray &wireAncestors) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapLibraryIdFromAncestors(wireAncestors)
        : QString{};
}

QVariantMap AuthenticationService::mapFilterOptions(
    const QJsonObject &wireFilters) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapFilterOptions(wireFilters)
        : QVariantMap{};
}

QStringList AuthenticationService::mapNamedItems(const QJsonObject &wireItems) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapNamedItems(wireItems)
        : QStringList{};
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

QVariantList AuthenticationService::mapChaptersFromItem(
    const QJsonObject &wireItem,
    const QString &connectionId,
    const QString &itemId) const
{
    return m_providerAdapter
        ? m_providerAdapter->mapChaptersFromItem(wireItem, connectionId, itemId)
        : QVariantList{};
}

QNetworkAccessManager *AuthenticationService::networkManager() const
{
    return m_transport ? m_transport->networkManager() : nullptr;
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
        && connection->providerKind == ProviderKind::Jellyfin
        && ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection->baseUrl
        && legacySession.userId == connection->accountId;
    ISecretStore *store = m_secretStore;
    const QString deviceId = configManager->getDeviceId();

    QFuture<RestorationResult> future = QtConcurrent::run(
        [session, legacySession, connection, pendingLegacyMigration,
         legacyMatchesConnection, store, deviceId]() {
            RestorationResult result{};
            result.connection = connection.value_or(ServerConnection{});
            if (connection.has_value()) {
                result.serverUrl = connection->baseUrl;
                result.userId = connection->accountId;
                result.username = connection->username;
            }

            if (!store || !connection.has_value() || !connection->isValid()) {
                return result;
            }

            CredentialStore credentials(store);
            if (connection->providerKind == ProviderKind::Jellyfin) {
                const CredentialReadResult access = credentials.readAccessToken(
                    *connection,
                    deviceId,
                    legacyMatchesConnection ? legacySession.serverUrl : QString(),
                    legacyMatchesConnection ? legacySession.username : QString(),
                    session.accessToken);
                result.accessToken = access.secret;
                result.error = access.error;
                result.cleanupError = access.cleanupError;
                result.legacyMigrationComplete = pendingLegacyMigration
                    && legacyMatchesConnection && !result.accessToken.isEmpty()
                    && result.error.isEmpty() && result.cleanupError.isEmpty();
            } else {
                result.accessToken = credentials.read(*connection, CredentialKind::AccessToken);
            }
            result.refreshToken = credentials.read(*connection, CredentialKind::RefreshToken);
            result.profileToken = credentials.read(*connection, CredentialKind::ProfileToken);
            result.success = !result.accessToken.isEmpty() || !result.refreshToken.isEmpty();
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
        } else if (result.success && activateProvider(
                       providerForKind(result.connection.providerKind))) {
            if (result.legacyMigrationComplete) {
                configManager->finalizeLegacyJellyfinMigration();
            }
            m_activeConnection = result.connection;
            m_refreshToken = result.refreshToken;
            m_profileToken = result.profileToken;
            restoreSession(result.serverUrl,
                           result.userId,
                           result.accessToken,
                           result.username);
        } else if (!result.error.isEmpty()) {
            qCWarning(lcAuth) << "Session restoration failed:" << result.error;
        } else if (result.success) {
            qCWarning(lcAuth)
                << "No provider adapter is registered for the stored connection";
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

QString AuthenticationService::normalizeUrl(const QString &url) const
{
    return ServerConnection::normalizeBaseUrl(url);
}

ProviderRequestContext AuthenticationService::requestContext(bool includeAuthentication) const
{
    ProviderRequestContext context;
    context.baseUrl = m_serverUrl;
    if (includeAuthentication) {
        context.accessToken = m_accessToken;
        context.profileId = m_pendingProfileId.isEmpty()
            ? m_activeConnection.profileId : m_pendingProfileId;
        if (!context.profileId.isEmpty()) {
            context.profileToken = m_profileToken;
        }
    }
    context.clientName = QStringLiteral("Bloom");
    context.clientVersion = QCoreApplication::applicationVersion();
    context.deviceId = m_configManager
        ? m_configManager->getDeviceId()
        : QStringLiteral("bloom-desktop-fallback");
    context.deviceName = QSysInfo::machineHostName();
    if (context.deviceName.isEmpty()) {
        context.deviceName = QStringLiteral("Bloom Device");
    }
    context.devicePlatform = QSysInfo::productType();
    if (context.devicePlatform.isEmpty()) {
        context.devicePlatform = QSysInfo::kernelType();
    }
    return context;
}

QNetworkRequest AuthenticationService::createRequest(const QString &endpoint) const
{
    return m_requestFactory
        ? m_requestFactory->createRequest(requestContext(true), endpoint)
        : QNetworkRequest{};
}

QNetworkRequest AuthenticationService::createUnauthenticatedRequest(
    const QString &endpoint) const
{
    return m_requestFactory
        ? m_requestFactory->createRequest(requestContext(false), endpoint)
        : QNetworkRequest{};
}

void AuthenticationService::setProviderSelection(const QString &selection)
{
    const QString normalized = selection.trimmed().toLower();
    if (normalized != QStringLiteral("auto")
        && normalized != QStringLiteral("jellyfin")
        && normalized != QStringLiteral("silo")) {
        emit loginError(tr("Unknown provider selection: %1").arg(selection));
        return;
    }
    if (normalized == m_providerSelection) {
        return;
    }

    const bool wasAuthenticated = isAuthenticated();
    clearAccountStateInternal(true, wasAuthenticated);
    m_providerSelection = normalized;
    emit providerSelectionChanged();

    if (normalized != QStringLiteral("auto")) {
        const ProviderKind kind = normalized == QStringLiteral("silo")
            ? ProviderKind::Silo : ProviderKind::Jellyfin;
        if (IProviderAdapter *adapter = providerForKind(kind)) {
            activateProvider(adapter);
        }
    }
}

void AuthenticationService::authenticate(const QString &serverUrl,
                                           const QString &username,
                                           const QString &password)
{
    clearAccountStateInternal(false, false);
    m_serverUrl = normalizeUrl(serverUrl);
    emit serverUrlChanged();
    const quint64 generation = m_stateGeneration;

    if (m_serverUrl.isEmpty()) {
        emit loginError(tr("Server URL is required."));
        return;
    }

    if (m_providerSelection == QStringLiteral("auto")) {
        probeProviderAndLogin(username, password, generation);
        return;
    }

    const ProviderKind kind = m_providerSelection == QStringLiteral("silo")
        ? ProviderKind::Silo : ProviderKind::Jellyfin;
    if (!activateProvider(providerForKind(kind))) {
        emit loginError(tr("The selected provider is not available."));
        return;
    }
    performLogin(username, password, generation);
}

void AuthenticationService::probeProviderAndLogin(const QString &username,
                                                    const QString &password,
                                                    quint64 generation)
{
    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    IProviderAdapter *jellyfin = providerForKind(ProviderKind::Jellyfin);
    const auto fallback = [this, jellyfin, username, password, generation]() {
        if (generation != m_stateGeneration) {
            return;
        }
        m_detectionResult = {};
        if (!activateProvider(jellyfin)) {
            emit loginError(tr("No compatible authentication provider is available."));
            return;
        }
        performLogin(username, password, generation);
    };

    if (!silo || !silo->requestFactory()) {
        fallback();
        return;
    }

    if (!m_transport || !networkManager()) {
        fallback();
        return;
    }
    const auto endpoint = silo->endpointFor(ProviderRoute::Health);
    if (!endpoint.has_value()) {
        fallback();
        return;
    }
    ProviderRequestContext context = requestContext(false);
    const IProviderRequestFactory *factory = silo->requestFactory();
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, factory, context, endpoint = *endpoint]() {
            return networkManager()->get(factory->createRequest(context, endpoint));
        },
        [this, silo, username, password, generation, fallback](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            const QJsonObject payload = QJsonDocument::fromJson(reply->readAll()).object();
            const auto detected = silo->mapDetectionResult(payload);
            if (!detected.has_value()) {
                fallback();
                return;
            }
            if (!activateProvider(silo)) {
                fallback();
                return;
            }
            m_detectionResult = *detected;
            performLogin(username, password, generation);
        },
        [fallback](const NetworkError &) {
            fallback();
        },
        options);
}

void AuthenticationService::performLogin(const QString &username,
                                          const QString &password,
                                          quint64 generation)
{
    if (!m_providerAuthenticator || !m_transport || !networkManager()) {
        emit loginError(tr("Authentication provider is unavailable."));
        return;
    }
    const ProviderAuthenticationRequest authenticationRequest =
        m_providerAuthenticator->createLoginRequest(
            username, password,
            m_providerSelection == QStringLiteral("auto") ? QString() : m_providerSelection);
    if (!authenticationRequest.isValid()) {
        emit loginError(tr("The provider cannot create a login request."));
        return;
    }

    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        authenticationRequest.endpoint,
        [this, endpoint = authenticationRequest.endpoint,
         body = authenticationRequest.body]() {
            return networkManager()->post(createUnauthenticatedRequest(endpoint), body);
        },
        [this, generation](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            const ProviderAuthenticationResult authentication =
                m_providerAuthenticator->parseLoginResponse(reply->readAll());
            if (!authentication.isValid()) {
                emit loginError(tr("Authentication response was incomplete."));
                return;
            }
            handleAuthenticationResult(authentication, generation);
        },
        [this, generation](const NetworkError &error) {
            if (generation != m_stateGeneration) {
                return;
            }
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

void AuthenticationService::handleAuthenticationResult(
    const ProviderAuthenticationResult &authentication,
    quint64 generation)
{
    if (generation != m_stateGeneration || !authentication.isValid()) {
        return;
    }

    m_accessToken = authentication.accessToken;
    m_refreshToken = authentication.refreshToken;
    m_profileToken = authentication.profileToken;
    m_userId = authentication.accountId;
    if (!authentication.username.isEmpty()) {
        m_username = authentication.username;
    }

    ServerConnection connection;
    if (m_configManager) {
        for (const ServerConnection &candidate : m_configManager->getConnections()) {
            if (candidate.providerKind == m_providerAdapter->providerKind()
                && candidate.baseUrl == m_serverUrl
                && candidate.accountId == m_userId) {
                connection = candidate;
                break;
            }
        }
    }
    connection.providerKind = m_providerAdapter->providerKind();
    connection.protocolMode = m_providerAdapter->protocolMode();
    connection.baseUrl = m_serverUrl;
    connection.accountId = m_userId;
    connection.profileId = authentication.profileId;
    if (connection.providerKind == ProviderKind::Jellyfin
        && connection.profileId.isEmpty()) {
        connection.profileId = m_userId;
    }
    connection.username = m_username;
    connection.displayName = m_username;
    if (m_detectionResult.providerKind == connection.providerKind) {
        connection.serverId = m_detectionResult.serverId;
        connection.serverName = m_detectionResult.serverName;
        connection.capabilities = capabilitiesObject(m_detectionResult.capabilities);
    }
    if (connection.connectionId.isEmpty()) {
        connection.connectionId = ServerConnection::createDeterministicConnectionId(
            connection.providerKind, connection.baseUrl, connection.accountId);
    }
    if (connection.credentialReference.isEmpty()) {
        connection.credentialReference =
            ServerConnection::createCredentialReference(connection.connectionId);
    }
    m_activeConnection = connection;

    persistConnection();
    persistCredentials();

    if (m_providerAdapter->supportsCapability(ProviderCapability::Profiles)) {
        loadProfiles(true);
    } else {
        finishAuthentication();
    }
}

void AuthenticationService::persistConnection()
{
    if (!m_configManager || !m_activeConnection.isValid()) {
        return;
    }
    m_configManager->upsertConnection(m_activeConnection, true);
    if (m_activeConnection.providerKind == ProviderKind::Jellyfin) {
        m_configManager->setJellyfinSession(
            m_serverUrl, m_userId, QString(), m_username);
    }
    m_activeConnection = m_configManager->getActiveConnection().value_or(m_activeConnection);
}

void AuthenticationService::persistCredentials()
{
    if (!m_secretStore || !m_activeConnection.isValid()) {
        return;
    }

    CredentialStore credentials(m_secretStore);
    bool accessStored = true;
    if (!m_accessToken.isEmpty()) {
        accessStored = credentials.write(
            m_activeConnection, CredentialKind::AccessToken, m_accessToken);
    } else {
        accessStored = credentials.remove(
            m_activeConnection, CredentialKind::AccessToken);
    }
    const bool refreshStored = !m_refreshToken.isEmpty()
        ? credentials.write(
              m_activeConnection, CredentialKind::RefreshToken, m_refreshToken)
        : credentials.remove(m_activeConnection, CredentialKind::RefreshToken);
    const bool profileStored = !m_profileToken.isEmpty()
        ? credentials.write(
              m_activeConnection, CredentialKind::ProfileToken, m_profileToken)
        : credentials.remove(m_activeConnection, CredentialKind::ProfileToken);
    if (!accessStored || !refreshStored || !profileStored) {
        qCWarning(lcAuth) << "Failed to persist one or more authentication credentials:"
                          << m_secretStore->lastError();
    }

    if (!accessStored || m_accessToken.isEmpty() || !m_configManager
        || m_activeConnection.providerKind != ProviderKind::Jellyfin) {
        return;
    }
    const ConfigManager::SessionData legacy =
        m_configManager->getPendingLegacyJellyfinSession();
    if (ServerConnection::normalizeBaseUrl(legacy.serverUrl) != m_activeConnection.baseUrl
        || legacy.userId != m_activeConnection.accountId) {
        return;
    }
    const CredentialReadResult cleanup = credentials.readAccessToken(
        m_activeConnection,
        m_configManager->getDeviceId(),
        legacy.serverUrl,
        legacy.username);
    if (cleanup.secret == m_accessToken && cleanup.error.isEmpty()
        && cleanup.cleanupError.isEmpty()) {
        m_configManager->finalizeLegacyJellyfinMigration();
    }
}

void AuthenticationService::finishAuthentication()
{
    if (m_accessToken.isEmpty() || m_userId.isEmpty()) {
        emit loginError(tr("Authentication response was incomplete."));
        return;
    }
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    updateAuthenticationStep(QStringLiteral("authenticated"));
    emit serverUrlChanged();
    emit userIdChanged();
    emit loginSuccess(m_userId, m_accessToken, m_username);
}

void AuthenticationService::loadProfiles(bool finishWhenUnavailable)
{
    if (!m_providerAdapter || !m_transport || !networkManager()) {
        if (finishWhenUnavailable) {
            emit loginError(tr("The authentication provider is unavailable."));
        }
        return;
    }
    const auto endpoint = m_providerAdapter->endpointFor(ProviderRoute::Profiles);
    if (!m_providerAdapter->supportsCapability(ProviderCapability::Profiles)
        || !endpoint.has_value()) {
        if (finishWhenUnavailable) {
            finishAuthentication();
        }
        return;
    }

    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, endpoint = *endpoint]() {
            return networkManager()->get(createRequest(endpoint));
        },
        [this, generation, finishWhenUnavailable](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            const auto mapped = m_providerAdapter->mapProfiles(
                responseArray(reply->readAll(), QStringLiteral("profiles")));
            if (!mapped.has_value()) {
                replaceProfiles({});
                emit loginError(tr("The provider returned an invalid profile list."));
                return;
            }
            replaceProfiles(*mapped);
            if (m_providerProfiles.isEmpty()) {
                emit loginError(tr("No profiles are available for this account."));
                if (!finishWhenUnavailable) {
                    updateAuthenticationStep(QStringLiteral("authenticated"));
                }
                return;
            }
            if (m_providerProfiles.size() == 1) {
                selectProfile(m_providerProfiles.constFirst().id);
            } else {
                updateAuthenticationStep(QStringLiteral("profiles"));
            }
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
                replaceProfiles({});
                emit loginError(tr("Unable to load profiles: %1").arg(error.userMessage));
            }
        },
        options);
}

void AuthenticationService::replaceProfiles(const QList<ProviderProfile> &profiles)
{
    m_providerProfiles = profiles;
    QVariantList values;
    values.reserve(profiles.size());
    for (const ProviderProfile &profile : profiles) {
        values.append(QVariantMap{
            {QStringLiteral("id"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("avatarUrl"), profile.avatarUrl},
            {QStringLiteral("hasPin"), profile.hasPin},
            {QStringLiteral("isChild"), profile.isChild},
            {QStringLiteral("isPrimary"), profile.isPrimary}
        });
    }
    m_profiles = values;
    emit profilesChanged();
}

void AuthenticationService::selectProfile(const QString &profileId)
{
    const auto profile = std::find_if(
        m_providerProfiles.cbegin(), m_providerProfiles.cend(),
        [&profileId](const ProviderProfile &candidate) {
            return candidate.id == profileId;
        });
    if (profile == m_providerProfiles.cend()) {
        emit loginError(tr("Unknown profile."));
        return;
    }
    if (m_transport) {
        m_transport->cancelAll();
    }
    ++m_stateGeneration;

    if (!m_profileToken.isEmpty()) {
        m_profileToken.clear();
        if (m_secretStore && m_activeConnection.isValid()) {
            CredentialStore(m_secretStore).remove(
                m_activeConnection, CredentialKind::ProfileToken);
        }
    }
    m_pendingProfileId = profileId;
    if (profile->hasPin) {
        updateAuthenticationStep(QStringLiteral("pin"));
        return;
    }
    verifyProfilePin(profileId, QString());
}

void AuthenticationService::verifyProfilePin(const QString &profileId, const QString &pin)
{
    if (!m_providerAuthenticator || profileId.isEmpty() || m_accessToken.isEmpty()
        || !m_transport || !networkManager()) {
        emit loginError(tr("A profile must be selected."));
        return;
    }
    const auto profile = std::find_if(
        m_providerProfiles.cbegin(), m_providerProfiles.cend(),
        [&profileId](const ProviderProfile &candidate) {
            return candidate.id == profileId;
        });
    if (m_transport) {
        m_transport->cancelAll();
    }
    ++m_stateGeneration;
    if (profile == m_providerProfiles.cend()) {
        emit loginError(tr("Unknown profile."));
        return;
    }
    if (!m_profileToken.isEmpty()) {
        m_profileToken.clear();
        if (m_secretStore && m_activeConnection.isValid()) {
            CredentialStore(m_secretStore).remove(
                m_activeConnection, CredentialKind::ProfileToken);
        }
    }
    m_pendingProfileId = profileId;
    const auto request = m_providerAuthenticator->createProfileLoginRequest(profileId, pin);
    if (!request.has_value() || !request->isValid()) {
        const auto profile = std::find_if(
            m_providerProfiles.cbegin(), m_providerProfiles.cend(),
            [&profileId](const ProviderProfile &candidate) {
                return candidate.id == profileId;
            });
        if (profile != m_providerProfiles.cend() && !profile->hasPin && pin.isEmpty()) {
            m_activeConnection.profileId = profileId;
            m_pendingProfileId.clear();
            persistConnection();
            persistCredentials();
            finishAuthentication();
            return;
        }
        emit loginError(tr("The provider does not support profile PIN verification."));
        return;
    }

    m_pendingProfileId = profileId;
    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        request->endpoint,
        [this, endpoint = request->endpoint, body = request->body]() {
            return networkManager()->post(createRequest(endpoint), body);
        },
        [this, generation](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            const ProviderProfileAuthenticationResult authentication =
                m_providerAuthenticator->parseProfileLoginResponse(reply->readAll());
            if (authentication.isIncorrectPin()) {
                updateAuthenticationStep(QStringLiteral("pin"));
                emit loginError(tr("Invalid profile PIN"));
                return;
            }
            if (!authentication.isValid() || authentication.profileToken.isEmpty()) {
                emit loginError(tr("Profile verification response was incomplete."));
                return;
            }
            m_profileToken = authentication.profileToken;
            m_activeConnection.profileId = m_pendingProfileId;
            m_pendingProfileId.clear();
            persistConnection();
            persistCredentials();
            finishAuthentication();
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
                emit loginError(error.code == 401
                    ? tr("Invalid profile PIN")
                    : tr("Profile verification failed: %1").arg(error.userMessage));
            }
        },
        options);
}

void AuthenticationService::switchProfile()
{
    if (m_accessToken.isEmpty() || !m_providerAdapter
        || !m_providerAdapter->supportsCapability(ProviderCapability::Profiles)) {
        return;
    }
    clearProfileStateInternal(true);
    loadProfiles(false);
}

void AuthenticationService::loadAuthSessions()
{
    if (!isAuthenticated() || !m_providerAdapter || !m_transport || !networkManager()) {
        return;
    }
    const auto endpoint = m_providerAdapter->endpointFor(ProviderRoute::AuthSessions);
    if (!m_providerAdapter->supportsCapability(ProviderCapability::AuthSessions)
        || !endpoint.has_value()) {
        replaceAuthSessions({});
        return;
    }

    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, endpoint = *endpoint]() {
            return networkManager()->get(createRequest(endpoint));
        },
        [this, generation](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            const auto mapped = m_providerAdapter->mapAuthSessions(
                responseArray(reply->readAll(), QStringLiteral("sessions")));
            if (!mapped.has_value()) {
                replaceAuthSessions({});
                emit loginError(tr("The provider returned an invalid session list."));
                return;
            }
            replaceAuthSessions(*mapped);
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
                replaceAuthSessions({});
                emit loginError(tr("Unable to load authentication sessions: %1")
                                    .arg(error.userMessage));
            }
        },
        options);
}

void AuthenticationService::replaceAuthSessions(
    const QList<ProviderAuthSession> &sessions)
{
    QVariantList values;
    values.reserve(sessions.size());
    for (const ProviderAuthSession &session : sessions) {
        values.append(QVariantMap{
            {QStringLiteral("id"), session.id},
            {QStringLiteral("deviceName"), session.deviceName},
            {QStringLiteral("ipAddress"), session.ipAddress},
            {QStringLiteral("createdAt"), session.createdAt},
            {QStringLiteral("expiresAt"), session.expiresAt},
            {QStringLiteral("revokedAt"), session.revokedAt},
            {QStringLiteral("isCurrent"), session.isCurrent}
        });
    }
    m_authSessions = values;
    emit authSessionsChanged();
}

void AuthenticationService::revokeAuthSession(const QString &sessionId)
{
    if (!isAuthenticated() || !m_providerAdapter || !m_transport || !networkManager()
        || sessionId.isEmpty()) {
        return;
    }
    ProviderRouteContext routeContext;
    routeContext.accountId = m_userId;
    routeContext.profileId = m_activeConnection.profileId;
    routeContext.sessionId = sessionId;
    const auto endpoint = m_providerAdapter->endpointFor(
        ProviderRoute::RevokeAuthSession, routeContext);
    if (!endpoint.has_value()) {
        emit loginError(tr("The provider does not support session revocation."));
        return;
    }
    bool revokingCurrent = false;
    for (const QVariant &value : m_authSessions) {
        const QVariantMap session = value.toMap();
        if (session.value(QStringLiteral("id")).toString() == sessionId) {
            revokingCurrent =
                session.value(QStringLiteral("isCurrent")).toBool();
            break;
        }
    }

    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, endpoint = *endpoint]() {
            return networkManager()->deleteResource(createRequest(endpoint));
        },
        [this, generation, revokingCurrent](QNetworkReply *) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (revokingCurrent) {
                logout();
            } else {
                loadAuthSessions();
            }
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
                replaceAuthSessions({});
                emit loginError(tr("Unable to revoke authentication session: %1")
                                    .arg(error.userMessage));
            }
        },
        options);
}

void AuthenticationService::remoteLogout()
{
    if (m_accessToken.isEmpty() || !m_providerAdapter || !m_transport
        || !networkManager()) {
        logout();
        return;
    }
    const auto endpoint = m_providerAdapter->endpointFor(ProviderRoute::CallerLogout);
    if (!endpoint.has_value()) {
        logout();
        return;
    }
    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, endpoint = *endpoint]() {
            return networkManager()->post(createRequest(endpoint), QByteArray{});
        },
        [this, generation](QNetworkReply *) {
            if (generation == m_stateGeneration) {
                logout();
            }
        },
        [this, generation](const NetworkError &error) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (error.code == 401) {
                logout();
            } else {
                emit loginError(tr("Remote logout failed: %1").arg(error.userMessage));
            }
        },
        options);
}

void AuthenticationService::refreshAuthentication(std::function<void(bool)> completion)
{
    if (!m_providerAuthenticator || m_refreshToken.isEmpty()
        || !m_transport || !networkManager()) {
        completion(false);
        return;
    }
    const auto request = m_providerAuthenticator->createRefreshRequest(m_refreshToken);
    if (!request.has_value() || !request->isValid()) {
        completion(false);
        return;
    }

    const quint64 generation = m_stateGeneration;
    const auto sharedCompletion =
        std::make_shared<std::function<void(bool)>>(std::move(completion));
    const auto complete = [sharedCompletion](bool success) {
        if (!*sharedCompletion) {
            return;
        }
        auto callback = std::move(*sharedCompletion);
        callback(success);
    };
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        request->endpoint,
        [this, endpoint = request->endpoint, body = request->body]() {
            return networkManager()->post(createUnauthenticatedRequest(endpoint), body);
        },
        [this, generation, complete](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                complete(false);
                return;
            }
            const ProviderAuthenticationResult authentication =
                m_providerAuthenticator->parseRefreshResponse(reply->readAll());
            if (!authentication.isValidRefresh()) {
                complete(false);
                return;
            }
            m_accessToken = authentication.accessToken;
            if (!authentication.refreshToken.isEmpty()) {
                m_refreshToken = authentication.refreshToken;
            }
            persistCredentials();
            complete(true);
        },
        [this, generation, complete](const NetworkError &) {
            if (generation == m_stateGeneration) {
                complete(false);
            }
        },
        options);
}

void AuthenticationService::restoreSession(const QString &serverUrl,
                                           const QString &userId,
                                           const QString &accessToken,
                                           const QString &username)
{
    if (m_transport) {
        m_transport->cancelAll();
    }
    ++m_stateGeneration;
    const quint64 generation = m_stateGeneration;
    const QString normalizedServerUrl = normalizeUrl(serverUrl);
    const bool switchingAccount = m_activeConnection.isValid()
        && (m_activeConnection.baseUrl != normalizedServerUrl
            || m_activeConnection.accountId != userId);
    if (switchingAccount) {
        m_refreshToken.clear();
        m_profileToken.clear();
        m_pendingProfileId.clear();
        m_activeConnection.profileId.clear();
        replaceProfiles({});
        replaceAuthSessions({});
    }
    m_serverUrl = normalizedServerUrl;
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = username;
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    updateAuthenticationStep(QStringLiteral("credentials"));

    const auto finishRestore = [this, generation]() {
        if (generation != m_stateGeneration) {
            return;
        }
        if (m_providerAdapter
            && m_providerAdapter->supportsCapability(ProviderCapability::Profiles)
            && (m_activeConnection.profileId.isEmpty() || m_profileToken.isEmpty())) {
            m_activeConnection.profileId.clear();
            m_profileToken.clear();
            loadProfiles(true);
        } else {
            finishAuthentication();
        }
    };

    validateAccessToken([this, generation, finishRestore](bool valid) {
        if (generation != m_stateGeneration) {
            return;
        }
        if (valid) {
            finishRestore();
            return;
        }
        refreshAuthentication([this, generation, finishRestore](bool refreshed) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (!refreshed) {
                qCWarning(lcAuth) << "Stored session is invalid or expired";
                clearAccountStateInternal(true, true);
                return;
            }
            validateAccessToken([this, generation, finishRestore](bool refreshValid) {
                if (generation != m_stateGeneration) {
                    return;
                }
                if (refreshValid) {
                    finishRestore();
                } else {
                    clearAccountStateInternal(true, true);
                }
            });
        });
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
    updateAuthenticationStep(QStringLiteral("authenticated"));
    emit serverUrlChanged();
    emit userIdChanged();
}

void AuthenticationService::logout()
{
    clearAccountStateInternal(true, true);
}

void AuthenticationService::clearAccountState()
{
    clearAccountStateInternal(true, true);
}

void AuthenticationService::clearProfileState()
{
    clearProfileStateInternal(true);
}

void AuthenticationService::clearProfileStateInternal(bool persist)
{
    const bool wasAuthenticated = isAuthenticated();
    if (m_transport) {
        m_transport->cancelAll();
    }
    ++m_stateGeneration;
    m_pendingProfileId.clear();
    m_profileToken.clear();
    m_activeConnection.profileId.clear();
    replaceProfiles({});
    replaceAuthSessions({});
    if (persist && m_activeConnection.isValid()) {
        if (m_secretStore) {
            CredentialStore(m_secretStore).remove(
                m_activeConnection, CredentialKind::ProfileToken);
        }
        persistConnection();
    }
    m_authenticationStep = m_accessToken.isEmpty()
        ? QStringLiteral("credentials") : QStringLiteral("profiles");
    emit authenticationStepChanged();
    if (wasAuthenticated != isAuthenticated()) {
        emit authenticatedChanged();
    }
}

void AuthenticationService::clearAccountStateInternal(bool removeCredentials,
                                                       bool emitLogout)
{
    if (m_transport) {
        m_transport->cancelAll();
    }
    const bool wasAuthenticated = isAuthenticated();
    ++m_stateGeneration;

    const ServerConnection connection = m_activeConnection.isValid()
        ? m_activeConnection
        : (m_configManager
               ? m_configManager->getActiveConnection().value_or(ServerConnection{})
               : ServerConnection{});
    if (removeCredentials && m_secretStore && connection.isValid()) {
        CredentialStore credentials(m_secretStore);
        const QString deviceId = m_configManager ? m_configManager->getDeviceId() : QString();
        credentials.removeAll(connection, deviceId);
    }
    if (m_configManager) {
        if (removeCredentials && connection.providerKind == ProviderKind::Jellyfin) {
            m_configManager->clearJellyfinSession();
        } else {
            m_configManager->clearActiveConnection();
        }
    }

    m_activeConnection = {};
    m_detectionResult = {};
    m_accessToken.clear();
    m_refreshToken.clear();
    m_profileToken.clear();
    m_userId.clear();
    m_username.clear();
    m_pendingProfileId.clear();
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    replaceProfiles({});
    replaceAuthSessions({});
    m_authenticationStep = QStringLiteral("credentials");
    emit authenticationStepChanged();
    emit serverUrlChanged();
    emit userIdChanged();
    if (wasAuthenticated) {
        emit authenticatedChanged();
    }
    if (emitLogout) {
        emit loggedOut();
    }
}

void AuthenticationService::updateAuthenticationStep(const QString &step)
{
    if (m_authenticationStep == step) {
        return;
    }
    const bool wasAuthenticated = isAuthenticated();
    m_authenticationStep = step;
    emit authenticationStepChanged();
    if (wasAuthenticated != isAuthenticated()) {
        emit authenticatedChanged();
    }
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
        if (!m_sessionExpiredEmitted) {
            m_sessionExpiredPending = true;
        }
    } else if (!m_sessionExpiredEmitted) {
        m_sessionExpiredEmitted = true;
        emit sessionExpired();
    }
}

void AuthenticationService::validateAccessToken(std::function<void(bool)> callback)
{
    if (!m_providerAuthenticator || m_accessToken.isEmpty() || m_userId.isEmpty()
        || !m_transport || !networkManager()) {
        callback(false);
        return;
    }
    const QString endpoint = m_providerAuthenticator->sessionValidationEndpoint(m_userId);
    if (endpoint.isEmpty()) {
        callback(false);
        return;
    }

    const auto sharedCallback =
        std::make_shared<std::function<void(bool)>>(std::move(callback));
    const auto complete = [sharedCallback](bool valid) {
        if (!*sharedCallback) {
            return;
        }
        auto resultCallback = std::move(*sharedCallback);
        resultCallback(valid);
    };
    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, endpoint]() {
            return networkManager()->get(createRequest(endpoint));
        },
        [this, complete](QNetworkReply *reply) {
            const int statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const ProviderAuthenticationResult identity =
                m_providerAuthenticator->parseSessionValidationResponse(reply->readAll());
            complete(statusCode >= 200 && statusCode < 300
                     && identity.accountId == m_userId);
        },
        [complete](const NetworkError &) {
            complete(false);
        },
        options);
}
