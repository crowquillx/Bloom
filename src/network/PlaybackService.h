#pragma once

#include <QObject>
#include <QNetworkReply>
#include <functional>
#include "Types.h"  // For data structs (PlaybackInfoResponse, MediaSegmentInfo, TrickplayTileInfo, etc.)

class AuthenticationService;

/**
 * @brief Handles playback reporting and playback-related metadata.
 * 
 * This service manages:
 * - Playback start/progress/pause/stop reporting to server
 * - Mark items as played/unplayed
 * - Playback info (media streams, track selection)
 * - Media segments (intro/outro markers)
 * - Trickplay thumbnails
 * 
 * Part of the service decomposition formerly handled by the legacy client (Roadmap 1.1).
 */
class PlaybackService : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackService(AuthenticationService *authService, QObject *parent = nullptr);
    
    // Playback Info - Get media streams and track information
    Q_INVOKABLE void getPlaybackInfo(const QString &itemId);
    
    // Media Segments - Get intro/outro markers for skip functionality
    Q_INVOKABLE void getMediaSegments(const QString &itemId);
    
    // Trickplay - Get thumbnail tile information for seek preview
    Q_INVOKABLE void getTrickplayInfo(const QString &itemId);
    Q_INVOKABLE QString getTrickplayTileUrl(const QString &itemId, int width, int tileIndex);
    
    // Playback Reporting (with track selection support)
    Q_INVOKABLE void reportPlaybackStart(const QString &itemId, const QString &mediaSourceId = QString(),
                                         int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                         const QString &playSessionId = QString(),
                                         bool canSeek = true, bool isPaused = false, bool isMuted = false,
                                         const QString &playMethod = QStringLiteral("DirectPlay"),
                                         const QString &repeatMode = QStringLiteral("RepeatNone"),
                                         const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE void reportPlaybackProgress(const QString &itemId, qint64 positionTicks,
                                            const QString &mediaSourceId = QString(),
                                            int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                            const QString &playSessionId = QString(),
                                            bool canSeek = true, bool isPaused = false, bool isMuted = false,
                                            const QString &playMethod = QStringLiteral("DirectPlay"),
                                            const QString &repeatMode = QStringLiteral("RepeatNone"),
                                            const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE void reportPlaybackPaused(const QString &itemId, qint64 positionTicks,
                                          const QString &mediaSourceId = QString(),
                                          int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                          const QString &playSessionId = QString(),
                                          bool canSeek = true, bool isMuted = false,
                                          const QString &playMethod = QStringLiteral("DirectPlay"),
                                          const QString &repeatMode = QStringLiteral("RepeatNone"),
                                          const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE void reportPlaybackResumed(const QString &itemId, qint64 positionTicks,
                                           const QString &mediaSourceId = QString(),
                                           int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                           const QString &playSessionId = QString(),
                                           bool canSeek = true, bool isMuted = false,
                                           const QString &playMethod = QStringLiteral("DirectPlay"),
                                           const QString &repeatMode = QStringLiteral("RepeatNone"),
                                           const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE void reportPlaybackStopped(const QString &itemId, qint64 positionTicks,
                                           const QString &mediaSourceId = QString(),
                                           int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                           const QString &playSessionId = QString(),
                                           bool canSeek = true, bool isPaused = false, bool isMuted = false,
                                           const QString &playMethod = QStringLiteral("DirectPlay"),
                                           const QString &repeatMode = QStringLiteral("RepeatNone"),
                                           const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE void markItemPlayed(const QString &itemId);

signals:
    // Playback info with media streams for track selection
    void playbackInfoLoaded(const QString &itemId, const PlaybackInfoResponse &playbackInfo);
    
    // Media segments loaded (intro/outro markers)
    void mediaSegmentsLoaded(const QString &itemId, const QList<MediaSegmentInfo> &segments);
    
    // Trickplay info loaded (thumbnail tile info)
    void trickplayInfoLoaded(const QString &itemId, const QMap<int, TrickplayTileInfo> &trickplayInfo);
    
    // Error signals
    void errorOccurred(const QString &endpoint, const QString &error);
    void networkError(const NetworkError &error);
    
    // Item marked as played signal
    void itemMarkedPlayed(const QString &itemId);

private:
    AuthenticationService *m_authService;
    RetryPolicy m_retryPolicy;
    
    // Retry mechanism types  
    using ResponseHandler = std::function<void(QNetworkReply*)>;
    using RequestFactory = std::function<QNetworkReply*()>;
    
    void sendRequestWithRetry(const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               int attemptNumber = 0);
    
    void handleReplyWithRetry(QNetworkReply *reply,
                               const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               int attemptNumber);
    
    void emitError(const NetworkError &error);
};
