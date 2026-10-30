#include "AuthenticationService.h"
#include "ConnectionSessionPool.h"
#include "DeviceSignInSession.h"
#include "HttpTransport.h"
#include "models/MediaModels.h"
#include "../security/ISecretStore.h"
#include "../utils/ConfigManager.h"
#include "providers/IProviderAdapter.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include <algorithm>
#include <tuple>
#include <utility>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
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

namespace {
// How long shutdown waits for pending keyring tasks before proceeding
// without them. Normal operations finish in milliseconds; only a stalled
// platform keyring hits this bound, and stalled tasks keep the store alive
// via shared ownership until they complete.
constexpr int kCredentialDrainTimeoutMs = 5000;
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
    add(ProviderCapability::ThemeSongs, "theme_songs");
    add(ProviderCapability::Trickplay, "trickplay");
    add(ProviderCapability::Catalog, "catalog");
    add(ProviderCapability::NativeState, "native_state");
    add(ProviderCapability::MediaSegments, "media_segments");
    add(ProviderCapability::Playback, "playback");
    add(ProviderCapability::PlaybackReporting, "playback_reporting");
    add(ProviderCapability::Watchlist, "watchlist");
    add(ProviderCapability::HomeSections, "home_sections");
    return result;
}
}

AuthenticationService::AuthenticationService(std::shared_ptr<ISecretStore> secretStore,
                                               QObject *parent)
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
    m_sessionPool = new ConnectionSessionPool(m_secretStore, this);
    configureTransport();
    initializeDeviceSignIn();
}

AuthenticationService::AuthenticationService(std::shared_ptr<ISecretStore> secretStore,
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
    m_sessionPool = new ConnectionSessionPool(m_secretStore, this);
    configureTransport();
    initializeDeviceSignIn();
}

AuthenticationService::AuthenticationService(
    std::shared_ptr<ISecretStore> secretStore,
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
    m_sessionPool = new ConnectionSessionPool(m_secretStore, this);
    configureTransport();
    initializeDeviceSignIn();
}

AuthenticationService::~AuthenticationService()
{
    if (m_transport) {
        disconnect(m_transport, nullptr, this, nullptr);
        m_transport->setUrlRedactor({});
        m_transport->setUnauthorizedRecovery({});
    }
    // Drain queued and running keyring tasks so a sign-out deletion queued
    // just before quit still executes instead of being lost at exit. The
    // wait is bounded: a task stalled by an unresponsive keyring must not
    // hang shutdown forever. Tasks hold a shared store reference (see
    // persistCredentialsAsync), so one that outlasts this wait still sees a
    // live store. Queued completions are posted events, so they cannot
    // deadlock this wait; keyring tasks never block on the UI thread.
    const auto state = m_credentialTasks;
    QMutexLocker locker(&state->mutex);
    while (state->pumpRunning) {
        if (!state->idle.wait(&state->mutex, kCredentialDrainTimeoutMs)) {
            qCWarning(lcAuth) << "Credential tasks still pending after"
                              << kCredentialDrainTimeoutMs
                              << "ms; continuing shutdown without them";
            break;
        }
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
        [this](std::function<void(TokenRenewal)> completion) {
            renewConnection(activeConnectionId(), std::move(completion));
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

bool AuthenticationService::supportsCapability(ProviderCapability capability) const
{
    return m_providerAdapter && m_providerAdapter->supportsCapability(capability);
}

std::optional<QString> AuthenticationService::endpointFor(
    ProviderRoute route, const ProviderRouteContext &context) const
{
    return m_providerAdapter
        ? m_providerAdapter->endpointFor(route, context)
        : std::nullopt;
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

QList<MediaSegmentInfo> AuthenticationService::mapMediaSegments(
    const QString &itemId, const QJsonObject &wireSegments) const
{
    if (!m_providerAdapter) {
        return {};
    }
    const auto mapped = m_providerAdapter->mapMediaSegments(itemId, wireSegments);
    return mapped.value_or(QList<MediaSegmentInfo>{});
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
    if (!m_memberConnectionIds.isEmpty()) {
        setMemberConnections(m_memberConnectionIds);
    }
    startRestoration();
}

void AuthenticationService::startRestoration()
{
    ConfigManager *configManager = m_configManager;
    m_isRestoringSession = true;
    const quint64 restorationGeneration = ++m_restorationGeneration;
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
    // Shared copy: a restore still running at shutdown keeps the store
    // alive instead of racing its destruction.
    const std::shared_ptr<ISecretStore> store = m_secretStore;
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

            CredentialStore credentials(store.get());
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
            this, [this, configManager, restorationGeneration]() {
        const RestorationResult result = m_restorationWatcher.result();
        const auto currentConnection = configManager->getActiveConnection();
        const bool connectionChanged = result.connection.isValid()
            && (!currentConnection.has_value()
                || currentConnection->connectionId != result.connection.connectionId);

        QString failure;
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
            QString accessToken = result.accessToken;
            // A merged read renewed this member moments ago; its background
            // write may not have reached storage yet.
            const QString connectionId = result.connection.connectionId;
            std::optional<ConnectionCredentials> renewed;
            if (m_unsavedRenewals.contains(connectionId)) {
                renewed = m_unsavedRenewals.value(connectionId);
            } else {
                renewed = m_sessionPool->renewedCredentials(connectionId);
            }
            if (renewed.has_value()) {
                accessToken = renewed->accessToken;
                m_refreshToken = renewed->refreshToken;
            }
            restoreSession(result.serverUrl,
                           result.userId,
                           accessToken,
                           result.username);
        } else if (!result.error.isEmpty()) {
            qCWarning(lcAuth) << "Session restoration failed:" << result.error;
            failure = result.error;
        } else if (result.success) {
            qCWarning(lcAuth)
                << "No provider adapter is registered for the stored connection";
            failure = tr("This server type is not available.");
        } else {
            failure = tr("This account has no saved sign-in. Sign in again.");
        }
        if (!connectionChanged && !failure.isEmpty() && !m_activatingConnectionId.isEmpty()) {
            failRestoration(failure);
        }
        if (!result.cleanupError.isEmpty()) {
            qCWarning(lcAuth) << "Legacy credential cleanup failed:"
                              << result.cleanupError;
        }
        // A fallback or next-member activation started inside failRestoration
        // owns the flag now.
        if (restorationGeneration == m_restorationGeneration) {
            m_isRestoringSession = false;
            emit isRestoringSessionChanged();
        }
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

    if (normalized != QStringLiteral("auto")) {
        const ProviderKind kind = normalized == QStringLiteral("silo")
            ? ProviderKind::Silo : ProviderKind::Jellyfin;
        if (IProviderAdapter *adapter = providerForKind(kind)) {
            activateProvider(adapter);
        }
    }
    emit providerSelectionChanged();
}

std::pair<QString, QString> AuthenticationService::resolveServerAddress(
    const QString &address) const
{
    const QString normalized = normalizeUrl(address);
    // A bare host ("media.example.com:8096") tries HTTPS first and falls back
    // to HTTP only when HTTPS produces no HTTP response at all. An explicit
    // scheme is used as typed and never downgraded.
    static const QRegularExpression schemePattern(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    if (!normalized.isEmpty() && !schemePattern.match(normalized).hasMatch()) {
        return {QStringLiteral("https://") + normalized, QStringLiteral("http://") + normalized};
    }
    return {normalized, QString()};
}

void AuthenticationService::authenticate(const QString &serverUrl,
                                           const QString &username,
                                           const QString &password)
{
    clearAccountStateInternal(false, false);
    std::tie(m_serverUrl, m_schemeFallbackUrl) = resolveServerAddress(serverUrl);
    emit serverUrlChanged();
    const quint64 generation = m_stateGeneration;

    if (m_serverUrl.isEmpty()) {
        emit loginError(tr("Server URL is required."));
        return;
    }

    startLogin(username, password, generation);
}

bool AuthenticationService::retryWithSchemeFallback(const NetworkError &error,
                                                     const QString &username,
                                                     const QString &password,
                                                     quint64 generation)
{
    if (error.httpStatus != 0) {
        // The server answered over HTTPS, so the scheme is settled.
        m_schemeFallbackUrl.clear();
        return false;
    }
    if (generation != m_stateGeneration || m_schemeFallbackUrl.isEmpty()
        || error.networkErrorCode == static_cast<int>(QNetworkReply::OperationCanceledError)) {
        return false;
    }
    qCInfo(lcAuth) << "HTTPS unreachable without a scheme; retrying over HTTP";
    m_serverUrl = std::exchange(m_schemeFallbackUrl, QString());
    emit serverUrlChanged();
    startLogin(username, password, generation);
    return true;
}

void AuthenticationService::startLogin(const QString &username,
                                        const QString &password,
                                        quint64 generation)
{
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
                                                    quint64 generation, int probeStep)
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
    const ProviderRoute routes[] = {ProviderRoute::Health, ProviderRoute::Ready,
                                    ProviderRoute::SystemInfo};
    const auto endpoint = silo->endpointFor(routes[probeStep]);
    if (!endpoint.has_value()) {
        fallback();
        return;
    }
    ProviderRequestContext context = requestContext(false);
    const IProviderRequestFactory *factory = silo->requestFactory();
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, factory, context, endpoint = *endpoint]() {
            return networkManager()->get(factory->createRequest(context, endpoint));
        },
        [this, silo, username, password, generation, fallback, probeStep](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                return;
            }
            // Any HTTP response confirms the scheme.
            m_schemeFallbackUrl.clear();
            if (probeStep < 2) {
                probeProviderAndLogin(username, password, generation, probeStep + 1);
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
        [this, username, password, generation, fallback](const NetworkError &error) {
            if (retryWithSchemeFallback(error, username, password, generation)) {
                return;
            }
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
        m_providerAuthenticator->createLoginRequest(username, password);
    if (!authenticationRequest.isValid()) {
        emit loginError(tr("The provider cannot create a login request."));
        return;
    }

    HttpRequestOptions options;
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
        [this, username, password, generation](const NetworkError &error) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (retryWithSchemeFallback(error, username, password, generation)) {
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
    setAccessTokenLifetime(authentication.expiresInSeconds);
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
    // A new sign-in replaces any renewal still in flight for this account.
    bumpCredentialEpoch(connection.connectionId);

    persistConnection();
    // Keyring writes can block on a locked store, so continue the login flow
    // once the write completes. The UI stays responsive and keeps showing
    // "Signing in…" until the session finishes authenticating.
    persistCredentialsAsync([this, generation]() {
        if (generation != m_stateGeneration) {
            return;
        }
        if (m_providerAdapter
            && m_providerAdapter->supportsCapability(ProviderCapability::Profiles)) {
            loadProfiles(true);
        } else {
            finishAuthentication();
        }
    });
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

void AuthenticationService::enqueueCredentialTask(std::function<void()> task)
{
    if (!task) {
        return;
    }
    const auto state = m_credentialTasks;
    bool startPump = false;
    {
        QMutexLocker locker(&state->mutex);
        state->tasks.enqueue(std::move(task));
        if (!state->pumpRunning) {
            state->pumpRunning = true;
            startPump = true;
        }
    }
    if (startPump) {
        // The pump drains itself; no caller needs the future.
        static_cast<void>(QtConcurrent::run([state]() {
            for (;;) {
                std::function<void()> next;
                {
                    QMutexLocker locker(&state->mutex);
                    if (state->tasks.isEmpty()) {
                        state->pumpRunning = false;
                        state->idle.wakeAll();
                        return;
                    }
                    next = state->tasks.dequeue();
                }
                next();
            }
        }));
    }
}

void AuthenticationService::persistCredentialsAsync(std::function<void()> continuation)
{
    const std::shared_ptr<ISecretStore> store = m_secretStore;
    const ServerConnection connection = m_activeConnection;
    // A merged space may also read this account through the pool (after a
    // route moves the live session away). Hand it the current tokens, so it
    // never renews with a refresh token this session already rotated.
    if (connection.isValid() && !m_accessToken.isEmpty()) {
        m_sessionPool->updateCredentials(connection.connectionId,
                                         {m_accessToken, m_refreshToken, m_profileToken});
    }
    if (!store || !connection.isValid()) {
        if (continuation) {
            continuation();
        }
        return;
    }

    CredentialPersistSnapshot snapshot;
    snapshot.connection = connection;
    snapshot.accessToken = m_accessToken;
    snapshot.refreshToken = m_refreshToken;
    snapshot.profileToken = m_profileToken;
    if (m_configManager) {
        snapshot.deviceId = m_configManager->getDeviceId();
        if (connection.providerKind == ProviderKind::Jellyfin) {
            const ConfigManager::SessionData legacy =
                m_configManager->getPendingLegacyJellyfinSession();
            snapshot.legacyServerUrl = legacy.serverUrl;
            snapshot.legacyUsername = legacy.username;
            snapshot.legacyUserId = legacy.userId;
        }
    }

    const auto outcome = std::make_shared<CredentialPersistOutcome>();
    const QPointer<AuthenticationService> guard(this);
    enqueueCredentialTask([store, snapshot, outcome, guard, continuation]() {
        CredentialStore credentials(store.get());
        bool accessStored = true;
        if (!snapshot.accessToken.isEmpty()) {
            accessStored = credentials.write(
                snapshot.connection, CredentialKind::AccessToken, snapshot.accessToken);
        } else {
            accessStored = credentials.remove(
                snapshot.connection, CredentialKind::AccessToken);
        }
        const bool refreshStored = !snapshot.refreshToken.isEmpty()
            ? credentials.write(
                  snapshot.connection, CredentialKind::RefreshToken, snapshot.refreshToken)
            : credentials.remove(snapshot.connection, CredentialKind::RefreshToken);
        const bool profileStored = !snapshot.profileToken.isEmpty()
            ? credentials.write(
                  snapshot.connection, CredentialKind::ProfileToken, snapshot.profileToken)
            : credentials.remove(snapshot.connection, CredentialKind::ProfileToken);
        if (!accessStored || !refreshStored || !profileStored) {
            outcome->warning = store->lastError();
        }

        if (accessStored && !snapshot.accessToken.isEmpty()
            && snapshot.connection.providerKind == ProviderKind::Jellyfin
            && ServerConnection::normalizeBaseUrl(snapshot.legacyServerUrl)
                   == snapshot.connection.baseUrl
            && snapshot.legacyUserId == snapshot.connection.accountId) {
            const CredentialReadResult cleanup = credentials.readAccessToken(
                snapshot.connection,
                snapshot.deviceId,
                snapshot.legacyServerUrl,
                snapshot.legacyUsername);
            if (cleanup.secret == snapshot.accessToken && cleanup.error.isEmpty()
                && cleanup.cleanupError.isEmpty()) {
                outcome->legacyVerified = true;
            } else if (!cleanup.error.isEmpty()) {
                outcome->warning = cleanup.error;
            } else if (!cleanup.cleanupError.isEmpty()) {
                outcome->warning = cleanup.cleanupError;
            }
        }

        // Deliver the outcome through the application instance, which outlives
        // both the service and the pump. Copying the QPointer here is safe;
        // it is only checked and dereferenced inside the functor, which runs
        // on the UI thread — the only thread that can destroy the service.
        // Qt drops the queued call if the dispatcher goes away first.
        if (QCoreApplication *dispatcher = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(
                dispatcher,
                [guard, outcome, continuation]() {
                    if (!guard) {
                        return;
                    }
                    guard->finishCredentialPersist(*outcome, continuation);
                },
                Qt::QueuedConnection);
        }
    });
}

void AuthenticationService::finishCredentialPersist(
    const CredentialPersistOutcome &outcome,
    const std::function<void()> &continuation)
{
    if (!outcome.warning.isEmpty()) {
        qCWarning(lcAuth) << "Failed to persist one or more authentication credentials:"
                          << outcome.warning;
    }
    if (outcome.legacyVerified && m_configManager) {
        m_configManager->finalizeLegacyJellyfinMigration();
    }
    if (continuation) {
        continuation();
    }
}

void AuthenticationService::removeCredentialAsync(const ServerConnection &connection,
                                                  CredentialKind kind)
{
    const std::shared_ptr<ISecretStore> store = m_secretStore;
    if (!store || !connection.isValid()) {
        return;
    }
    enqueueCredentialTask([store, connection, kind]() {
        CredentialStore(store.get()).remove(connection, kind);
    });
}

void AuthenticationService::removeAllCredentialsAsync(const ServerConnection &connection,
                                                     const QString &deviceId)
{
    if (connection.isValid()) {
        bumpCredentialEpoch(connection.connectionId);
    }
    const std::shared_ptr<ISecretStore> store = m_secretStore;
    if (!store || !connection.isValid()) {
        return;
    }
    enqueueCredentialTask([store, connection, deviceId]() {
        CredentialStore(store.get()).removeAll(connection, deviceId);
    });
}

void AuthenticationService::finishAuthentication()
{
    if (m_accessToken.isEmpty() || m_userId.isEmpty()) {
        emit loginError(tr("Authentication response was incomplete."));
        return;
    }
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    // Publish identity changes before authenticatedChanged. Main.qml navigates
    // on that transition and may start catalog requests immediately; emitting
    // userIdChanged afterward would cancel those fresh requests.
    emit serverUrlChanged();
    emit userIdChanged();
    updateAuthenticationStep(QStringLiteral("authenticated"));
    emit loginSuccess(m_userId, m_accessToken, m_username);
    finishActivation();
}

void AuthenticationService::finishActivation()
{
    if (m_activatingConnectionId.isEmpty()) {
        return;
    }
    const QString activated = std::exchange(m_activatingConnectionId, QString());
    m_activationFallbackConnectionId.clear();
    emit connectionActivated(activated);
}

void AuthenticationService::failRestoration(const QString &error, bool keepCredentials)
{
    if (m_activatingConnectionId.isEmpty()) {
        if (!keepCredentials) {
            clearAccountStateInternal(true, true);
            return;
        }
        // The server could not be reached: keep the saved sign-in, and keep
        // the account selected so the next launch tries it again.
        const QString connectionId = activeConnectionId();
        clearAccountStateInternal(false, true);
        if (m_configManager && !connectionId.isEmpty()
            && m_configManager->getConnection(connectionId).has_value()) {
            m_configManager->setActiveConnection(connectionId);
        }
        if (!error.isEmpty()) {
            emit loginError(error);
        }
        return;
    }

    // An explicit switch never deletes credentials: the server may only be
    // unreachable. Fall back to the session the user switched away from.
    const QString failed = std::exchange(m_activatingConnectionId, QString());
    const QString fallback = std::exchange(m_activationFallbackConnectionId, QString());
    clearAccountStateInternal(false, false);
    emit connectionActivationFailed(
        failed, error.isEmpty() ? tr("Could not sign in to this server.") : error);
    if (!m_activatingConnectionId.isEmpty()) {
        // A listener chose another connection (for example the next member
        // of a Bloom profile); that choice replaces the fallback.
        return;
    }
    if (!fallback.isEmpty() && fallback != failed && m_configManager
        && m_configManager->getConnection(fallback).has_value()) {
        activateConnection(fallback);
        // A failed fallback ends at the login screen rather than looping.
        m_activationFallbackConnectionId.clear();
    } else {
        emit loggedOut();
    }
}

QString AuthenticationService::activeConnectionId() const
{
    if (m_activeConnection.isValid()) {
        return m_activeConnection.connectionId;
    }
    const auto connection = m_configManager
        ? m_configManager->getActiveConnection() : std::nullopt;
    return connection.has_value() ? connection->connectionId : QString();
}

void AuthenticationService::setMemberConnections(const QStringList &connectionIds)
{
    // Kept so initialize() can resolve members requested before ConfigManager
    // was attached (services are constructed before session restore).
    m_memberConnectionIds = connectionIds;
    QList<ServerConnection> connections;
    if (m_configManager) {
        for (const QString &connectionId : connectionIds) {
            if (const auto connection = m_configManager->getConnection(connectionId)) {
                connections.append(*connection);
            }
        }
    }
    m_sessionPool->setConnections(connections);
}

bool AuthenticationService::hasSessionForConnection(const QString &connectionId) const
{
    return requestContextForConnection(connectionId).has_value();
}

std::optional<ProviderRequestContext> AuthenticationService::requestContextForConnection(
    const QString &connectionId) const
{
    if (connectionId.isEmpty()) {
        return std::nullopt;
    }
    if (isAuthenticated() && connectionId == activeConnectionId()) {
        return requestContext(true);
    }
    const auto connection = m_sessionPool->connection(connectionId);
    const auto credentials = m_sessionPool->credentials(connectionId);
    if (!connection.has_value() || !credentials.has_value()) {
        return std::nullopt;
    }
    // Device fields are installation-wide; only server and account differ.
    ProviderRequestContext context = requestContext(false);
    context.baseUrl = connection->baseUrl;
    context.accessToken = credentials->accessToken;
    context.profileId = connection->profileId;
    if (!context.profileId.isEmpty()) {
        context.profileToken = credentials->profileToken;
    }
    return context;
}

IProviderAdapter *AuthenticationService::providerForConnection(const QString &connectionId) const
{
    if (connectionId.isEmpty()) {
        return nullptr;
    }
    if (connectionId == activeConnectionId() && m_providerAdapter) {
        return m_providerAdapter;
    }
    const auto connection = m_configManager
        ? m_configManager->getConnection(connectionId) : std::nullopt;
    return connection.has_value() ? providerForKind(connection->providerKind) : nullptr;
}

std::optional<QNetworkRequest> AuthenticationService::createRequestForConnection(
    const QString &connectionId, const QString &endpoint) const
{
    const auto context = requestContextForConnection(connectionId);
    IProviderAdapter *adapter = providerForConnection(connectionId);
    if (!context.has_value() || !adapter || !adapter->requestFactory()) {
        return std::nullopt;
    }
    return adapter->requestFactory()->createRequest(*context, endpoint);
}

void AuthenticationService::activateConnection(const QString &connectionId)
{
    if (!m_configManager) {
        return;
    }
    const QString normalized = connectionId.trimmed();
    if (!m_configManager->getConnection(normalized).has_value()) {
        emit connectionActivationFailed(
            normalized, tr("This server connection no longer exists."));
        return;
    }
    if (isAuthenticated() && activeConnectionId() == normalized) {
        emit connectionActivated(normalized);
        return;
    }

    const QString previous = isAuthenticated() ? activeConnectionId() : QString();
    clearAccountStateInternal(false, false);
    m_activatingConnectionId = normalized;
    m_activationFallbackConnectionId = previous;
    m_configManager->setActiveConnection(normalized);
    startRestoration();
}

void AuthenticationService::suspendSession()
{
    clearAccountStateInternal(false, true);
}

void AuthenticationService::removeStoredCredentials(const ServerConnection &connection)
{
    if (!connection.isValid()) {
        return;
    }
    m_sessionPool->forget(connection.connectionId);
    if (m_secretStore) {
        removeAllCredentialsAsync(
            connection, m_configManager ? m_configManager->getDeviceId() : QString());
    }
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
                responseArray(reply->readAll(), QStringLiteral("items")));
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
    abandonLiveRenewal();

    if (!m_profileToken.isEmpty()) {
        m_profileToken.clear();
        removeCredentialAsync(m_activeConnection, CredentialKind::ProfileToken);
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
    abandonLiveRenewal();
    const quint64 pinGeneration = m_stateGeneration;
    if (profile == m_providerProfiles.cend()) {
        emit loginError(tr("Unknown profile."));
        return;
    }
    if (!m_profileToken.isEmpty()) {
        m_profileToken.clear();
        removeCredentialAsync(m_activeConnection, CredentialKind::ProfileToken);
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
            persistCredentialsAsync([this, pinGeneration]() {
                if (pinGeneration != m_stateGeneration) {
                    return;
                }
                finishAuthentication();
            });
            return;
        }
        emit loginError(tr("The provider does not support profile PIN verification."));
        return;
    }

    m_pendingProfileId = profileId;
    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
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
            persistCredentialsAsync([this, generation]() {
                if (generation != m_stateGeneration) {
                    return;
                }
                finishAuthentication();
            });
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
                responseArray(reply->readAll(), QStringLiteral("items")));
            if (!mapped.has_value()) {
                emit loginError(tr("The provider returned an invalid session list."));
                return;
            }
            replaceAuthSessions(*mapped);
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
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
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        *endpoint,
        [this, endpoint = *endpoint]() {
            return networkManager()->deleteResource(createRequest(endpoint));
        },
        [this, generation, revokingCurrent, sessionId](QNetworkReply *) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (revokingCurrent) {
                logout();
            } else {
                for (qsizetype i = m_authSessions.size() - 1; i >= 0; --i) {
                    if (m_authSessions.at(i).toMap().value(
                            QStringLiteral("id")).toString() == sessionId) {
                        m_authSessions.removeAt(i);
                    }
                }
                emit authSessionsChanged();
                loadAuthSessions();
            }
        },
        [this, generation](const NetworkError &error) {
            if (generation == m_stateGeneration) {
                emit loginError(tr("Unable to revoke authentication session: %1")
                                    .arg(error.userMessage));
            }
        },
        options);
}

namespace {

// Only an explicit refusal of the refresh token means the sign-in is gone;
// anything else (no response, server error, deadline) may succeed later.
TokenRenewal renewalOutcome(const NetworkError &error)
{
    const int status = error.httpStatus;
    return status == 400 || status == 401 || status == 403
        ? TokenRenewal::Rejected
        : TokenRenewal::Unavailable;
}

} // namespace

void AuthenticationService::renewConnection(const QString &connectionId,
                                            std::function<void(TokenRenewal)> completion)
{
    auto &waiting = m_pendingRenewals[connectionId];
    waiting.append(std::move(completion));
    if (waiting.size() > 1) {
        // Joins the attempt already in flight for this connection.
        return;
    }
    if (connectionId == activeConnectionId()) {
        refreshLiveSession();
    } else {
        renewPooledConnection(connectionId);
    }
}

void AuthenticationService::setAccessTokenLifetime(qint64 expiresInSeconds)
{
    m_accessTokenLifetimeSeconds = qMax<qint64>(0, expiresInSeconds);
    m_accessTokenExpiresAt = expiresInSeconds > 0
        ? QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds)
        : QDateTime();
}

void AuthenticationService::ensureAccessTokenValidFor(qint64 seconds,
                                                      std::function<void()> done)
{
    const bool expiring = !m_accessTokenExpiresAt.isValid()
        || QDateTime::currentDateTimeUtc().secsTo(m_accessTokenExpiresAt)
            < qMin(seconds, m_accessTokenLifetimeSeconds / 2);
    if (!expiring || m_refreshToken.isEmpty() || !isAuthenticated()) {
        done();
        return;
    }
    renewConnection(activeConnectionId(), [done = std::move(done)](TokenRenewal) { done(); });
}

void AuthenticationService::finishRenewal(const QString &connectionId, TokenRenewal outcome)
{
    if (connectionId == m_liveRenewalConnectionId) {
        m_liveRenewalConnectionId.clear();
    }
    const auto waiting = m_pendingRenewals.take(connectionId);
    for (const auto &callback : waiting) {
        if (callback) {
            callback(outcome);
        }
    }
}

void AuthenticationService::bumpCredentialEpoch(const QString &connectionId)
{
    ++m_credentialEpochs[connectionId];
    m_unsavedRenewals.remove(connectionId);
}

void AuthenticationService::abandonLiveRenewal()
{
    if (m_liveRenewalConnectionId.isEmpty()) {
        return;
    }
    const QString connectionId = std::exchange(m_liveRenewalConnectionId, QString());
    const auto waiting = m_pendingRenewals.take(connectionId);
    // Deliver after the session change that canceled the request finishes.
    QTimer::singleShot(0, this, [waiting]() {
        for (const auto &callback : waiting) {
            if (callback) {
                callback(TokenRenewal::Unavailable);
            }
        }
    });
}

void AuthenticationService::refreshLiveSession()
{
    const QString connectionId = activeConnectionId();
    if (!m_transport || !networkManager()) {
        finishRenewal(connectionId, TokenRenewal::Unavailable);
        return;
    }
    // Without a refresh token (Jellyfin, or a provider that issued none) a
    // rejected access token cannot be renewed.
    const auto request = m_providerAuthenticator && !m_refreshToken.isEmpty()
        ? m_providerAuthenticator->createRefreshRequest(m_refreshToken)
        : std::nullopt;
    if (!request.has_value() || !request->isValid()) {
        finishRenewal(connectionId, TokenRenewal::Rejected);
        return;
    }

    m_liveRenewalConnectionId = connectionId;
    const quint64 generation = m_stateGeneration;
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        request->endpoint,
        [this, endpoint = request->endpoint, body = request->body]() {
            return networkManager()->post(createUnauthenticatedRequest(endpoint), body);
        },
        [this, generation, connectionId](QNetworkReply *reply) {
            if (generation != m_stateGeneration) {
                finishRenewal(connectionId, TokenRenewal::Unavailable);
                return;
            }
            const ProviderAuthenticationResult authentication =
                m_providerAuthenticator->parseRefreshResponse(reply->readAll());
            if (!authentication.isValidRefresh()) {
                qCWarning(lcAuth) << "Token refresh returned an unreadable response";
                finishRenewal(connectionId, TokenRenewal::Unavailable);
                return;
            }
            m_accessToken = authentication.accessToken;
            setAccessTokenLifetime(authentication.expiresInSeconds);
            if (!authentication.refreshToken.isEmpty()) {
                m_refreshToken = authentication.refreshToken;
            }
            // Persist in the background: the refreshed tokens are already in
            // memory, so request recovery must not wait on keyring latency.
            persistCredentialsAsync();
            finishRenewal(connectionId, TokenRenewal::Renewed);
        },
        [this, generation, connectionId](const NetworkError &error) {
            finishRenewal(connectionId, generation == m_stateGeneration
                                            ? renewalOutcome(error)
                                            : TokenRenewal::Unavailable);
        },
        options);
}

void AuthenticationService::renewPooledConnection(const QString &connectionId)
{
    const auto connection = m_sessionPool->connection(connectionId);
    IProviderAdapter *adapter = providerForConnection(connectionId);
    const IProviderAuthenticator *authenticator = adapter ? adapter->authenticator() : nullptr;
    if (!connection.has_value() || !authenticator || !adapter->requestFactory()
        || !networkManager()) {
        finishRenewal(connectionId, TokenRenewal::Unavailable);
        return;
    }
    if (m_sessionPool->isRejected(connectionId)) {
        finishRenewal(connectionId, TokenRenewal::Rejected);
        return;
    }
    const auto credentials = m_sessionPool->credentials(connectionId);
    if (!credentials.has_value()) {
        // Still loading from storage.
        finishRenewal(connectionId, TokenRenewal::Unavailable);
        return;
    }
    const auto request = credentials->refreshToken.isEmpty()
        ? std::nullopt
        : authenticator->createRefreshRequest(credentials->refreshToken);
    if (!request.has_value() || !request->isValid()) {
        m_sessionPool->markRejected(connectionId);
        finishRenewal(connectionId, TokenRenewal::Rejected);
        return;
    }

    if (!m_renewalTransport) {
        m_renewalTransport = std::make_unique<HttpTransport>(networkManager());
        m_renewalTransport->setUrlRedactor([](const QUrl &url) {
            return url.toString(QUrl::RemoveQuery | QUrl::RemoveUserInfo);
        });
    }
    ProviderRequestContext context = requestContext(false);
    context.baseUrl = connection->baseUrl;
    const QNetworkRequest networkRequest =
        adapter->requestFactory()->createRequest(context, request->endpoint);
    const quint64 epoch = m_credentialEpochs.value(connectionId);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    options.attemptTimeoutMs = 15000;
    m_renewalTransport->sendWithRetry(
        this,
        request->endpoint,
        [this, networkRequest, body = request->body]() {
            return networkManager()->post(networkRequest, body);
        },
        [this, connectionId, epoch, credentials = *credentials,
         authenticator](QNetworkReply *reply) {
            const ProviderAuthenticationResult authentication =
                authenticator->parseRefreshResponse(reply->readAll());
            if (!authentication.isValidRefresh()) {
                qCWarning(lcAuth) << "Token refresh returned an unreadable response";
                finishRenewal(connectionId, TokenRenewal::Unavailable);
                return;
            }
            // Signed in again, signed out, or removed while renewing: these
            // tokens must not replace the new sign-in or revive a deleted one.
            const auto saved = m_configManager
                ? m_configManager->getConnection(connectionId) : std::nullopt;
            if (m_credentialEpochs.value(connectionId) != epoch || !saved.has_value()) {
                finishRenewal(connectionId, TokenRenewal::Unavailable);
                return;
            }
            ConnectionCredentials renewed = credentials;
            renewed.accessToken = authentication.accessToken;
            if (!authentication.refreshToken.isEmpty()) {
                renewed.refreshToken = authentication.refreshToken;
            }
            if (m_activeConnection.isValid() && m_activeConnection.connectionId == connectionId) {
                // It became the live (or restoring) session meanwhile; that
                // session adopts the rotated tokens. Persisting also updates
                // the pool.
                m_accessToken = renewed.accessToken;
                setAccessTokenLifetime(authentication.expiresInSeconds);
                m_refreshToken = renewed.refreshToken;
                persistCredentialsAsync();
                finishRenewal(connectionId, TokenRenewal::Renewed);
                return;
            }
            // Saved even when a space switch dropped the member from the
            // pool: the server may have retired the old refresh token.
            m_sessionPool->updateCredentials(connectionId, renewed);
            m_unsavedRenewals.insert(connectionId, renewed);
            const std::shared_ptr<ISecretStore> store = m_secretStore;
            if (store) {
                const QPointer<AuthenticationService> guard(this);
                enqueueCredentialTask([store, connection = *saved, renewed, guard]() {
                    CredentialStore credentialStore(store.get());
                    const bool stored = credentialStore.write(
                        connection, CredentialKind::AccessToken, renewed.accessToken)
                        && credentialStore.write(
                            connection, CredentialKind::RefreshToken, renewed.refreshToken);
                    if (!stored) {
                        qCWarning(lcAuth) << "Could not save renewed tokens:"
                                          << store->lastError();
                    }
                    QMetaObject::invokeMethod(guard, [guard, connection, renewed]() {
                        if (!guard) {
                            return;
                        }
                        const auto unsaved = guard->m_unsavedRenewals.constFind(
                            connection.connectionId);
                        if (unsaved != guard->m_unsavedRenewals.cend()
                            && unsaved->accessToken == renewed.accessToken) {
                            guard->m_unsavedRenewals.erase(unsaved);
                        }
                    }, Qt::QueuedConnection);
                });
            }
            finishRenewal(connectionId, TokenRenewal::Renewed);
        },
        [this, connectionId, epoch](const NetworkError &error) {
            if (m_credentialEpochs.value(connectionId) != epoch) {
                // Refers to a sign-in that has since been replaced.
                finishRenewal(connectionId, TokenRenewal::Unavailable);
                return;
            }
            const TokenRenewal outcome = renewalOutcome(error);
            if (outcome == TokenRenewal::Rejected) {
                qCWarning(lcAuth) << "Server rejected token renewal for a space member";
                m_sessionPool->markRejected(connectionId);
            }
            finishRenewal(connectionId, outcome);
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
    abandonLiveRenewal();
    const quint64 generation = m_stateGeneration;
    const QString normalizedServerUrl = normalizeUrl(serverUrl);
    const bool switchingAccount = m_activeConnection.isValid()
        && (m_activeConnection.baseUrl != normalizedServerUrl
            || m_activeConnection.accountId != userId);
    if (switchingAccount) {
        Bloom::ArtworkRef::clearTransientSourceUrls();
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
    setAccessTokenLifetime(-1);
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

    const QString unreachable =
        tr("Could not reach the server. Your sign-in is saved, so try again later.");
    validateAccessToken([this, generation, finishRestore, unreachable](SessionValidity validity) {
        if (generation != m_stateGeneration) {
            return;
        }
        if (validity == SessionValidity::Valid) {
            finishRestore();
            return;
        }
        if (validity == SessionValidity::Unreachable) {
            failRestoration(unreachable, true);
            return;
        }
        renewConnection(activeConnectionId(),
                        [this, generation, finishRestore, unreachable](TokenRenewal outcome) {
            if (generation != m_stateGeneration) {
                return;
            }
            if (outcome != TokenRenewal::Renewed) {
                qCWarning(lcAuth) << (outcome == TokenRenewal::Rejected
                                          ? "Stored session is invalid or expired"
                                          : "Could not reach the server to renew the session");
                failRestoration(outcome == TokenRenewal::Rejected ? QString() : unreachable,
                                outcome == TokenRenewal::Unavailable);
                return;
            }
            validateAccessToken([this, generation, finishRestore, unreachable](
                                    SessionValidity renewedValidity) {
                if (generation != m_stateGeneration) {
                    return;
                }
                if (renewedValidity == SessionValidity::Valid) {
                    finishRestore();
                } else {
                    failRestoration(
                        renewedValidity == SessionValidity::Unreachable ? unreachable : QString(),
                        renewedValidity == SessionValidity::Unreachable);
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
    setAccessTokenLifetime(-1);
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
    abandonLiveRenewal();
    m_pendingProfileId.clear();
    m_profileToken.clear();
    m_activeConnection.profileId.clear();
    replaceProfiles({});
    replaceAuthSessions({});
    if (persist && m_activeConnection.isValid()) {
        removeCredentialAsync(m_activeConnection, CredentialKind::ProfileToken);
        persistConnection();
    }
    // Only notify when the step actually changes: a redundant emit would make
    // LoginScreen clear a freshly set "Signing in…" status message.
    const QString nextStep = m_accessToken.isEmpty()
        ? QStringLiteral("credentials") : QStringLiteral("profiles");
    if (m_authenticationStep != nextStep) {
        m_authenticationStep = nextStep;
        emit authenticationStepChanged();
    }
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
    Bloom::ArtworkRef::clearTransientSourceUrls();
    // Leaving or replacing the signed-out state abandons a code sign-in.
    m_deviceSignIn->cancel();
    const bool wasAuthenticated = isAuthenticated();
    ++m_stateGeneration;
    abandonLiveRenewal();
    m_activatingConnectionId.clear();
    m_activationFallbackConnectionId.clear();

    const ServerConnection connection = m_activeConnection.isValid()
        ? m_activeConnection
        : (m_configManager
               ? m_configManager->getActiveConnection().value_or(ServerConnection{})
               : ServerConnection{});
    if (removeCredentials && connection.isValid()) {
        // Merged reads fall back to pooled tokens once another member is live;
        // a signed-out account must not stay reachable through them.
        m_sessionPool->forget(connection.connectionId);
    }
    if (removeCredentials && m_secretStore && connection.isValid()) {
        const QString deviceId = m_configManager ? m_configManager->getDeviceId() : QString();
        removeAllCredentialsAsync(connection, deviceId);
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
    setAccessTokenLifetime(-1);
    m_refreshToken.clear();
    m_profileToken.clear();
    m_userId.clear();
    m_username.clear();
    m_pendingProfileId.clear();
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    replaceProfiles({});
    replaceAuthSessions({});
    // Only notify when the step actually changes: authenticate() clears stale
    // account state right after LoginScreen sets "Signing in…", and a
    // redundant emit would wipe that status before the login even starts.
    if (m_authenticationStep != QStringLiteral("credentials")) {
        m_authenticationStep = QStringLiteral("credentials");
        emit authenticationStepChanged();
    }
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
    // A code sign-in that reached profiles, PIN, or the app is done.
    if (step != QStringLiteral("credentials")
        && m_deviceSignIn->state() == DeviceSignInSession::State::SigningIn) {
        m_deviceSignIn->reset();
    }
    emit authenticationStepChanged();
    if (wasAuthenticated != isAuthenticated()) {
        emit authenticatedChanged();
    }
    if (!m_activatingConnectionId.isEmpty()
        && (step == QStringLiteral("profiles") || step == QStringLiteral("pin"))) {
        // The saved provider profile is gone or needs a PIN: hand the
        // half-restored session to the login screen instead of failing.
        const QString connectionId = std::exchange(m_activatingConnectionId, QString());
        m_activationFallbackConnectionId.clear();
        emit signInRequired(connectionId);
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

void AuthenticationService::validateAccessToken(std::function<void(SessionValidity)> callback)
{
    if (!m_transport || !networkManager()) {
        callback(SessionValidity::Unreachable);
        return;
    }
    const QString endpoint = m_providerAuthenticator && !m_userId.isEmpty()
        ? m_providerAuthenticator->sessionValidationEndpoint(m_userId)
        : QString();
    // No access token (only a refresh token was saved) needs a renewal.
    if (m_accessToken.isEmpty() || endpoint.isEmpty()) {
        callback(SessionValidity::Invalid);
        return;
    }

    const auto sharedCallback =
        std::make_shared<std::function<void(SessionValidity)>>(std::move(callback));
    const auto complete = [sharedCallback](SessionValidity validity) {
        if (!*sharedCallback) {
            return;
        }
        auto resultCallback = std::move(*sharedCallback);
        resultCallback(validity);
    };
    HttpRequestOptions options;
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
                             && identity.accountId == m_userId
                         ? SessionValidity::Valid
                         : SessionValidity::Invalid);
        },
        [complete](const NetworkError &error) {
            complete(error.httpStatus == 401 || error.httpStatus == 403
                         ? SessionValidity::Invalid
                         : SessionValidity::Unreachable);
        },
        options);
}

// ---------------------------------------------------------------------------
// Sign-in discovery and device sign-in ("sign in with a code")
// ---------------------------------------------------------------------------

void AuthenticationService::initializeDeviceSignIn()
{
    m_deviceSignIn = new DeviceSignInSession(this);
    connect(m_deviceSignIn, &DeviceSignInSession::changed,
            this, &AuthenticationService::deviceSignInChanged);
    // Profile loading or credential storage can still fail after approval;
    // the code screen then shows that error instead of "Signing in".
    connect(this, &AuthenticationService::loginError, this, [this](const QString &error) {
        m_deviceSignIn->fail(error);
    });
}

QVariantMap AuthenticationService::signInOptions() const
{
    return {
        {QStringLiteral("server"), m_signInDiscovery.address},
        {QStringLiteral("state"), m_signInDiscovery.state},
        {QStringLiteral("silo"), m_signInDiscovery.silo},
        {QStringLiteral("passwordSignIn"), m_signInDiscovery.passwordSignIn},
        {QStringLiteral("deviceSignIn"), m_signInDiscovery.deviceSignIn},
    };
}

QVariantMap AuthenticationService::deviceSignIn() const
{
    return m_deviceSignIn->toVariantMap();
}

HttpRequestOptions AuthenticationService::discoveryRequestOptions(int maxAttempts) const
{
    // Public reads: safe to replay, short deadlines so the form never waits long.
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Idempotent;
    options.retryPolicy.maxAttempts = maxAttempts;
    options.retryPolicy.baseDelayMs = 500;
    options.attemptTimeoutMs = 10000;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    return options;
}

void AuthenticationService::checkSignInOptions(const QString &serverAddress)
{
    const QString address = serverAddress.trimmed();
    if (address == m_signInDiscovery.address
        && (m_signInDiscovery.state == QStringLiteral("checking")
            || m_signInDiscovery.state == QStringLiteral("ready"))) {
        return;
    }

    const quint64 generation = ++m_signInDiscoveryGeneration;
    m_signInDiscovery = {};
    m_signInDiscovery.address = address;
    if (address.isEmpty()) {
        emit signInOptionsChanged();
        return;
    }

    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    if (m_providerSelection == QStringLiteral("jellyfin") || !silo || !silo->requestFactory()
        || !networkManager()) {
        // Only native Silo offers anything beyond the password form.
        m_signInDiscovery.state = QStringLiteral("ready");
        emit signInOptionsChanged();
        return;
    }
    if (!m_discoveryTransport || m_discoveryTransport->networkManager() != networkManager()) {
        m_discoveryTransport = std::make_unique<HttpTransport>(networkManager());
        const IProviderRequestFactory *factory = silo->requestFactory();
        m_discoveryTransport->setUrlRedactor([factory](const QUrl &url) {
            return factory->redactedUrl(url);
        });
    }
    m_signInDiscovery.state = QStringLiteral("checking");
    emit signInOptionsChanged();

    const auto [baseUrl, fallbackUrl] = resolveServerAddress(address);
    discoverSilo(baseUrl, fallbackUrl, generation);
}

void AuthenticationService::discoverSilo(const QString &baseUrl,
                                         const QString &fallbackUrl,
                                         quint64 generation)
{
    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    const auto endpoint = silo->endpointFor(ProviderRoute::SystemInfo);
    if (!endpoint) {
        finishSignInDiscovery(QStringLiteral("ready"), generation);
        return;
    }
    ProviderRequestContext context = requestContext(false);
    context.baseUrl = baseUrl;
    const IProviderRequestFactory *factory = silo->requestFactory();
    m_discoveryTransport->sendWithRetry(
        this,
        *endpoint,
        [this, factory, context, endpoint = *endpoint]() {
            return networkManager()->get(factory->createRequest(context, endpoint));
        },
        [this, silo, baseUrl, generation](QNetworkReply *reply) {
            if (generation != m_signInDiscoveryGeneration) {
                return;
            }
            m_signInDiscovery.baseUrl = baseUrl;
            const auto detected = silo->mapDetectionResult(
                QJsonDocument::fromJson(reply->readAll()).object());
            if (!detected) {
                // Jellyfin or another server: password sign-in only.
                finishSignInDiscovery(QStringLiteral("ready"), generation);
                return;
            }
            m_signInDiscovery.silo = true;
            m_signInDiscovery.detection = *detected;
            discoverDeviceSignIn(generation);
        },
        [this, baseUrl, fallbackUrl, generation](const NetworkError &error) {
            if (generation != m_signInDiscoveryGeneration) {
                return;
            }
            if (error.httpStatus != 0) {
                // The server answered, just not as native Silo.
                m_signInDiscovery.baseUrl = baseUrl;
                finishSignInDiscovery(QStringLiteral("ready"), generation);
            } else if (!fallbackUrl.isEmpty()) {
                discoverSilo(fallbackUrl, QString(), generation);
            } else {
                finishSignInDiscovery(QStringLiteral("unreachable"), generation);
            }
        },
        discoveryRequestOptions(1));
}

void AuthenticationService::discoverDeviceSignIn(quint64 generation)
{
    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    const IProviderAuthenticator *authenticator = silo->authenticator();
    const auto endpoint = authenticator->deviceAuthorizationCapabilityEndpoint();
    if (!endpoint) {
        discoverSignInMethods(generation);
        return;
    }
    ProviderRequestContext context = requestContext(false);
    context.baseUrl = m_signInDiscovery.baseUrl;
    const IProviderRequestFactory *factory = silo->requestFactory();
    m_discoveryTransport->sendWithRetry(
        this,
        *endpoint,
        [this, factory, context, endpoint = *endpoint]() {
            return networkManager()->get(factory->createRequest(context, endpoint));
        },
        [this, authenticator, generation](QNetworkReply *reply) {
            if (generation != m_signInDiscoveryGeneration) {
                return;
            }
            const auto capability =
                authenticator->parseDeviceAuthorizationCapability(reply->readAll());
            m_signInDiscovery.deviceSignIn = capability && capability->available;
            m_signInDiscovery.deviceCanWithdraw = capability && capability->cancel;
            discoverSignInMethods(generation);
        },
        [this, generation](const NetworkError &) {
            if (generation == m_signInDiscoveryGeneration) {
                discoverSignInMethods(generation);
            }
        },
        discoveryRequestOptions(2));
}

void AuthenticationService::discoverSignInMethods(quint64 generation)
{
    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    const IProviderAuthenticator *authenticator = silo->authenticator();
    const auto endpoint = authenticator->signInMethodsEndpoint();
    if (!endpoint) {
        finishSignInDiscovery(QStringLiteral("ready"), generation);
        return;
    }
    ProviderRequestContext context = requestContext(false);
    context.baseUrl = m_signInDiscovery.baseUrl;
    const IProviderRequestFactory *factory = silo->requestFactory();
    m_discoveryTransport->sendWithRetry(
        this,
        *endpoint,
        [this, factory, context, endpoint = *endpoint]() {
            return networkManager()->get(factory->createRequest(context, endpoint));
        },
        [this, authenticator, generation](QNetworkReply *reply) {
            if (generation != m_signInDiscoveryGeneration) {
                return;
            }
            const auto methods = authenticator->parseSignInMethods(reply->readAll());
            // Without an answer, keep the password form.
            m_signInDiscovery.passwordSignIn =
                !methods || methods->passwordSignIn.value_or(true);
            finishSignInDiscovery(QStringLiteral("ready"), generation);
        },
        [this, generation](const NetworkError &) {
            finishSignInDiscovery(QStringLiteral("ready"), generation);
        },
        discoveryRequestOptions(2));
}

void AuthenticationService::finishSignInDiscovery(const QString &state, quint64 generation)
{
    if (generation != m_signInDiscoveryGeneration) {
        return;
    }
    // A server without password sign-in must still offer a way in.
    if (!m_signInDiscovery.deviceSignIn) {
        m_signInDiscovery.passwordSignIn = true;
    }
    m_signInDiscovery.state = state;
    emit signInOptionsChanged();
}

void AuthenticationService::startDeviceSignIn(const QString &serverAddress)
{
    const QString address = serverAddress.trimmed();
    IProviderAdapter *silo = providerForKind(ProviderKind::Silo);
    if (address != m_signInDiscovery.address
        || m_signInDiscovery.state != QStringLiteral("ready")
        || !m_signInDiscovery.deviceSignIn || !silo) {
        emit loginError(tr("This server doesn't offer sign-in with a code."));
        return;
    }

    clearAccountStateInternal(false, false);
    if (!activateProvider(silo)) {
        emit loginError(tr("The selected provider is not available."));
        return;
    }
    m_serverUrl = m_signInDiscovery.baseUrl;
    m_schemeFallbackUrl.clear();
    m_detectionResult = m_signInDiscovery.detection;
    emit serverUrlChanged();

    const quint64 generation = m_stateGeneration;
    DeviceSignInSession::Target target;
    target.networkManager = networkManager();
    target.requestFactory = m_requestFactory;
    target.authenticator = m_providerAuthenticator;
    target.context = requestContext(false);
    target.canWithdraw = m_signInDiscovery.deviceCanWithdraw;
    m_deviceSignIn->start(target, [this, generation](
                                      const ProviderAuthenticationResult &authentication) {
        if (generation != m_stateGeneration) {
            m_deviceSignIn->reset();
            return;
        }
        // From here the sign-in is identical to a password login.
        handleAuthenticationResult(authentication, generation);
    });
}

void AuthenticationService::cancelDeviceSignIn()
{
    m_deviceSignIn->cancel();
}
