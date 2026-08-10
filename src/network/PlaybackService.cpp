#include "PlaybackService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "MediaSegmentProviderService.h"
#include "providers/ICatalogProvider.h"
#include "../utils/ConfigManager.h"
#include "../utils/BloomLogging.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QSysInfo>
#include <QStringList>

namespace {

QString activeConnectionId(AuthenticationService *authService, ConfigManager *configManager)
{
    ConfigManager *config = configManager
        ? configManager
        : (authService ? authService->configManager() : nullptr);
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    return connection.has_value() ? connection->connectionId : QString();
}
QString playbackRequestIdentity(AuthenticationService *authService,
                                ConfigManager *configManager,
                                const PlaybackProviderContext &context,
                                const IPlaybackProvider *provider)
{
    const QStringList fields{
        context.serverUrl.toString(QUrl::FullyEncoded),
        context.accessToken,
        context.profileId,
        context.profileToken,
        context.deviceId,
        activeConnectionId(authService, configManager),
        authService ? authService->getUserId() : QString(),
        QString::number(static_cast<int>(
            authService ? authService->activeProviderKind() : ProviderKind::Jellyfin)),
        QString::number(reinterpret_cast<quintptr>(provider), 16)
    };
    return fields.join(QChar(0x1f));
}


PlaybackReport makePlaybackReport(PlaybackReportEvent event,
                                  const QString &itemId,
                                  qint64 positionMs,
                                  const QString &mediaSourceId,
                                  int audioStreamIndex,
                                  int subtitleStreamIndex,
                                  const QString &playSessionId,
                                  bool canSeek,
                                  bool isPaused,
                                  bool isMuted,
                                  const QString &playMethod,
                                  const QString &repeatMode,
                                  const QString &playbackOrder)
{
    PlaybackReport report;
    report.event = event;
    report.media.itemId = itemId;
    report.positionMs = positionMs;
    report.mediaVersionId = mediaSourceId;
    report.audioTrackId = audioStreamIndex >= 0
        ? QString::number(audioStreamIndex) : QString();
    report.subtitleTrackId = subtitleStreamIndex >= 0
        ? QString::number(subtitleStreamIndex) : QString();
    report.playbackSessionId = playSessionId;
    report.canSeek = canSeek;
    report.isPaused = isPaused;
    report.isMuted = isMuted;
    report.playbackMethod = playMethod;
    report.repeatMode = repeatMode;
    report.playbackOrder = playbackOrder;
    return report;
}

QUrl resolvePlaybackUrl(const QUrl &serverUrl, const QUrl &url)
{
    if (!url.isRelative() || !serverUrl.isValid()) {
        return url;
    }
    QUrl origin = serverUrl;
    origin.setPath(QStringLiteral("/"));
    return origin.resolved(url);
}

} // namespace

PlaybackService::PlaybackService(AuthenticationService *authService,
                                 ConfigManager *configManager,
                                 MediaSegmentProviderService *mediaSegmentProviderService,
                                 QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_transport(authService ? authService->transport() : nullptr)
    , m_configManager(configManager)
    , m_mediaSegmentProviderService(mediaSegmentProviderService)
    , m_retryPolicy{3, 1000, true}
{
}

PlaybackService::~PlaybackService() = default;

Bloom::PlaybackDescriptor PlaybackService::createPlaybackDescriptor(
    const QString &itemId,
    const QVariantMap &providerSource,
    int selectedAudioTrack,
    int selectedSubtitleTrack,
    qint64 startPositionMs,
    const QString &playbackSessionId,
    bool emitFailure)
{
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider) {
        if (emitFailure) {
            NetworkError error;
            error.code = -1;
            error.endpoint = QStringLiteral("createPlaybackDescriptor");
            error.userMessage = tr("Playback provider is unavailable.");
            emitError(error);
        }
        return {};
    }

    Bloom::MediaRef media;
    media.itemId = itemId;
    ConfigManager *config = m_configManager
        ? m_configManager
        : (m_authService ? m_authService->configManager() : nullptr);
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    if (connection.has_value()) {
        media.connectionId = connection->connectionId;
    }
    const PlaybackProviderContext context = providerContext();
    const Bloom::PlaybackDescriptor descriptor = provider->createDescriptor(
        context,
        media,
        providerSource,
        selectedAudioTrack,
        selectedSubtitleTrack,
        startPositionMs,
        playbackSessionId);
    if (!descriptor.isValid() && emitFailure) {
        NetworkError error;
        error.code = -2;
        error.endpoint = QStringLiteral("createPlaybackDescriptor");
        error.userMessage = tr("The playback provider returned an invalid stream request.");
        emitError(error);
    }
    return descriptor;
}

PlaybackProviderContext PlaybackService::providerContext() const
{
    PlaybackProviderContext context;
    if (!m_authService) {
        return context;
    }
    context.serverUrl = QUrl(m_authService->getServerUrl());
    context.accessToken = m_authService->getAccessToken();
    context.clientName = QStringLiteral("Bloom");
    context.clientVersion = QCoreApplication::applicationVersion();
    ConfigManager *config = m_configManager
        ? m_configManager : m_authService->configManager();
    if (config) {
        context.deviceId = config->getDeviceId();
        const auto connection = config->getActiveConnection();
        if (connection.has_value()) {
            context.profileId = connection->profileId;
        }
    }
    context.deviceName = QSysInfo::machineHostName();
    context.devicePlatform = QSysInfo::prettyProductName();
    return context;
}

quint64 PlaybackService::beginRequest(const QString &operation,
                                      const QString &itemId,
                                      const QString &requestContext)
{
    const QString key = operation + QChar(0x1f) + requestContext + QChar(0x1f) + itemId;
    const quint64 generation = ++m_nextRequestGeneration;
    m_requestGenerations.insert(key, generation);
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    m_requestIdentities.insert(
        key,
        playbackRequestIdentity(m_authService, m_configManager,
                                 providerContext(), provider));
    return generation;
}

void PlaybackService::endRequest(const QString &operation,
                                 const QString &itemId,
                                 const QString &requestContext,
                                 quint64 generation)
{
    const QString key = operation + QChar(0x1f) + requestContext + QChar(0x1f) + itemId;
    if (m_requestGenerations.value(key, 0) == generation) {
        m_requestGenerations.remove(key);
        m_requestIdentities.remove(key);
    }
}

bool PlaybackService::isCurrentRequest(const QString &operation,
                                       const QString &itemId,
                                       const QString &requestContext,
                                       quint64 generation) const
{
    const QString key = operation + QChar(0x1f) + requestContext + QChar(0x1f) + itemId;
    if (m_requestGenerations.value(key, 0) != generation) {
        return false;
    }
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    return m_requestIdentities.value(key)
        == playbackRequestIdentity(m_authService, m_configManager,
                                    providerContext(), provider);
}

QNetworkReply *PlaybackService::sendProviderRequest(const QString &endpoint,
                                                    const QString &method,
                                                    const QJsonObject &body) const
{
    if (!m_authService) {
        return nullptr;
    }
    QNetworkRequest request = m_authService->createRequest(endpoint);
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QString normalizedMethod = method.trimmed().toUpper();
    if (normalizedMethod == QStringLiteral("GET")) {
        return m_authService->networkManager()->get(request);
    }
    if (normalizedMethod == QStringLiteral("POST")) {
        return m_authService->networkManager()->post(request, payload);
    }
    if (normalizedMethod == QStringLiteral("PUT")) {
        return m_authService->networkManager()->put(request, payload);
    }
    if (normalizedMethod == QStringLiteral("DELETE")) {
        if (body.isEmpty()) {
            return m_authService->networkManager()->deleteResource(request);
        }
        return m_authService->networkManager()->sendCustomRequest(
            request, QByteArrayLiteral("DELETE"), payload);
    }
    return m_authService->networkManager()->sendCustomRequest(
        request, normalizedMethod.toLatin1(), payload);
}

void PlaybackService::emitDescriptorFailure(const QString &itemId,
                                             const QString &requestContext,
                                             const QString &error)
{
    emit playbackDescriptorFailedForRequest(itemId, error, requestContext);
}

void PlaybackService::requestPlaybackDescriptor(const QString &itemId,
                                                const QVariantMap &providerSource,
                                                int selectedAudioTrack,
                                                int selectedSubtitleTrack,
                                                qint64 startPositionMs,
                                                const QString &playbackSessionId,
                                                const QString &requestContext)
{
    const quint64 generation =
        beginRequest(QStringLiteral("descriptor"), itemId, requestContext);
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider || !m_authService->isAuthenticated()) {
        endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
        emitDescriptorFailure(itemId, requestContext, tr("Not authenticated"));
        return;
    }

    Bloom::MediaRef media;
    media.itemId = itemId;
    ConfigManager *config = m_configManager ? m_configManager : m_authService->configManager();
    if (const auto connection = config ? config->getActiveConnection() : std::nullopt;
        connection.has_value()) {
        media.connectionId = connection->connectionId;
    }
    const PlaybackProviderContext context = providerContext();
    const PlaybackStartRequest startRequest = provider->createPlaybackStartRequest(
        context, media, providerSource, selectedAudioTrack, selectedSubtitleTrack,
        startPositionMs);
    if (!startRequest.isValid()) {
        QTimer::singleShot(0, this, [this, provider, itemId, providerSource,
                                     selectedAudioTrack, selectedSubtitleTrack,
                                     startPositionMs, playbackSessionId,
                                     requestContext, generation, context, media]() {
            if (!isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
                return;
            }
            const Bloom::PlaybackDescriptor descriptor = provider->createDescriptor(
                context, media, providerSource, selectedAudioTrack,
                selectedSubtitleTrack, startPositionMs, playbackSessionId);
            endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
            if (!descriptor.isValid()) {
                emitDescriptorFailure(itemId, requestContext,
                                      tr("The playback provider returned an invalid stream request."));
                return;
            }
            emit playbackDescriptorLoadedForRequest(itemId, descriptor, requestContext);
        });
        return;
    }

    sendRequestWithRetry(
        startRequest.endpoint,
        [this, startRequest, itemId, requestContext, generation]() -> QNetworkReply * {
            if (!isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                  requestContext, generation)) {
                return nullptr;
            }
            return sendProviderRequest(startRequest.endpoint, startRequest.method,
                                       startRequest.body);
        },
        [this, provider, itemId, providerSource, requestContext, generation, context, media]
        (QNetworkReply *reply) {
            if (!isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
                return;
            }
            const PlaybackStartParseResult parsed = provider->parsePlaybackStartResponse(
                context, media,
                QJsonDocument::fromJson(reply->readAll()).object(), providerSource);
            if (!parsed.valid || !parsed.descriptor.isValid()) {
                if (isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                     requestContext, generation)) {
                    endRequest(QStringLiteral("descriptor"), itemId,
                               requestContext, generation);
                    emitDescriptorFailure(
                        itemId, requestContext,
                        parsed.error.isEmpty()
                            ? tr("The playback provider returned an invalid stream request.")
                            : parsed.error);
                } else {
                    endRequest(QStringLiteral("descriptor"), itemId,
                               requestContext, generation);
                }
                return;
            }
            Bloom::PlaybackDescriptor descriptor = parsed.descriptor;
            descriptor.stream.url = resolvePlaybackUrl(context.serverUrl,
                                                        descriptor.stream.url);
            if (isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("descriptor"), itemId,
                           requestContext, generation);
                emit playbackDescriptorLoadedForRequest(itemId, descriptor, requestContext);
            } else {
                endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
            }
        },
        [this, itemId, requestContext, generation](const NetworkError &error) {
            if (isCurrentRequest(QStringLiteral("descriptor"), itemId,
                                requestContext, generation)) {
                endRequest(QStringLiteral("descriptor"), itemId,
                           requestContext, generation);
                emitDescriptorFailure(itemId, requestContext, error.userMessage);
            } else {
                endRequest(QStringLiteral("descriptor"), itemId, requestContext, generation);
            }
        },
        0, true, RetrySafety::Never);
}

bool PlaybackService::switchPlaybackAudio(const QString &playbackSessionId,
                                           int audioTrackIndex,
                                           qint64 positionMs,
                                           const QString &requestContext)
{
    const quint64 generation =
        beginRequest(QStringLiteral("audio"), playbackSessionId, requestContext);
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider || !m_authService->isAuthenticated() || !m_transport) {
        endRequest(QStringLiteral("audio"), playbackSessionId, requestContext, generation);
        if (!requestContext.isEmpty()) {
            emit playbackAudioSwitchFailedForRequest(playbackSessionId,
                                                      tr("Not authenticated"),
                                                      requestContext);
        }
        return false;
    }
    const PlaybackProviderContext context = providerContext();
    const PlaybackAudioSwitchRequest switchRequest =
        provider->createAudioSwitchRequest(context, playbackSessionId,
                                           audioTrackIndex, positionMs);
    if (!switchRequest.isValid()) {
        endRequest(QStringLiteral("audio"), playbackSessionId, requestContext, generation);
        if (!requestContext.isEmpty()) {
            emit playbackAudioSwitchFailedForRequest(playbackSessionId,
                                                      tr("Audio switching is unavailable."),
                                                      requestContext);
        }
        return false;
    }
    sendRequestWithRetry(
        switchRequest.endpoint,
        [this, switchRequest, playbackSessionId, requestContext, generation]() -> QNetworkReply * {
            if (!isCurrentRequest(QStringLiteral("audio"), playbackSessionId,
                                  requestContext, generation)) {
                return nullptr;
            }
            return sendProviderRequest(switchRequest.endpoint, switchRequest.method,
                                       switchRequest.body);
        },
        [this, provider, playbackSessionId, requestContext, generation, context]
        (QNetworkReply *reply) {
            if (!isCurrentRequest(QStringLiteral("audio"), playbackSessionId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("audio"), playbackSessionId, requestContext, generation);
                return;
            }
            const PlaybackAudioSwitchParseResult parsed =
                provider->parseAudioSwitchResponse(
                    context, QJsonDocument::fromJson(reply->readAll()).object());
            if (!parsed.valid || !parsed.reloadUrl.isValid()) {
                if (isCurrentRequest(QStringLiteral("audio"), playbackSessionId,
                                     requestContext, generation)) {
                    endRequest(QStringLiteral("audio"), playbackSessionId,
                               requestContext, generation);
                    if (!requestContext.isEmpty()) {
                        emit playbackAudioSwitchFailedForRequest(
                            playbackSessionId,
                            parsed.error.isEmpty() ? tr("Audio switching failed.") : parsed.error,
                            requestContext);
                    }
                } else {
                    endRequest(QStringLiteral("audio"), playbackSessionId,
                               requestContext, generation);
                }
                return;
            }
            const QUrl reloadUrl = resolvePlaybackUrl(context.serverUrl, parsed.reloadUrl);
            if (isCurrentRequest(QStringLiteral("audio"), playbackSessionId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("audio"), playbackSessionId,
                           requestContext, generation);
                emit playbackAudioSwitchedForRequest(playbackSessionId,
                                                     reloadUrl, requestContext);
            } else {
                endRequest(QStringLiteral("audio"), playbackSessionId,
                           requestContext, generation);
            }
        },
        [this, playbackSessionId, requestContext, generation](const NetworkError &error) {
            if (isCurrentRequest(QStringLiteral("audio"), playbackSessionId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("audio"), playbackSessionId,
                           requestContext, generation);
                if (!requestContext.isEmpty()) {
                    emit playbackAudioSwitchFailedForRequest(playbackSessionId,
                                                             error.userMessage,
                                                             requestContext);
                }
            } else {
                endRequest(QStringLiteral("audio"), playbackSessionId,
                           requestContext, generation);
            }
        });
    return true;
}

void PlaybackService::requestPlaybackRecovery(const QString &itemId,
                                              const QVariantMap &providerSource,
                                              int selectedAudioTrack,
                                              qint64 startPositionMs,
                                              const QString &requestContext)
{
    const quint64 generation =
        beginRequest(QStringLiteral("recovery"), itemId, requestContext);
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider || !m_authService->isAuthenticated()) {
        endRequest(QStringLiteral("recovery"), itemId, requestContext, generation);
        if (!requestContext.isEmpty()) {
            emit playbackRecoveryFailedForRequest(itemId, tr("Not authenticated"),
                                                   requestContext);
        }
        return;
    }
    Bloom::MediaRef media;
    media.itemId = itemId;
    ConfigManager *config = m_configManager ? m_configManager : m_authService->configManager();
    if (const auto connection = config ? config->getActiveConnection() : std::nullopt;
        connection.has_value()) {
        media.connectionId = connection->connectionId;
    }
    const PlaybackProviderContext context = providerContext();
    const PlaybackRecoveryRequest recoveryRequest =
        provider->createPlaybackRecoveryRequest(
            context, media, providerSource, selectedAudioTrack, startPositionMs);
    if (!recoveryRequest.isValid()) {
        endRequest(QStringLiteral("recovery"), itemId, requestContext, generation);
        if (!requestContext.isEmpty()) {
            emit playbackRecoveryFailedForRequest(itemId,
                                                   tr("Playback recovery is unavailable."),
                                                   requestContext);
        }
        return;
    }
    sendRequestWithRetry(
        recoveryRequest.endpoint,
        [this, recoveryRequest, itemId, requestContext, generation]() -> QNetworkReply * {
            if (!isCurrentRequest(QStringLiteral("recovery"), itemId,
                                  requestContext, generation)) {
                return nullptr;
            }
            return sendProviderRequest(recoveryRequest.endpoint,
                                       recoveryRequest.method,
                                       recoveryRequest.body);
        },
        [this, provider, itemId, providerSource, requestContext, generation, context, media]
        (QNetworkReply *reply) {
            if (!isCurrentRequest(QStringLiteral("recovery"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("recovery"), itemId, requestContext, generation);
                return;
            }
            const PlaybackStartParseResult parsed =
                provider->parsePlaybackRecoveryResponse(
                    context, media,
                    QJsonDocument::fromJson(reply->readAll()).object(),
                    providerSource);
            if (!parsed.valid || !parsed.descriptor.isValid()) {
                if (isCurrentRequest(QStringLiteral("recovery"), itemId,
                                     requestContext, generation)) {
                    endRequest(QStringLiteral("recovery"), itemId,
                               requestContext, generation);
                    if (!requestContext.isEmpty()) {
                        emit playbackRecoveryFailedForRequest(
                            itemId,
                            parsed.error.isEmpty() ? tr("Playback recovery failed.")
                                                   : parsed.error,
                            requestContext);
                    }
                } else {
                    endRequest(QStringLiteral("recovery"), itemId,
                               requestContext, generation);
                }
                return;
            }
            Bloom::PlaybackDescriptor descriptor = parsed.descriptor;
            descriptor.stream.url = resolvePlaybackUrl(context.serverUrl,
                                                        descriptor.stream.url);
            if (isCurrentRequest(QStringLiteral("recovery"), itemId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("recovery"), itemId,
                           requestContext, generation);
                emit playbackRecoveryLoadedForRequest(itemId, descriptor,
                                                      requestContext);
            } else {
                endRequest(QStringLiteral("recovery"), itemId, requestContext, generation);
            }
        },
        [this, itemId, requestContext, generation](const NetworkError &error) {
            if (isCurrentRequest(QStringLiteral("recovery"), itemId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("recovery"), itemId,
                           requestContext, generation);
                if (!requestContext.isEmpty()) {
                    emit playbackRecoveryFailedForRequest(itemId, error.userMessage,
                                                          requestContext);
                }
            } else {
                endRequest(QStringLiteral("recovery"), itemId, requestContext, generation);
            }
        });
}

// ============================================================================
// Request Helpers
// ============================================================================

void PlaybackService::sendRequestWithRetry(const QString &endpoint,
                                            RequestFactory requestFactory,
                                            ResponseHandler responseHandler,
                                            FailureHandler failureHandler,
                                            int attemptNumber,
                                            bool deferSessionExpiry,
                                            RetrySafety retrySafety)
{
    Q_UNUSED(attemptNumber)
    if (!m_transport) {
        NetworkError error;
        error.code = -1;
        error.endpoint = endpoint;
        error.userMessage = tr("Network transport is unavailable.");
        emitError(error);
        if (failureHandler) {
            failureHandler(error);
        }
        return;
    }

    HttpRequestOptions options;
    options.retryPolicy = m_retryPolicy;
    options.retrySafety = retrySafety;
    options.unauthorizedPolicy = deferSessionExpiry
        ? UnauthorizedPolicy::DeferSessionExpiry
        : UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        std::move(requestFactory),
        std::move(responseHandler),
        [this, failureHandler = std::move(failureHandler)](const NetworkError &error) {
            if (error.code == 401 && failureHandler) {
                failureHandler(error);
                return;
            }
            emitError(error);
            if (failureHandler) {
                failureHandler(error);
            }
        },
        options);
}

void PlaybackService::emitError(const NetworkError &error)
{
    qCWarning(lcPlayback) << "Emitting error for endpoint:" << error.endpoint
                               << "User message:" << error.userMessage;
    emit errorOccurred(error.endpoint, error.userMessage);
    emit networkError(error);
}

// ============================================================================
// Playback Info
// ============================================================================

void PlaybackService::getPlaybackInfo(const QString &itemId)
{
    getPlaybackInfo(itemId, QString());
}

void PlaybackService::getPlaybackInfo(const QString &itemId, const QString &requestContext)
{
    const quint64 generation =
        beginRequest(QStringLiteral("info"), itemId, requestContext);
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider || !m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = QStringLiteral("getPlaybackInfo");
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        endRequest(QStringLiteral("info"), itemId, requestContext, generation);
        emitError(error);
        if (!requestContext.isEmpty()) {
            emit playbackInfoFailedForRequest(itemId, error.userMessage, requestContext);
        }
        return;
    }

    Bloom::MediaRef media;
    media.itemId = itemId;
    ConfigManager *config = m_configManager ? m_configManager : m_authService->configManager();
    if (const auto connection = config ? config->getActiveConnection() : std::nullopt;
        connection.has_value()) {
        media.connectionId = connection->connectionId;
    }
    const PlaybackProviderContext context = providerContext();
    const PlaybackInfoRequest providerRequest =
        provider->createPlaybackInfoRequest(context, media, {});
    if (!providerRequest.isValid()) {
        const QString endpoint = QStringLiteral("/Items/%1/PlaybackInfo?UserId=%2")
            .arg(itemId, m_authService->getUserId());
        sendRequestWithRetry(
            endpoint,
            [this, endpoint, itemId, requestContext, generation]() -> QNetworkReply * {
                if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                      requestContext, generation)) {
                    return nullptr;
                }
                QNetworkRequest request = m_authService->createRequest(endpoint);
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
                return m_authService->networkManager()->post(request, QByteArray("{}"));
            },
            [this, itemId, requestContext, generation](QNetworkReply *reply) {
                if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                      requestContext, generation)) {
                    endRequest(QStringLiteral("info"), itemId, requestContext, generation);
                    return;
                }
                const PlaybackInfoResponse info =
                    m_authService->mapPlaybackInfo(
                        QJsonDocument::fromJson(reply->readAll()).object());
                if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                      requestContext, generation)) {
                    endRequest(QStringLiteral("info"), itemId, requestContext, generation);
                    return;
                }
                endRequest(QStringLiteral("info"), itemId, requestContext, generation);
                emit playbackInfoLoaded(itemId, info);
                if (!requestContext.isEmpty()) {
                    emit playbackInfoLoadedForRequest(itemId, info, requestContext);
                }
            },
            [this, itemId, requestContext, generation](const NetworkError &error) {
                if (isCurrentRequest(QStringLiteral("info"), itemId,
                                     requestContext, generation)) {
                    endRequest(QStringLiteral("info"), itemId,
                               requestContext, generation);
                    if (!requestContext.isEmpty()) {
                        emit playbackInfoFailedForRequest(itemId, error.userMessage,
                                                          requestContext);
                    }
                }
            },
            0,
            true,
            RetrySafety::Idempotent);
        return;
    }

    sendRequestWithRetry(
        providerRequest.endpoint,
        [this, providerRequest, itemId, requestContext, generation]() -> QNetworkReply * {
            if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                  requestContext, generation)) {
                return nullptr;
            }
            return sendProviderRequest(providerRequest.endpoint, providerRequest.method,
                                       providerRequest.body);
        },
        [this, provider, itemId, requestContext, generation, context, media]
        (QNetworkReply *reply) {
            if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("info"), itemId, requestContext, generation);
                return;
            }
            const PlaybackInfoParseResult parsed = provider->parsePlaybackInfoResponse(
                context, media,
                QJsonDocument::fromJson(reply->readAll()).object());
            if (!parsed.valid) {
                if (isCurrentRequest(QStringLiteral("info"), itemId,
                                     requestContext, generation)) {
                    endRequest(QStringLiteral("info"), itemId,
                               requestContext, generation);
                    if (!requestContext.isEmpty()) {
                        emit playbackInfoFailedForRequest(
                            itemId,
                            parsed.error.isEmpty() ? tr("Playback information is unavailable.")
                                                   : parsed.error,
                            requestContext);
                    }
                }
                return;
            }
            if (!isCurrentRequest(QStringLiteral("info"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("info"), itemId, requestContext, generation);
                return;
            }
            endRequest(QStringLiteral("info"), itemId, requestContext, generation);
            emit playbackInfoLoaded(itemId, parsed.response);
            if (!requestContext.isEmpty()) {
                emit playbackInfoLoadedForRequest(itemId, parsed.response, requestContext);
            }
        },
        [this, itemId, requestContext, generation](const NetworkError &error) {
            if (isCurrentRequest(QStringLiteral("info"), itemId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("info"), itemId,
                           requestContext, generation);
                if (!requestContext.isEmpty()) {
                    emit playbackInfoFailedForRequest(itemId, error.userMessage,
                                                      requestContext);
                }
            }
        },
        0,
        true,
        RetrySafety::Idempotent);
}

void PlaybackService::getAdditionalParts(const QString &itemId)
{
    getAdditionalParts(itemId, QString());
}

void PlaybackService::getAdditionalParts(const QString &itemId,
                                         const QString &requestContext)
{
    const quint64 generation =
        beginRequest(QStringLiteral("parts"), itemId, requestContext);
    if (!m_authService || !m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = QStringLiteral("getAdditionalParts");
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        endRequest(QStringLiteral("parts"), itemId, requestContext, generation);
        emitError(error);
        if (!requestContext.isEmpty()) {
            emit additionalPartsFailedForRequest(itemId, error.userMessage, requestContext);
        }
        return;
    }
    if (m_authService->activeProviderKind() == ProviderKind::Silo) {
        endRequest(QStringLiteral("parts"), itemId, requestContext, generation);
        emit additionalPartsLoaded(itemId, {});
        if (!requestContext.isEmpty()) {
            emit additionalPartsLoadedForRequest(itemId, {}, requestContext);
        }
        return;
    }

    const QString connectionId = activeConnectionId(m_authService, m_configManager);
    const QString endpoint = QStringLiteral("/Videos/%1/AdditionalParts?UserId=%2")
        .arg(itemId, m_authService->getUserId());

    sendRequestWithRetry(endpoint,
        [this, endpoint, itemId, requestContext, generation]() -> QNetworkReply * {
            if (!isCurrentRequest(QStringLiteral("parts"), itemId,
                                  requestContext, generation)) {
                return nullptr;
            }
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, requestContext, connectionId, generation](QNetworkReply *reply) {
            if (!isCurrentRequest(QStringLiteral("parts"), itemId,
                                  requestContext, generation)) {
                endRequest(QStringLiteral("parts"), itemId, requestContext, generation);
                return;
            }
            const ParsedItemsResult response =
                m_authService->parseItemsResponse(reply->readAll(), QString());
            const QVariantList parts = response.success
                ? m_authService->mapMediaItems(response.items, connectionId)
                : QVariantList{};
            endRequest(QStringLiteral("parts"), itemId, requestContext, generation);
            emit additionalPartsLoaded(itemId, parts);
            if (!requestContext.isEmpty()) {
                emit additionalPartsLoadedForRequest(itemId, parts, requestContext);
            }
        },
        [this, itemId, requestContext, generation](const NetworkError &error) {
            if (isCurrentRequest(QStringLiteral("parts"), itemId,
                                 requestContext, generation)) {
                endRequest(QStringLiteral("parts"), itemId,
                           requestContext, generation);
                if (!requestContext.isEmpty()) {
                    emit additionalPartsFailedForRequest(itemId, error.userMessage,
                                                         requestContext);
                }
            }
        },
        0,
        true,
        RetrySafety::Idempotent);
}

void PlaybackService::getMediaSegments(const QString &itemId)
{
    getMediaSegments(itemId, QString());
}

void PlaybackService::getMediaSegments(const QString &itemId, const QString &fileId)
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        qCDebug(lcPlayback) << "getMediaSegments: Not authenticated, skipping";
        emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
        return;
    }

    const bool nativeProvider = m_authService->activeProviderKind() == ProviderKind::Silo;
    QString endpoint;
    if (nativeProvider) {
        ProviderRouteContext routeContext;
        routeContext.itemId = itemId;
        routeContext.fileId = fileId;
        const auto nativeEndpoint = m_authService->endpointFor(
            ProviderRoute::MediaSegments, routeContext);
        if (!m_authService->supportsCapability(ProviderCapability::MediaSegments)
            || !nativeEndpoint.has_value()) {
            // Native providers must not fall through to Jellyfin routes when a
            // capability is absent. External providers may still fill markers.
            maybeLoadExternalMediaSegments(itemId, {});
            return;
        }
        endpoint = *nativeEndpoint;
    } else {
        // GET /Episode/{id}/IntroSkipperSegments
        // This endpoint is provided by the "Intro Skipper" plugin on Jellyfin.
        endpoint = QString("/Episode/%1/IntroSkipperSegments").arg(itemId);
    }

    qCDebug(lcPlayback) << "Getting media segments for item:" << itemId;
    QNetworkRequest request = m_authService->createRequest(endpoint);
    QNetworkReply *reply = m_authService->networkManager()->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId, nativeProvider]() {
        reply->deleteLater();
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (httpStatus == 401) {
            qCWarning(lcPlayback) << "Session expired while fetching media segments for" << itemId;
            m_authService->checkForSessionExpiry(reply, true);
            finishMediaSegments(itemId, {});
            return;
        }

        if (httpStatus == 404) {
            if (!nativeProvider) {
                qCDebug(lcPlayback) << "Intro Skipper plugin not available for" << itemId;
            }
            maybeLoadExternalMediaSegments(itemId, {});
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcPlayback) << "Failed to get media segments for" << itemId
                                   << "Error:" << reply->errorString();
            maybeLoadExternalMediaSegments(itemId, {});
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject object = document.object();
        const QList<MediaSegmentInfo> segments = nativeProvider
            ? m_authService->mapMediaSegments(itemId, object)
            : m_authService->mapIntroSkipperSegments(itemId, object);
        maybeLoadExternalMediaSegments(itemId, segments);
    });
}

void PlaybackService::maybeLoadExternalMediaSegments(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments)
{
    if (!m_mediaSegmentProviderService || !m_configManager
        || !m_configManager->getExternalSegmentProvidersEnabled()
        || !MediaSegmentProviderService::hasMissingSupportedSegmentTypes(serverSegments)) {
        finishMediaSegments(itemId, serverSegments);
        return;
    }

    if (!serverSegments.isEmpty()) {
        finishMediaSegments(itemId, serverSegments);
    }

    loadMediaSegmentLookupContext(itemId, serverSegments);
}

void PlaybackService::loadMediaSegmentLookupContext(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments)
{
    const bool nativeProvider = m_authService->activeProviderKind() == ProviderKind::Silo;
    QString endpoint;
    if (nativeProvider) {
        ProviderRouteContext routeContext;
        routeContext.itemId = itemId;
        const auto nativeEndpoint = m_authService->endpointFor(
            ProviderRoute::CatalogItem, routeContext);
        if (!nativeEndpoint.has_value()) {
            finishMediaSegments(itemId, serverSegments);
            return;
        }
        endpoint = *nativeEndpoint;
    } else {
        const QString fields =
            QStringLiteral("ProviderIds,ParentIndexNumber,IndexNumber,SeriesId,RunTimeTicks,Type");
        endpoint = QString("/Users/%1/Items/%2?Fields=%3")
            .arg(m_authService->getUserId(), itemId, fields);
    }

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, serverSegments, nativeProvider](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QVariantMap item = m_authService->mapMediaItem(doc.object(), QString());
            MediaSegmentLookupContext context;
            context.itemId = itemId;
            context.type = item.value(QStringLiteral("mediaType")).toString();
            context.seriesId = item.value(QStringLiteral("seriesId")).toString();
            context.seasonNumber = item.value(QStringLiteral("parentIndexNumber"), -1).toInt();
            context.episodeNumber = item.value(QStringLiteral("indexNumber"), -1).toInt();
            context.durationMs = item.value(QStringLiteral("durationMs")).toLongLong();

            const QVariantMap providerIds = item.value(QStringLiteral("providerIds")).toMap();
            context.imdbId = providerIds.value(QStringLiteral("Imdb")).toString();
            context.tmdbId = providerIds.value(QStringLiteral("Tmdb")).toString();
            context.tvdbId = providerIds.value(QStringLiteral("Tvdb")).toString();

            const bool needsSeriesProviderIds = context.type.compare(QStringLiteral("Episode"), Qt::CaseInsensitive) == 0
                && !context.seriesId.isEmpty()
                && (context.imdbId.isEmpty() || context.tmdbId.isEmpty());
            if (!needsSeriesProviderIds) {
                QPointer<PlaybackService> self(this);
                m_mediaSegmentProviderService->fetchExternalSegments(context, serverSegments,
                    [self, itemId, serverSegments](const QList<MediaSegmentInfo> &segments) {
                        if (!self) return;
                        self->finishExternalMediaSegments(itemId, serverSegments, segments);
                    });
                return;
            }

            QString seriesEndpoint;
            if (nativeProvider) {
                ProviderRouteContext seriesContext;
                seriesContext.itemId = context.seriesId;
                const auto nativeSeriesEndpoint = m_authService->endpointFor(
                    ProviderRoute::CatalogItem, seriesContext);
                if (!nativeSeriesEndpoint.has_value()) {
                    QPointer<PlaybackService> self(this);
                    m_mediaSegmentProviderService->fetchExternalSegments(context, serverSegments,
                        [self, itemId, serverSegments](const QList<MediaSegmentInfo> &segments) {
                            if (!self) return;
                            self->finishExternalMediaSegments(itemId, serverSegments, segments);
                        });
                    return;
                }
                seriesEndpoint = *nativeSeriesEndpoint;
            } else {
                seriesEndpoint = QString("/Users/%1/Items/%2?Fields=ProviderIds")
                    .arg(m_authService->getUserId(), context.seriesId);
            }
            sendRequestWithRetry(seriesEndpoint,
                [this, seriesEndpoint]() {
                    QNetworkRequest request = m_authService->createRequest(seriesEndpoint);
                    return m_authService->networkManager()->get(request);
                },
                [this, context, serverSegments, itemId](QNetworkReply *seriesReply) mutable {
                    const QJsonDocument seriesDoc = QJsonDocument::fromJson(seriesReply->readAll());
                    const QVariantMap seriesProviderIds = m_authService
                        ->mapMediaItem(seriesDoc.object(), QString())
                        .value(QStringLiteral("providerIds")).toMap();
                    if (context.imdbId.isEmpty()) context.imdbId = seriesProviderIds.value(QStringLiteral("Imdb")).toString();
                    if (context.tmdbId.isEmpty()) context.tmdbId = seriesProviderIds.value(QStringLiteral("Tmdb")).toString();
                    if (context.tvdbId.isEmpty()) context.tvdbId = seriesProviderIds.value(QStringLiteral("Tvdb")).toString();

                    QPointer<PlaybackService> self(this);
                    m_mediaSegmentProviderService->fetchExternalSegments(context, serverSegments,
                        [self, itemId, serverSegments](const QList<MediaSegmentInfo> &segments) {
                            if (!self) return;
                            self->finishExternalMediaSegments(itemId, serverSegments, segments);
                        });
                },
                [this, context, serverSegments, itemId](const NetworkError &error) mutable {
                    Q_UNUSED(error);
                    qCDebug(lcPlayback) << "Failed to fetch series provider IDs for" << context.seriesId
                                             << "- external segment lookup may be incomplete";
                    QPointer<PlaybackService> self(this);
                    m_mediaSegmentProviderService->fetchExternalSegments(context, serverSegments,
                        [self, itemId, serverSegments](const QList<MediaSegmentInfo> &segments) {
                            if (!self) return;
                            self->finishExternalMediaSegments(itemId, serverSegments, segments);
                        });
                },
                0,
                true,
                RetrySafety::Idempotent);
        },
        [this, itemId, serverSegments](const NetworkError &error) {
            Q_UNUSED(error);
            finishMediaSegments(itemId, serverSegments);
        },
        0,
        true,
        RetrySafety::Idempotent);
}

void PlaybackService::finishExternalMediaSegments(const QString &itemId,
                                                  const QList<MediaSegmentInfo> &serverSegments,
                                                  const QList<MediaSegmentInfo> &mergedSegments)
{
    // Preserve every server-owned type while filling only types absent from
    // the server response. The provider service normally already performs
    // this merge; repeating it here keeps this boundary deterministic.
    finishMediaSegments(itemId,
                        MediaSegmentProviderService::mergeSegmentsByType(
                            serverSegments, mergedSegments));
}



void PlaybackService::finishMediaSegments(const QString &itemId, const QList<MediaSegmentInfo> &segments)
{
    qCDebug(lcPlayback) << "Media segments loaded for" << itemId
                             << "- Count:" << segments.size();

    for (const auto &segment : segments) {
        qCDebug(lcPlayback) << "  Segment:" << segment.typeString
                                 << "Source:" << segment.source
                                 << "Start:" << segment.startSeconds() << "s"
                                 << "End:" << segment.endSeconds() << "s";
    }

    emit mediaSegmentsLoaded(itemId, segments);
}

void PlaybackService::getTrickplayInfo(const QString &itemId)
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        qCDebug(lcPlayback) << "getTrickplayInfo: Not authenticated, skipping";
        emit trickplayInfoLoaded(itemId, QMap<int, TrickplayTileInfo>());
        return;
    }

    if (m_authService->activeProviderKind() == ProviderKind::Silo) {
        // Trickplay is not part of the advertised native contract. Do not
        // probe Jellyfin metadata routes for native sessions.
        emit trickplayInfoLoaded(itemId, QMap<int, TrickplayTileInfo>());
        return;
    }

    qCDebug(lcPlayback) << "Getting trickplay info for item:" << itemId;
    const QString endpoint = QString("/Items/%1?Fields=Trickplay").arg(itemId);
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const TrickplayTileInfoMap trickplayInfo =
                m_authService->mapTrickplayInfo(doc.object());
            if (trickplayInfo.isEmpty()) {
                qCDebug(lcPlayback) << "No trickplay info available for" << itemId;
            } else {
                qCDebug(lcPlayback) << "Trickplay info loaded for" << itemId
                                    << "- Resolutions:" << trickplayInfo.keys();
            }
            emit trickplayInfoLoaded(itemId, trickplayInfo);
        },
        FailureHandler(),
        0,
        true,
        RetrySafety::Idempotent);
}

QString PlaybackService::getTrickplayTileUrl(const QString &itemId, int width, int tileIndex)
{
    const IPlaybackProvider *provider =
        m_authService ? m_authService->playbackProvider() : nullptr;
    if (!m_authService || !provider) {
        return {};
    }
    return provider->createTrickplayTileUrl(
        providerContext(),
        itemId,
        width,
        tileIndex).toString();
}
// Playback Reporting
// ============================================================================

void PlaybackService::reportPlaybackStart(const QString &itemId, const QString &mediaSourceId,
                                          int audioStreamIndex, int subtitleStreamIndex,
                                          const QString &playSessionId,
                                          bool canSeek, bool isPaused, bool isMuted,
                                          const QString &playMethod,
                                          const QString &repeatMode,
                                          const QString &playbackOrder)
{
    sendPlaybackReport(makePlaybackReport(PlaybackReportEvent::Start, itemId, -1,
                                          mediaSourceId, audioStreamIndex,
                                          subtitleStreamIndex, playSessionId,
                                          canSeek, isPaused, isMuted, playMethod,
                                          repeatMode, playbackOrder));
}
void PlaybackService::reportPlaybackProgress(const QString &itemId, qint64 positionMs,
                                              const QString &mediaSourceId,
                                              int audioStreamIndex, int subtitleStreamIndex,
                                              const QString &playSessionId,
                                              bool canSeek, bool isPaused, bool isMuted,
                                              const QString &playMethod,
                                              const QString &repeatMode,
                                              const QString &playbackOrder)
{
    sendPlaybackReport(makePlaybackReport(PlaybackReportEvent::Progress, itemId, positionMs,
                                          mediaSourceId, audioStreamIndex,
                                          subtitleStreamIndex, playSessionId,
                                          canSeek, isPaused, isMuted, playMethod,
                                          repeatMode, playbackOrder));
}

void PlaybackService::reportPlaybackPaused(const QString &itemId, qint64 positionMs,
                                            const QString &mediaSourceId,
                                            int audioStreamIndex, int subtitleStreamIndex,
                                            const QString &playSessionId,
                                            bool canSeek, bool isMuted,
                                            const QString &playMethod,
                                            const QString &repeatMode,
                                            const QString &playbackOrder)
{
    sendPlaybackReport(makePlaybackReport(PlaybackReportEvent::Pause, itemId, positionMs,
                                          mediaSourceId, audioStreamIndex,
                                          subtitleStreamIndex, playSessionId,
                                          canSeek, true, isMuted, playMethod,
                                          repeatMode, playbackOrder));
}

void PlaybackService::reportPlaybackResumed(const QString &itemId, qint64 positionMs,
                                             const QString &mediaSourceId,
                                             int audioStreamIndex, int subtitleStreamIndex,
                                             const QString &playSessionId,
                                             bool canSeek, bool isMuted,
                                             const QString &playMethod,
                                             const QString &repeatMode,
                                             const QString &playbackOrder)
{
    sendPlaybackReport(makePlaybackReport(PlaybackReportEvent::Resume, itemId, positionMs,
                                          mediaSourceId, audioStreamIndex,
                                          subtitleStreamIndex, playSessionId,
                                          canSeek, false, isMuted, playMethod,
                                          repeatMode, playbackOrder));
}

void PlaybackService::reportPlaybackStopped(const QString &itemId, qint64 positionMs,
                                             const QString &mediaSourceId,
                                             int audioStreamIndex, int subtitleStreamIndex,
                                             const QString &playSessionId,
                                             bool canSeek, bool isPaused, bool isMuted,
                                             const QString &playMethod,
                                             const QString &repeatMode,
                                             const QString &playbackOrder)
{
    sendPlaybackReport(makePlaybackReport(PlaybackReportEvent::Stop, itemId, positionMs,
                                          mediaSourceId, audioStreamIndex,
                                          subtitleStreamIndex, playSessionId,
                                          canSeek, isPaused, isMuted, playMethod,
                                          repeatMode, playbackOrder));
}

void PlaybackService::sendPlaybackReport(const PlaybackReport &report,
                                         std::function<void()> completion,
                                         const IPlaybackProvider *providerOverride)
{
    const IPlaybackProvider *provider = providerOverride
        ? providerOverride
        : (m_authService ? m_authService->playbackProvider() : nullptr);
    if (!m_authService || !m_authService->isAuthenticated() || !provider) {
        if (completion) {
            completion();
        }
        return;
    }

    const QString sessionId = report.playbackSessionId;
    if (report.event == PlaybackReportEvent::Stop
        && !sessionId.isEmpty()
        && m_pendingProgressReports.value(sessionId, 0) > 0) {
        m_pendingStopReports.insert(sessionId, report);
        m_pendingStopProviders.insert(sessionId, provider);
        m_pendingStopRequestIdentities.insert(
            sessionId,
            playbackRequestIdentity(m_authService, m_configManager, providerContext(), provider));
        return;
    }

    if (report.event == PlaybackReportEvent::Progress && !sessionId.isEmpty()) {
        m_pendingProgressReports[sessionId] =
            m_pendingProgressReports.value(sessionId, 0) + 1;
        const auto originalCompletion = std::move(completion);
        completion = [this, sessionId, originalCompletion]() mutable {
            if (originalCompletion) {
                originalCompletion();
            }
            const int remaining = m_pendingProgressReports.value(sessionId, 0) - 1;
            if (remaining > 0) {
                m_pendingProgressReports.insert(sessionId, remaining);
                return;
            }
            m_pendingProgressReports.remove(sessionId);
            const auto stop = m_pendingStopReports.take(sessionId);
            const IPlaybackProvider *stopProvider =
                m_pendingStopProviders.take(sessionId);
            const QString stopIdentity =
                m_pendingStopRequestIdentities.take(sessionId);
            // A progress completion may outlive a server/account switch. Never
            // send the old session's stop report through the new credentials.
            if (!stop.playbackSessionId.isEmpty()
                && stopProvider
                && stopProvider == (m_authService ? m_authService->playbackProvider() : nullptr)
                && stopIdentity
                    == playbackRequestIdentity(m_authService, m_configManager,
                                                providerContext(), stopProvider)) {
                sendPlaybackReport(stop, {}, stopProvider);
            }
        };
    }

    PlaybackReport providerReport = report;
    ConfigManager *config = m_configManager
        ? m_configManager
        : m_authService->configManager();
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    if (connection.has_value()) {
        providerReport.media.connectionId = connection->connectionId;
    }

    const PlaybackReportRequest providerRequest =
        provider->createReportRequest(providerReport);
    if (!providerRequest.isValid()) {
        qCWarning(lcPlayback) << "Playback provider returned an invalid report request"
                              << "itemId=" << report.media.itemId;
        if (completion) {
            completion();
        }
        return;
    }

    qCDebug(lcPlayback) << "Reporting playback event"
                        << "itemId=" << report.media.itemId
                        << "positionMs=" << report.positionMs
                        << "endpoint=" << providerRequest.endpoint;

    sendRequestWithRetry(
        providerRequest.endpoint,
        [this, providerRequest]() {
            return sendProviderRequest(providerRequest.endpoint,
                                       providerRequest.method,
                                       providerRequest.body);
        },
        [this, itemId = report.media.itemId, completion](QNetworkReply *reply) mutable {
            if (reply->error() != QNetworkReply::NoError) {
                qCWarning(lcPlayback) << "Failed to report playback event for" << itemId
                                      << ":" << reply->errorString();
            }
            if (completion) {
                completion();
            }
        },
        [completion](const NetworkError &) mutable {
            if (completion) {
                completion();
            }
        },
        0,
        providerRequest.deferSessionExpiry);
}

void PlaybackService::markItemPlayed(const QString &itemId)
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        return;
    }

    if (m_authService->activeProviderKind() == ProviderKind::Silo) {
        const ICatalogProvider *catalogProvider = m_authService->catalogProvider();
        ProviderCatalogQuery query;
        query.userId = m_authService->getUserId();
        query.itemId = itemId;
        query.stateValue = true;
        const ProviderCatalogRequest providerRequest =
            catalogProvider
                ? catalogProvider->createRequest(ProviderCatalogOperation::SetWatched, query)
                : ProviderCatalogRequest{};
        if (!providerRequest.supported || providerRequest.relativeEndpoint.isEmpty()) {
            qCWarning(lcPlayback) << "Silo watched-state request unavailable for" << itemId;
            return;
        }
        QString method;
        switch (providerRequest.method) {
        case ProviderHttpMethod::Get: method = QStringLiteral("GET"); break;
        case ProviderHttpMethod::Post: method = QStringLiteral("POST"); break;
        case ProviderHttpMethod::Put: method = QStringLiteral("PUT"); break;
        case ProviderHttpMethod::Delete: method = QStringLiteral("DELETE"); break;
        }
        const QString endpoint = providerRequest.relativeEndpoint;
        sendRequestWithRetry(
            endpoint,
            [this, endpoint, method, body = providerRequest.body]() {
                return sendProviderRequest(endpoint, method,
                                           QJsonDocument::fromJson(body).object());
            },
            [this, itemId](QNetworkReply *reply) {
                if (reply->error() != QNetworkReply::NoError) {
                    qCWarning(lcPlayback) << "Failed to mark Silo item as played:" << itemId
                                          << ":" << reply->errorString();
                } else {
                    emit itemMarkedPlayed(itemId);
                }
            },
            FailureHandler(),
            0,
            false);
        return;
    }

    const QString endpoint = QString("/Users/%1/PlayedItems/%2")
        .arg(m_authService->getUserId(), itemId);
    sendRequestWithRetry(
        endpoint,
        [this, endpoint]() {
            return sendProviderRequest(endpoint, QStringLiteral("POST"), {});
        },
        [this, itemId](QNetworkReply *reply) {
            if (reply->error() != QNetworkReply::NoError) {
                qCWarning(lcPlayback) << "Failed to mark item as played:" << itemId
                                      << ":" << reply->errorString();
            } else {
                qCDebug(lcPlayback) << "Successfully marked item as played:" << itemId;
                emit itemMarkedPlayed(itemId);
            }
        },
        FailureHandler(),
        0,
        false);
}
