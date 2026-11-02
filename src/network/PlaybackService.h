#pragma once

#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QNetworkReply>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include "Types.h"
#include "models/MediaModels.h"  // For data structs (PlaybackInfoResponse, MediaSegmentInfo, TrickplayTileInfo, etc.)
#include "providers/IPlaybackProvider.h"
struct PlaybackProviderContext;
struct PlaybackReportRequest;
class IPlaybackProvider;
namespace Bloom { class AudioOutputService; }
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

    Bloom::AudioOutputService *audioOutput() const { return m_audioOutput; }

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
    // Asks the provider for a new stream in an active session: a track change on
    // a stream that carries only the selected tracks, or a seek outside what a
    // server stream has produced. False when the provider has no such request.
    virtual bool replanPlayback(const QString &playbackSessionId,
                                const PlaybackReplan &replan,
                                const QString &requestContext);

    // Playback Info - Get media streams and track information
    Q_INVOKABLE virtual void getPlaybackInfo(const QString &itemId);
    Q_INVOKABLE virtual void getAdditionalParts(const QString &itemId);
    virtual void getPlaybackInfo(const QString &itemId, const QString &requestContext);
    virtual void getAdditionalParts(const QString &itemId, const QString &requestContext);
    
    // Media Segments - Get intro/outro markers for skip functionality
    Q_INVOKABLE void getMediaSegments(const QString &itemId);
    Q_INVOKABLE void getMediaSegments(const QString &itemId, const QString &fileId);
    
    // Trickplay - Get thumbnail tile information for seek preview
    Q_INVOKABLE void getTrickplayInfo(const QString &itemId, const QString &fileId = QString());
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

    /// Remembers a track the user picked on the server so other devices
    /// follow (Silo only). Best-effort: a failure is only logged and never
    /// touches the local choice. Writes for the same content and kind are
    /// sent one at a time; while one is in flight only the newest waiting
    /// write is kept. A write picked under another account, profile or
    /// server than the current one is dropped.
    virtual void writeTrackPreference(const TrackPreferenceWrite &write);
    /// The server, account and profile a track preference write goes to;
    /// empty when signed out.
    virtual QString trackPreferenceWriteIdentity() const;
    /// The account's own language defaults on each source that carries no
    /// server preferences of its own (Jellyfin UserConfiguration).
    PlaybackInfoResponse withAccountPlaybackDefaults(PlaybackInfoResponse info) const;
    /// Whether the server stores a track preference for the item (Silo
    /// getAudioPreference), applied to `response` before `done` runs.
    void readTrackPreference(const IPlaybackProvider *provider,
                             const PlaybackInfoRequest &request,
                             const PlaybackInfoResponse &response,
                             std::function<void(const PlaybackInfoResponse &)> done);

    /// True when the active provider expires sessions that are started but
    /// not yet streamed (see IPlaybackProvider::queuedSessionsExpire).
    virtual bool queuedPlaybackSessionsExpire() const;

signals:
    // The account, profile or server track preference writes go to may have
    // changed; writes picked under another one must be dropped.
    void trackPreferenceIdentityChanged();
    // Playback info with media streams for track selection
    void playbackInfoLoaded(const QString &itemId, const PlaybackInfoResponse &playbackInfo);
    void playbackDescriptorLoadedForRequest(const QString &itemId,
                                            const Bloom::PlaybackDescriptor &descriptor,
                                            const QString &requestContext);
    void playbackDescriptorFailedForRequest(const QString &itemId,
                                            const QString &error,
                                            const QString &requestContext);
    void playbackReplannedForRequest(const QString &playbackSessionId,
                                     const Bloom::PlaybackDescriptor &descriptor,
                                     const QString &requestContext);
    void playbackReplanFailedForRequest(const QString &playbackSessionId,
                                        const QString &error,
                                        const QString &requestContext);
    // A progress report found the server-side session gone. endedByServer
    // means the player must leave playback instead of starting a new session.
    void playbackSessionLost(const QString &playbackSessionId, bool endedByServer);
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

private:
    Bloom::PlaybackDescriptor withAudioPlan(Bloom::PlaybackDescriptor descriptor) const;
    Bloom::AudioOutputService *m_audioOutput;
    AuthenticationService *m_authService;
    HttpTransport *m_transport = nullptr;
    ConfigManager *m_configManager = nullptr;
    MediaSegmentProviderService *m_mediaSegmentProviderService = nullptr;
    RetryPolicy m_retryPolicy;
    QHash<QString, int> m_pendingProgressReports;
    QHash<QString, const IPlaybackProvider *> m_pendingStopProviders;
    QHash<QString, QString> m_pendingStopRequestIdentities;
    QHash<QString, PlaybackReport> m_pendingStopReports;
    // Keys (request identity, kind, series) with a track preference write in
    // flight, and the newest write waiting behind each.
    QSet<QString> m_trackPreferenceWritesInFlight;
    QHash<QString, TrackPreferenceWrite> m_queuedTrackPreferenceWrites;
    
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
                               RetrySafety retrySafety = RetrySafety::Never);
    PlaybackProviderContext providerContext() const;
    void loadPlaybackCapabilities(const IPlaybackProvider *provider,
                                  const PlaybackProviderContext &context,
                                  std::function<void(bool)> completion);
    QHash<QString, QPair<QByteArray, QJsonObject>> m_playbackCapabilities;
    quint64 beginRequest(const QString &operation,
                         const QString &itemId,
                         const QString &requestContext);
    void endRequest(const QString &operation,
                    const QString &itemId,
                    const QString &requestContext,
                    quint64 generation);
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
    QHash<QString, QString> m_requestIdentities;
    quint64 m_nextRequestGeneration = 0;
    QHash<QString, quint64> m_requestGenerations;
    
    void emitError(const NetworkError &error);
    void maybeLoadExternalMediaSegments(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments);
    void loadMediaSegmentLookupContext(const QString &itemId, const QList<MediaSegmentInfo> &serverSegments);
    void finishExternalMediaSegments(const QString &itemId,
                                     const QList<MediaSegmentInfo> &serverSegments,
                                     const QList<MediaSegmentInfo> &mergedSegments);
    void finishMediaSegments(const QString &itemId,
                             const QList<MediaSegmentInfo> &segments);
    static constexpr qint64 kPlaybackTokenLifetimeSeconds = 4 * 60 * 60;
    void handleReportFailure(const IPlaybackProvider *provider,
                             const PlaybackReport &report,
                             int httpStatus);
    void sendPlaybackReport(const PlaybackReport &report,
                            std::function<void()> completion = {},
                            const IPlaybackProvider *providerOverride = nullptr);
};
