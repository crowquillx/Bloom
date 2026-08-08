#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QNetworkReply>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include "Types.h"
#include "models/MediaModels.h"  // For data structs (PlaybackInfoResponse, MediaSegmentInfo, TrickplayTileInfo, etc.)
struct PlaybackProviderContext;
struct PlaybackReportRequest;
class IPlaybackProvider;
class AuthenticationService;
class ConfigManager;
class HttpTransport;
class MediaSegmentProviderService;
struct PlaybackReport;

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
    explicit PlaybackService(AuthenticationService *authService,
                             ConfigManager *configManager = nullptr,
                             MediaSegmentProviderService *mediaSegmentProviderService = nullptr,
                             QObject *parent = nullptr);
    ~PlaybackService() override;

    virtual Bloom::PlaybackDescriptor createPlaybackDescriptor(
        const QString &itemId,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs = 0,
        const QString &playbackSessionId = QString(),
        bool emitFailure = true);

    // Provider-neutral asynchronous playback plan API. Jellyfin completes this
    // through its existing descriptor path; native providers execute their plan.
    Q_INVOKABLE virtual void requestPlaybackDescriptor(
        const QString &itemId,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs = 0,
        const QString &playbackSessionId = QString(),
        const QString &requestContext = QString());
    Q_INVOKABLE virtual bool switchPlaybackAudio(const QString &playbackSessionId,
                                                  int audioTrackIndex,
                                                  qint64 positionMs = 0,
                                                  const QString &requestContext = QString());
    Q_INVOKABLE virtual void requestPlaybackRecovery(
        const QString &itemId,
        const QVariantMap &providerSource,
        qint64 startPositionMs = 0,
        const QString &requestContext = QString());

    // Playback Info - Get media streams and track information
    Q_INVOKABLE virtual void getPlaybackInfo(const QString &itemId);
    Q_INVOKABLE virtual void getAdditionalParts(const QString &itemId);
    virtual void getPlaybackInfo(const QString &itemId, const QString &requestContext);
    virtual void getAdditionalParts(const QString &itemId, const QString &requestContext);
    
    // Media Segments - Get intro/outro markers for skip functionality
    Q_INVOKABLE void getMediaSegments(const QString &itemId);
    
    // Trickplay - Get thumbnail tile information for seek preview
    Q_INVOKABLE void getTrickplayInfo(const QString &itemId);
    Q_INVOKABLE QString getTrickplayTileUrl(const QString &itemId, int width, int tileIndex);
    
    // Playback Reporting (with track selection support)
    Q_INVOKABLE virtual void reportPlaybackStart(const QString &itemId, const QString &mediaSourceId = QString(),
                                                 int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                                 const QString &playSessionId = QString(),
                                                 bool canSeek = true, bool isPaused = false, bool isMuted = false,
                                                 const QString &playMethod = QStringLiteral("DirectPlay"),
                                                 const QString &repeatMode = QStringLiteral("RepeatNone"),
                                                 const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE virtual void reportPlaybackProgress(const QString &itemId, qint64 positionMs,
                                                    const QString &mediaSourceId = QString(),
                                                    int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                                    const QString &playSessionId = QString(),
                                                    bool canSeek = true, bool isPaused = false, bool isMuted = false,
                                                    const QString &playMethod = QStringLiteral("DirectPlay"),
                                                    const QString &repeatMode = QStringLiteral("RepeatNone"),
                                                    const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE virtual void reportPlaybackPaused(const QString &itemId, qint64 positionMs,
                                                  const QString &mediaSourceId = QString(),
                                                  int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                                  const QString &playSessionId = QString(),
                                                  bool canSeek = true, bool isMuted = false,
                                                  const QString &playMethod = QStringLiteral("DirectPlay"),
                                                  const QString &repeatMode = QStringLiteral("RepeatNone"),
                                                  const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE virtual void reportPlaybackResumed(const QString &itemId, qint64 positionMs,
                                                   const QString &mediaSourceId = QString(),
                                                   int audioStreamIndex = -1, int subtitleStreamIndex = -1,
                                                   const QString &playSessionId = QString(),
                                                   bool canSeek = true, bool isMuted = false,
                                                   const QString &playMethod = QStringLiteral("DirectPlay"),
                                                   const QString &repeatMode = QStringLiteral("RepeatNone"),
                                                   const QString &playbackOrder = QStringLiteral("Default"));
    Q_INVOKABLE virtual void reportPlaybackStopped(const QString &itemId, qint64 positionMs,
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
    void playbackDescriptorLoadedForRequest(const QString &itemId,
                                            const Bloom::PlaybackDescriptor &descriptor,
                                            const QString &requestContext);
    void playbackDescriptorFailedForRequest(const QString &itemId,
                                            const QString &error,
                                            const QString &requestContext);
    void playbackAudioSwitchedForRequest(const QString &playbackSessionId,
                                         const QUrl &reloadUrl,
                                         const QString &requestContext);
    void playbackAudioSwitchFailedForRequest(const QString &playbackSessionId,
                                             const QString &error,
                                             const QString &requestContext);
    void playbackRecoveryLoadedForRequest(const QString &itemId,
                                          const Bloom::PlaybackDescriptor &descriptor,
                                          const QString &requestContext);
    void playbackRecoveryFailedForRequest(const QString &itemId,
                                          const QString &error,
                                          const QString &requestContext);
    void playbackInfoLoadedForRequest(const QString &itemId,
                                      const PlaybackInfoResponse &playbackInfo,
                                      const QString &requestContext);
    void playbackInfoFailedForRequest(const QString &itemId,
                                      const QString &error,
                                      const QString &requestContext);
    void additionalPartsLoaded(const QString &itemId, const QVariantList &parts);
    void additionalPartsLoadedForRequest(const QString &itemId,
                                         const QVariantList &parts,
                                         const QString &requestContext);
    void additionalPartsFailedForRequest(const QString &itemId,
                                         const QString &error,
                                         const QString &requestContext);
    
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
    HttpTransport *m_transport = nullptr;
    ConfigManager *m_configManager = nullptr;
    MediaSegmentProviderService *m_mediaSegmentProviderService = nullptr;
    RetryPolicy m_retryPolicy;
    QHash<QString, int> m_pendingProgressReports;
    QHash<QString, const IPlaybackProvider *> m_pendingStopProviders;
    QHash<QString, PlaybackReport> m_pendingStopReports;
    
    // Retry mechanism types  
    using ResponseHandler = std::function<void(QNetworkReply*)>;
    using RequestFactory = std::function<QNetworkReply*()>;
    using FailureHandler = std::function<void(const NetworkError&)>;
    void sendRequestWithRetry(const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               FailureHandler failureHandler = FailureHandler(),
                               int attemptNumber = 0,
                               bool deferSessionExpiry = true,
                               bool enableTransientRetry = true);
    PlaybackProviderContext providerContext() const;
    quint64 beginRequest(const QString &operation,
                         const QString &itemId,
                         const QString &requestContext);
    bool isCurrentRequest(const QString &operation,
                          const QString &itemId,
                          const QString &requestContext,
                          quint64 generation) const;
    QNetworkReply *sendProviderRequest(const QString &endpoint,
                                       const QString &method,
                                       const QJsonObject &body) const;
    void emitDescriptorFailure(const QString &itemId,
                               const QString &requestContext,
                               const QString &error);
    QHash<QString, quint64> m_requestGenerations;
    
    void emitError(const NetworkError &error);
    void maybeLoadExternalMediaSegments(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments);
    void loadMediaSegmentLookupContext(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments);
    void finishExternalMediaSegments(const QString &itemId,
                                     const QList<MediaSegmentInfo> &serverSegments,
                                     const QList<MediaSegmentInfo> &mergedSegments);
    void finishMediaSegments(const QString &itemId,
                             const QList<MediaSegmentInfo> &segments);
    void sendPlaybackReport(const PlaybackReport &report,
                            std::function<void()> completion = {},
                            const IPlaybackProvider *providerOverride = nullptr);
};
