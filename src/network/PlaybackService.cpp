#include "PlaybackService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "MediaSegmentProviderService.h"
#include "providers/IPlaybackProvider.h"
#include "../utils/ConfigManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QLoggingCategory>
#include <cmath>
#include <optional>
#include "../utils/BloomLogging.h"

namespace {

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
    , m_provider(authService ? authService->playbackProvider() : nullptr)
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
    const QString &playbackSessionId)
{
    if (!m_authService || !m_provider) {
        NetworkError error;
        error.code = -1;
        error.endpoint = QStringLiteral("createPlaybackDescriptor");
        error.userMessage = tr("Playback provider is unavailable.");
        emitError(error);
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
    const PlaybackProviderContext context{
        QUrl(m_authService->getServerUrl()),
        m_authService->getAccessToken()
    };
    const Bloom::PlaybackDescriptor descriptor = m_provider->createDescriptor(
        context,
        media,
        providerSource,
        selectedAudioTrack,
        selectedSubtitleTrack,
        startPositionMs,
        playbackSessionId);
    if (!descriptor.isValid()) {
        NetworkError error;
        error.code = -2;
        error.endpoint = QStringLiteral("createPlaybackDescriptor");
        error.userMessage = tr("The playback provider returned an invalid stream request.");
        emitError(error);
    }
    return descriptor;
}

// ============================================================================
// Request Helpers
// ============================================================================

void PlaybackService::sendRequestWithRetry(const QString &endpoint,
                                            RequestFactory requestFactory,
                                            ResponseHandler responseHandler,
                                            FailureHandler failureHandler,
                                            int attemptNumber)
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
    options.unauthorizedPolicy = UnauthorizedPolicy::DeferSessionExpiry;
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
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getPlaybackInfo";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        if (!requestContext.isEmpty()) {
            emit playbackInfoFailedForRequest(itemId, error.userMessage, requestContext);
        }
        return;
    }
    
    QString endpoint = QString("/Items/%1/PlaybackInfo?UserId=%2")
        .arg(itemId, m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            return m_authService->networkManager()->post(request, QByteArray("{}"));
        },
        [this, itemId, requestContext](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            PlaybackInfoResponse info = PlaybackInfoResponse::fromJson(doc.object());
            emit playbackInfoLoaded(itemId, info);
            if (!requestContext.isEmpty()) {
                emit playbackInfoLoadedForRequest(itemId, info, requestContext);
            }
        },
        [this, itemId, requestContext](const NetworkError &error) {
            if (!requestContext.isEmpty()) {
                emit playbackInfoFailedForRequest(itemId, error.userMessage, requestContext);
            }
        });
}

void PlaybackService::getAdditionalParts(const QString &itemId)
{
    getAdditionalParts(itemId, QString());
}

void PlaybackService::getAdditionalParts(const QString &itemId, const QString &requestContext)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getAdditionalParts";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        if (!requestContext.isEmpty()) {
            emit additionalPartsFailedForRequest(itemId, error.userMessage, requestContext);
        }
        return;
    }

    QString endpoint = QString("/Videos/%1/AdditionalParts?UserId=%2")
        .arg(itemId, m_authService->getUserId());

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, requestContext](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray parts;
            if (doc.isObject()) {
                parts = doc.object().value(QStringLiteral("Items")).toArray();
            }
            emit additionalPartsLoaded(itemId, parts);
            if (!requestContext.isEmpty()) {
                emit additionalPartsLoadedForRequest(itemId, parts, requestContext);
            }
        },
        [this, itemId, requestContext](const NetworkError &error) {
            if (!requestContext.isEmpty()) {
                emit additionalPartsFailedForRequest(itemId, error.userMessage, requestContext);
            }
        });
}

void PlaybackService::getMediaSegments(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) {
        qCDebug(lcPlayback) << "getMediaSegments: Not authenticated, skipping";
        emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
        return;
    }
    
    qCDebug(lcPlayback) << "Getting media segments for item:" << itemId;
    
    // GET /Episode/{id}/IntroSkipperSegments
    // This endpoint is provided by the "Intro Skipper" plugin on the Jellyfin server
    // If not available (404), we silently return empty segments
    QString endpoint = QString("/Episode/%1/IntroSkipperSegments").arg(itemId);
    
    QNetworkRequest request = m_authService->createRequest(endpoint);
    QNetworkReply *reply = m_authService->networkManager()->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        // Check for session expiry (defer during playback)
        if (httpStatus == 401) {
            qCWarning(lcPlayback) << "Session expired while fetching media segments for" << itemId;
            m_authService->checkForSessionExpiry(reply, true);
            finishMediaSegments(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        // 404 is expected if Intro Skipper plugin is not installed
        // Just emit empty segments silently
        if (httpStatus == 404) {
            qCDebug(lcPlayback) << "Intro Skipper plugin not available for" << itemId;
            maybeLoadExternalMediaSegments(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcPlayback) << "Failed to get media segments for" << itemId
                                       << "Error:" << reply->errorString();
            maybeLoadExternalMediaSegments(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        
        maybeLoadExternalMediaSegments(itemId, parseIntroSkipperSegments(itemId, obj));
    });
}

QList<MediaSegmentInfo> PlaybackService::parseIntroSkipperSegments(const QString &itemId, const QJsonObject &obj) const
{
    QList<MediaSegmentInfo> segments;

    static const QMap<QString, QPair<MediaSegmentType, QString>> typeMapping = {
        {"Introduction", {MediaSegmentType::Intro, "Intro"}},
        {"Credits", {MediaSegmentType::Outro, "Outro"}},
        {"Recap", {MediaSegmentType::Recap, "Recap"}},
        {"Preview", {MediaSegmentType::Preview, "Preview"}},
        {"Commercial", {MediaSegmentType::Commercial, "Commercial"}}
    };

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString typeName = it.key();
        const QJsonObject segmentObj = it.value().toObject();

        if (!segmentObj["Valid"].toBool(false)) {
            continue;
        }

        MediaSegmentInfo info;
        info.itemId = segmentObj["EpisodeId"].toString(itemId);
        info.source = QStringLiteral("jellyfin");

        const double startSeconds = segmentObj["Start"].toDouble();
        const double endSeconds = segmentObj["End"].toDouble();
        if (startSeconds < 0.0 || endSeconds <= startSeconds) {
            continue;
        }
        info.startMs = qRound64(startSeconds * 1000.0);
        info.endMs = qRound64(endSeconds * 1000.0);

        if (typeMapping.contains(typeName)) {
            const auto mapping = typeMapping[typeName];
            info.type = mapping.first;
            info.typeString = mapping.second;
        } else {
            info.type = MediaSegmentType::Unknown;
            info.typeString = typeName;
        }

        segments.append(info);
    }

    return segments;
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
    const QString fields = QStringLiteral("ProviderIds,ParentIndexNumber,IndexNumber,SeriesId,RunTimeTicks,Type");
    const QString endpoint = QString("/Users/%1/Items/%2?Fields=%3")
        .arg(m_authService->getUserId(), itemId, fields);

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, serverSegments](QNetworkReply *reply) {
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

            const QString seriesEndpoint = QString("/Users/%1/Items/%2?Fields=ProviderIds")
                .arg(m_authService->getUserId(), context.seriesId);
            sendRequestWithRetry(seriesEndpoint,
                [this, seriesEndpoint]() {
                    QNetworkRequest request = m_authService->createRequest(seriesEndpoint);
                    return m_authService->networkManager()->get(request);
                },
                [this, context, serverSegments, itemId](QNetworkReply *seriesReply) mutable {
                    const QJsonDocument seriesDoc = QJsonDocument::fromJson(seriesReply->readAll());
                    const QJsonObject seriesProviderIds = seriesDoc.object().value(QStringLiteral("ProviderIds")).toObject();
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
                });
        },
        [this, itemId, serverSegments](const NetworkError &error) {
            Q_UNUSED(error);
            if (serverSegments.isEmpty()) {
                finishMediaSegments(itemId, serverSegments);
            }
        });
}

void PlaybackService::finishExternalMediaSegments(const QString &itemId,
                                                  const QList<MediaSegmentInfo> &serverSegments,
                                                  const QList<MediaSegmentInfo> &mergedSegments)
{
    if (mergedSegments.size() > serverSegments.size() || serverSegments.isEmpty()) {
        finishMediaSegments(itemId, mergedSegments);
    }
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
    if (!m_authService->isAuthenticated()) {
        qCDebug(lcPlayback) << "getTrickplayInfo: Not authenticated, skipping";
        emit trickplayInfoLoaded(itemId, QMap<int, TrickplayTileInfo>());
        return;
    }
    
    qCDebug(lcPlayback) << "Getting trickplay info for item:" << itemId;
    
    // GET /Items/{itemId}?Fields=Trickplay
    // The dedicated /Videos/{itemId}/Trickplay endpoint may not exist on all Jellyfin versions,
    // but the Trickplay field is available in the Item response
    QString endpoint = QString("/Items/%1?Fields=Trickplay").arg(itemId);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            
            const QVariantMap item = m_authService->mapMediaItem(obj, QString());
            const double durationSeconds =
                static_cast<double>(item.value(QStringLiteral("durationMs")).toLongLong()) / 1000.0;
            
            // Debug: Log the raw Trickplay JSON response
            QJsonObject trickplayRaw = obj["Trickplay"].toObject();
            qCDebug(lcPlayback) << "Trickplay raw JSON for" << itemId << ":"
                                      << QJsonDocument(trickplayRaw).toJson(QJsonDocument::Compact)
                                      << "Duration:" << durationSeconds << "s";
            
            // Trickplay is nested: { "Trickplay": { "<itemId>": { "320": { ... } } } }
            QJsonObject trickplayObj = obj["Trickplay"].toObject();
            QMap<int, TrickplayTileInfo> trickplayInfo;
            
            // The trickplay data is keyed by the item's media source ID (often same as item ID)
            // We need to look at all keys to find the trickplay data
            for (auto mediaSourceIt = trickplayObj.begin(); mediaSourceIt != trickplayObj.end(); ++mediaSourceIt) {
                QJsonObject resolutionsObj = mediaSourceIt.value().toObject();
                
                for (auto it = resolutionsObj.begin(); it != resolutionsObj.end(); ++it) {
                    bool ok;
                    int width = it.key().toInt(&ok);
                    if (ok && it.value().isObject()) {
                        TrickplayTileInfo info = TrickplayTileInfo::fromJson(it.value().toObject());
                        
                        // Calculate correct thumbnail count from duration if the API value seems wrong
                        // The API's ThumbnailCount can be stale or incorrect in some Jellyfin versions
                        if (info.interval > 0 && durationSeconds > 0) {
                            int calculatedCount = static_cast<int>(std::ceil(durationSeconds * 1000.0 / info.interval));
                            if (calculatedCount > info.thumbnailCount) {
                                qCDebug(lcPlayback) << "Overriding ThumbnailCount from" << info.thumbnailCount
                                                          << "to calculated" << calculatedCount
                                                          << "(duration:" << durationSeconds << "s, interval:" << info.interval << "ms)";
                                info.thumbnailCount = calculatedCount;
                            }
                        }
                        
                        trickplayInfo.insert(width, info);
                    }
                }
                
                // Usually there's only one media source with trickplay data
                if (!trickplayInfo.isEmpty()) {
                    break;
                }
            }
            
            if (trickplayInfo.isEmpty()) {
                qCDebug(lcPlayback) << "No trickplay info available for" << itemId;
            } else {
                qCDebug(lcPlayback) << "Trickplay info loaded for" << itemId 
                                          << "- Resolutions:" << trickplayInfo.keys();
            }
            
            emit trickplayInfoLoaded(itemId, trickplayInfo);
        });
}

QString PlaybackService::getTrickplayTileUrl(const QString &itemId, int width, int tileIndex)
{
    return QString("%1/Videos/%2/Trickplay/%3/%4.jpg?api_key=%5")
        .arg(m_authService->getServerUrl())
        .arg(itemId)
        .arg(width)
        .arg(tileIndex)
        .arg(m_authService->getAccessToken());
}

// ============================================================================
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

void PlaybackService::sendPlaybackReport(const PlaybackReport &report)
{
    if (!m_authService || !m_authService->isAuthenticated() || !m_provider) {
        return;
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
        m_provider->createReportRequest(providerReport);
    if (!providerRequest.isValid()) {
        qCWarning(lcPlayback) << "Playback provider returned an invalid report request"
                              << "itemId=" << report.media.itemId;
        return;
    }

    qCDebug(lcPlayback) << "Reporting playback event"
                        << "itemId=" << report.media.itemId
                        << "positionMs=" << report.positionMs
                        << "endpoint=" << providerRequest.endpoint;

    QNetworkRequest request = m_authService->createRequest(providerRequest.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_authService->networkManager()->post(
        request, QJsonDocument(providerRequest.body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, itemId = report.media.itemId,
             deferSessionExpiry = providerRequest.deferSessionExpiry]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, deferSessionExpiry)) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcPlayback) << "Failed to report playback event for" << itemId
                                  << ":" << reply->errorString();
        }
    });
}

void PlaybackService::markItemPlayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(lcPlayback) << "Marking item as played:" << itemId;

    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, false)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcPlayback) << "Failed to mark item as played:" << itemId 
                                       << ":" << reply->errorString();
        } else {
            qCDebug(lcPlayback) << "Successfully marked item as played:" << itemId;
            emit itemMarkedPlayed(itemId);
        }
    });
}
