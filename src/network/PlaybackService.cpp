#include "PlaybackService.h"
#include "AuthenticationService.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QLoggingCategory>
#include <cmath>

Q_LOGGING_CATEGORY(playbackService, "bloom.playback")

namespace {
QJsonObject buildPlaybackPayload(const QString &itemId, qint64 positionTicks,
                                 const QString &mediaSourceId,
                                 int audioStreamIndex, int subtitleStreamIndex,
                                 const QString &playSessionId,
                                 bool canSeek, bool isPaused, bool isMuted,
                                 const QString &playMethod,
                                 const QString &repeatMode,
                                 const QString &playbackOrder)
{
    QJsonObject json;
    json["ItemId"] = itemId;
    if (positionTicks >= 0) {
        json["PositionTicks"] = positionTicks;
    }
    json["CanSeek"] = canSeek;
    json["IsPaused"] = isPaused;
    json["IsMuted"] = isMuted;
    json["PlayMethod"] = playMethod.isEmpty() ? QStringLiteral("DirectPlay") : playMethod;
    json["RepeatMode"] = repeatMode.isEmpty() ? QStringLiteral("RepeatNone") : repeatMode;
    json["PlaybackOrder"] = playbackOrder.isEmpty() ? QStringLiteral("Default") : playbackOrder;

    if (!mediaSourceId.isEmpty()) json["MediaSourceId"] = mediaSourceId;
    if (audioStreamIndex >= 0) json["AudioStreamIndex"] = audioStreamIndex;
    if (subtitleStreamIndex >= 0) json["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (!playSessionId.isEmpty()) json["PlaySessionId"] = playSessionId;

    return json;
}
}

PlaybackService::PlaybackService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_retryPolicy{3, 1000, true}
{
}

// ============================================================================
// Request Helpers
// ============================================================================

void PlaybackService::sendRequestWithRetry(const QString &endpoint,
                                            RequestFactory requestFactory,
                                            ResponseHandler responseHandler,
                                            int attemptNumber)
{
    qCDebug(playbackService) << "Sending request to:" << endpoint 
                             << "attempt:" << (attemptNumber + 1) << "/" << m_retryPolicy.maxRetries;
    
    QNetworkReply *reply = requestFactory();
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint, requestFactory, responseHandler, attemptNumber]() {
        handleReplyWithRetry(reply, endpoint, requestFactory, responseHandler, attemptNumber);
    });
}

void PlaybackService::handleReplyWithRetry(QNetworkReply *reply,
                                            const QString &endpoint,
                                            RequestFactory requestFactory,
                                            ResponseHandler responseHandler,
                                            int attemptNumber)
{
    reply->deleteLater();
    
    if (reply->error() == QNetworkReply::NoError) {
        qCDebug(playbackService) << "Request succeeded:" << endpoint;
        responseHandler(reply);
        return;
    }
    
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    if (httpStatus == 401) {
        qCWarning(playbackService) << "Session expired (401) for endpoint:" << endpoint;
        return;
    }
    
    NetworkError netError = ErrorHandler::createError(reply, endpoint);
    
    qCWarning(playbackService) << "Request failed:" << endpoint
                               << "Error:" << reply->error()
                               << "HTTP Status:" << httpStatus
                               << "Attempt:" << (attemptNumber + 1);
    
    bool shouldRetry = m_retryPolicy.retryOnTransient 
                       && ErrorHandler::isTransientError(reply->error())
                       && !ErrorHandler::isClientError(httpStatus)
                       && attemptNumber < m_retryPolicy.maxRetries - 1;
    
    if (shouldRetry) {
        int delayMs = ErrorHandler::calculateBackoffDelay(attemptNumber, m_retryPolicy);
        qCInfo(playbackService) << "Retrying request to:" << endpoint << "in" << delayMs << "ms";
        
        QTimer::singleShot(delayMs, this, [this, endpoint, requestFactory, responseHandler, attemptNumber]() {
            sendRequestWithRetry(endpoint, requestFactory, responseHandler, attemptNumber + 1);
        });
    } else {
        emitError(netError);
    }
}

void PlaybackService::emitError(const NetworkError &error)
{
    qCWarning(playbackService) << "Emitting error for endpoint:" << error.endpoint
                               << "User message:" << error.userMessage;
    emit errorOccurred(error.endpoint, error.userMessage);
    emit networkError(error);
}

// ============================================================================
// Playback Info
// ============================================================================

void PlaybackService::getPlaybackInfo(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getPlaybackInfo";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
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
        [this, itemId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            PlaybackInfoResponse info = PlaybackInfoResponse::fromJson(doc.object());
            emit playbackInfoLoaded(itemId, info);
        });
}

void PlaybackService::getMediaSegments(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) {
        qCDebug(playbackService) << "getMediaSegments: Not authenticated, skipping";
        emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
        return;
    }
    
    qCDebug(playbackService) << "Getting media segments for item:" << itemId;
    
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
            qCWarning(playbackService) << "Session expired while fetching media segments for" << itemId;
            m_authService->checkForSessionExpiry(reply, true);
            emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        // 404 is expected if Intro Skipper plugin is not installed
        // Just emit empty segments silently
        if (httpStatus == 404) {
            qCDebug(playbackService) << "Intro Skipper plugin not available for" << itemId;
            emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to get media segments for" << itemId
                                       << "Error:" << reply->errorString();
            emit mediaSegmentsLoaded(itemId, QList<MediaSegmentInfo>());
            return;
        }
        
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        
        QList<MediaSegmentInfo> segments;
        
        // Intro Skipper returns a dictionary with segment type names as keys:
        // { "Introduction": { "EpisodeId": "...", "Start": 114.114, "End": 204.204, "Valid": true },
        //   "Credits": { "EpisodeId": "...", "Start": 1329.328, "End": 1427.426, "Valid": true } }
        // Note: Start/End are in seconds, not ticks
        
        // Map of Intro Skipper type names to our types
        static const QMap<QString, QPair<MediaSegmentType, QString>> typeMapping = {
            {"Introduction", {MediaSegmentType::Intro, "Intro"}},
            {"Credits", {MediaSegmentType::Outro, "Outro"}},
            {"Recap", {MediaSegmentType::Recap, "Recap"}},
            {"Preview", {MediaSegmentType::Preview, "Preview"}},
            {"Commercial", {MediaSegmentType::Commercial, "Commercial"}}
        };
        
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            QString typeName = it.key();
            QJsonObject segmentObj = it.value().toObject();
            
            // Only process valid segments
            if (!segmentObj["Valid"].toBool(false)) {
                continue;
            }
            
            MediaSegmentInfo info;
            info.itemId = segmentObj["EpisodeId"].toString();
            
            // Convert seconds to ticks (1 tick = 100 nanoseconds = 0.0000001 seconds)
            double startSeconds = segmentObj["Start"].toDouble();
            double endSeconds = segmentObj["End"].toDouble();
            info.startTicks = static_cast<qint64>(startSeconds * 10000000.0);
            info.endTicks = static_cast<qint64>(endSeconds * 10000000.0);
            
            // Map the type
            if (typeMapping.contains(typeName)) {
                auto mapping = typeMapping[typeName];
                info.type = mapping.first;
                info.typeString = mapping.second;
            } else {
                info.type = MediaSegmentType::Unknown;
                info.typeString = typeName;
            }
            
            segments.append(info);
        }
        
        qCDebug(playbackService) << "Media segments loaded for" << itemId 
                                  << "- Count:" << segments.size();
        
        for (const auto &segment : segments) {
            qCDebug(playbackService) << "  Segment:" << segment.typeString
                                      << "Start:" << segment.startSeconds() << "s"
                                      << "End:" << segment.endSeconds() << "s";
        }
        
        emit mediaSegmentsLoaded(itemId, segments);
    });
}

void PlaybackService::getTrickplayInfo(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) {
        qCDebug(playbackService) << "getTrickplayInfo: Not authenticated, skipping";
        emit trickplayInfoLoaded(itemId, QMap<int, TrickplayTileInfo>());
        return;
    }
    
    qCDebug(playbackService) << "Getting trickplay info for item:" << itemId;
    
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
            
            // Get RunTimeTicks to calculate actual thumbnail count
            qint64 runTimeTicks = static_cast<qint64>(obj["RunTimeTicks"].toDouble());
            double durationSeconds = runTimeTicks / 10000000.0;
            
            // Debug: Log the raw Trickplay JSON response
            QJsonObject trickplayRaw = obj["Trickplay"].toObject();
            qCDebug(playbackService) << "Trickplay raw JSON for" << itemId << ":"
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
                                qCDebug(playbackService) << "Overriding ThumbnailCount from" << info.thumbnailCount
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
                qCDebug(playbackService) << "No trickplay info available for" << itemId;
            } else {
                qCDebug(playbackService) << "Trickplay info loaded for" << itemId 
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
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Reporting playback start for item:" << itemId
                             << "mediaSourceId:" << mediaSourceId
                             << "audioIndex:" << audioStreamIndex
                             << "subtitleIndex:" << subtitleStreamIndex;

    QJsonObject json = buildPlaybackPayload(itemId, -1, mediaSourceId,
                                            audioStreamIndex, subtitleStreamIndex,
                                            playSessionId,
                                            canSeek, isPaused, isMuted,
                                            playMethod, repeatMode, playbackOrder);
    
    QNetworkRequest request = m_authService->createRequest("/Sessions/Playing");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, true)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to report playback start for" << itemId 
                                       << ":" << reply->errorString();
        }
    });
}

void PlaybackService::reportPlaybackProgress(const QString &itemId, qint64 positionTicks,
                                              const QString &mediaSourceId,
                                              int audioStreamIndex, int subtitleStreamIndex,
                                              const QString &playSessionId,
                                              bool canSeek, bool isPaused, bool isMuted,
                                              const QString &playMethod,
                                              const QString &repeatMode,
                                              const QString &playbackOrder)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Reporting playback progress for item:" << itemId 
                             << "position:" << positionTicks;

    QJsonObject json = buildPlaybackPayload(itemId, positionTicks, mediaSourceId,
                                            audioStreamIndex, subtitleStreamIndex,
                                            playSessionId,
                                            canSeek, isPaused, isMuted,
                                            playMethod, repeatMode, playbackOrder);
    json["EventName"] = "TimeUpdate";
    
    QNetworkRequest request = m_authService->createRequest("/Sessions/Playing/Progress");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, true)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to report playback progress for" << itemId 
                                       << ":" << reply->errorString();
        }
    });
}

void PlaybackService::reportPlaybackPaused(const QString &itemId, qint64 positionTicks,
                                            const QString &mediaSourceId,
                                            int audioStreamIndex, int subtitleStreamIndex,
                                            const QString &playSessionId,
                                            bool canSeek, bool isMuted,
                                            const QString &playMethod,
                                            const QString &repeatMode,
                                            const QString &playbackOrder)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Reporting playback paused for item:" << itemId 
                             << "position:" << positionTicks;

    QJsonObject json = buildPlaybackPayload(itemId, positionTicks, mediaSourceId,
                                            audioStreamIndex, subtitleStreamIndex,
                                            playSessionId,
                                            canSeek, true, isMuted,
                                            playMethod, repeatMode, playbackOrder);
    json["EventName"] = "Pause";
    
    QNetworkRequest request = m_authService->createRequest("/Sessions/Playing/Progress");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, true)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to report playback paused for" << itemId 
                                       << ":" << reply->errorString();
        }
    });
}

void PlaybackService::reportPlaybackResumed(const QString &itemId, qint64 positionTicks,
                                             const QString &mediaSourceId,
                                             int audioStreamIndex, int subtitleStreamIndex,
                                             const QString &playSessionId,
                                             bool canSeek, bool isMuted,
                                             const QString &playMethod,
                                             const QString &repeatMode,
                                             const QString &playbackOrder)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Reporting playback resumed for item:" << itemId 
                             << "position:" << positionTicks;

    QJsonObject json = buildPlaybackPayload(itemId, positionTicks, mediaSourceId,
                                            audioStreamIndex, subtitleStreamIndex,
                                            playSessionId,
                                            canSeek, false, isMuted,
                                            playMethod, repeatMode, playbackOrder);
    json["EventName"] = "Unpause";
    
    QNetworkRequest request = m_authService->createRequest("/Sessions/Playing/Progress");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, true)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to report playback resumed for" << itemId 
                                       << ":" << reply->errorString();
        }
    });
}

void PlaybackService::reportPlaybackStopped(const QString &itemId, qint64 positionTicks,
                                             const QString &mediaSourceId,
                                             int audioStreamIndex, int subtitleStreamIndex,
                                             const QString &playSessionId,
                                             bool canSeek, bool isPaused, bool isMuted,
                                             const QString &playMethod,
                                             const QString &repeatMode,
                                             const QString &playbackOrder)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Reporting playback stopped for item:" << itemId 
                             << "position:" << positionTicks;

    QJsonObject json = buildPlaybackPayload(itemId, positionTicks, mediaSourceId,
                                            audioStreamIndex, subtitleStreamIndex,
                                            playSessionId,
                                            canSeek, isPaused, isMuted,
                                            playMethod, repeatMode, playbackOrder);
    json["EventName"] = "Stop";
    
    QNetworkRequest request = m_authService->createRequest("/Sessions/Playing/Stopped");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, false)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to report playback stopped for" << itemId 
                                       << ":" << reply->errorString();
        }
    });
}

void PlaybackService::markItemPlayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;

    qCDebug(playbackService) << "Marking item as played:" << itemId;

    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply, false)) return;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(playbackService) << "Failed to mark item as played:" << itemId 
                                       << ":" << reply->errorString();
        } else {
            qCDebug(playbackService) << "Successfully marked item as played:" << itemId;
            emit itemMarkedPlayed(itemId);
        }
    });
}
