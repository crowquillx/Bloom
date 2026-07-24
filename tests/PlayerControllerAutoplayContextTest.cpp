#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>

#include "player/PlayerController.h"

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"
#include "utils/TrackPreferencesManager.h"

class FakePlayerBackend final : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit FakePlayerBackend(QObject *parent = nullptr)
        : IPlayerBackend(parent)
    {
    }

    QString backendName() const override
    {
        return QStringLiteral("fake-backend");
    }

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) override
    {
        Q_UNUSED(mpvBin);
        lastStartArgs = args;
        lastStartUrl = mediaUrl;
        m_running = true;
        emit stateChanged(true);
    }

    void appendUrlsToPlaylist(const QStringList &mediaUrls) override
    {
        appendedUrls.append(mediaUrls);
    }

    void stopMpv() override
    {
        ++stopCallCount;
        if (!m_running) {
            return;
        }
        m_running = false;
        if (emitStopStateChangeSynchronously) {
            emit stateChanged(false);
        }
    }

    bool isRunning() const override
    {
        return m_running;
    }

    void sendCommand(const QStringList &command) override
    {
        commands.append(command);
    }

    void sendVariantCommand(const QVariantList &command) override
    {
        variantCommands.append(command);
    }

    bool supportsEmbeddedVideo() const override
    {
        return false;
    }

    bool attachVideoTarget(QObject *target) override
    {
        Q_UNUSED(target);
        return false;
    }

    void detachVideoTarget(QObject *target) override
    {
        Q_UNUSED(target);
    }

    void setVideoViewport(const QRectF &viewport) override
    {
        Q_UNUSED(viewport);
    }

private:
    bool m_running = false;

public:
    bool emitStopStateChangeSynchronously = true;
    int stopCallCount = 0;
    QList<QVariantList> variantCommands;
    QList<QStringList> commands;
    QStringList appendedUrls;
    QStringList lastStartArgs;
    QString lastStartUrl;

    void setRunning(bool running)
    {
        m_running = running;
    }

    void emitAudioTrackId(int mpvTrackId)
    {
        emit audioTrackChanged(mpvTrackId);
    }

    void emitSubtitleTrackId(int mpvTrackId)
    {
        emit subtitleTrackChanged(mpvTrackId);
    }

    void emitPlaylistPosition(int index)
    {
        emit playlistPositionChanged(index);
    }

    void emitRunningState(bool running)
    {
        m_running = running;
        emit stateChanged(running);
    }

    void emitPlaybackEndedSignal()
    {
        emit playbackEnded();
    }

    void emitCacheEnd(double seconds)
    {
        emit cacheEndChanged(seconds);
    }

    void emitPosition(double seconds)
    {
        emit positionChanged(seconds);
    }

    void emitPausedForCache(bool paused)
    {
        emit pausedForCacheChanged(paused);
    }
};

class FakeLibraryService final : public LibraryService
{
    Q_OBJECT

public:
    explicit FakeLibraryService(AuthenticationService *authService, QObject *parent = nullptr)
        : LibraryService(authService, parent)
    {
    }

    void getNextUnplayedEpisode(const QString &seriesId,
                                const QString &excludeItemId = QString(),
                                const QString &requestContext = QString()) override
    {
        requestedSeriesIds.append(seriesId);
        requestedExcludeIds.append(excludeItemId);
        requestedContexts.append(requestContext);
    }

    void getSeriesDetails(const QString &seriesId) override
    {
        requestedSeriesDetailsIds.append(seriesId);
    }
    
    void resolveLibraryForItem(const QString &itemId) override
    {
        requestedLibraryResolutionIds.append(itemId);
    }

    QString getActiveConnectionId() const override
    {
        return activeConnectionIdValue;
    }

    QString getStreamUrl(const QString &itemId) override
    {
        requestedBasicStreamItemIds.append(itemId);
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                  int audioStreamIndex = -1, int subtitleStreamIndex = -1) override
    {
        requestedTrackStreamItemIds.append(itemId);
        requestedStreamMediaSourceIds.append(mediaSourceId);
        requestedAudioStreamIndexes.append(audioStreamIndex);
        requestedSubtitleStreamIndexes.append(subtitleStreamIndex);
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QString activeConnectionIdValue;
    QStringList requestedSeriesIds;
    QStringList requestedExcludeIds;
    QStringList requestedContexts;
    QStringList requestedSeriesDetailsIds;
    QStringList requestedLibraryResolutionIds;
    QStringList requestedBasicStreamItemIds;
    QStringList requestedTrackStreamItemIds;
    QStringList requestedStreamMediaSourceIds;
    QList<int> requestedAudioStreamIndexes;
    QList<int> requestedSubtitleStreamIndexes;
};

class FakePlaybackService final : public PlaybackService
{
    Q_OBJECT

public:
    struct Report
    {
        QString itemId;
        qint64 positionMs = 0;
        QString mediaSourceId;
        int audioStreamIndex = -1;
        int subtitleStreamIndex = -1;
        QString playSessionId;
        bool canSeek = false;
        bool isPaused = false;
        bool isMuted = false;
        QString playMethod;
    };

    explicit FakePlaybackService(AuthenticationService *authService, QObject *parent = nullptr)
        : PlaybackService(authService, nullptr, nullptr, parent)
        , m_authService(authService)
    {
    }

    Bloom::PlaybackDescriptor createPlaybackDescriptor(
        const QString &itemId,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs,
        const QString &playbackSessionId) override
    {
        if (!providerSource.value(QStringLiteral("directStreamUrl")).toString().isEmpty()
            || !providerSource.value(QStringLiteral("transcodingUrl")).toString().isEmpty()) {
            JellyfinPlaybackProvider provider;
            const PlaybackProviderContext context{
                QUrl(m_authService ? m_authService->getServerUrl() : QString()),
                m_authService ? m_authService->getAccessToken() : QString()
            };
            return provider.createDescriptor(
                context,
                Bloom::MediaRef{QStringLiteral("test-connection"), itemId},
                providerSource,
                selectedAudioTrack,
                selectedSubtitleTrack,
                startPositionMs,
                playbackSessionId);
        }

        requestedDescriptorItemIds.append(itemId);
        requestedDescriptorMediaSourceIds.append(
            providerSource.value(QStringLiteral("id")).toString());
        requestedDescriptorAudioIndexes.append(selectedAudioTrack);
        requestedDescriptorSubtitleIndexes.append(selectedSubtitleTrack);

        Bloom::PlaybackDescriptor descriptor;
        descriptor.media = {QStringLiteral("test-connection"), itemId};
        descriptor.mediaVersionId = providerSource.value(QStringLiteral("id")).toString();
        descriptor.playbackSessionId = playbackSessionId;
        descriptor.startPositionMs = startPositionMs;
        descriptor.stream.url = QUrl(QStringLiteral("https://example.invalid/") + itemId);
        descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
        return descriptor;
    }

    void getPlaybackInfo(const QString &itemId) override
    {
        requestedPlaybackInfoItemIds.append(itemId);
        requestedPlaybackInfoContexts.append(QString());
    }

    void getPlaybackInfo(const QString &itemId, const QString &requestContext) override
    {
        requestedPlaybackInfoItemIds.append(itemId);
        requestedPlaybackInfoContexts.append(requestContext);
    }

    void getAdditionalParts(const QString &itemId) override
    {
        requestedAdditionalPartsItemIds.append(itemId);
        requestedAdditionalPartsContexts.append(QString());
    }

    void getAdditionalParts(const QString &itemId, const QString &requestContext) override
    {
        requestedAdditionalPartsItemIds.append(itemId);
        requestedAdditionalPartsContexts.append(requestContext);
    }

    void reportPlaybackProgress(const QString &itemId, qint64 positionMs,
                                const QString &mediaSourceId,
                                int audioStreamIndex, int subtitleStreamIndex,
                                const QString &playSessionId,
                                bool canSeek, bool isPaused, bool isMuted,
                                const QString &playMethod,
                                const QString &repeatMode,
                                const QString &playbackOrder) override
    {
        Q_UNUSED(repeatMode);
        Q_UNUSED(playbackOrder);
        progressReports.append({itemId, positionMs, mediaSourceId, audioStreamIndex,
                                subtitleStreamIndex, playSessionId, canSeek, isPaused,
                                isMuted, playMethod});
    }

    void reportPlaybackStopped(const QString &itemId, qint64 positionMs,
                               const QString &mediaSourceId,
                               int audioStreamIndex, int subtitleStreamIndex,
                               const QString &playSessionId,
                               bool canSeek, bool isPaused, bool isMuted,
                               const QString &playMethod,
                               const QString &repeatMode,
                               const QString &playbackOrder) override
    {
        Q_UNUSED(repeatMode);
        Q_UNUSED(playbackOrder);
        stoppedReports.append({itemId, positionMs, mediaSourceId, audioStreamIndex,
                               subtitleStreamIndex, playSessionId, canSeek, isPaused,
                               isMuted, playMethod});
    }

    QList<Report> progressReports;
    QList<Report> stoppedReports;
    QStringList requestedPlaybackInfoItemIds;
    QStringList requestedPlaybackInfoContexts;
    QStringList requestedAdditionalPartsItemIds;
    QStringList requestedAdditionalPartsContexts;
    mutable QStringList requestedDescriptorItemIds;
    mutable QStringList requestedDescriptorMediaSourceIds;
    mutable QList<int> requestedDescriptorAudioIndexes;
    mutable QList<int> requestedDescriptorSubtitleIndexes;

private:
    AuthenticationService *m_authService = nullptr;
};

static MediaSourceInfo buildMediaSourceInfo(const QString &id,
                                            const QString &name,
                                            const QString &path,
                                            const QList<QVariantMap> &streams,
                                            int defaultAudio = -1,
                                            int defaultSubtitle = -1,
                                            const QString &container = QStringLiteral("mkv"),
                                            int bitRate = 0,
                                            qint64 runTimeTicks = 0,
                                            const QString &directStreamUrl = QString(),
                                            const QString &transcodingUrl = QString())
{
    MediaSourceInfo info;
    info.id = id;
    info.name = name;
    info.path = path;
    info.directStreamUrl = directStreamUrl;
    info.transcodingUrl = transcodingUrl;
    info.container = container;
    info.bitRate = bitRate;
    info.runTimeTicks = runTimeTicks;
    info.defaultAudioStreamIndex = defaultAudio;
    info.defaultSubtitleStreamIndex = defaultSubtitle;
    for (const QVariantMap &stream : streams) {
        MediaStreamInfo mediaStream;
        mediaStream.index = stream.value(QStringLiteral("index"), -1).toInt();
        mediaStream.type = stream.value(QStringLiteral("type")).toString();
        mediaStream.codec = stream.value(QStringLiteral("codec")).toString();
        mediaStream.language = stream.value(QStringLiteral("language")).toString();
        mediaStream.title = stream.value(QStringLiteral("title")).toString();
        mediaStream.displayTitle = stream.value(QStringLiteral("displayTitle")).toString();
        mediaStream.isDefault = stream.value(QStringLiteral("isDefault"), false).toBool();
        mediaStream.isForced = stream.value(QStringLiteral("isForced"), false).toBool();
        mediaStream.channels = stream.value(QStringLiteral("channels"), 0).toInt();
        mediaStream.bitRate = stream.value(QStringLiteral("bitRate"), 0).toInt();
        mediaStream.width = stream.value(QStringLiteral("width"), 0).toInt();
        mediaStream.height = stream.value(QStringLiteral("height"), 0).toInt();
        mediaStream.averageFrameRate = stream.value(QStringLiteral("averageFrameRate"), 0.0).toDouble();
        mediaStream.realFrameRate = stream.value(QStringLiteral("realFrameRate"), 0.0).toDouble();
        mediaStream.profile = stream.value(QStringLiteral("profile")).toString();
        mediaStream.videoRange = stream.value(QStringLiteral("videoRange")).toString();
        mediaStream.videoRangeType = stream.value(QStringLiteral("videoRangeType")).toString();
        mediaStream.codecTag = stream.value(QStringLiteral("codecTag")).toString();
        mediaStream.codecTagString = stream.value(QStringLiteral("codecTagString")).toString();
        mediaStream.codecId = stream.value(QStringLiteral("codecId")).toString();
        mediaStream.dolbyVisionProfile = stream.value(QStringLiteral("dolbyVisionProfile"), 0).toInt();
        mediaStream.dolbyVisionLevel = stream.value(QStringLiteral("dolbyVisionLevel"), 0).toInt();
        mediaStream.dolbyVisionBlSignalCompatibilityId =
            stream.value(QStringLiteral("dolbyVisionBlSignalCompatibilityId"), 0).toInt();
        mediaStream.videoDoViTitle = stream.value(QStringLiteral("videoDoViTitle")).toString();
        info.mediaStreams.append(mediaStream);
    }
    return info;
}

static QVariantMap buildMediaSource(const QList<QVariantMap> &streams,
                                    int defaultAudio = -1,
                                    int defaultSubtitle = -1)
{
    PlaybackInfoResponse info;
    info.mediaSources = {
        buildMediaSourceInfo(QStringLiteral("media-source-1"),
                             QStringLiteral("Default"),
                             QStringLiteral("/library/default.mkv"),
                             streams,
                             defaultAudio,
                             defaultSubtitle)
    };
    return info.getMediaSourcesVariant().first().toMap();
}

static PlaybackInfoResponse buildPlaybackInfo(const QList<MediaSourceInfo> &mediaSources,
                                              const QString &playSessionId = QStringLiteral("play-session"))
{
    PlaybackInfoResponse info;
    info.playSessionId = playSessionId;
    info.mediaSources = mediaSources;
    return info;
}

class PlayerControllerAutoplayContextTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void playbackServiceReportsMissingProvider();
    void thresholdMetRequestsNextEpisodeDirectly();
    void userStopPastThresholdRequestsNextEpisode();
    void userStopBelowThresholdWaitsForBackendExit();
    void replacementPlaybackWaitsForQueuedTerminalFinalization();
    void replacementPlaybackRetainsFinalizedStreamMetadata();
    void explicitStopReportsFinalProgressAndStoppedOnce();
    void explicitPausedStopReportsPausedState();
    void explicitMultipartStopReportsActiveSegmentContext();
    void playbackEndedUpgradesQueuedStopFinalization();
    void nextEpisodeNavigationKeepsAwaitingUntilQueuedDelivery();
    void upNextIdleParkingInvalidatesQueuedDisplayRestore();
    void deferredPostPlaybackDisplayRestoreCanBeReleased();
    void nextEpisodeNavigationUsesPendingTrackContext();
    void nextEpisodeIgnoresMismatchedSeries();
    void nextEpisodeIgnoresMismatchedConnection();
    void playbackPrefetchIgnoresGenericNextEpisodeResponses();
    void autoplayPlaybackInfoErrorFallsBackToBasicPlayback();
    void autoplayToneMapStateSurvivesPlaybackInfoFallback();
    void displayHdrPolicyDoesNotToggleWhenToneMappingToSdr();
    void dolbyVisionWithHdr10BaseLayerDoesNotForceToneMap();
    void autoplayPlaybackInfoUsesStoredSubtitlePreferenceWhenOverrideUnset();
    void explicitSeasonPreferencesBeatGlobalTrackDefaults();
    void globalAudioLanguageSelectsMatchingStream();
    void configTrackLanguageSelectionCanonicalizesAliases();
    void globalSubtitleLanguageSelectsMatchingStream();
    void globalFileDefaultPrefersFileFlagOverJellyfinDefault();
    void globalSubtitleOffDisablesSubtitles();
    void globalSubtitleForcedSelectsForcedTrack();
    void globalSubtitleForcedFallsBackToOffWhenNoForcedTrackExists();
    void unavailableGlobalLanguageFallsBackToJellyfinDefault();
    void autoplayPlaybackInfoUsesGlobalSubtitlePreferenceWhenOverrideAndSeasonUnset();
    void staleAutoplayPlaybackInfoResponseFallsBackAfterTimeout();
    void playUrlWithTracksKeepsNewSessionMetadataWhenReplacingPlayback();
    void embeddedVideoShrinkToggleEmitsAndPersists();
    void playbackTogglesSendMpvCycleValuesCommands();
    void requestPlaybackPromptsForVersionSelection();
    void requestPlaybackRecoversLibraryProfileFromSeriesDetails();
    void remoteMountStartupBufferingAppendsMpvArgs();
    void remoteMountStartupCacheReadyWhileBufferingUnpausesAndPlays();
    void remoteMountPausedForCacheReleaseUnpausesManualPause();
    void requestPlaybackWaitsForSeriesDetailsParentIdBeforeStarting();
    void requestPlaybackUsesRecoveredLibraryWhenSeriesDetailsArriveBeforePlaybackInfo();
    void requestPlaybackFallsBackWithoutRecoveredLibraryProfileWhenParentIdMissing();
    void requestPlaybackKeepsSeriesProfilePriorityOverRecoveredLibrary();
    void explicitPlaybackIgnoresUnscopedStalePlaybackInfo();
    void explicitPlaybackIgnoresCanceledRequestScopedPlaybackInfo();
    void primaryPlaybackInfoFailureDoesNotUseBasicStreamFallback();
    void additionalPartsFailureStillStartsPrimary();
    void additionalPartPlaybackInfoFailureSkipsPart();
    void retryRefreshesPlaybackInfoBeforeRestarting();
    void recoverableBackendStreamErrorStartsRecovery();
    void recoverableErrorAfterBackendStopUpgradesTerminalTransition();
    void backendStopWithoutRecoverableErrorGoesIdle();
    void playbackInfoDirectStreamUrlIsPreferred();
    void multipartIntermediateEndIsIgnoredUntilFinalSegment();
    void versionAffinityPrefersMatchingParentPath();
    void startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned();
    void startupTrackSelectionRespectsPinnedUrlUnlessUserOverride();
    void runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone();
    void runtimeSubtitleDelayAppliesAndPersists();
    void backendTrackSyncUsesReverseMap();

private:
    QTemporaryDir m_configHome;
#ifdef Q_OS_LINUX
    QByteArray m_previousConfigHome;
    bool m_hadPreviousConfigHome = false;
#elif defined(Q_OS_WIN)
    QByteArray m_previousAppData;
    bool m_hadPreviousAppData = false;
#elif defined(Q_OS_MACOS)
    QByteArray m_previousHome;
    bool m_hadPreviousHome = false;
#endif
};

void PlayerControllerAutoplayContextTest::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_LINUX
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    m_hadPreviousConfigHome = !m_previousConfigHome.isNull();
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
#elif defined(Q_OS_WIN)
    m_previousAppData = qgetenv("APPDATA");
    m_hadPreviousAppData = !m_previousAppData.isNull();
    qputenv("APPDATA", m_configHome.path().toUtf8());
#elif defined(Q_OS_MACOS)
    m_previousHome = qgetenv("HOME");
    m_hadPreviousHome = !m_previousHome.isNull();
    qputenv("HOME", m_configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(m_configHome.path() + QStringLiteral("/Library/Preferences")));
#endif
}

void PlayerControllerAutoplayContextTest::cleanupTestCase()
{
#ifdef Q_OS_LINUX
    if (m_hadPreviousConfigHome) {
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
    } else {
        qunsetenv("XDG_CONFIG_HOME");
    }
#elif defined(Q_OS_WIN)
    if (m_hadPreviousAppData) {
        qputenv("APPDATA", m_previousAppData);
    } else {
        qunsetenv("APPDATA");
    }
#elif defined(Q_OS_MACOS)
    if (m_hadPreviousHome) {
        qputenv("HOME", m_previousHome);
    } else {
        qunsetenv("HOME");
    }
#endif
    QStandardPaths::setTestModeEnabled(false);
}

void PlayerControllerAutoplayContextTest::playbackServiceReportsMissingProvider()
{
    PlaybackService playbackService(nullptr);
    QSignalSpy errorSpy(&playbackService, &PlaybackService::errorOccurred);

    const Bloom::PlaybackDescriptor descriptor = playbackService.createPlaybackDescriptor(
        QStringLiteral("item-1"), QVariantMap{}, -1, -1);

    QVERIFY(!descriptor.isValid());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().at(0).toString(), QStringLiteral("createPlaybackDescriptor"));
    QVERIFY(!errorSpy.first().at(1).toString().isEmpty());
}

void PlayerControllerAutoplayContextTest::thresholdMetRequestsNextEpisodeDirectly()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.prepareTerminalTransition(PlayerController::TerminalReason::PlaybackEnd);

    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QVERIFY(controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QCOMPARE(controller.m_pendingAutoplayItemId, QStringLiteral("item-1"));
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));

    controller.m_hasEvaluatedCompletionForAttempt = true;
    controller.prepareTerminalTransition(PlayerController::TerminalReason::PlaybackEnd);
    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);
}

void PlayerControllerAutoplayContextTest::userStopPastThresholdRequestsNextEpisode()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.stop();
    QCoreApplication::processEvents();

    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedSeriesIds.first(), QStringLiteral("series-1"));
    QCOMPARE(libraryService.requestedExcludeIds.first(), QStringLiteral("item-1"));
    QCOMPARE(libraryService.requestedContexts.first(),
             QStringLiteral("player:resolve:series-1:item-1"));
    QVERIFY(controller.m_shouldAutoplay);
    QVERIFY(controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QCOMPARE(controller.m_pendingAutoplayItemId, QStringLiteral("item-1"));
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
}

void PlayerControllerAutoplayContextTest::userStopBelowThresholdWaitsForBackendExit()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.emitStopStateChangeSynchronously = false;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 40.0;
    controller.m_playbackState = PlayerController::Playing;
    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("stale-item");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("stale-series");

    controller.stop();

    QCOMPARE(backend.stopCallCount, 1);
    QCOMPARE(controller.playbackState(), PlayerController::Playing);
    QVERIFY(controller.m_terminalTransitionActive);
    QVERIFY(controller.m_terminalFinalizationQueued);
    QVERIFY(!controller.awaitingNextEpisodeResolution());
    QVERIFY(!controller.m_shouldAutoplay);
    QVERIFY(controller.m_pendingAutoplayItemId.isEmpty());
    QVERIFY(controller.m_pendingAutoplaySeriesId.isEmpty());
    QVERIFY(libraryService.requestedSeriesIds.isEmpty());

    backend.emitRunningState(false);
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Idle);
    QVERIFY(!controller.m_terminalTransitionActive);
}

void PlayerControllerAutoplayContextTest::replacementPlaybackWaitsForQueuedTerminalFinalization()
{
    ConfigManager config;
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.emitStopStateChangeSynchronously = false;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 40.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.stop();
    QVERIFY(controller.m_terminalTransitionActive);
    QVERIFY(controller.m_terminalFinalizationQueued);
    QCOMPARE(controller.playbackState(), PlayerController::Playing);

    controller.playUrl(QStringLiteral("https://example.invalid/item-2"),
                       QStringLiteral("item-2"));

    QCOMPARE(backend.stopCallCount, 1);
    QVERIFY(controller.m_terminalTransitionActive);
    QCOMPARE(controller.playbackState(), PlayerController::Playing);
    QVERIFY(backend.lastStartUrl.isEmpty());

    backend.emitRunningState(false);
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Loading);
    QCOMPARE(controller.currentItemId(), QStringLiteral("item-2"));
    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/item-2"));
    QVERIFY(!controller.m_terminalTransitionActive);
}

void PlayerControllerAutoplayContextTest::replacementPlaybackRetainsFinalizedStreamMetadata()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.emitStopStateChangeSynchronously = false;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_playbackState = PlayerController::Playing;
    controller.stop();

    controller.m_nextPlaybackMethod = QStringLiteral("directStream");
    controller.m_nextStreamPinsAudioTrack = true;
    controller.m_nextPinnedAudioTrack = 4;
    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/item-2"),
                                 QStringLiteral("item-2"),
                                 0,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QStringLiteral("source-2"),
                                 QStringLiteral("session-2"),
                                 QVariantMap{},
                                 4,
                                 -1);

    QVERIFY(controller.m_playbackSegments.isEmpty());

    backend.emitRunningState(false);
    QCoreApplication::processEvents();

    QCOMPARE(controller.m_playbackSegments.size(), 1);
    QCOMPARE(controller.m_playMethod, QStringLiteral("directStream"));
    QVERIFY(controller.m_streamPinsAudioTrack);
    QCOMPARE(controller.m_pinnedAudioTrack, 4);
    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/item-2"));
}

void PlayerControllerAutoplayContextTest::explicitStopReportsFinalProgressAndStoppedOnce()
{
    ConfigManager config;
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.emitStopStateChangeSynchronously = false;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_mediaSourceId = QStringLiteral("media-source-1");
    controller.m_playSessionId = QStringLiteral("play-session-1");
    controller.m_selectedAudioTrack = 2;
    controller.m_selectedSubtitleTrack = 5;
    controller.m_duration = 100.0;
    controller.m_currentPosition = 42.5;
    controller.m_playbackState = PlayerController::Playing;

    controller.stop();

    QCOMPARE(playbackService.progressReports.size(), 1);
    QCOMPARE(playbackService.stoppedReports.size(), 1);
    const FakePlaybackService::Report progress = playbackService.progressReports.first();
    const FakePlaybackService::Report stopped = playbackService.stoppedReports.first();
    QCOMPARE(progress.itemId, QStringLiteral("item-1"));
    QCOMPARE(progress.positionMs, 42500LL);
    QCOMPARE(progress.mediaSourceId, QStringLiteral("media-source-1"));
    QCOMPARE(progress.audioStreamIndex, 2);
    QCOMPARE(progress.subtitleStreamIndex, 5);
    QCOMPARE(progress.playSessionId, QStringLiteral("play-session-1"));
    QVERIFY(progress.canSeek);
    QVERIFY(!progress.isPaused);
    QCOMPARE(stopped.itemId, progress.itemId);
    QCOMPARE(stopped.positionMs, progress.positionMs);
    QCOMPARE(stopped.mediaSourceId, progress.mediaSourceId);
    QCOMPARE(stopped.audioStreamIndex, progress.audioStreamIndex);
    QCOMPARE(stopped.subtitleStreamIndex, progress.subtitleStreamIndex);
    QCOMPARE(stopped.playSessionId, progress.playSessionId);
    QVERIFY(!stopped.isPaused);

    backend.emitRunningState(false);
    QCoreApplication::processEvents();

    QCOMPARE(playbackService.progressReports.size(), 1);
    QCOMPARE(playbackService.stoppedReports.size(), 1);
    QCOMPARE(controller.playbackState(), PlayerController::Idle);
}

void PlayerControllerAutoplayContextTest::explicitPausedStopReportsPausedState()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_mediaSourceId = QStringLiteral("media-source-1");
    controller.m_playSessionId = QStringLiteral("play-session-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 12.0;
    controller.m_playbackState = PlayerController::Paused;

    controller.stop();
    QCoreApplication::processEvents();

    QCOMPARE(playbackService.progressReports.size(), 1);
    QCOMPARE(playbackService.stoppedReports.size(), 1);
    QVERIFY(playbackService.progressReports.first().isPaused);
    QVERIFY(playbackService.stoppedReports.first().isPaused);
}

void PlayerControllerAutoplayContextTest::explicitMultipartStopReportsActiveSegmentContext()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("logical-item");
    controller.m_duration = 120.0;
    controller.m_currentPosition = 70.0;
    controller.m_segmentRelativePosition = 10.0;
    controller.m_playbackState = PlayerController::Playing;
    controller.m_activePlaybackSegmentIndex = 1;
    controller.m_playbackSegments = {
        QVariantMap{
            {QStringLiteral("itemId"), QStringLiteral("part-1")},
            {QStringLiteral("mediaSourceId"), QStringLiteral("part-1-source")},
            {QStringLiteral("playSessionId"), QStringLiteral("session-1")},
            {QStringLiteral("audioIndex"), 2},
            {QStringLiteral("subtitleIndex"), -1},
            {QStringLiteral("durationMs"), 60000LL}
        },
        QVariantMap{
            {QStringLiteral("itemId"), QStringLiteral("part-2")},
            {QStringLiteral("mediaSourceId"), QStringLiteral("part-2-source")},
            {QStringLiteral("playSessionId"), QStringLiteral("session-2")},
            {QStringLiteral("audioIndex"), 3},
            {QStringLiteral("subtitleIndex"), 7},
            {QStringLiteral("durationMs"), 60000LL}
        }
    };

    controller.stop();
    QCoreApplication::processEvents();

    QCOMPARE(playbackService.progressReports.size(), 1);
    QCOMPARE(playbackService.stoppedReports.size(), 1);
    const FakePlaybackService::Report progress = playbackService.progressReports.first();
    const FakePlaybackService::Report stopped = playbackService.stoppedReports.first();
    QCOMPARE(progress.itemId, QStringLiteral("part-2"));
    QCOMPARE(progress.positionMs, 10000LL);
    QCOMPARE(progress.mediaSourceId, QStringLiteral("part-2-source"));
    QCOMPARE(progress.playSessionId, QStringLiteral("session-2"));
    QCOMPARE(stopped.itemId, QStringLiteral("part-2"));
    QCOMPARE(stopped.positionMs, 10000LL);
    QCOMPARE(stopped.mediaSourceId, QStringLiteral("part-2-source"));
    QCOMPARE(stopped.playSessionId, QStringLiteral("session-2"));
    QCOMPARE(stopped.audioStreamIndex, 3);
    QCOMPARE(stopped.subtitleStreamIndex, 7);
}

void PlayerControllerAutoplayContextTest::playbackEndedUpgradesQueuedStopFinalization()
{
    ConfigManager config;
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.requestTerminalTransition(PlayerController::TerminalReason::Stop);
    QVERIFY(controller.m_terminalTransitionActive);
    controller.queueTerminalFinalization();
    QVERIFY(controller.m_terminalFinalizationQueued);

    controller.onPlaybackEnded();
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Idle);
    QVERIFY(!controller.m_terminalTransitionActive);
    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedContexts.first(),
             QStringLiteral("player:resolve:series-1:item-1"));
}

void PlayerControllerAutoplayContextTest::nextEpisodeNavigationKeepsAwaitingUntilQueuedDelivery()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.setAwaitingNextEpisodeResolution(true);

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);
    QSignalSpy awaitingSpy(&controller, &PlayerController::awaitingNextEpisodeResolutionChanged);

    const QVariantMap episodeData{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("name"), QStringLiteral("Episode 2")},
        {QStringLiteral("seriesName"), QStringLiteral("Series A")},
        {QStringLiteral("parentIndexNumber"), 1},
        {QStringLiteral("indexNumber"), 2}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QVariantMap, episodeData),
                                      Q_ARG(QString, QStringLiteral("player:resolve:series-1:item-1"))));

    QVERIFY(controller.awaitingNextEpisodeResolution());
    QCOMPARE(navigationSpy.count(), 0);

    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 1);
    QVERIFY(!controller.awaitingNextEpisodeResolution());
    QCOMPARE(awaitingSpy.count(), 1);
}

void PlayerControllerAutoplayContextTest::upNextIdleParkingInvalidatesQueuedDisplayRestore()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_awaitingNextEpisodeResolution = true;
    controller.m_deferredPostPlaybackDisplayRestorePending = true;
    controller.m_deferredPostPlaybackNeedsHdrRestore = true;
    controller.m_deferredPostPlaybackNeedsRefreshRestore = false;

    const quint64 generationBeforeIdle = controller.m_displayRestoreGeneration;

    controller.onEnterIdleState();

    QVERIFY(controller.m_displayRestoreGeneration > generationBeforeIdle);
    QVERIFY(!controller.m_deferredPostPlaybackDisplayRestorePending);
    QVERIFY(!controller.m_deferredPostPlaybackNeedsHdrRestore);
    QVERIFY(!controller.m_deferredPostPlaybackNeedsRefreshRestore);
}

void PlayerControllerAutoplayContextTest::deferredPostPlaybackDisplayRestoreCanBeReleased()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_deferredPostPlaybackDisplayRestorePending = true;
    controller.m_deferredPostPlaybackNeedsHdrRestore = true;
    controller.m_deferredPostPlaybackNeedsRefreshRestore = true;
    const quint64 generationBeforeRelease = controller.m_displayRestoreGeneration;

    controller.releaseDeferredPostPlaybackDisplayRestore();

    QVERIFY(!controller.m_deferredPostPlaybackDisplayRestorePending);
    QVERIFY(!controller.m_deferredPostPlaybackNeedsHdrRestore);
    QVERIFY(!controller.m_deferredPostPlaybackNeedsRefreshRestore);
    QVERIFY(controller.m_displayRestoreGeneration > generationBeforeRelease);
}

void PlayerControllerAutoplayContextTest::nextEpisodeNavigationUsesPendingTrackContext()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.m_pendingAutoplayAudioTrack = 3;
    controller.m_pendingAutoplaySubtitleTrack = 6;
    controller.m_pendingAutoplaySubtitleMode = TrackPreferenceMode::ExplicitStream;

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);

    const QVariantMap episodeData{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("name"), QStringLiteral("Episode 2")},
        {QStringLiteral("seriesName"), QStringLiteral("Series A")},
        {QStringLiteral("parentIndexNumber"), 1},
        {QStringLiteral("indexNumber"), 2}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QVariantMap, episodeData),
                                      Q_ARG(QString, QStringLiteral("player:resolve:series-1:item-1"))));
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 1);
    const QList<QVariant> signalArgs = navigationSpy.takeFirst();
    QCOMPARE(signalArgs.at(1).toString(), QStringLiteral("series-1"));
    QCOMPARE(signalArgs.at(2).toInt(), 3);
    QCOMPARE(signalArgs.at(3).toInt(), 6);

    QVERIFY(!controller.m_shouldAutoplay);
    // Pending autoplay context is intentionally kept until playNextEpisode() consumes it.
    QCOMPARE(controller.m_pendingAutoplayItemId, QStringLiteral("item-1"));
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, 3);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, 6);
}

void PlayerControllerAutoplayContextTest::nextEpisodeIgnoresMismatchedSeries()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.m_pendingAutoplayAudioTrack = 4;
    controller.m_pendingAutoplaySubtitleTrack = 7;

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);

    const QVariantMap episodeData{
        {QStringLiteral("itemId"), QStringLiteral("episode-x")},
        {QStringLiteral("name"), QStringLiteral("Episode X")},
        {QStringLiteral("seriesName"), QStringLiteral("Unexpected Series")},
        {QStringLiteral("parentIndexNumber"), 1},
        {QStringLiteral("indexNumber"), 9}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("series-other")),
                                      Q_ARG(QVariantMap, episodeData),
                                      Q_ARG(QString, QStringLiteral("player:resolve:series-1:item-1"))));
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, 4);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, 7);
}

void PlayerControllerAutoplayContextTest::nextEpisodeIgnoresMismatchedConnection()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    libraryService.activeConnectionIdValue = QStringLiteral("connection-a");
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);
    const QVariantMap episodeData{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("name"), QStringLiteral("Episode 2")},
        {QStringLiteral("seriesName"), QStringLiteral("Series A")},
        {QStringLiteral("parentIndexNumber"), 1},
        {QStringLiteral("indexNumber"), 2}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("connection-b")),
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QVariantMap, episodeData),
                                      Q_ARG(QString, QStringLiteral("player:resolve:series-1:item-1"))));
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QVERIFY(controller.m_waitingForNextEpisodeAtPlaybackEnd);
}

void PlayerControllerAutoplayContextTest::playbackPrefetchIgnoresGenericNextEpisodeResponses()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(false);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_currentItemId = QStringLiteral("s1e22");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.maybeTriggerNextEpisodePrefetch();

    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedExcludeIds.first(), QStringLiteral("s1e22"));
    QCOMPARE(libraryService.requestedContexts.first(),
             QStringLiteral("player:prefetch:series-1:s1e22"));

    const QVariantMap specialEpisode{
        {QStringLiteral("itemId"), QStringLiteral("special-s0e1")},
        {QStringLiteral("name"), QStringLiteral("Special 1")},
        {QStringLiteral("seriesName"), QStringLiteral("Series A")},
        {QStringLiteral("parentIndexNumber"), 0},
        {QStringLiteral("indexNumber"), 1},
        {QStringLiteral("parentId"), QStringLiteral("season-0")}
    };
    const QVariantMap genericEpisode{
        {QStringLiteral("itemId"), QStringLiteral("s2e1")},
        {QStringLiteral("name"), QStringLiteral("Episode 1")},
        {QStringLiteral("seriesName"), QStringLiteral("Series A")},
        {QStringLiteral("parentIndexNumber"), 2},
        {QStringLiteral("indexNumber"), 1},
        {QStringLiteral("parentId"), QStringLiteral("season-2")}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QVariantMap, specialEpisode),
                                      Q_ARG(QString, QStringLiteral("player:prefetch:series-1:s1e22"))));
    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QVariantMap, genericEpisode),
                                      Q_ARG(QString, QStringLiteral(""))));

    controller.m_hasEvaluatedCompletionForAttempt = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("s1e22");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.m_pendingAutoplayAudioTrack = 2;
    controller.m_pendingAutoplaySubtitleTrack = 5;
    controller.m_pendingAutoplaySubtitleMode = TrackPreferenceMode::ExplicitStream;
    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);
    controller.consumePrefetchedNextEpisodeAndNavigate();
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 1);
    const QList<QVariant> signalArgs = navigationSpy.takeFirst();
    const QVariantMap navigatedEpisode = signalArgs.at(0).toMap();
    QCOMPARE(navigatedEpisode.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("special-s0e1"));
    QCOMPARE(signalArgs.at(1).toString(), QStringLiteral("series-1"));
}

void PlayerControllerAutoplayContextTest::autoplayPlaybackInfoErrorFallsBackToBasicPlayback()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-9");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-9");
    controller.m_pendingAutoplayLibraryId = QStringLiteral("library-9");
    controller.m_pendingAutoplayFramerate = 23.976;
    controller.m_pendingAutoplayIsHDR = true;
    controller.m_pendingAutoplayToneMapToSdr = true;
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-9")},
        {QStringLiteral("parentId"), QStringLiteral("season-9")},
        {QStringLiteral("positionMs"), 420'000}
    };
    controller.m_waitingForAutoplayPlaybackInfo = true;

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackServiceErrorOccurred",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("getPlaybackInfo")),
                                      Q_ARG(QString, QStringLiteral("request failed"))));

    QVERIFY(!controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(controller.m_currentItemId, QStringLiteral("episode-9"));
    QCOMPARE(controller.m_currentSeriesId, QStringLiteral("series-9"));
    QCOMPARE(controller.m_currentSeasonId, QStringLiteral("season-9"));
    QCOMPARE(controller.m_currentLibraryId, QStringLiteral("library-9"));
    QCOMPARE(controller.m_pendingUrl, QStringLiteral("https://example.invalid/episode-9"));
    QCOMPARE(controller.m_startPositionMs, 420000LL);
    QVERIFY(controller.m_contentShouldToneMapToSdr);
}

void PlayerControllerAutoplayContextTest::autoplayToneMapStateSurvivesPlaybackInfoFallback()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-dv");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-dv");
    controller.m_pendingAutoplayLibraryId = QStringLiteral("library-dv");
    controller.m_pendingAutoplayFramerate = 24.0;
    controller.m_pendingAutoplayIsHDR = true;
    controller.m_pendingAutoplayToneMapToSdr = true;
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-dv")},
        {QStringLiteral("parentId"), QStringLiteral("season-dv")}
    };

    controller.fallbackToPendingAutoplayPlayback();

    QCOMPARE(controller.m_currentItemId, QStringLiteral("episode-dv"));
    QVERIFY(controller.m_contentIsHDR);
    QVERIFY(controller.m_contentShouldToneMapToSdr);
}

void PlayerControllerAutoplayContextTest::displayHdrPolicyDoesNotToggleWhenToneMappingToSdr()
{
    ConfigManager config;
    config.setEnableHDR(true);
    config.setHDROutputMode(QStringLiteral("match-content"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_contentIsHDR = true;
    controller.m_contentShouldToneMapToSdr = true;

    const PlayerController::HdrPlaybackPolicy policy = controller.computeEffectiveHdrPlaybackPolicy();
    QVERIFY(policy.toneMapToSdr);
    QVERIFY(!policy.outputHdr);
    QVERIFY(!policy.shouldToggleDisplayHdr);
}

void PlayerControllerAutoplayContextTest::dolbyVisionWithHdr10BaseLayerDoesNotForceToneMap()
{
    ConfigManager config;
    config.setEnableHDR(true);
    config.setHDROutputMode(QStringLiteral("match-content"));
    config.setDolbyVisionFallbackMode(QStringLiteral("prefer-compatible-hdr"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("media-source-dv-hdr10"),
        QStringLiteral("DV HDR10"),
        QStringLiteral("/library/dv-hdr10.mkv"),
        {
            QVariantMap{
                {QStringLiteral("index"), 0},
                {QStringLiteral("type"), QStringLiteral("Video")},
                {QStringLiteral("codec"), QStringLiteral("hevc")},
                {QStringLiteral("codecTagString"), QStringLiteral("dovi")},
                {QStringLiteral("videoRange"), QStringLiteral("HDR")},
                {QStringLiteral("videoRangeType"), QStringLiteral("HDR10")},
                {QStringLiteral("dolbyVisionProfile"), 5}
            },
            QVariantMap{
                {QStringLiteral("index"), 1},
                {QStringLiteral("type"), QStringLiteral("Audio")},
                {QStringLiteral("codec"), QStringLiteral("aac")},
                {QStringLiteral("isDefault"), true}
            }
        },
        1);
    const PlaybackInfoResponse playbackInfo = buildPlaybackInfo({mediaSource});
    const QVariantList mediaSources = playbackInfo.getMediaSourcesVariant();

    const QVariantMap context = controller.resolveSegmentPlaybackContext(QStringLiteral("item-dv-hdr10"),
                                                                         true,
                                                                         playbackInfo,
                                                                         mediaSources,
                                                                         mediaSources.first().toMap(),
                                                                         -2,
                                                                         -2,
                                                                         false);

    QVERIFY(!context.isEmpty());
    QVERIFY(context.value(QStringLiteral("isHDR")).toBool());
    QCOMPARE(context.value(QStringLiteral("hdrKind")).toString(), QStringLiteral("dolby-vision-compatible"));
    QVERIFY(!context.value(QStringLiteral("toneMapToSdr")).toBool());

    const MediaSourceInfo jellyfinNumericSource = MediaSourceInfo::fromJson(QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("media-source-dv-p7-json")},
        {QStringLiteral("Name"), QStringLiteral("Obsession (2026) [DV HDR10]")},
        {QStringLiteral("Path"), QStringLiteral("/library/Obsession (2026) [DV HDR10].mkv")},
        {QStringLiteral("Container"), QStringLiteral("mkv")},
        {QStringLiteral("DefaultAudioStreamIndex"), 1},
        {QStringLiteral("MediaStreams"), QJsonArray{
            QJsonObject{
                {QStringLiteral("Index"), 0},
                {QStringLiteral("Type"), 1},
                {QStringLiteral("Codec"), QStringLiteral("hevc")},
                {QStringLiteral("Profile"), QStringLiteral("Main 10")},
                {QStringLiteral("DisplayTitle"), QStringLiteral("4K HEVC Dolby Vision Profile 7.6 (HDR10)")},
                {QStringLiteral("VideoRange"), 2},
                {QStringLiteral("VideoRangeType"), 7},
                {QStringLiteral("DvProfile"), 7},
                {QStringLiteral("DvLevel"), 6},
                {QStringLiteral("DvBlSignalCompatibilityId"), 6},
                {QStringLiteral("VideoDoViTitle"), QStringLiteral("Dolby Vision Profile 7.6 (HDR10)")}
            },
            QJsonObject{
                {QStringLiteral("Index"), 1},
                {QStringLiteral("Type"), 0},
                {QStringLiteral("Codec"), QStringLiteral("truehd")},
                {QStringLiteral("IsDefault"), true}
            }
        }}
    });
    const PlaybackInfoResponse jellyfinNumericPlaybackInfo = buildPlaybackInfo({jellyfinNumericSource});
    const QVariantList jellyfinNumericMediaSources = jellyfinNumericPlaybackInfo.getMediaSourcesVariant();
    const QVariantMap jellyfinNumericContext =
        controller.resolveSegmentPlaybackContext(QStringLiteral("item-dv-p7-json"),
                                                 true,
                                                 jellyfinNumericPlaybackInfo,
                                                 jellyfinNumericMediaSources,
                                                 jellyfinNumericMediaSources.first().toMap(),
                                                 -2,
                                                 -2,
                                                 false);

    QVERIFY(!jellyfinNumericContext.isEmpty());
    QVERIFY(jellyfinNumericContext.value(QStringLiteral("isHDR")).toBool());
    QCOMPARE(jellyfinNumericContext.value(QStringLiteral("hdrKind")).toString(),
             QStringLiteral("dolby-vision-compatible"));
    QVERIFY(!jellyfinNumericContext.value(QStringLiteral("toneMapToSdr")).toBool());

    const MediaSourceInfo filenameOnlySource = buildMediaSourceInfo(
        QStringLiteral("media-source-dv-hdr10-name"),
        QStringLiteral("Obsession (2026) [Remux-2160p][DV HDR10][HEVC]"),
        QStringLiteral("/library/Obsession (2026) [Remux-2160p][DV HDR10][HEVC].mkv"),
        {
            QVariantMap{
                {QStringLiteral("index"), 0},
                {QStringLiteral("type"), QStringLiteral("Video")},
                {QStringLiteral("codec"), QStringLiteral("hevc")},
                {QStringLiteral("displayTitle"), QStringLiteral("4K HEVC Dolby Vision")},
                {QStringLiteral("dolbyVisionProfile"), 5}
            },
            QVariantMap{
                {QStringLiteral("index"), 1},
                {QStringLiteral("type"), QStringLiteral("Audio")},
                {QStringLiteral("codec"), QStringLiteral("aac")},
                {QStringLiteral("isDefault"), true}
            }
        },
        1);
    const PlaybackInfoResponse filenameOnlyPlaybackInfo = buildPlaybackInfo({filenameOnlySource});
    const QVariantList filenameOnlyMediaSources = filenameOnlyPlaybackInfo.getMediaSourcesVariant();
    const QVariantMap filenameOnlyContext =
        controller.resolveSegmentPlaybackContext(QStringLiteral("item-dv-hdr10-name"),
                                                 true,
                                                 filenameOnlyPlaybackInfo,
                                                 filenameOnlyMediaSources,
                                                 filenameOnlyMediaSources.first().toMap(),
                                                 -2,
                                                 -2,
                                                 false);

    QVERIFY(!filenameOnlyContext.isEmpty());
    QVERIFY(filenameOnlyContext.value(QStringLiteral("isHDR")).toBool());
    QCOMPARE(filenameOnlyContext.value(QStringLiteral("hdrKind")).toString(), QStringLiteral("dolby-vision-compatible"));
    QVERIFY(!filenameOnlyContext.value(QStringLiteral("toneMapToSdr")).toBool());
}

void PlayerControllerAutoplayContextTest::autoplayPlaybackInfoUsesStoredSubtitlePreferenceWhenOverrideUnset()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    ScopedTrackPreferences targetSeasonPreferences;
    targetSeasonPreferences.subtitle.mode = TrackPreferenceMode::ExplicitStream;
    targetSeasonPreferences.subtitle.streamIndex = 12;
    trackPrefs.setSeasonPreferences(QStringLiteral("season-target"), targetSeasonPreferences);

    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-11");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-target");
    controller.m_pendingAutoplayLibraryId = QStringLiteral("library-11");
    controller.m_pendingAutoplayAudioTrack = 4;
    controller.m_pendingAutoplaySubtitleTrack = -1;
    controller.m_pendingAutoplaySubtitleMode = TrackPreferenceMode::Unset;
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-11")},
        {QStringLiteral("seasonId"), QStringLiteral("season-target")}
    };
    controller.m_waitingForAutoplayPlaybackInfo = true;

    PlaybackInfoResponse playbackInfo;
    playbackInfo.playSessionId = QStringLiteral("play-session-11");
    MediaSourceInfo mediaSource;
    mediaSource.id = QStringLiteral("media-source-11");
    mediaSource.mediaStreams = {
        MediaStreamInfo{.index = 4, .type = QStringLiteral("Audio")},
        MediaStreamInfo{.index = 12, .type = QStringLiteral("Subtitle")}
    };
    playbackInfo.mediaSources.append(mediaSource);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackInfoLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("episode-11")),
                                      Q_ARG(PlaybackInfoResponse, playbackInfo)));

    QVERIFY(!controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(controller.m_currentSeriesId, QStringLiteral("series-11"));
    QCOMPARE(controller.m_currentLibraryId, QStringLiteral("library-11"));
    QCOMPARE(controller.selectedSubtitleTrack(), 12);
    QCOMPARE(controller.m_mediaSourceId, QStringLiteral("media-source-11"));
    QCOMPARE(controller.m_playSessionId, QStringLiteral("play-session-11"));
}

void PlayerControllerAutoplayContextTest::explicitSeasonPreferencesBeatGlobalTrackDefaults()
{
    ConfigManager config;
    config.setDefaultAudioTrackSelection(QStringLiteral("jpn"));
    config.setDefaultSubtitleTrackSelection(QStringLiteral("spa"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    ScopedTrackPreferences preferences;
    preferences.audio.mode = TrackPreferenceMode::ExplicitStream;
    preferences.audio.streamIndex = 2;
    preferences.subtitle.mode = TrackPreferenceMode::ExplicitStream;
    preferences.subtitle.streamIndex = 12;
    trackPrefs.setSeasonPreferences(QStringLiteral("season-override"), preferences);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}, {QStringLiteral("language"), QStringLiteral("eng")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 4}, {QStringLiteral("language"), QStringLiteral("jpn")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 12}, {QStringLiteral("language"), QStringLiteral("eng")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 14}, {QStringLiteral("language"), QStringLiteral("spa")}}
        }),
        QStringLiteral("season-override"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("audioIndex")).toInt(), 2);
    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), 12);
    QCOMPARE(resolved.value(QStringLiteral("audioSource")).toString(), QStringLiteral("explicit"));
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("explicit"));
}

void PlayerControllerAutoplayContextTest::globalAudioLanguageSelectsMatchingStream()
{
    ConfigManager config;
    config.setDefaultAudioTrackSelection(QStringLiteral("jpn"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}, {QStringLiteral("language"), QStringLiteral("eng")}, {QStringLiteral("isDefault"), true}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}, {QStringLiteral("language"), QStringLiteral("jpn")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 3}, {QStringLiteral("language"), QStringLiteral("ja")}, {QStringLiteral("isDefault"), true}}
        }, 1),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("audioIndex")).toInt(), 3);
    QCOMPARE(resolved.value(QStringLiteral("audioSource")).toString(), QStringLiteral("global-language"));
}

void PlayerControllerAutoplayContextTest::configTrackLanguageSelectionCanonicalizesAliases()
{
    ConfigManager config;

    config.setDefaultAudioTrackSelection(QStringLiteral("en"));
    config.setDefaultSubtitleTrackSelection(QStringLiteral("es"));

    QCOMPARE(config.getDefaultAudioTrackSelection(), QStringLiteral("eng"));
    QCOMPARE(config.getDefaultSubtitleTrackSelection(), QStringLiteral("spa"));
}

void PlayerControllerAutoplayContextTest::globalSubtitleLanguageSelectsMatchingStream()
{
    ConfigManager config;
    config.setDefaultSubtitleTrackSelection(QStringLiteral("spa"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 5}, {QStringLiteral("language"), QStringLiteral("eng")}, {QStringLiteral("isDefault"), true}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 6}, {QStringLiteral("language"), QStringLiteral("spa")}, {QStringLiteral("isForced"), true}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 7}, {QStringLiteral("language"), QStringLiteral("es")}}
        }, 1, 5),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), 7);
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("global-language"));
}

void PlayerControllerAutoplayContextTest::globalFileDefaultPrefersFileFlagOverJellyfinDefault()
{
    ConfigManager config;
    config.setDefaultAudioTrackSelection(QStringLiteral("file-default"));
    config.setDefaultSubtitleTrackSelection(QStringLiteral("file-default"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}, {QStringLiteral("isDefault"), true}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 11}, {QStringLiteral("isDefault"), true}}
        }, 1, 10),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("audioIndex")).toInt(), 2);
    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), 11);
    QCOMPARE(resolved.value(QStringLiteral("audioSource")).toString(), QStringLiteral("file-default"));
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("file-default"));
}

void PlayerControllerAutoplayContextTest::globalSubtitleOffDisablesSubtitles()
{
    ConfigManager config;
    config.setDefaultSubtitleTrackSelection(QStringLiteral("off"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}, {QStringLiteral("isDefault"), true}}
        }, 1, 10),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), -1);
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("global-off"));
}

void PlayerControllerAutoplayContextTest::globalSubtitleForcedSelectsForcedTrack()
{
    ConfigManager config;
    config.setDefaultSubtitleTrackSelection(QStringLiteral("forced"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}, {QStringLiteral("isDefault"), true}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 11}, {QStringLiteral("isForced"), true}}
        }, 1, 10),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), 11);
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("global-forced"));
}

void PlayerControllerAutoplayContextTest::globalSubtitleForcedFallsBackToOffWhenNoForcedTrackExists()
{
    ConfigManager config;
    config.setDefaultSubtitleTrackSelection(QStringLiteral("forced"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}, {QStringLiteral("isDefault"), true}}
        }, 1, 10),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), -1);
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("global-forced-off"));
}

void PlayerControllerAutoplayContextTest::unavailableGlobalLanguageFallsBackToJellyfinDefault()
{
    ConfigManager config;
    config.setDefaultAudioTrackSelection(QStringLiteral("jpn"));
    config.setDefaultSubtitleTrackSelection(QStringLiteral("spa"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager, &playbackService, &libraryService, &authService);

    const QVariantMap resolved = controller.resolveTrackSelectionForMediaSource(
        buildMediaSource({
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}, {QStringLiteral("language"), QStringLiteral("eng")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}, {QStringLiteral("language"), QStringLiteral("eng")}}
        }, 1, 10),
        QStringLiteral("season-global"),
        false);

    QCOMPARE(resolved.value(QStringLiteral("audioIndex")).toInt(), 1);
    QCOMPARE(resolved.value(QStringLiteral("subtitleIndex")).toInt(), 10);
    QCOMPARE(resolved.value(QStringLiteral("audioSource")).toString(), QStringLiteral("jellyfin-default"));
    QCOMPARE(resolved.value(QStringLiteral("subtitleSource")).toString(), QStringLiteral("jellyfin-default"));
}

void PlayerControllerAutoplayContextTest::autoplayPlaybackInfoUsesGlobalSubtitlePreferenceWhenOverrideAndSeasonUnset()
{
    ConfigManager config;
    config.setDefaultSubtitleTrackSelection(QStringLiteral("spa"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-12");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-global");
    controller.m_pendingAutoplayLibraryId = QStringLiteral("library-12");
    controller.m_pendingAutoplayAudioTrack = 1;
    controller.m_pendingAutoplaySubtitleTrack = -1;
    controller.m_pendingAutoplaySubtitleMode = TrackPreferenceMode::Unset;
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-12")},
        {QStringLiteral("seasonId"), QStringLiteral("season-global")}
    };
    controller.m_waitingForAutoplayPlaybackInfo = true;

    PlaybackInfoResponse playbackInfo;
    playbackInfo.playSessionId = QStringLiteral("play-session-12");
    MediaSourceInfo mediaSource;
    mediaSource.id = QStringLiteral("media-source-12");
    mediaSource.mediaStreams = {
        MediaStreamInfo{.index = 1, .type = QStringLiteral("Audio")},
        MediaStreamInfo{.index = 20, .type = QStringLiteral("Subtitle"), .language = QStringLiteral("eng")},
        MediaStreamInfo{.index = 21, .type = QStringLiteral("Subtitle"), .language = QStringLiteral("spa")}
    };
    playbackInfo.mediaSources.append(mediaSource);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackInfoLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("episode-12")),
                                      Q_ARG(PlaybackInfoResponse, playbackInfo)));

    QCOMPARE(controller.selectedSubtitleTrack(), 21);
    QCOMPARE(controller.m_mediaSourceId, QStringLiteral("media-source-12"));
    QCOMPARE(controller.m_playSessionId, QStringLiteral("play-session-12"));
}

void PlayerControllerAutoplayContextTest::staleAutoplayPlaybackInfoResponseFallsBackAfterTimeout()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-10");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-10");
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-10")},
        {QStringLiteral("seasonId"), QStringLiteral("season-10")}
    };
    controller.m_waitingForAutoplayPlaybackInfo = true;

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackInfoLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("stale-episode")),
                                      Q_ARG(PlaybackInfoResponse, PlaybackInfoResponse{})));
    QVERIFY(controller.m_waitingForAutoplayPlaybackInfo);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onAutoplayPlaybackInfoTimeout",
                                      Qt::DirectConnection));

    QVERIFY(!controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(controller.m_currentItemId, QStringLiteral("episode-10"));
    QCOMPARE(controller.m_currentSeriesId, QStringLiteral("series-10"));
    QCOMPARE(controller.m_currentSeasonId, QStringLiteral("season-10"));
    QCOMPARE(controller.m_pendingUrl, QStringLiteral("https://example.invalid/episode-10"));
}

void PlayerControllerAutoplayContextTest::playUrlWithTracksKeepsNewSessionMetadataWhenReplacingPlayback()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    backend.startMpv(QString(), QStringList{}, QString());
    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("old-item");
    controller.m_currentSeriesId = QStringLiteral("old-series");
    controller.m_currentSeasonId = QStringLiteral("old-season");
    controller.m_currentLibraryId = QStringLiteral("old-library");
    controller.m_mediaSourceId = QStringLiteral("old-media-source");
    controller.m_playSessionId = QStringLiteral("old-play-session");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 10.0;

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 3}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 6}}
    });

    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/new-item"),
                                 QStringLiteral("new-item"),
                                 0,
                                 QStringLiteral("new-series"),
                                 QStringLiteral("new-season"),
                                 QStringLiteral("new-library"),
                                 QStringLiteral("new-media-source"),
                                 QStringLiteral("new-play-session"),
                                 mediaSource,
                                 3,
                                 6);

    QCOMPARE(controller.m_currentItemId, QStringLiteral("new-item"));
    QCOMPARE(controller.m_currentSeriesId, QStringLiteral("new-series"));
    QCOMPARE(controller.m_currentSeasonId, QStringLiteral("new-season"));
    QCOMPARE(controller.m_currentLibraryId, QStringLiteral("new-library"));
    QCOMPARE(controller.m_mediaSourceId, QStringLiteral("new-media-source"));
    QCOMPARE(controller.m_playSessionId, QStringLiteral("new-play-session"));
    QCOMPARE(controller.selectedAudioTrack(), 3);
    QCOMPARE(controller.selectedSubtitleTrack(), 6);
    QCOMPARE(controller.m_activeMediaSource.value(QStringLiteral("id")).toString(), QStringLiteral("media-source-1"));
}

void PlayerControllerAutoplayContextTest::embeddedVideoShrinkToggleEmitsAndPersists()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    QSignalSpy shrinkSpy(&controller, &PlayerController::embeddedVideoShrinkEnabledChanged);

    QVERIFY(!controller.embeddedVideoShrinkEnabled());
    controller.setEmbeddedVideoShrinkEnabled(true);
    QVERIFY(controller.embeddedVideoShrinkEnabled());
    QCOMPARE(shrinkSpy.count(), 1);

    // idempotent set should not emit
    controller.setEmbeddedVideoShrinkEnabled(true);
    QCOMPARE(shrinkSpy.count(), 1);

    controller.setEmbeddedVideoShrinkEnabled(false);
    QVERIFY(!controller.embeddedVideoShrinkEnabled());
    QCOMPARE(shrinkSpy.count(), 2);
}

void PlayerControllerAutoplayContextTest::playbackTogglesSendMpvCycleValuesCommands()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;

    controller.toggleSubtitleAssOverride();
    controller.toggleDeband();

    QCOMPARE(backend.commands.size(), 2);
    QCOMPARE(backend.commands.at(0),
             QStringList({QStringLiteral("cycle-values"),
                          QStringLiteral("sub-ass-override"),
                          QStringLiteral("no"),
                          QStringLiteral("yes")}));
    QCOMPARE(backend.commands.at(1),
             QStringList({QStringLiteral("cycle-values"),
                          QStringLiteral("deband"),
                          QStringLiteral("no"),
                          QStringLiteral("yes")}));
}

void PlayerControllerAutoplayContextTest::requestPlaybackPromptsForVersionSelection()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    QSignalSpy selectionSpy(&controller, &PlayerController::playbackVersionSelectionRequested);
    authService.restoreSession(QStringLiteral("https://example.invalid"),
                               QStringLiteral("user-1"),
                               QStringLiteral("token-1"));

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("allowVersionPrompt"), true}
    });
    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"), QStringLiteral("library-1"));

    const MediaSourceInfo versionA = buildMediaSourceInfo(
        QStringLiteral("source-1080p"),
        QStringLiteral("1080p"),
        QStringLiteral("/library/show/1080p/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);
    const MediaSourceInfo versionB = buildMediaSourceInfo(
        QStringLiteral("source-4k"),
        QStringLiteral("4K"),
        QStringLiteral("/library/show/4k/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 3840},
            {QStringLiteral("height"), 2160},
            {QStringLiteral("codec"), QStringLiteral("hevc")},
            {QStringLiteral("profile"), QStringLiteral("Main 10")},
            {QStringLiteral("videoRange"), QStringLiteral("HDR10")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        22000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({versionA, versionB}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QCOMPARE(selectionSpy.count(), 1);
    const QList<QVariant> arguments = selectionSpy.takeFirst();
    const QVariantMap dialogModel = arguments.at(1).toMap();
    const QVariantList options = dialogModel.value(QStringLiteral("options")).toList();
    QCOMPARE(dialogModel.value(QStringLiteral("title")).toString(), QStringLiteral("Select Version"));
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("source-1080p"));
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("source-4k"));
}

void PlayerControllerAutoplayContextTest::requestPlaybackRecoversLibraryProfileFromSeriesDetails()
{
    ConfigManager config;
    config.setMpvProfile(QStringLiteral("Library Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-library-profile=yes")}}
    });
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Library Preferred"));

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("libraryId"), QStringLiteral("")},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    QCOMPARE(libraryService.requestedLibraryResolutionIds, QStringList{QStringLiteral("series-1")});

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("source-1"),
        QStringLiteral("Default"),
        QStringLiteral("/library/show/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QVERIFY(backend.lastStartArgs.isEmpty());
    QVERIFY(backend.lastStartUrl.isEmpty());

    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
}

void PlayerControllerAutoplayContextTest::remoteMountStartupBufferingAppendsMpvArgs()
{
    ConfigManager config;
    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"), QStringLiteral("remote-mount"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.playUrl(QStringLiteral("https://example.invalid/video.mkv"),
                       QStringLiteral("episode-1"),
                       0,
                       QStringLiteral("series-1"),
                       QStringLiteral("season-1"),
                       QStringLiteral("library-1"));

    QTRY_VERIFY(backend.lastStartArgs.contains(QStringLiteral("--cache-pause-initial=yes")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--pause=yes")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--cache-pause-wait=60")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--cache-secs=120")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--demuxer-readahead-secs=60")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--demuxer-max-bytes=2048M")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--demuxer-max-back-bytes=512M")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--stream-buffer-size=4MiB")));

    backend.emitCacheEnd(59.0);
    QVERIFY(backend.variantCommands.isEmpty());

    backend.emitCacheEnd(60.0);
    QTRY_VERIFY(!backend.variantCommands.isEmpty());
    QCOMPARE(backend.variantCommands.last(), QVariantList({
        QStringLiteral("set_property"),
        QStringLiteral("pause"),
        false,
    }));
    QVERIFY(!controller.m_waitingForRemoteMountInitialCache);
}

void PlayerControllerAutoplayContextTest::remoteMountStartupCacheReadyWhileBufferingUnpausesAndPlays()
{
    ConfigManager config;
    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"), QStringLiteral("remote-mount"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.playUrl(QStringLiteral("https://example.invalid/video.mkv"),
                       QStringLiteral("episode-1"),
                       0,
                       QStringLiteral("series-1"),
                       QStringLiteral("season-1"),
                       QStringLiteral("library-1"));

    QTRY_VERIFY(backend.lastStartArgs.contains(QStringLiteral("--pause=yes")));
    QVERIFY(controller.isStartupBuffering());

    backend.emitPosition(0.0);
    QCOMPARE(controller.playbackState(), PlayerController::Buffering);
    QVERIFY(controller.isStartupBuffering());
    backend.variantCommands.clear();

    backend.emitCacheEnd(60.0);

    QTRY_COMPARE(controller.playbackState(), PlayerController::Playing);
    QVERIFY(!controller.m_waitingForRemoteMountInitialCache);
    QVERIFY(!controller.isStartupBuffering());
    QVERIFY(backend.variantCommands.contains(QVariantList({
        QStringLiteral("set_property"),
        QStringLiteral("pause"),
        false,
    })));
}

void PlayerControllerAutoplayContextTest::remoteMountPausedForCacheReleaseUnpausesManualPause()
{
    ConfigManager config;
    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"), QStringLiteral("remote-mount"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.playUrl(QStringLiteral("https://example.invalid/video.mkv"),
                       QStringLiteral("episode-1"),
                       0,
                       QStringLiteral("series-1"),
                       QStringLiteral("season-1"),
                       QStringLiteral("library-1"));

    QTRY_VERIFY(backend.lastStartArgs.contains(QStringLiteral("--cache-pause-initial=yes")));
    QVERIFY(controller.m_waitingForRemoteMountInitialCache);
    backend.variantCommands.clear();

    backend.emitPausedForCache(false);

    QTRY_VERIFY(!backend.variantCommands.isEmpty());
    QCOMPARE(backend.variantCommands.last(), QVariantList({
        QStringLiteral("set_property"),
        QStringLiteral("pause"),
        false,
    }));
    QVERIFY(!controller.m_waitingForRemoteMountInitialCache);
    QCOMPARE(controller.playbackState(), PlayerController::Loading);
}

void PlayerControllerAutoplayContextTest::requestPlaybackWaitsForSeriesDetailsParentIdBeforeStarting()
{
    ConfigManager config;
    config.setMpvProfile(QStringLiteral("Library Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-library-profile=yes")}}
    });
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Library Preferred"));

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("source-1"),
        QStringLiteral("Default"),
        QStringLiteral("/library/show/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(backend.lastStartArgs.isEmpty());

    emit libraryService.itemLibraryResolved(QStringLiteral("other-series"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(backend.lastStartArgs.isEmpty());

    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
}

void PlayerControllerAutoplayContextTest::requestPlaybackUsesRecoveredLibraryWhenSeriesDetailsArriveBeforePlaybackInfo()
{
    ConfigManager config;
    config.setMpvProfile(QStringLiteral("Library Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-library-profile=yes")}}
    });
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Library Preferred"));

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    QCOMPARE(libraryService.requestedLibraryResolutionIds, QStringList{QStringLiteral("series-1")});

    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(backend.lastStartArgs.isEmpty());

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("source-1"),
        QStringLiteral("Default"),
        QStringLiteral("/library/show/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
}

void PlayerControllerAutoplayContextTest::requestPlaybackFallsBackWithoutRecoveredLibraryProfileWhenParentIdMissing()
{
    ConfigManager config;
    config.setMpvProfile(QStringLiteral("Library Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-library-profile=yes")}}
    });
    config.setMpvProfile(QStringLiteral("Default Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-default-profile=yes")}}
    });
    config.setDefaultProfileName(QStringLiteral("Default Preferred"));
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Library Preferred"));

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("source-1"),
        QStringLiteral("Default"),
        QStringLiteral("/library/show/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QVERIFY(backend.lastStartUrl.isEmpty());

    emit libraryService.itemLibraryResolutionFailed(QStringLiteral("series-1"),
                                                    QStringLiteral("missing"));

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-default-profile=yes")));
    QVERIFY(!backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
}

void PlayerControllerAutoplayContextTest::requestPlaybackKeepsSeriesProfilePriorityOverRecoveredLibrary()
{
    ConfigManager config;
    config.setMpvProfile(QStringLiteral("Library Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-library-profile=yes")}}
    });
    config.setMpvProfile(QStringLiteral("Series Preferred"), QVariantMap{
        {QStringLiteral("hwdecEnabled"), false},
        {QStringLiteral("hwdecMethod"), QStringLiteral("")},
        {QStringLiteral("deinterlace"), false},
        {QStringLiteral("deinterlaceMethod"), QStringLiteral("")},
        {QStringLiteral("videoOutput"), QStringLiteral("gpu")},
        {QStringLiteral("interpolation"), false},
        {QStringLiteral("extraArgs"), QVariantList{QStringLiteral("--test-series-profile=yes")}}
    });
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("Library Preferred"));
    config.setSeriesProfile(QStringLiteral("series-1"), QStringLiteral("Series Preferred"));

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    const MediaSourceInfo mediaSource = buildMediaSourceInfo(
        QStringLiteral("source-1"),
        QStringLiteral("Default"),
        QStringLiteral("/library/show/episode-1.mkv"),
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("codec"), QStringLiteral("h264")},
            {QStringLiteral("profile"), QStringLiteral("High")},
            {QStringLiteral("videoRange"), QStringLiteral("SDR")}
        }},
        -1,
        -1,
        QStringLiteral("mkv"),
        8000000,
        1200000000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);
    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-series-profile=yes")));
    QVERIFY(!backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
}

void PlayerControllerAutoplayContextTest::explicitPlaybackIgnoresUnscopedStalePlaybackInfo()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();

    const MediaSourceInfo staleSource = buildMediaSourceInfo(
        QStringLiteral("stale-source"), QStringLiteral("Old"), QStringLiteral("/old/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    const MediaSourceInfo freshSource = buildMediaSourceInfo(
        QStringLiteral("fresh-source"), QStringLiteral("New"), QStringLiteral("/new/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"), buildPlaybackInfo({staleSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});
    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(playbackService.requestedDescriptorMediaSourceIds.isEmpty());

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({freshSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
             QStringList{QStringLiteral("fresh-source")});
}

void PlayerControllerAutoplayContextTest::explicitPlaybackIgnoresCanceledRequestScopedPlaybackInfo()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString oldRequestId = controller.m_pendingPlaybackRequests.constBegin().key();
    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString newRequestId = controller.m_pendingPlaybackRequests.constBegin().key();

    const MediaSourceInfo staleSource = buildMediaSourceInfo(
        QStringLiteral("stale-source"), QStringLiteral("Old"), QStringLiteral("/old/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    const MediaSourceInfo freshSource = buildMediaSourceInfo(
        QStringLiteral("fresh-source"), QStringLiteral("New"), QStringLiteral("/new/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({staleSource}),
                                                      oldRequestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, oldRequestId);
    QVERIFY(backend.lastStartUrl.isEmpty());

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({freshSource}),
                                                      newRequestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, newRequestId);

    QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
             QStringList{QStringLiteral("fresh-source")});
}

void PlayerControllerAutoplayContextTest::primaryPlaybackInfoFailureDoesNotUseBasicStreamFallback()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();

    emit playbackService.playbackInfoFailedForRequest(QStringLiteral("episode-1"),
                                                      QStringLiteral("media source missing"),
                                                      requestId);

    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(libraryService.requestedBasicStreamItemIds.isEmpty());
    QVERIFY(libraryService.requestedTrackStreamItemIds.isEmpty());
    QVERIFY(controller.hasError());
    QCOMPARE(controller.errorMessage(), QStringLiteral("media source missing"));
}

void PlayerControllerAutoplayContextTest::additionalPartsFailureStillStartsPrimary()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();

    const MediaSourceInfo primarySource = buildMediaSourceInfo(
        QStringLiteral("primary-source"), QStringLiteral("Primary"), QStringLiteral("/primary/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({primarySource}),
                                                      requestId);
    emit playbackService.additionalPartsFailedForRequest(QStringLiteral("episode-1"),
                                                         QStringLiteral("additional parts missing"),
                                                         requestId);

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
             QStringList{QStringLiteral("primary-source")});
    QVERIFY(backend.appendedUrls.isEmpty());
}

void PlayerControllerAutoplayContextTest::additionalPartPlaybackInfoFailureSkipsPart()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();

    const MediaSourceInfo primarySource = buildMediaSourceInfo(
        QStringLiteral("primary-source"), QStringLiteral("Primary"), QStringLiteral("/primary/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({primarySource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(
        QStringLiteral("episode-1"),
        QJsonArray{QJsonObject{{QStringLiteral("Id"), QStringLiteral("episode-1-part-2")}}},
        requestId);
    emit playbackService.playbackInfoFailedForRequest(QStringLiteral("episode-1-part-2"),
                                                      QStringLiteral("part missing"),
                                                      requestId);

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
             QStringList{QStringLiteral("primary-source")});
    QVERIFY(backend.appendedUrls.isEmpty());
}

void PlayerControllerAutoplayContextTest::retryRefreshesPlaybackInfoBeforeRestarting()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Error;
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/old-stream");
    controller.m_recoveryContext.valid = true;
    controller.m_recoveryContext.itemId = QStringLiteral("episode-1");
    controller.m_recoveryContext.url = QStringLiteral("https://example.invalid/old-stream");
    controller.m_recoveryContext.mediaSourceId = QStringLiteral("old-source");
    controller.m_recoveryContext.startPositionMs = 120000;
    controller.m_recoveryContext.audioStreamIndex = 1;
    controller.m_recoveryContext.subtitleStreamIndex = -1;

    controller.retry();

    QVERIFY(backend.lastStartUrl.isEmpty());
    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();

    const MediaSourceInfo freshSource = buildMediaSourceInfo(
        QStringLiteral("fresh-source"), QStringLiteral("New"), QStringLiteral("/new/file.mkv"),
        {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}},
        1,
        -1);
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({freshSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
    QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
             QStringList{QStringLiteral("fresh-source")});
    QCOMPARE(controller.m_startPositionMs, 120000LL);
}

void PlayerControllerAutoplayContextTest::recoverableBackendStreamErrorStartsRecovery()
{
    ConfigManager config;
    config.setAutoRecoverPlayback(true);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("episode-1");
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/episode-1");
    controller.m_mediaSourceId = QStringLiteral("media-source-1");
    controller.m_selectedAudioTrack = 1;
    controller.m_selectedSubtitleTrack = -1;
    controller.m_segmentRelativePosition = 5.75;

    controller.onProcessError(QStringLiteral("recoverable-stream-ended-prematurely: [error][ffmpeg] http: Stream ends prematurely"));

    QVERIFY(controller.m_lastErrorWasNetworkRecoverable);
    QVERIFY(controller.m_recoveryContext.valid);
    QCOMPARE(controller.m_recoveryContext.itemId, QStringLiteral("episode-1"));
    QCOMPARE(controller.m_recoveryContext.mediaSourceId, QStringLiteral("media-source-1"));
    QCOMPARE(controller.m_recoveryContext.startPositionMs, 5750LL);
}

void PlayerControllerAutoplayContextTest::recoverableErrorAfterBackendStopUpgradesTerminalTransition()
{
    ConfigManager config;
    config.setAutoRecoverPlayback(true);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("episode-1");
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/episode-1");
    controller.m_mediaSourceId = QStringLiteral("media-source-1");
    controller.m_selectedAudioTrack = 1;
    controller.m_selectedSubtitleTrack = -1;
    controller.m_segmentRelativePosition = 5.75;
    backend.setRunning(true);

    backend.emitRunningState(false);
    QVERIFY(controller.m_terminalTransitionActive);
    QCOMPARE(controller.m_pendingTerminalReason, PlayerController::TerminalReason::Stop);
    QVERIFY(controller.isPlaybackActive());

    controller.onProcessError(QStringLiteral("recoverable-stream-ended-prematurely: [error][ffmpeg] http: Stream ends prematurely"));

    QCOMPARE(controller.m_pendingTerminalReason, PlayerController::TerminalReason::Error);
    QVERIFY(controller.m_lastErrorWasNetworkRecoverable);
    QVERIFY(controller.m_recoveryContext.valid);
    QCOMPARE(controller.m_recoveryContext.itemId, QStringLiteral("episode-1"));
    QCOMPARE(controller.m_recoveryContext.mediaSourceId, QStringLiteral("media-source-1"));
    QCOMPARE(controller.m_recoveryContext.startPositionMs, 5750LL);
    QVERIFY(controller.isPlaybackActive());

    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Error);
    QVERIFY(controller.m_recoveryContext.valid);
    QVERIFY(controller.m_isRecovering);
    QVERIFY(controller.isPlaybackActive());
}

void PlayerControllerAutoplayContextTest::backendStopWithoutRecoverableErrorGoesIdle()
{
    ConfigManager config;
    config.setAutoRecoverPlayback(true);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("episode-1");
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/episode-1");
    backend.setRunning(true);

    backend.emitRunningState(false);
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Idle);
    QVERIFY(!controller.m_recoveryContext.valid);
    QVERIFY(!controller.m_isRecovering);
    QVERIFY(!controller.isPlaybackActive());
}

void PlayerControllerAutoplayContextTest::playbackInfoDirectStreamUrlIsPreferred()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    authService.restoreSession(QStringLiteral("https://jellyfin.example"),
                               QStringLiteral("user-1"),
                               QStringLiteral("token-1"));
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("preferredAudioIndex"), 3},
        {QStringLiteral("preferredSubtitleIndex"), -1},
        {QStringLiteral("allowVersionPrompt"), false}
    });

    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    const MediaSourceInfo source = buildMediaSourceInfo(
        QStringLiteral("media-source-1"),
        QStringLiteral("Hosted"),
        QStringLiteral("/hosted/file.mkv"),
        {
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 3}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}, {QStringLiteral("index"), 0}}
        },
        3,
        -1,
        QStringLiteral("mkv"),
        0,
        0,
        QStringLiteral("/Videos/episode-1/stream?Static=true"));

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({source}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QJsonArray{}, requestId);

    const QUrl startedUrl(backend.lastStartUrl);
    QCOMPARE(startedUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(startedUrl.host(), QStringLiteral("jellyfin.example"));
    QCOMPARE(startedUrl.path(), QStringLiteral("/Videos/episode-1/stream"));

    const QUrlQuery query(startedUrl);
    QCOMPARE(query.queryItemValue(QStringLiteral("api_key")), QStringLiteral("token-1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("MediaSourceId")), QStringLiteral("media-source-1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("AudioStreamIndex")), QStringLiteral("3"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("SubtitleStreamIndex")));
    QVERIFY(libraryService.requestedTrackStreamItemIds.isEmpty());
}

void PlayerControllerAutoplayContextTest::multipartIntermediateEndIsIgnoredUntilFinalSegment()
{
    ConfigManager config;
    config.setPlaybackCompletionThreshold(90);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    const PlaybackInfoResponse segmentInfo = buildPlaybackInfo({
        buildMediaSourceInfo(QStringLiteral("part-1-source"),
                             QStringLiteral("1080p"),
                             QStringLiteral("/library/show/1080p/part-1.mkv"),
                             {
                                 QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}},
                                 QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}, {QStringLiteral("index"), 0}}
                             },
                             2,
                             -1,
                             QStringLiteral("mkv"),
                             8000000,
                             600000000),
        buildMediaSourceInfo(QStringLiteral("part-2-source"),
                             QStringLiteral("1080p"),
                             QStringLiteral("/library/show/1080p/part-2.mkv"),
                             {
                                 QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}},
                                 QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}, {QStringLiteral("index"), 0}}
                             },
                             2,
                             -1,
                             QStringLiteral("mkv"),
                             8000000,
                             600000000)
    });

    const QVariantMap part1Source = segmentInfo.getMediaSourcesVariant().at(0).toMap();
    const QVariantMap part2Source = segmentInfo.getMediaSourcesVariant().at(1).toMap();

    controller.m_currentItemId = QStringLiteral("logical-item");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_playbackState = PlayerController::Playing;
    controller.m_playbackSegments = {
        QVariantMap{
            {QStringLiteral("itemId"), QStringLiteral("part-1")},
            {QStringLiteral("mediaSourceId"), QStringLiteral("part-1-source")},
            {QStringLiteral("playSessionId"), QStringLiteral("session-1")},
            {QStringLiteral("mediaSource"), part1Source},
            {QStringLiteral("audioIndex"), 2},
            {QStringLiteral("subtitleIndex"), -1},
            {QStringLiteral("availableAudioTracks"), controller.buildAvailableTrackOptions(part1Source, QStringLiteral("Audio"))},
            {QStringLiteral("availableSubtitleTracks"), controller.buildAvailableTrackOptions(part1Source, QStringLiteral("Subtitle"))},
            {QStringLiteral("durationMs"), 60000LL},
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/part-1")}
        },
        QVariantMap{
            {QStringLiteral("itemId"), QStringLiteral("part-2")},
            {QStringLiteral("mediaSourceId"), QStringLiteral("part-2-source")},
            {QStringLiteral("playSessionId"), QStringLiteral("session-2")},
            {QStringLiteral("mediaSource"), part2Source},
            {QStringLiteral("audioIndex"), 2},
            {QStringLiteral("subtitleIndex"), -1},
            {QStringLiteral("availableAudioTracks"), controller.buildAvailableTrackOptions(part2Source, QStringLiteral("Audio"))},
            {QStringLiteral("availableSubtitleTracks"), controller.buildAvailableTrackOptions(part2Source, QStringLiteral("Subtitle"))},
            {QStringLiteral("durationMs"), 60000LL},
            {QStringLiteral("url"), QStringLiteral("https://example.invalid/part-2")}
        }
    };
    controller.m_activePlaybackSegmentIndex = 0;
    controller.m_mediaSourceId = QStringLiteral("part-1-source");
    controller.m_playSessionId = QStringLiteral("session-1");
    controller.m_segmentRelativePosition = 59.5;

    controller.onPlaybackEnded();
    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);

    controller.onPlaylistPositionChanged(1);
    QCOMPARE(controller.m_activePlaybackSegmentIndex, 1);
    QCOMPARE(controller.m_activePlaybackSegmentOffsetMs, 60000LL);
    QCOMPARE(controller.m_mediaSourceId, QStringLiteral("part-2-source"));
    QCOMPARE(controller.m_playSessionId, QStringLiteral("session-2"));
    QCOMPARE(controller.activePlaybackSegmentItemId(), QStringLiteral("part-2"));
}

void PlayerControllerAutoplayContextTest::versionAffinityPrefersMatchingParentPath()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    const PlaybackInfoResponse currentInfo = buildPlaybackInfo({
        buildMediaSourceInfo(QStringLiteral("current"),
                             QStringLiteral("4K"),
                             QStringLiteral("/library/show/4k/current.mkv"),
                             {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")},
                                          {QStringLiteral("width"), 3840},
                                          {QStringLiteral("height"), 2160},
                                          {QStringLiteral("codec"), QStringLiteral("hevc")}}})
    });
    controller.updateVersionAffinityFromMediaSource(currentInfo.getMediaSourcesVariant().first().toMap());

    const PlaybackInfoResponse nextInfo = buildPlaybackInfo({
        buildMediaSourceInfo(QStringLiteral("episode-1080p"),
                             QStringLiteral("1080p"),
                             QStringLiteral("/library/show/1080p/next.mkv"),
                             {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")},
                                          {QStringLiteral("width"), 1920},
                                          {QStringLiteral("height"), 1080},
                                          {QStringLiteral("codec"), QStringLiteral("h264")}}}),
        buildMediaSourceInfo(QStringLiteral("episode-4k"),
                             QStringLiteral("4K"),
                             QStringLiteral("/library/show/4k/next.mkv"),
                             {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")},
                                          {QStringLiteral("width"), 3840},
                                          {QStringLiteral("height"), 2160},
                                          {QStringLiteral("codec"), QStringLiteral("hevc")}}})
    });

    const QVariantMap chosen = controller.selectMediaSourceForRequest(nextInfo.getMediaSourcesVariant(),
                                                                      QString(),
                                                                      true);
    QCOMPARE(chosen.value(QStringLiteral("id")).toString(), QStringLiteral("episode-4k"));
}

void PlayerControllerAutoplayContextTest::startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingUrl = QStringLiteral("https://example.invalid/stream");
    controller.m_selectedAudioTrack = 7;
    controller.m_selectedSubtitleTrack = 11;
    controller.updateTrackMappings(buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 5}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 7}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 8}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 11}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 14}}
    }));

    controller.onEnterBufferingState();

    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "aid", 2}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", 3}));
}

void PlayerControllerAutoplayContextTest::startupTrackSelectionRespectsPinnedUrlUnlessUserOverride()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_pendingUrl = QStringLiteral("https://example.invalid/stream");
    controller.m_streamPinsAudioTrack = true;
    controller.m_streamPinsSubtitleTrack = true;
    controller.m_pinnedAudioTrack = 4;
    controller.m_pinnedSubtitleTrack = 8;
    controller.m_selectedAudioTrack = 4;
    controller.m_selectedSubtitleTrack = 8;
    controller.updateTrackMappings(buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 4}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 9}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 8}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}}
    }));

    controller.onEnterBufferingState();

    // Startup now applies explicit deterministic selection even when URL pins match.
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "aid", 1}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", 1}));

    backend.variantCommands.clear();
    controller.m_selectedAudioTrack = 9;
    controller.m_selectedSubtitleTrack = -1;
    controller.onEnterBufferingState();

    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "aid", 2}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", "no"}));
}

void PlayerControllerAutoplayContextTest::runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentSeasonId = QStringLiteral("season-42");
    controller.updateTrackMappings(buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 5}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 8}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 9}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 10}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 13}}
    }));

    controller.setSelectedAudioTrack(5);
    controller.setSelectedSubtitleTrack(13);
    controller.setSelectedSubtitleTrack(-1);

    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "aid", 2}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", 4}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", "no"}));
    const ScopedTrackPreferences preferences = trackPrefs.getSeasonPreferences(QStringLiteral("season-42"));
    QCOMPARE(preferences.audio.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(preferences.audio.streamIndex, 5);
    QCOMPARE(preferences.subtitle.mode, TrackPreferenceMode::Off);
}

void PlayerControllerAutoplayContextTest::runtimeSubtitleDelayAppliesAndPersists()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentSeasonId = QStringLiteral("season-delay");

    controller.setSubtitleDelayMs(-42);
    QCOMPARE(controller.subtitleDelayMs(), -42);
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sub-delay", -0.042}));

    const ScopedTrackPreferences preferences = trackPrefs.getSeasonPreferences(QStringLiteral("season-delay"));
    QCOMPARE(preferences.subtitleDelayMs, -42);

    controller.resetSubtitleDelay();
    QCOMPARE(controller.subtitleDelayMs(), 0);
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sub-delay", 0.0}));
    QVERIFY(trackPrefs.getSeasonPreferences(QStringLiteral("season-delay")).isEmpty());
}

void PlayerControllerAutoplayContextTest::backendTrackSyncUsesReverseMap()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentSeasonId = QStringLiteral("season-84");
    controller.m_applyingInitialTracks = false;
    controller.updateTrackMappings(buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 2}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 7}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 5}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 12}}
    }));

    controller.cycleAudioTrack();
    controller.cycleSubtitleTrack();
    backend.emitAudioTrackId(2);
    backend.emitSubtitleTrackId(2);

    QCOMPARE(controller.selectedAudioTrack(), 7);
    QCOMPARE(controller.selectedSubtitleTrack(), 12);

    const ScopedTrackPreferences preferences = trackPrefs.getSeasonPreferences(QStringLiteral("season-84"));
    QCOMPARE(preferences.audio.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(preferences.audio.streamIndex, 7);
    QCOMPARE(preferences.subtitle.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(preferences.subtitle.streamIndex, 12);
}

QTEST_MAIN(PlayerControllerAutoplayContextTest)
#include "PlayerControllerAutoplayContextTest.moc"
