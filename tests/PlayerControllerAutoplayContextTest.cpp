#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QScopeGuard>
#include <QUrlQuery>
#include <algorithm>

#include "player/PlayerController.h"

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"
#include "utils/TrackPreferencesManager.h"
#include "support/DisplayTestUtils.h"

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
        return backendNameValue;
    }

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) override
    {
        Q_UNUSED(mpvBin);
        ++startCallCount;
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
        if (keepRunningUntilStopCompletion) {
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

    void requestAudioDiagnostics(const QString &tag) override
    {
        requestedAudioDiagnostics.append(tag);
    }

    void refreshAudioDevices() override
    {
        ++refreshAudioDevicesCount;
    }

private:
    bool m_running = false;

public:
    bool emitStopStateChangeSynchronously = true;
    bool keepRunningUntilStopCompletion = false;
    QString backendNameValue = QStringLiteral("fake-backend");
    int stopCallCount = 0;
    QList<QVariantList> variantCommands;
    QList<QStringList> commands;
    QStringList appendedUrls;
    QStringList lastStartArgs;
    QString lastStartUrl;
    int startCallCount = 0;
    QStringList requestedAudioDiagnostics;
    int refreshAudioDevicesCount = 0;

    void setRunning(bool running)
    {
        m_running = running;
    }

    void completeStop()
    {
        m_running = false;
        emit stateChanged(false);
    }

    void emitError(const QString &error)
    {
        emit errorOccurred(error);
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

    void emitPlaybackEndedSignal(
        Bloom::PlaybackPolicy::EndFileReason reason = Bloom::PlaybackPolicy::EndFileReason::Stop,
        const QString &detail = QString())
    {
        emit playbackEnded(reason, detail);
    }

    void emitFileLoaded()
    {
        emit fileLoaded();
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

    void emitAudioDevices(const QVariantList &devices)
    {
        emit audioDeviceListChanged(devices);
    }

    void emitPlaybackDiagnostics(const QString &stage, const QVariantMap &properties)
    {
        emit playbackDiagnosticsAvailable(stage, properties);
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

    using LibraryService::getItems;
    void getItems(const LibraryItemQuery &query) override
    {
        itemQueries.append(query);
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

    QList<LibraryItemQuery> itemQueries;
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
        const QString &playbackSessionId,
        bool emitFailure = true) override
    {
        Q_UNUSED(emitFailure)
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

    void requestPlaybackDescriptor(const QString &itemId,
                                   const QVariantMap &providerSource,
                                   int selectedAudioTrack,
                                   int selectedSubtitleTrack,
                                   qint64 startPositionMs,
                                   const QString &playbackSessionId,
                                   const QString &requestContext) override
    {
        const Bloom::PlaybackDescriptor descriptor = createPlaybackDescriptor(
            itemId, providerSource, selectedAudioTrack, selectedSubtitleTrack,
            startPositionMs, playbackSessionId);
        requestedDescriptorContexts.append(requestContext);
        if (deferDescriptorRequests) {
            return;
        }
        if (descriptor.stream.isValid()) {
            emit playbackDescriptorLoadedForRequest(itemId, descriptor, requestContext);
        } else {
            emit playbackDescriptorFailedForRequest(itemId,
                                                     QStringLiteral("invalid descriptor"),
                                                     requestContext);
        }
    }

    bool replanPlayback(const QString &playbackSessionId,
                        const PlaybackReplan &replan,
                        const QString &requestContext) override
    {
        replans.append(replan);
        replanContexts.append(requestContext);
        if (deferReplans) {
            return true;
        }
        Bloom::PlaybackDescriptor descriptor;
        descriptor.playbackSessionId = playbackSessionId;
        descriptor.stream.url = QUrl(QStringLiteral("https://example.invalid/reloaded"));
        descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
        emit playbackReplannedForRequest(playbackSessionId, descriptor, requestContext);
        return true;
    }

    void completeReplan(const QString &playbackSessionId,
                        const Bloom::PlaybackDescriptor &descriptor)
    {
        emit playbackReplannedForRequest(playbackSessionId, descriptor, replanContexts.last());
    }

    void failReplan(const QString &playbackSessionId, const QString &error)
    {
        emit playbackReplanFailedForRequest(playbackSessionId, error, replanContexts.last());
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
    bool deferDescriptorRequests = false;
    bool deferReplans = false;
    QList<PlaybackReplan> replans;
    QStringList replanContexts;
    bool queuedSessionsExpire = false;
    bool queuedPlaybackSessionsExpire() const override { return queuedSessionsExpire; }
    QStringList requestedDescriptorContexts;
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
                                            qint64 durationMs = 0,
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
    info.durationMs = durationMs;
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
    void localPreviewsGenerateOnRequestAndYieldToServer();
    void autoplayRetainsDescriptorForLocalPreviews();
    void descriptorExternalSubtitleReusesItsSourceEntry();
    void jellyfinDeliveryUrlSubtitleIsAddedToMpv();
    void seekPreviewSettingsGateServerAndLocalPreviews();
    void thresholdMetRequestsNextEpisodeDirectly();
    void userStopPastThresholdRequestsNextEpisode();
    void userStopBelowThresholdWaitsForBackendExit();
    void replacementPlaybackWaitsForQueuedTerminalFinalization();
    void replacementPlaybackRetainsFinalizedStreamMetadata();
    void explicitStopReportsFinalProgressAndStoppedOnce();
    void explicitPausedStopReportsPausedState();
    void explicitMultipartStopReportsActiveSegmentContext();
    void playbackEndedUpgradesQueuedStopFinalization();
    void playbackPrefetchIgnoresGenericNextEpisodeResponses();
    void autoplayPlaybackInfoErrorFallsBackToBasicPlayback();
    void autoplayToneMapStateSurvivesPlaybackInfoFallback();
    void staleNativeAudioSwitchResponseIsIgnoredAfterNonNativeSelection();
    void autoplayDescriptorWaitIsBoundedAndFallbackIsOneShot();
    void nextEpisodeNavigationUsesPendingTrackContext();
    void nextEpisodeNavigationKeepsAwaitingUntilQueuedDelivery();
    void nextEpisodeLookupFailureEndsUpNextHandoff();
    void upNextIdleParkingInvalidatesQueuedDisplayRestore();
    void deferredPostPlaybackDisplayRestoreCanBeReleased();
    void nextEpisodeIgnoresMismatchedSeries();
    void nextEpisodeIgnoresMismatchedConnection();
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
    void requestPlaybackAppliesVersionPreference_data();
    void requestPlaybackAppliesVersionPreference();
    void switchPlaybackVersionRestartsAtCurrentPosition();
    void switchPlaybackVersionKeepsQueuedResumePosition();
    void multipartPlaybackPublishesNoVersions();
    void extraPlaybackStaysOffTheParentTimeline();
    void expiringSessionsDeferLaterMultipartSegments();
    void pausedPlaybackSendsMinuteHeartbeat();
    void inPlaceReloadKeepsHeadersPlaylistAndOrigins();
    void lostServerSessionRecoversOrLeaves();
    void adjacentEpisodesCrossSeasonBoundaries();
    void requestPlaybackRecoversLibraryProfileFromSeriesDetails();
    void remoteMountStartupBufferingAppendsMpvArgs();
    void remoteMountStartupCacheReadyWhileBufferingUnpausesAndPlays();
    void remoteMountPausedForCacheReleaseUnpausesManualPause();
    void playbackWaitsForAsynchronousHdrAndRefreshPreparation();
    void displayTimeoutDoesNotFreezePlaybackStartup();
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
    void embeddedFallbackWaitsForAsynchronousBackendStop();
    void playbackInfoDirectStreamUrlIsPreferred();
    void multipartIntermediateEndIsIgnoredUntilFinalSegment();
    void endFileErrorWhileLoadingFailsWithoutTimeout();
    void endFileErrorMidPlaybackIsNotANaturalEnd();
    void endFileErrorOnReloadedNonFinalSegmentFails();
    void playbackEndReachesIdleFromEveryActiveState_data();
    void playbackEndReachesIdleFromEveryActiveState();
    void terminalFinalizationLeavesActiveStateWithoutTransition();
    void versionAffinityPrefersMatchingParentPath();
    void startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned();
    void startupTrackSelectionRespectsPinnedUrlUnlessUserOverride();
    void runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone();
    void runtimeSubtitleDelayAppliesAndPersists();
    void backendTrackSyncUsesReverseMap();
    void descriptorPinnedTrackIdsDefaultToMinusOne();
    void serverStreamReplansKeepTheSourceTimeline();
    void audioStartupPlanOverridesProfileOutputArgs();
    void autoAudioDeviceChangesReloadAfterInitialBaseline();
    void explicitAudioDeviceReloadsOnDisappearAndReconnect();
    void exclusiveFailureFallsBackToSharedThenSystemDefault();
    void selectAudioOutputDeviceReopensAudioWithoutRestart();
    void idleAudioDeviceRefreshUsesBackendEnumeration();
    void audioDiagnosticsFallbackToPcmOnceAndThrottleNullRetries();
    void audioDiagnosticsWithoutOutputEvidenceDoNotFallback();
    void externalSubtitleSelectionReaddsUnresolvedTrack();

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

    const Bloom::PlaybackDescriptor silentDescriptor =
        playbackService.createPlaybackDescriptor(
            QStringLiteral("item-1"), QVariantMap{}, -1, -1, 0, QString(), false);
    QVERIFY(!silentDescriptor.isValid());
    QCOMPARE(errorSpy.count(), 0);

    const Bloom::PlaybackDescriptor descriptor = playbackService.createPlaybackDescriptor(
        QStringLiteral("item-1"), QVariantMap{}, -1, -1);
    QVERIFY(!descriptor.isValid());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().at(0).toString(), QStringLiteral("createPlaybackDescriptor"));
    QVERIFY(!errorSpy.first().at(1).toString().isEmpty());
}

void PlayerControllerAutoplayContextTest::localPreviewsGenerateOnRequestAndYieldToServer()
{
#if !defined(BLOOM_HAS_LIBMPV)
    QSKIP("Local preview decoding requires libmpv.");
#endif
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = { QStringLiteral("connection"), QStringLiteral("item") };
    descriptor.mediaVersionId = QStringLiteral("version");
    descriptor.stream.url = QUrl::fromLocalFile(QString::fromUtf8(BLOOM_SEEK_PREVIEW_HDR_FIXTURE));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.durationMs = 11000;
    controller.m_currentItemId = descriptor.media.itemId;
    controller.m_playbackState = PlayerController::Playing;
    controller.m_playbackSegments = { QVariantMap {
        { QStringLiteral("itemId"), descriptor.media.itemId },
        { QStringLiteral("descriptor"), QVariant::fromValue(descriptor) }
    } };
    controller.m_activePlaybackSegmentIndex = 0;
    controller.m_localSeekPreviewResumeTimer->setInterval(0);
    controller.updateLocalSeekPreviewSuspension();
    QTRY_VERIFY(!controller.m_localSeekPreview->isSuspended());
    QSignalSpy frames(&controller, &PlayerController::trickplayFramesChanged);
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, {});
    QVERIFY(controller.hasTrickplay());
    QVERIFY(controller.m_useLocalTrickplay);
    // Nothing is read until the filmstrip asks for frames.
    controller.onPositionChanged(5);
    QTest::qWait(1000);
    QVERIFY(!controller.m_localSeekPreview->isRunning());
    QCOMPARE(frames.count(), 0);
    QVERIFY(controller.trickplayPreviewUrlAt(0).isEmpty());
    QVERIFY(controller.trickplayPreviewUrlAt(10).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 2, 15000);
    QVERIFY(!controller.trickplayPreviewUrlAt(10).isEmpty());
    const QString beforeAudioSwitch = controller.trickplayPreviewUrlAt(10);
    controller.m_playSessionId = QStringLiteral("session");
    controller.m_pendingAudioSwitchContext = QStringLiteral("audio-refresh");
    Bloom::PlaybackDescriptor replacement = descriptor;
    replacement.playbackSessionId = QStringLiteral("session");
    replacement.stream.url = QUrl::fromLocalFile(QStringLiteral("/missing/audio-only-replacement"));
    controller.onPlaybackReplannedForRequest(QStringLiteral("session"), replacement,
                                             QStringLiteral("audio-refresh"));
    controller.onBackendFileLoaded();
    QVERIFY(controller.m_useLocalTrickplay);
    QCOMPARE(controller.trickplayPreviewUrlAt(10), beforeAudioSwitch);
    controller.m_activePlaybackSegmentOffsetMs = 60000;
    QVERIFY(controller.trickplayPreviewUrlAt(10).isEmpty());
    QVERIFY(!controller.trickplayPreviewUrlAt(70).isEmpty());
    controller.onPositionChanged(10);
    QCOMPARE(controller.trickplayPreviewUrl(), controller.trickplayPreviewUrlAt(70));
    controller.m_activePlaybackSegmentOffsetMs = 0;

    TrickplayTileInfo serverInfo;
    serverInfo.width = 2;
    serverInfo.height = 2;
    serverInfo.interval = 10000;
    serverInfo.thumbnailCount = 1;
    serverInfo.tileWidth = 1;
    serverInfo.tileHeight = 1;
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, { { 2, serverInfo } });
    QVERIFY(controller.m_trickplayProcessor->isProcessing());
    QVERIFY(!controller.m_useLocalTrickplay);
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, {});
    QVERIFY(controller.m_trickplayProcessor->isProcessing());
    QVERIFY(!controller.m_useLocalTrickplay);
    QVERIFY(!controller.m_localSeekPreview->isAvailable());
    controller.m_trickplayProcessor->clear();

    QTemporaryDir directory;
    QFile binary(directory.filePath(QStringLiteral("server.bin")));
    QVERIFY(binary.open(QIODevice::WriteOnly));
    binary.write(QByteArray(16, static_cast<char>(128)));
    binary.close();
    controller.onTrickplayProcessingComplete(descriptor.media.itemId, 1, 10000, 2, 2, binary.fileName());
    QVERIFY(controller.hasTrickplay());
    QVERIFY(!controller.m_useLocalTrickplay);
    QVERIFY(controller.trickplayPreviewUrlAt(0).startsWith(QStringLiteral("data:image/")));
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, {});
    QVERIFY(controller.hasTrickplay());
    QVERIFY(!controller.m_useLocalTrickplay);
    QVERIFY(controller.trickplayPreviewUrlAt(0).startsWith(QStringLiteral("data:image/")));
    controller.m_activePlaybackSegmentOffsetMs = 60000;
    QVERIFY(controller.trickplayPreviewUrlAt(0).isEmpty());
    QVERIFY(controller.trickplayPreviewUrlAt(60).startsWith(QStringLiteral("data:image/")));
    controller.m_activePlaybackSegmentOffsetMs = 0;
    QVERIFY(!controller.m_localSeekPreview->isAvailable());

    controller.onTrickplayProcessingFailed(descriptor.media.itemId, QStringLiteral("server tile failed"));
    QVERIFY(controller.m_useLocalTrickplay);
    controller.m_terminalTransitionActive = true;
    controller.m_useLocalTrickplay = false;
    controller.m_localSeekPreview->clear();
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, {});
    QVERIFY(!controller.hasTrickplay());
    QVERIFY(!controller.m_localSeekPreview->isAvailable());
}

void PlayerControllerAutoplayContextTest::descriptorExternalSubtitleReusesItsSourceEntry()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    Bloom::PlaybackDescriptor descriptor;
    Bloom::PlaybackTrack listed;
    listed.trackId = QStringLiteral("0");
    listed.language = QStringLiteral("eng");
    listed.isExternal = true;
    listed.sourceIndex = 7;
    listed.externalUrl = QUrl(QStringLiteral("https://example.invalid/listed.vtt"));
    Bloom::PlaybackTrack downloaded = listed;
    downloaded.trackId = QStringLiteral("5");
    downloaded.sourceIndex = -1;
    downloaded.externalUrl = QUrl(QStringLiteral("https://example.invalid/downloaded.vtt"));
    descriptor.subtitleTracks = {listed, downloaded};
    descriptor.selectedSubtitleTrackId = listed.trackId;

    QVariantList tracks = {QVariantMap{{QStringLiteral("index"), 2}},
                           QVariantMap{{QStringLiteral("index"), 7}}};
    int selected = 2;
    controller.appendDescriptorExternalSubtitleTracks(tracks, descriptor, &selected);
    QCOMPARE(tracks.size(), 3);
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("index")).toInt(), 7);
    QVERIFY(tracks.at(1).toMap().value(QStringLiteral("isExternal")).toBool());
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("externalUrl")).toString(),
             QStringLiteral("https://example.invalid/listed.vtt"));
    QCOMPARE(tracks.at(2).toMap().value(QStringLiteral("index")).toInt(), -1000);
    QCOMPARE(selected, 7);

    // mpv numbers only the file's embedded subtitles before any sub-add.
    controller.updateTrackMappings({{QStringLiteral("mediaStreams"), QVariantList{
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")},
                    {QStringLiteral("index"), 7}, {QStringLiteral("isExternal"), true}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")},
                    {QStringLiteral("index"), 2}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")},
                    {QStringLiteral("index"), 3}}
    }}});
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(2), 1);
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(3), 2);
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(7), -1);
}

void PlayerControllerAutoplayContextTest::jellyfinDeliveryUrlSubtitleIsAddedToMpv()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const auto subtitle = [](int index, const QString &language, bool external,
                             const QString &deliveryUrl) {
        QJsonObject stream{
            {QStringLiteral("Index"), index},
            {QStringLiteral("Type"), QStringLiteral("Subtitle")},
            {QStringLiteral("Codec"), QStringLiteral("subrip")},
            {QStringLiteral("Language"), language},
            {QStringLiteral("DisplayTitle"), language + QStringLiteral(" - SRT")},
            {QStringLiteral("IsExternal"), external},
            {QStringLiteral("DeliveryMethod"),
             external ? QStringLiteral("External") : QStringLiteral("Embed")}
        };
        if (!deliveryUrl.isEmpty()) {
            stream.insert(QStringLiteral("DeliveryUrl"), deliveryUrl);
        }
        return stream;
    };
    const PlaybackInfoResponse info = JellyfinModelMapper::playbackInfo({
        {QStringLiteral("MediaSources"), QJsonArray{QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("source-1")},
            {QStringLiteral("DirectStreamUrl"), QStringLiteral("/Videos/movie-1/stream?Static=true")},
            {QStringLiteral("MediaStreams"), QJsonArray{
                QJsonObject{{QStringLiteral("Index"), 0}, {QStringLiteral("Type"), QStringLiteral("Video")}},
                QJsonObject{{QStringLiteral("Index"), 1}, {QStringLiteral("Type"), QStringLiteral("Audio")}},
                subtitle(2, QStringLiteral("jpn"), false, {}),
                subtitle(3, QStringLiteral("eng"), true,
                         QStringLiteral("/Videos/movie-1/source-1/Subtitles/3/0/Stream.subrip?ApiKey=server-token")),
                subtitle(4, QStringLiteral("fra"), true,
                         QStringLiteral("/Videos/movie-1/source-1/Subtitles/4/0/Stream.subrip?ApiKey=server-token"))
            }}
        }}}
    });
    const QVariantMap source = info.getMediaSourcesVariant().first().toMap();
    const JellyfinPlaybackProvider provider;
    const Bloom::PlaybackDescriptor descriptor = provider.createDescriptor(
        PlaybackProviderContext{QUrl(QStringLiteral("https://media.example.test/")),
                                QStringLiteral("secret-token")},
        Bloom::MediaRef{QStringLiteral("connection-1"), QStringLiteral("movie-1")},
        source, 1, 3, 0, QStringLiteral("session-1"));

    QVariantMap segment{
        {QStringLiteral("itemId"), QStringLiteral("movie-1")},
        {QStringLiteral("mediaSource"), source},
        {QStringLiteral("audioIndex"), 1},
        {QStringLiteral("subtitleIndex"), 3},
        {QStringLiteral("availableSubtitleTracks"),
         controller.buildAvailableTrackOptions(source, QStringLiteral("Subtitle"))}
    };
    controller.applyPlaybackDescriptorToSegment(segment, descriptor);

    // Sidecars keep their source index and menu position; nothing is duplicated.
    const QVariantList tracks = segment.value(QStringLiteral("availableSubtitleTracks")).toList();
    QCOMPARE(tracks.size(), 3);
    QList<int> indexes;
    for (const QVariant &track : tracks) {
        indexes.append(track.toMap().value(QStringLiteral("index")).toInt());
    }
    QCOMPARE(indexes, (QList<int>{2, 3, 4}));
    QVERIFY(!tracks.at(0).toMap().value(QStringLiteral("isExternal")).toBool());
    const QString englishUrl = QStringLiteral(
        "https://media.example.test/Videos/movie-1/source-1/Subtitles/3/0/Stream.subrip?ApiKey=secret-token");
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("externalUrl")).toString(), englishUrl);
    QCOMPARE(segment.value(QStringLiteral("subtitleIndex")).toInt(), 3);
    QVERIFY(!segment.value(QStringLiteral("pinsSubtitleTrack")).toBool());
    QVERIFY(!segment.value(QStringLiteral("url")).toString().contains(QStringLiteral("SubtitleStreamIndex")));

    // mpv's own numbering covers only the embedded subtitle.
    controller.updateTrackMappings(source);
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(2), 1);
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(3), -1);

    controller.m_playbackSegments = {segment};
    controller.m_activePlaybackSegmentIndex = 0;
    controller.m_availableSubtitleTracks = tracks;
    controller.m_playbackState = PlayerController::Playing;
    controller.queueActiveSegmentExternalSubtitleTracks();

    // Only the selected sidecar is loaded, with select so it is the subtitle
    // mpv shows. The other one is registered and loaded when chosen.
    QCOMPARE(backend.variantCommands.size(), 1);
    const QVariantList selected = backend.variantCommands.at(0);
    QCOMPARE(selected.at(0).toString(), QStringLiteral("sub-add"));
    QCOMPARE(selected.at(1).toString(), englishUrl);
    QCOMPARE(selected.at(2).toString(), QStringLiteral("select"));
    QCOMPARE(selected.at(3).toString(), QStringLiteral("eng - SRT"));
    QCOMPARE(selected.at(4).toString(), QStringLiteral("eng"));
    QCOMPARE(controller.m_pendingExternalSubtitleIndex, 3);
    QVERIFY(controller.m_externalSubtitleTrackMap.contains(3));
    QCOMPARE(controller.m_externalSubtitleTrackMap.value(4), -1);

    // mpv reports the selected sidecar's sid, which is recorded.
    controller.syncBackendSubtitleTrack(2);
    QCOMPARE(controller.m_externalSubtitleTrackMap.value(3), 2);
    QCOMPARE(controller.selectedSubtitleTrack(), 3);
    backend.variantCommands.clear();

    // Choosing the other sidecar loads it once, with select.
    controller.setSelectedSubtitleTrack(4);
    QList<QVariantList> subAdds;
    for (const QVariantList &command : backend.variantCommands) {
        if (command.value(0).toString() == QStringLiteral("sub-add")) {
            subAdds.append(command);
        }
    }
    QCOMPARE(subAdds.size(), 1);
    QVERIFY(subAdds.first().at(1).toString().contains(QStringLiteral("/Subtitles/4/0/")));
    QCOMPARE(subAdds.first().at(2).toString(), QStringLiteral("select"));
    QCOMPARE(subAdds.first().at(4).toString(), QStringLiteral("fra"));
    controller.syncBackendSubtitleTrack(3);
    QCOMPARE(controller.m_externalSubtitleTrackMap.value(4), 3);
    QCOMPARE(controller.selectedSubtitleTrack(), 4);

    // Switching back uses the recorded sid instead of fetching the file again.
    backend.variantCommands.clear();
    controller.setSelectedSubtitleTrack(3);
    for (const QVariantList &command : backend.variantCommands) {
        QVERIFY(command.value(0).toString() != QStringLiteral("sub-add"));
    }
}

void PlayerControllerAutoplayContextTest::autoplayRetainsDescriptorForLocalPreviews()
{
#if !defined(BLOOM_HAS_LIBMPV)
    QSKIP("Local preview decoding requires libmpv.");
#endif
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = {QStringLiteral("connection"), QStringLiteral("autoplay-item")};
    descriptor.mediaVersionId = QStringLiteral("autoplay-version");
    descriptor.playbackSessionId = QStringLiteral("autoplay-session");
    descriptor.stream.url = QUrl::fromLocalFile(QString::fromUtf8(BLOOM_SEEK_PREVIEW_HDR_FIXTURE));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.stream.headers = {{QStringLiteral("X-Preview-Test"), QStringLiteral("retained")}};
    descriptor.durationMs = 11000;
    Bloom::PlaybackTrack externalSubtitle;
    externalSubtitle.trackId = QStringLiteral("external-subtitle");
    externalSubtitle.isExternal = true;
    externalSubtitle.externalUrl = QUrl(QStringLiteral("https://example.invalid/autoplay.srt"));
    descriptor.subtitleTracks = {externalSubtitle};
    descriptor.selectedSubtitleTrackId = externalSubtitle.trackId;
    const QVariantList audioTracks = {QVariantMap{{QStringLiteral("index"), 4}}};
    const QVariantList subtitleTracks = {QVariantMap{{QStringLiteral("index"), 9}}};
    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 4}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 9}}
    });
    const QString context = QStringLiteral("autoplay|descriptor|autoplay-item|1");
    controller.m_pendingAutoplayDescriptorContext = context;
    controller.m_pendingAutoplayDescriptorItemId = descriptor.media.itemId;
    controller.m_pendingAutoplayDescriptorSource = mediaSource;
    controller.m_pendingAutoplayDescriptorAudioIndex = 4;
    controller.m_pendingAutoplayDescriptorSubtitleIndex = 9;
    controller.m_pendingAutoplayDescriptorAudioTracks = audioTracks;
    controller.m_pendingAutoplayDescriptorSubtitleTracks = subtitleTracks;
    controller.onPlaybackDescriptorLoadedForRequest(descriptor.media.itemId, descriptor, context);

    const QVariantMap segment = controller.activePlaybackSegment();
    const auto stored = segment.value(QStringLiteral("descriptor")).value<Bloom::PlaybackDescriptor>();
    QCOMPARE(stored.media.connectionId, descriptor.media.connectionId);
    QCOMPARE(stored.media.itemId, descriptor.media.itemId);
    QCOMPARE(stored.stream.url, descriptor.stream.url);
    QCOMPARE(stored.stream.headers, descriptor.stream.headers);
    QCOMPARE(segment.value(QStringLiteral("mediaSourceId")).toString(), descriptor.mediaVersionId);
    QCOMPARE(segment.value(QStringLiteral("playSessionId")).toString(), descriptor.playbackSessionId);
    QCOMPARE(segment.value(QStringLiteral("mediaSource")).toMap(), mediaSource);
    QCOMPARE(segment.value(QStringLiteral("audioIndex")).toInt(), 4);
    QCOMPARE(segment.value(QStringLiteral("subtitleIndex")).toInt(), -1000);
    QCOMPARE(segment.value(QStringLiteral("availableAudioTracks")).toList(), audioTracks);
    const auto storedSubtitleTracks = segment.value(QStringLiteral("availableSubtitleTracks")).toList();
    QCOMPARE(storedSubtitleTracks.size(), 2);
    QCOMPARE(storedSubtitleTracks.first(), subtitleTracks.first());
    QCOMPARE(storedSubtitleTracks.last().toMap().value(QStringLiteral("trackId")).toString(),
             externalSubtitle.trackId);
    controller.m_playbackState = PlayerController::Playing;
    controller.m_localSeekPreviewResumeTimer->setInterval(0);
    controller.updateLocalSeekPreviewSuspension();
    QTRY_VERIFY(!controller.m_localSeekPreview->isSuspended());
    QSignalSpy frames(&controller, &PlayerController::trickplayFramesChanged);
    controller.onTrickplayInfoLoaded(descriptor.media.itemId, {});
    QVERIFY(controller.m_useLocalTrickplay);
    controller.trickplayPreviewUrlAt(0);
    controller.trickplayPreviewUrlAt(10);
    QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 2, 15000);
    QVERIFY(!controller.trickplayPreviewUrlAt(10).isEmpty());
}

void PlayerControllerAutoplayContextTest::seekPreviewSettingsGateServerAndLocalPreviews()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = { QStringLiteral("connection"), QStringLiteral("item") };
    descriptor.mediaVersionId = QStringLiteral("version");
    descriptor.stream.url = QUrl::fromLocalFile(QString::fromUtf8(BLOOM_SEEK_PREVIEW_HDR_FIXTURE));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.durationMs = 11000;
    controller.m_currentItemId = descriptor.media.itemId;
    controller.m_playbackSegments = { QVariantMap {
        { QStringLiteral("itemId"), descriptor.media.itemId },
        { QStringLiteral("descriptor"), QVariant::fromValue(descriptor) }
    } };
    controller.m_activePlaybackSegmentIndex = 0;
    // The unauthenticated service answers server lookups synchronously.
    QSignalSpy serverLookups(&playbackService, &PlaybackService::trickplayInfoLoaded);

    config.setLocalSeekPreviewsEnabled(false);
    controller.requestTrickplayInfo(descriptor.media.itemId);
    QCOMPARE(serverLookups.count(), 1);
    QVERIFY(!controller.m_useLocalTrickplay);
    QVERIFY(!controller.hasTrickplay());
    QVERIFY(!controller.m_localSeekPreview->isAvailable());

    config.setServerTrickplayEnabled(false);
    controller.requestTrickplayInfo(descriptor.media.itemId);
    QCoreApplication::processEvents();
    QCOMPARE(serverLookups.count(), 1);
    QVERIFY(!controller.hasTrickplay());

    // Local previews wait for healthy playback and stop again while it struggles.
    QVERIFY(controller.m_localSeekPreview->isSuspended());
    controller.m_localSeekPreviewResumeTimer->setInterval(0);
    controller.m_playbackState = PlayerController::Playing;
    controller.updateLocalSeekPreviewSuspension();
    QTRY_VERIFY(!controller.m_localSeekPreview->isSuspended());
    controller.m_playbackState = PlayerController::Buffering;
    controller.updateLocalSeekPreviewSuspension();
    QVERIFY(controller.m_localSeekPreview->isSuspended());
    controller.m_playbackState = PlayerController::Paused;
    controller.updateLocalSeekPreviewSuspension();
    QTRY_VERIFY(!controller.m_localSeekPreview->isSuspended());

#if defined(BLOOM_HAS_LIBMPV)
    config.setLocalSeekPreviewsEnabled(true);
    controller.requestTrickplayInfo(descriptor.media.itemId);
    QCOMPARE(serverLookups.count(), 1);
    QTRY_VERIFY(controller.m_useLocalTrickplay);
    QVERIFY(!controller.m_localSeekPreview->isRunning());
#endif
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

    // Terminal finalization is deliberately queued so backend callbacks cannot
    // re-enter the playback state machine. Wait for that queued boundary rather
    // than assuming one processEvents() pass drains it on every Qt version.
    QTRY_COMPARE(libraryService.requestedSeriesIds.size(), 1);
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

void PlayerControllerAutoplayContextTest::nextEpisodeLookupFailureEndsUpNextHandoff()
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

    controller.m_shouldAutoplay = true;
    controller.m_waitingForNextEpisodeAtPlaybackEnd = true;
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.setAwaitingNextEpisodeResolution(true);
    controller.m_deferredPostPlaybackDisplayRestorePending = true;
    controller.m_deferredPostPlaybackNeedsHdrRestore = true;

    QSignalSpy navigationSpy(&controller, &PlayerController::navigateToNextEpisode);

    // A failure for another request (a prefetch) leaves the handoff alone.
    emit libraryService.nextUnplayedEpisodeFailed(QStringLiteral("series-1"),
                                                  QStringLiteral("offline"),
                                                  QStringLiteral("player:prefetch:series-1:item-1"));
    QVERIFY(controller.awaitingNextEpisodeResolution());
    QVERIFY(controller.m_deferredPostPlaybackDisplayRestorePending);

    const quint64 generationBeforeFailure = controller.m_displayRestoreGeneration;
    emit libraryService.nextUnplayedEpisodeFailed(QStringLiteral("series-1"),
                                                  QStringLiteral("offline"),
                                                  QStringLiteral("player:resolve:series-1:item-1"));
    QCoreApplication::processEvents();

    QVERIFY(!controller.awaitingNextEpisodeResolution());
    QVERIFY(!controller.m_shouldAutoplay);
    QVERIFY(!controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QVERIFY(controller.m_pendingAutoplayItemId.isEmpty());
    QVERIFY(!controller.m_deferredPostPlaybackDisplayRestorePending);
    QVERIFY(controller.m_displayRestoreGeneration > generationBeforeFailure);
    QCOMPARE(navigationSpy.count(), 0);
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

void PlayerControllerAutoplayContextTest::staleNativeAudioSwitchResponseIsIgnoredAfterNonNativeSelection()
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
    controller.m_playbackState = PlayerController::Playing;
    controller.m_streamPinsAudioTrack = true;
    controller.m_playSessionId = QStringLiteral("session-audio");
    controller.m_selectedAudioTrack = 3;
    controller.m_pendingAudioSwitchContext = QStringLiteral("audio|7|4");
    controller.m_pendingAudioSwitchPreviousTrack = 3;

    controller.setSelectedAudioTrack(-1);

    // The replan stays in flight, but its failure no longer owns the audio.
    QCOMPARE(controller.m_pendingAudioSwitchPreviousTrack, -2);
    QCOMPARE(controller.selectedAudioTrack(), -1);
    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackReplanFailedForRequest",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("session-audio")),
                                      Q_ARG(QString, QStringLiteral("stale failure")),
                                      Q_ARG(QString, QStringLiteral("audio|7|4"))));
    QCOMPARE(controller.selectedAudioTrack(), -1);
}

void PlayerControllerAutoplayContextTest::autoplayDescriptorWaitIsBoundedAndFallbackIsOneShot()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    playbackService.deferDescriptorRequests = true;
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-bound");
    controller.m_pendingAutoplaySeasonId = QStringLiteral("season-bound");
    controller.m_pendingAutoplayEpisodeData = QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-bound")},
        {QStringLiteral("seasonId"), QStringLiteral("season-bound")}
    };
    controller.m_waitingForAutoplayPlaybackInfo = true;

    PlaybackInfoResponse playbackInfo;
    playbackInfo.playSessionId = QStringLiteral("session-bound");
    MediaSourceInfo mediaSource;
    mediaSource.id = QStringLiteral("source-bound");
    mediaSource.mediaStreams = {
        MediaStreamInfo{.index = 1, .type = QStringLiteral("Audio")}
    };
    playbackInfo.mediaSources.append(mediaSource);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onPlaybackInfoLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("episode-bound")),
                                      Q_ARG(PlaybackInfoResponse, playbackInfo)));
    QVERIFY(controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(playbackService.requestedDescriptorContexts.size(), 1);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onAutoplayPlaybackInfoTimeout",
                                      Qt::DirectConnection));
    QVERIFY(controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(playbackService.requestedDescriptorContexts.size(), 2);

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onAutoplayPlaybackInfoTimeout",
                                      Qt::DirectConnection));
    QVERIFY(!controller.m_waitingForAutoplayPlaybackInfo);
    QCOMPARE(playbackService.requestedDescriptorContexts.size(), 2);
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

    const MediaSourceInfo jellyfinNumericSource = JellyfinModelMapper::mediaSource(QJsonObject{
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

void PlayerControllerAutoplayContextTest::requestPlaybackAppliesVersionPreference_data()
{
    QTest::addColumn<QString>("preference");
    QTest::addColumn<QString>("expectedSourceId");

    // An empty expected source means the version dialog is requested.
    QTest::newRow("ask prompts") << QStringLiteral("ask") << QString();
    QTest::newRow("highest quality plays 4K") << QStringLiteral("highest-quality")
                                              << QStringLiteral("source-4k");
    QTest::newRow("smallest plays 1080p") << QStringLiteral("smallest")
                                          << QStringLiteral("source-1080p");
}

void PlayerControllerAutoplayContextTest::requestPlaybackAppliesVersionPreference()
{
    QFETCH(QString, preference);
    QFETCH(QString, expectedSourceId);

    ConfigManager config;
    config.setPlaybackVersionPreference(preference);
    const auto restorePreference = qScopeGuard([&config] {
        config.setPlaybackVersionPreference(QStringLiteral("highest-quality"));
    });
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
        120000);
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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({versionA, versionB}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

    if (!expectedSourceId.isEmpty()) {
        QCOMPARE(selectionSpy.count(), 0);
        QCOMPARE(playbackService.requestedDescriptorMediaSourceIds,
                 QStringList{expectedSourceId});
        return;
    }

    QCOMPARE(selectionSpy.count(), 1);
    const QList<QVariant> arguments = selectionSpy.takeFirst();
    const QVariantMap dialogModel = arguments.at(1).toMap();
    const QVariantList options = dialogModel.value(QStringLiteral("options")).toList();
    QCOMPARE(dialogModel.value(QStringLiteral("title")).toString(), QStringLiteral("Select Version"));
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("source-1080p"));
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("source-4k"));
}

void PlayerControllerAutoplayContextTest::switchPlaybackVersionRestartsAtCurrentPosition()
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
    authService.restoreSession(QStringLiteral("https://example.invalid"),
                               QStringLiteral("user-1"),
                               QStringLiteral("token-1"));

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("movie-1")},
        {QStringLiteral("libraryId"), QStringLiteral("library-1")},
        {QStringLiteral("overlayTitle"), QStringLiteral("Movie")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const MediaSourceInfo versionA = buildMediaSourceInfo(
        QStringLiteral("source-1080p"), QStringLiteral("1080p"),
        QStringLiteral("/library/movie/1080p.mkv"), {});
    const MediaSourceInfo versionB = buildMediaSourceInfo(
        QStringLiteral("source-4k"), QStringLiteral("4K"),
        QStringLiteral("/library/movie/4k.mkv"), {});
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("movie-1"),
                                                      buildPlaybackInfo({versionA, versionB}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("movie-1"), QVariantList{}, requestId);

    controller.m_currentItemId = QStringLiteral("movie-1");
    QVariantList versions = controller.playbackVersions();
    QCOMPARE(versions.size(), 2);
    QString selectedId;
    QString otherId;
    for (const QVariant &value : versions) {
        const QVariantMap version = value.toMap();
        if (version.value(QStringLiteral("selected")).toBool()) {
            QVERIFY(selectedId.isEmpty());
            selectedId = version.value(QStringLiteral("id")).toString();
        } else {
            otherId = version.value(QStringLiteral("id")).toString();
        }
    }
    QVERIFY(!selectedId.isEmpty());
    QVERIFY(!otherId.isEmpty());

    // Versions are scoped to the item they were resolved for.
    controller.m_currentItemId = QStringLiteral("other-item");
    QVERIFY(controller.playbackVersions().isEmpty());
    controller.m_currentItemId = QStringLiteral("movie-1");

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentPosition = 42.5;
    controller.m_pendingPlaybackRequests.clear();

    controller.switchPlaybackVersion(selectedId);
    controller.switchPlaybackVersion(QStringLiteral("unknown-source"));
    QVERIFY(controller.m_pendingPlaybackRequests.isEmpty());

    controller.switchPlaybackVersion(otherId);
    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    const auto pending = controller.m_pendingPlaybackRequests.constBegin().value();
    QCOMPARE(pending.itemId, QStringLiteral("movie-1"));
    QCOMPARE(pending.chosenMediaSourceId, otherId);
    QCOMPARE(pending.startPositionMs, qint64(42500));
    QCOMPARE(pending.libraryId, QStringLiteral("library-1"));
    QCOMPARE(pending.overlayTitle, QStringLiteral("Movie"));
    QVERIFY(pending.isMovie);
    QVERIFY(!pending.allowVersionPrompt);
}

void PlayerControllerAutoplayContextTest::switchPlaybackVersionKeepsQueuedResumePosition()
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

    controller.m_currentItemId = QStringLiteral("movie-1");
    controller.setPlaybackVersions(QStringLiteral("movie-1"), QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("a")}, {QStringLiteral("selected"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("b")}, {QStringLiteral("selected"), false}}
    });

    // Still loading a resumed item: no position update yet, seek queued.
    controller.m_playbackState = PlayerController::Loading;
    controller.m_currentPosition = 0.0;
    controller.m_startPositionMs = 600000;
    controller.m_seekTargetWhileBuffering = 600.0;
    controller.switchPlaybackVersion(QStringLiteral("b"));
    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    QCOMPARE(controller.m_pendingPlaybackRequests.constBegin().value().startPositionMs, qint64(600000));

    controller.m_pendingPlaybackRequests.clear();
    controller.m_seekTargetWhileBuffering = -1;
    controller.switchPlaybackVersion(QStringLiteral("b"));
    QCOMPARE(controller.m_pendingPlaybackRequests.constBegin().value().startPositionMs, qint64(600000));
}

void PlayerControllerAutoplayContextTest::extraPlaybackStaysOffTheParentTimeline()
{
    ConfigManager config;
    config.setAutoplayNextEpisode(true);
    config.setPlaybackCompletionThreshold(90);
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

    // A request for an extra drops the series context and the resume point,
    // even when the caller passes them.
    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("extra-1")},
        {QStringLiteral("contentKind"), QStringLiteral("extra")},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seasonId"), QStringLiteral("season-1")},
        {QStringLiteral("startPositionMs"), 60000},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    const auto pending = controller.m_pendingPlaybackRequests.constBegin().value();
    QCOMPARE(pending.contentKind, Bloom::PlaybackPolicy::PlaybackContentKind::Extra);
    QVERIFY(pending.seriesId.isEmpty());
    QVERIFY(pending.seasonId.isEmpty());
    QCOMPARE(pending.startPositionMs, qint64(0));
    controller.m_pendingPlaybackRequests.clear();

    // Finishing an extra past the threshold never resolves a next episode,
    // and a mid-extra prefetch is never started.
    controller.m_currentItemId = QStringLiteral("extra-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_currentContentKind = Bloom::PlaybackPolicy::PlaybackContentKind::Extra;
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    controller.m_playbackState = PlayerController::Playing;

    controller.maybeTriggerNextEpisodePrefetch();
    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);

    controller.prepareTerminalTransition(PlayerController::TerminalReason::PlaybackEnd);
    QVERIFY(!controller.m_shouldAutoplay);
    QVERIFY(!controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QVERIFY(controller.m_pendingAutoplayItemId.isEmpty());
    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);

    // Track picks during an extra are not saved for any scope.
    controller.m_currentSeriesId.clear();
    controller.persistAudioPreferenceForCurrentScope(2);
    controller.persistSubtitlePreferenceForCurrentScope(3);
    QVERIFY(trackPrefs.getMoviePreferences(QStringLiteral("extra-1")).isEmpty());

    // The same state for a movie saves them.
    controller.m_currentContentKind = Bloom::PlaybackPolicy::PlaybackContentKind::Feature;
    controller.persistAudioPreferenceForCurrentScope(2);
    QVERIFY(!trackPrefs.getMoviePreferences(QStringLiteral("extra-1")).isEmpty());
    trackPrefs.clearMoviePreferences(QStringLiteral("extra-1"));
}

void PlayerControllerAutoplayContextTest::multipartPlaybackPublishesNoVersions()
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
        {QStringLiteral("itemId"), QStringLiteral("movie-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    const auto source = [](const QString &id, const QString &path) {
        return buildMediaSourceInfo(id, id, path, {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    };
    emit playbackService.playbackInfoLoadedForRequest(
        QStringLiteral("movie-1"),
        buildPlaybackInfo({source(QStringLiteral("hd"), QStringLiteral("/hd/part1.mkv")),
                           source(QStringLiteral("uhd"), QStringLiteral("/uhd/part1.mkv"))}),
        requestId);
    emit playbackService.additionalPartsLoadedForRequest(
        QStringLiteral("movie-1"),
        QVariantList{QVariantMap{{QStringLiteral("itemId"), QStringLiteral("movie-1-part-2")}}},
        requestId);
    emit playbackService.playbackInfoLoadedForRequest(
        QStringLiteral("movie-1-part-2"),
        buildPlaybackInfo({source(QStringLiteral("hd-2"), QStringLiteral("/hd/part2.mkv")),
                           source(QStringLiteral("uhd-2"), QStringLiteral("/uhd/part2.mkv"))}),
        requestId);

    QVERIFY(!backend.appendedUrls.isEmpty());
    QCOMPARE(controller.m_playbackVersionsItemId, QStringLiteral("movie-1"));
    QVERIFY(controller.m_playbackVersions.isEmpty());
}

void PlayerControllerAutoplayContextTest::expiringSessionsDeferLaterMultipartSegments()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    playbackService.queuedSessionsExpire = true;
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    controller.requestPlayback(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("movie-1")},
        {QStringLiteral("isMovie"), true},
        {QStringLiteral("allowVersionPrompt"), false}
    });
    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    const auto source = [](const QString &id, const QString &path) {
        return buildMediaSourceInfo(id, id, path, {QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}}});
    };
    emit playbackService.playbackInfoLoadedForRequest(
        QStringLiteral("movie-1"),
        buildPlaybackInfo({source(QStringLiteral("hd"), QStringLiteral("/hd/part1.mkv"))}),
        requestId);
    emit playbackService.additionalPartsLoadedForRequest(
        QStringLiteral("movie-1"),
        QVariantList{QVariantMap{{QStringLiteral("itemId"), QStringLiteral("movie-1-part-2")}}},
        requestId);
    emit playbackService.playbackInfoLoadedForRequest(
        QStringLiteral("movie-1-part-2"),
        buildPlaybackInfo({source(QStringLiteral("hd-2"), QStringLiteral("/hd/part2.mkv"))}),
        requestId);

    // Only the first part's session starts before playback.
    QCOMPARE(playbackService.requestedDescriptorItemIds, QStringList{QStringLiteral("movie-1")});
    QVERIFY(backend.appendedUrls.isEmpty());
    QCOMPARE(controller.m_playbackSegments.size(), 2);
    QVERIFY(controller.m_playbackSegments.at(1).value(QStringLiteral("deferredDescriptor")).toBool());

    // The next part's session starts only shortly before the handoff.
    controller.m_playbackSegments[0][QStringLiteral("durationMs")] = 600000;
    controller.m_segmentRelativePosition = 10.0;
    controller.requestDeferredSegmentDescriptor();
    QCOMPARE(playbackService.requestedDescriptorItemIds, QStringList{QStringLiteral("movie-1")});
    controller.m_segmentRelativePosition = 600.0 - PlayerController::kDeferredSegmentLeadSeconds;
    controller.requestDeferredSegmentDescriptor();
    QCOMPARE(playbackService.requestedDescriptorItemIds.last(), QStringLiteral("movie-1-part-2"));
    QVERIFY(playbackService.requestedDescriptorContexts.last().startsWith(QStringLiteral("segment|")));
    QCOMPARE(backend.appendedUrls, QStringList{QStringLiteral("https://example.invalid/movie-1-part-2")});
    QVERIFY(!controller.m_playbackSegments.at(1).contains(QStringLiteral("deferredDescriptor")));

    controller.requestDeferredSegmentDescriptor();
    QCOMPARE(backend.appendedUrls.size(), 1);
}

void PlayerControllerAutoplayContextTest::pausedPlaybackSendsMinuteHeartbeat()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_playSessionId = QStringLiteral("play-session-1");
    controller.m_playbackState = PlayerController::Playing;
    controller.m_progressReportTimer->start();

    // Leaving Playing stops the timer; the paused state must keep it running.
    QVERIFY(controller.processEvent(PlayerController::Event::Pause));
    QCOMPARE(controller.m_playbackState, PlayerController::Paused);
    QVERIFY(controller.m_progressReportTimer->isActive());
    playbackService.progressReports.clear();

    for (int tick = 1; tick < PlayerController::kPausedProgressEveryTicks; ++tick) {
        controller.reportPlaybackProgress();
    }
    QVERIFY(playbackService.progressReports.isEmpty());
    controller.reportPlaybackProgress();
    QCOMPARE(playbackService.progressReports.size(), 1);
    QVERIFY(playbackService.progressReports.first().isPaused);

    QVERIFY(controller.processEvent(PlayerController::Event::Resume));
    QCOMPARE(controller.m_playbackState, PlayerController::Playing);
    QVERIFY(controller.m_progressReportTimer->isActive());
}

void PlayerControllerAutoplayContextTest::inPlaceReloadKeepsHeadersPlaylistAndOrigins()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const auto segment = [](const QString &name) {
        return QVariantMap{{QStringLiteral("itemId"), name},
                           {QStringLiteral("playSessionId"), QStringLiteral("session-") + name},
                           {QStringLiteral("url"), QStringLiteral("https://silo.example.test/") + name},
                           {QStringLiteral("durationMs"), 600000}};
    };
    controller.m_playbackSegments = {segment(QStringLiteral("a")), segment(QStringLiteral("b")),
                                     segment(QStringLiteral("c"))};
    controller.applyPlaybackSegment(1, false);
    controller.m_playbackState = PlayerController::Playing;
    controller.m_pendingAudioSwitchContext = QStringLiteral("replan|test");

    Bloom::PlaybackDescriptor descriptor;
    descriptor.playbackSessionId = QStringLiteral("session-b");
    descriptor.stream.url = QUrl(QStringLiteral("https://silo.example.test/b/master.m3u8"));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectStream;
    descriptor.stream.headers = {{QStringLiteral("Authorization"), QStringLiteral("Bearer token")}};
    backend.variantCommands.clear();
    emit playbackService.playbackReplannedForRequest(
        QStringLiteral("session-b"), descriptor, QStringLiteral("replan|test"));

    // Headers are replaced entry by entry, which every backend applies.
    QVERIFY(backend.variantCommands.contains(QVariantList(
        {QStringLiteral("change-list"), QStringLiteral("http-header-fields"), QStringLiteral("clr"), QString()})));
    QVERIFY(backend.variantCommands.contains(QVariantList(
        {QStringLiteral("change-list"), QStringLiteral("http-header-fields"), QStringLiteral("append"),
         QStringLiteral("Authorization: Bearer token")})));

    // The replacement playlist starts at the reloaded segment; part c follows it.
    backend.emitPlaylistPosition(0);
    QCOMPARE(controller.m_activePlaybackSegmentIndex, 1);
    backend.emitFileLoaded();
    QCOMPARE(backend.appendedUrls, QStringList{QStringLiteral("https://silo.example.test/c")});
    backend.emitPlaylistPosition(0);
    QCOMPARE(controller.m_activePlaybackSegmentIndex, 1);
    backend.emitPlaylistPosition(1);
    QCOMPARE(controller.m_activePlaybackSegmentIndex, 2);

    // Credentials never follow a sidecar to another origin.
    controller.m_playbackSegments[2][QStringLiteral("streamHeaders")] = QVariantMap{
        {QStringLiteral("Authorization"), QStringLiteral("Bearer token")}};
    const auto subAdds = [&backend]() {
        return std::count_if(backend.variantCommands.cbegin(), backend.variantCommands.cend(),
                             [](const QVariantList &command) {
                                 return !command.isEmpty()
                                     && command.first().toString() == QStringLiteral("sub-add");
                             });
    };
    backend.variantCommands.clear();
    controller.addExternalSubtitleTrackInternal(QStringLiteral("https://elsewhere.example.test/x.vtt"),
                                                QStringLiteral("Other"), QStringLiteral("eng"), -1, false);
    QCOMPARE(subAdds(), 0);
    controller.addExternalSubtitleTrackInternal(QStringLiteral("https://silo.example.test/y.vtt"),
                                                QStringLiteral("Same"), QStringLiteral("eng"), -1, false);
    QCOMPARE(subAdds(), 1);
}

void PlayerControllerAutoplayContextTest::lostServerSessionRecoversOrLeaves()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_playSessionId = QStringLiteral("play-session-1");
    controller.m_playbackState = PlayerController::Playing;

    emit playbackService.playbackSessionLost(QStringLiteral("other-session"), true);
    QVERIFY(controller.m_errorMessage.isEmpty());

    emit playbackService.playbackSessionLost(QStringLiteral("play-session-1"), false);
    QVERIFY(controller.m_lastErrorWasNetworkRecoverable);
    QVERIFY(controller.m_errorMessage.contains(QStringLiteral("expired")));

    controller.m_playbackState = PlayerController::Playing;
    controller.m_terminalTransitionActive = false;
    emit playbackService.playbackSessionLost(QStringLiteral("play-session-1"), true);
    QVERIFY(!controller.m_lastErrorWasNetworkRecoverable);
    QCOMPARE(controller.m_errorMessage, QStringLiteral("The server ended this playback."));
}

void PlayerControllerAutoplayContextTest::adjacentEpisodesCrossSeasonBoundaries()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    libraryService.activeConnectionIdValue = QStringLiteral("conn-1");
    FakePlayerBackend backend;

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);

    const auto row = [](const QString &id, const QString &mediaType, const QString &seasonId,
                        int seasonNumber, int number) {
        return QVariantMap{
            {QStringLiteral("itemId"), id},
            {QStringLiteral("mediaType"), mediaType},
            {QStringLiteral("seasonId"), seasonId},
            {QStringLiteral("seriesName"), QStringLiteral("Show")},
            {QStringLiteral("name"), QStringLiteral("Episode ") + id},
            {QStringLiteral("parentIndexNumber"), seasonNumber},
            {QStringLiteral("indexNumber"), number}
        };
    };
    const auto keyFor = [&libraryService](const QString &parentId) {
        for (const LibraryItemQuery &query : std::as_const(libraryService.itemQueries)) {
            if (query.parentId == parentId) return query.requestKey;
        }
        return QString();
    };
    const auto typesFor = [&libraryService](const QString &parentId) {
        for (const LibraryItemQuery &query : std::as_const(libraryService.itemQueries)) {
            if (query.parentId == parentId) return query.includeItemTypes;
        }
        return QStringList();
    };

    controller.m_currentItemId = QStringLiteral("s1e2");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_currentSeasonId = QStringLiteral("season-1");
    controller.m_currentLibraryId = QStringLiteral("library-1");
    controller.m_playbackState = PlayerController::Playing;
    controller.m_selectedAudioTrack = 3;

    QSignalSpy changedSpy(&controller, &PlayerController::adjacentEpisodesChanged);
    controller.refreshAdjacentEpisodes();
    QCOMPARE(libraryService.itemQueries.size(), 2);
    QVERIFY(controller.hasEpisodeNavigation());
    QVERIFY(!controller.hasPreviousEpisode());
    QVERIFY(!controller.hasNextEpisode());
    QVERIFY(changedSpy.count() >= 1);

    const QString seasonKey = keyFor(QStringLiteral("season-1"));
    const QString seriesKey = keyFor(QStringLiteral("series-1"));
    QVERIFY(!seasonKey.isEmpty());
    QVERIFY(!seriesKey.isEmpty());
    // Typed listings route to the hierarchy endpoints on Silo.
    QCOMPARE(typesFor(QStringLiteral("season-1")), QStringList{QStringLiteral("Episode")});
    QCOMPARE(typesFor(QStringLiteral("series-1")), QStringList{QStringLiteral("Season")});

    const QVariantList seasonOne{
        row(QStringLiteral("s1e1"), QStringLiteral("Episode"), QStringLiteral("season-1"), 1, 1),
        row(QStringLiteral("s1e2"), QStringLiteral("Episode"), QStringLiteral("season-1"), 1, 2)
    };
    // Responses for another connection or for view-model queries are ignored.
    emit libraryService.canonicalItemsLoadedForConnection(
        QStringLiteral("conn-2"), QStringLiteral("season-1"), seasonKey, seasonOne, 2);
    emit libraryService.canonicalItemsLoadedForConnection(
        QStringLiteral("conn-1"), QStringLiteral("season-1"), QStringLiteral("season-1"), seasonOne, 2);
    QVERIFY(!controller.hasPreviousEpisode());

    emit libraryService.canonicalItemsLoadedForConnection(
        QStringLiteral("conn-1"), QStringLiteral("season-1"), seasonKey, seasonOne, 2);
    QVERIFY(controller.hasPreviousEpisode());
    QVERIFY(!controller.hasNextEpisode());

    // The last episode of a season continues into the next non-special season.
    emit libraryService.canonicalItemsLoadedForConnection(
        QStringLiteral("conn-1"), QStringLiteral("series-1"), seriesKey,
        QVariantList{row(QStringLiteral("specials"), QStringLiteral("Season"), QString(), -1, 0),
                     row(QStringLiteral("season-1"), QStringLiteral("Season"), QString(), -1, 1),
                     row(QStringLiteral("season-2"), QStringLiteral("Season"), QString(), -1, 2)},
        3);
    QCOMPARE(libraryService.itemQueries.size(), 3);
    const QString nextSeasonKey = keyFor(QStringLiteral("season-2"));
    QVERIFY(!nextSeasonKey.isEmpty());
    emit libraryService.canonicalItemsLoadedForConnection(
        QStringLiteral("conn-1"), QStringLiteral("season-2"), nextSeasonKey,
        QVariantList{row(QStringLiteral("s2e1"), QStringLiteral("Episode"), QStringLiteral("season-2"), 2, 1),
                     row(QStringLiteral("s2e2"), QStringLiteral("Episode"), QStringLiteral("season-2"), 2, 2)},
        2);
    QVERIFY(controller.hasNextEpisode());
    QCOMPARE(controller.m_nextEpisode.value(QStringLiteral("itemId")).toString(), QStringLiteral("s2e1"));

    controller.skipToNextEpisode();
    QCOMPARE(controller.m_pendingPlaybackRequests.size(), 1);
    const auto pending = controller.m_pendingPlaybackRequests.constBegin().value();
    QCOMPARE(pending.itemId, QStringLiteral("s2e1"));
    QCOMPARE(pending.seriesId, QStringLiteral("series-1"));
    QCOMPARE(pending.seasonId, QStringLiteral("season-2"));
    QCOMPARE(pending.libraryId, QStringLiteral("library-1"));
    QCOMPARE(pending.preferredAudioIndex, 3);
    QCOMPARE(pending.overlayTitle, QStringLiteral("Show"));
    QCOMPARE(pending.overlaySubtitle, QStringLiteral("S2 E1 - Episode s2e1"));
    QVERIFY(!pending.isMovie);
    QVERIFY(!pending.allowVersionPrompt);

    // A movie has no episode navigation and issues no listing requests.
    controller.m_currentItemId = QStringLiteral("movie-1");
    controller.m_currentSeriesId.clear();
    controller.m_currentSeasonId.clear();
    controller.refreshAdjacentEpisodes();
    QVERIFY(!controller.hasEpisodeNavigation());
    QVERIFY(!controller.hasNextEpisode());
    QCOMPARE(libraryService.itemQueries.size(), 3);
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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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
        120000);

    const QString requestId = controller.m_pendingPlaybackRequests.constBegin().key();
    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({mediaSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);
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
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QVariantList{});
    QVERIFY(backend.lastStartUrl.isEmpty());
    QVERIFY(playbackService.requestedDescriptorMediaSourceIds.isEmpty());

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({freshSource}),
                                                      requestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, oldRequestId);
    QVERIFY(backend.lastStartUrl.isEmpty());

    emit playbackService.playbackInfoLoadedForRequest(QStringLiteral("episode-1"),
                                                      buildPlaybackInfo({freshSource}),
                                                      newRequestId);
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, newRequestId);

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
        QVariantList{QVariantMap{{QStringLiteral("itemId"), QStringLiteral("episode-1-part-2")}}},
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
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

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

void PlayerControllerAutoplayContextTest::embeddedFallbackWaitsForAsynchronousBackendStop()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    backend.backendNameValue = QStringLiteral("linux-libmpv-opengl");
    backend.keepRunningUntilStopCompletion = true;
    backend.setRunning(true);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);
    controller.m_playbackState = PlayerController::Loading;
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/fallback-media");

    backend.emitError(QStringLiteral("linux-libmpv-render-unavailable: test failure"));

    QCOMPARE(backend.stopCallCount, 1);
    QCOMPARE(controller.m_playerBackend, &backend);
    QCOMPARE(controller.m_backendFallbackSource.data(), &backend);
    backend.emitError(QStringLiteral("linux-libmpv-render-unavailable: repeated failure"));
    QCOMPARE(backend.stopCallCount, 1);
    QCOMPARE(controller.playbackState(), PlayerController::Loading);
    QCoreApplication::processEvents();
    QCOMPARE(controller.m_playerBackend, &backend);

    backend.completeStop();
    QTRY_VERIFY(controller.m_playerBackend != &backend);
    QCOMPARE(controller.m_playerBackend->backendName(), QStringLiteral("null-backend"));
    QVERIFY(controller.m_backendFallbackSource.isNull());
    QCOMPARE(controller.playbackState(), PlayerController::Loading);
}

void PlayerControllerAutoplayContextTest::playbackWaitsForAsynchronousHdrAndRefreshPreparation()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom display-command integration test");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sequenceMarker = directory.filePath(QStringLiteral("display-order.txt"));
    ConfigManager config;
    config.setEnableHDR(true);
    config.setHDROutputMode(QStringLiteral("match-content"));
    config.setEnableFramerateMatching(true);
    config.setFramerateMatchDelay(0);
    config.setLinuxHDRCommand(BloomTest::shellCommand(
        QStringLiteral("printf hdr >> %1; sleep 1").arg(sequenceMarker)));
    config.setLinuxRefreshRateCommand(
        BloomTest::shellCommand(
            QStringLiteral("printf refresh >> %1; sleep 1").arg(sequenceMarker)));
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
    controller.m_playbackState = PlayerController::Loading;
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/async-display");
    controller.m_contentIsHDR = true;
    controller.m_contentShouldToneMapToSdr = false;
    controller.m_contentFramerate = 23.976;

    QElapsedTimer timer;
    timer.start();
    controller.startPlayback(controller.m_pendingUrl);
    QVERIFY2(timer.elapsed() < 50, "display preparation blocked playback startup");
    QVERIFY(backend.lastStartUrl.isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(
        backend.lastStartUrl,
        QStringLiteral("https://example.invalid/async-display"), 5000);
    QFile sequenceFile(sequenceMarker);
    QVERIFY(sequenceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(sequenceFile.readAll()),
             QStringLiteral("hdrrefresh"));
    QVERIFY(displayManager.needsHdrRestore());
    QVERIFY(displayManager.needsRefreshRestore());
#endif
}

void PlayerControllerAutoplayContextTest::displayTimeoutDoesNotFreezePlaybackStartup()
{
#ifdef Q_OS_WIN
    QSKIP("Linux custom display-command integration test");
#else
    ConfigManager config;
    config.setEnableHDR(true);
    config.setHDROutputMode(QStringLiteral("match-content"));
    config.setEnableFramerateMatching(false);
    config.setLinuxHDRCommand(BloomTest::shellCommand(QStringLiteral("sleep 2")));
    TrackPreferencesManager trackPrefs;
    DisplayManagerOptions options;
    options.commandDeadlineMs = 100;
    DisplayManager displayManager(&config, options);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    QSignalSpy hdrSpy(&displayManager, &DisplayManager::hdrChangeFinished);

    PlayerController controller(&backend,
                                &config,
                                &trackPrefs,
                                &displayManager,
                                &playbackService,
                                &libraryService,
                                &authService);
    controller.m_playbackState = PlayerController::Loading;
    controller.m_pendingUrl = QStringLiteral("https://example.invalid/display-timeout");
    controller.m_contentIsHDR = true;

    QElapsedTimer timer;
    timer.start();
    controller.startPlayback(controller.m_pendingUrl);
    QVERIFY2(timer.elapsed() < 50, "display timeout setup blocked playback startup");
    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 1, 1000);
    QCOMPARE(hdrSpy.first().at(1).toBool(), false);
    QTRY_COMPARE_WITH_TIMEOUT(
        backend.lastStartUrl,
        QStringLiteral("https://example.invalid/display-timeout"), 1000);

    config.setLinuxHDRCommand(BloomTest::shellCommand(QStringLiteral("true")));
    displayManager.setHDRAsync(false);
    QTRY_COMPARE_WITH_TIMEOUT(hdrSpy.count(), 2, 1000);
#endif
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
    emit playbackService.additionalPartsLoadedForRequest(QStringLiteral("episode-1"), QVariantList{}, requestId);

    const QUrl startedUrl(backend.lastStartUrl);
    QCOMPARE(startedUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(startedUrl.host(), QStringLiteral("jellyfin.example"));
    QCOMPARE(startedUrl.path(), QStringLiteral("/Videos/episode-1/stream"));

    const QUrlQuery query(startedUrl);
    QCOMPARE(query.queryItemValue(QStringLiteral("ApiKey")), QStringLiteral("token-1"));
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
                             60000),
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
                             60000)
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
            {QStringLiteral("subtitleIndex"), -1000},
            {QStringLiteral("availableAudioTracks"), controller.buildAvailableTrackOptions(part2Source, QStringLiteral("Audio"))},
            {QStringLiteral("availableSubtitleTracks"), QVariantList{
                QVariantMap{
                    {QStringLiteral("index"), -1000},
                    {QStringLiteral("displayTitle"), QStringLiteral("Part 2 selected")},
                    {QStringLiteral("language"), QStringLiteral("eng")},
                    {QStringLiteral("isExternal"), true},
                    {QStringLiteral("externalUrl"), QStringLiteral("https://example.invalid/part-2-selected.vtt")}
                },
                QVariantMap{
                    {QStringLiteral("index"), -1001},
                    {QStringLiteral("displayTitle"), QStringLiteral("Part 2 alternate")},
                    {QStringLiteral("language"), QStringLiteral("fra")},
                    {QStringLiteral("isExternal"), true},
                    {QStringLiteral("externalUrl"), QStringLiteral("https://example.invalid/part-2-alternate.vtt")}
                }
            }},
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
    QCOMPARE(controller.m_externalSubtitleTrackMap.size(), 2);
    QVERIFY(controller.m_externalSubtitleTrackMap.contains(-1000));
    QVERIFY(controller.m_externalSubtitleTrackMap.contains(-1001));
    QVERIFY(controller.m_pendingExternalSubtitleTracks.isEmpty());
    // Only the selected sidecar is loaded; the alternate is registered for later.
    QCOMPARE(backend.variantCommands.size(), 1);
    QCOMPARE(backend.variantCommands.at(0).at(0).toString(), QStringLiteral("sub-add"));
    QCOMPARE(backend.variantCommands.at(0).at(1).toString(),
             QStringLiteral("https://example.invalid/part-2-selected.vtt"));
    QCOMPARE(backend.variantCommands.at(0).at(2).toString(), QStringLiteral("select"));
    QCOMPARE(controller.m_externalSubtitleTrackMap.value(-1001), -1);

    // Re-emitting the same playlist position must not add duplicate subtitle tracks.
    controller.onPlaylistPositionChanged(1);
    QCOMPARE(backend.variantCommands.size(), 1);
}

void PlayerControllerAutoplayContextTest::endFileErrorWhileLoadingFailsWithoutTimeout()
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

    controller.m_playbackState = PlayerController::Loading;
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_loadingTimeoutTimer->start(30000);
    backend.setRunning(true);

    backend.emitPlaybackEndedSignal(Bloom::PlaybackPolicy::EndFileReason::Error,
                                    QStringLiteral("loading failed"));
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Error);
    QVERIFY(!controller.m_loadingTimeoutTimer->isActive());
    QVERIFY(controller.errorMessage().contains(QStringLiteral("loading failed")));
    QCOMPARE(backend.stopCallCount, 1);
}

void PlayerControllerAutoplayContextTest::endFileErrorMidPlaybackIsNotANaturalEnd()
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

    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_currentSeriesId = QStringLiteral("series-1");
    controller.m_duration = 100.0;
    controller.m_currentPosition = 95.0;
    backend.setRunning(true);

    backend.emitPlaybackEndedSignal(Bloom::PlaybackPolicy::EndFileReason::Error,
                                    QStringLiteral("unrecognized file format"));
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Error);
    QVERIFY(!controller.m_shouldAutoplay);
    QVERIFY(!controller.awaitingNextEpisodeResolution());
    QCOMPARE(libraryService.requestedSeriesIds.size(), 0);
}

void PlayerControllerAutoplayContextTest::endFileErrorOnReloadedNonFinalSegmentFails()
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

    // An in-place reload of part 1 of 2 leaves a one-entry mpv playlist, so
    // a failed replacement arrives as a terminal end while the active
    // segment is not the last one. The old file's end was already consumed.
    controller.m_playbackState = PlayerController::Playing;
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_playbackSegments = {
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("part-1")}},
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("part-2")}},
    };
    controller.m_activePlaybackSegmentIndex = 0;
    controller.m_ignoreReplacedFileEnd = true;
    backend.setRunning(true);

    backend.emitPlaybackEndedSignal(Bloom::PlaybackPolicy::EndFileReason::Error,
                                    QStringLiteral("loading failed"));
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Error);
    QVERIFY(!controller.m_ignoreReplacedFileEnd);
}

void PlayerControllerAutoplayContextTest::playbackEndReachesIdleFromEveryActiveState_data()
{
    QTest::addColumn<int>("state");
    QTest::newRow("loading") << static_cast<int>(PlayerController::Loading);
    QTest::newRow("buffering") << static_cast<int>(PlayerController::Buffering);
    QTest::newRow("playing") << static_cast<int>(PlayerController::Playing);
    QTest::newRow("paused") << static_cast<int>(PlayerController::Paused);
}

void PlayerControllerAutoplayContextTest::playbackEndReachesIdleFromEveryActiveState()
{
    QFETCH(int, state);
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

    controller.m_playbackState = static_cast<PlayerController::PlaybackState>(state);
    controller.m_currentItemId = QStringLiteral("item-1");
    backend.setRunning(true);

    backend.emitPlaybackEndedSignal(Bloom::PlaybackPolicy::EndFileReason::Eof);
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Idle);
    QVERIFY(!controller.m_terminalTransitionActive);
}

void PlayerControllerAutoplayContextTest::terminalFinalizationLeavesActiveStateWithoutTransition()
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

    controller.m_transitions.remove({PlayerController::Loading, PlayerController::Event::Stop});
    controller.m_playbackState = PlayerController::Loading;
    controller.m_currentItemId = QStringLiteral("item-1");
    controller.m_loadingTimeoutTimer->start(30000);
    backend.setRunning(true);

    controller.requestTerminalTransition(PlayerController::TerminalReason::Stop);
    QCoreApplication::processEvents();

    QCOMPARE(controller.playbackState(), PlayerController::Idle);
    QVERIFY(!controller.m_loadingTimeoutTimer->isActive());
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

void PlayerControllerAutoplayContextTest::descriptorPinnedTrackIdsDefaultToMinusOne()
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

    Bloom::PlaybackDescriptor descriptor;
    descriptor.stream.url = QUrl(QStringLiteral("https://example.invalid/stream"));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    QVariantMap segment;
    controller.applyPlaybackDescriptorToSegment(segment, descriptor);
    QCOMPARE(segment.value(QStringLiteral("pinnedAudioTrack")).toInt(), -1);
    QCOMPARE(segment.value(QStringLiteral("pinnedSubtitleTrack")).toInt(), -1);

    descriptor.stream.pinnedAudioTrackId = QStringLiteral("not-a-number");
    descriptor.stream.pinnedSubtitleTrackId = QStringLiteral("also-invalid");
    controller.applyPlaybackDescriptorToSegment(segment, descriptor);
    controller.m_pendingAutoplayDescriptorContext =
        QStringLiteral("autoplay|descriptor|item-1");
    controller.m_pendingAutoplayDescriptorItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplayDescriptorAudioIndex = -1;
    controller.m_pendingAutoplayDescriptorSubtitleIndex = -1;
    controller.onPlaybackDescriptorLoadedForRequest(
        QStringLiteral("item-1"),
        descriptor,
        controller.m_pendingAutoplayDescriptorContext);
    QCOMPARE(controller.m_nextPinnedAudioTrack, -1);
    QCOMPARE(controller.m_nextPinnedSubtitleTrack, -1);
    QCOMPARE(segment.value(QStringLiteral("pinnedAudioTrack")).toInt(), -1);
    QCOMPARE(segment.value(QStringLiteral("pinnedSubtitleTrack")).toInt(), -1);
}

void PlayerControllerAutoplayContextTest::serverStreamReplansKeepTheSourceTimeline()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    FakePlaybackService playbackService(&authService);
    playbackService.deferReplans = true;
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Video")}, {QStringLiteral("index"), 0}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 0}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")}, {QStringLiteral("index"), 1}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 2}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 3}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")}, {QStringLiteral("index"), 4}}
    });
    // A copy remux anchored at 30 s: it carries audio 0 and subtitle 3 (an HLS
    // rendition), offers subtitle 2 through a new stream, and cannot deliver 4.
    const auto packaged = [](qint64 offsetMs, qint64 startMs) {
        Bloom::PlaybackDescriptor descriptor;
        descriptor.media = {QStringLiteral("connection"), QStringLiteral("item")};
        descriptor.playbackSessionId = QStringLiteral("session-1");
        descriptor.durationMs = 600000;
        descriptor.startPositionMs = startMs;
        descriptor.stream.url = QUrl(QStringLiteral("https://example.invalid/master.m3u8"));
        descriptor.stream.method = Bloom::PlaybackMethod::DirectStream;
        descriptor.stream.carriesSourceTracks = false;
        descriptor.stream.seekRequiresReanchor = true;
        descriptor.stream.timelineOffsetMs = offsetMs;
        descriptor.stream.deliveredAudioTracks = {0};
        descriptor.stream.deliveredSubtitleTracks = {3};
        descriptor.stream.pinsAudioTrack = true;
        descriptor.stream.pinnedAudioTrackId = QStringLiteral("0");
        descriptor.selectedAudioTrackId = QStringLiteral("0");
        descriptor.selectedSubtitleTrackId = QStringLiteral("2");
        Bloom::PlaybackTrack replanned;
        replanned.trackId = QStringLiteral("1");
        replanned.sourceIndex = 2;
        replanned.requiresReplan = true;
        Bloom::PlaybackTrack rendition;
        rendition.trackId = QStringLiteral("2");
        rendition.sourceIndex = 3;
        Bloom::PlaybackTrack sidecar;
        sidecar.trackId = QStringLiteral("0");
        sidecar.language = QStringLiteral("ger");
        sidecar.isExternal = true;
        sidecar.externalUrl = QUrl(QStringLiteral("https://example.invalid/0.vtt"));
        descriptor.subtitleTracks = {sidecar, replanned, rendition};
        return descriptor;
    };

    QVariantMap segment{
        {QStringLiteral("itemId"), QStringLiteral("item")},
        {QStringLiteral("mediaSource"), mediaSource},
        {QStringLiteral("audioIndex"), 1},
        {QStringLiteral("subtitleIndex"), 4},
        {QStringLiteral("availableSubtitleTracks"),
         controller.buildAvailableTrackOptions(mediaSource, QStringLiteral("Subtitle"))}
    };
    controller.applyPlaybackDescriptorToSegment(segment, packaged(30000, 100000));
    QCOMPARE(segment.value(QStringLiteral("audioIndex")).toInt(), 0);
    controller.m_playbackSegments = {segment};
    controller.applyPlaybackSegment(0, false);
    controller.m_playbackState = PlayerController::Playing;
    controller.m_duration = 600.0;

    QCOMPARE(controller.selectedSubtitleTrack(), 3);
    QCOMPARE(controller.mpvAudioTrackForSourceIndex(0), 1);
    QCOMPARE(controller.mpvAudioTrackForSourceIndex(1), -1);
    QCOMPARE(controller.mpvSubtitleTrackForSourceIndex(3), 1);
    QStringList menu;
    for (const QVariant &option : controller.m_availableSubtitleTracks) {
        const QVariantMap map = option.toMap();
        menu.append(QStringLiteral("%1:%2:%3")
                        .arg(map.value(QStringLiteral("index")).toInt())
                        .arg(int(map.value(QStringLiteral("isExternal")).toBool()))
                        .arg(int(map.value(QStringLiteral("requiresReplan")).toBool())));
    }
    QCOMPARE(menu, QStringList({QStringLiteral("2:0:1"), QStringLiteral("3:0:0"),
                                QStringLiteral("-1000:1:0")}));

    // mpv's clock starts at the anchor; Bloom's timeline and reports do not.
    backend.emitPosition(10.0);
    QCOMPARE(controller.m_currentPosition, 40.0);
    QCOMPARE(controller.currentReportingPositionMs(), 40000);
    backend.emitCacheEnd(20.0);
    QCOMPARE(controller.cacheEndSeconds(), 50.0);
    controller.onDurationChanged(20.0);
    QCOMPARE(controller.m_duration, 600.0);

    // Inside what the server has produced, mpv seeks on its own clock.
    backend.variantCommands.clear();
    controller.seek(45.0);
    QCOMPARE(backend.variantCommands.size(), 1);
    QCOMPARE(backend.variantCommands.first(), QVariantList({QStringLiteral("seek"), 15.0,
                                                            QStringLiteral("absolute")}));
    QVERIFY(playbackService.replans.isEmpty());

    // Elsewhere the server re-anchors the stream.
    controller.seek(500.0);
    QCOMPARE(playbackService.replans.size(), 1);
    QCOMPARE(playbackService.replans.first().operation, PlaybackReplan::Operation::SeekReanchor);
    QCOMPARE(playbackService.replans.first().positionMs, 500000);
    // A second seek waits for the first replan.
    controller.seek(520.0);
    QCOMPARE(playbackService.replans.size(), 1);

    backend.variantCommands.clear();
    playbackService.completeReplan(QStringLiteral("session-1"), packaged(498000, 500000));
    QVERIFY(controller.m_awaitingInPlaceReload);
    QVERIFY(std::any_of(backend.variantCommands.cbegin(), backend.variantCommands.cend(),
                        [](const QVariantList &command) {
                            return command == QVariantList({QStringLiteral("loadfile"),
                                                            QStringLiteral("https://example.invalid/master.m3u8"),
                                                            QStringLiteral("replace"), -1,
                                                            QStringLiteral("start=2.000")});
                        }));
    // The queued seek follows the first one.
    QCOMPARE(playbackService.replans.size(), 2);
    QCOMPARE(playbackService.replans.last().positionMs, 520000);

    // Replacing the file ends the previous one; that is not the end of playback.
    backend.emitPlaybackEndedSignal();
    QCOMPARE(controller.m_playbackState, PlayerController::Playing);
    QVERIFY(!controller.m_terminalTransitionActive);
    backend.emitPosition(400.0);
    QCOMPARE(controller.m_currentPosition, 40.0);
    backend.variantCommands.clear();
    backend.emitFileLoaded();
    QVERIFY(!controller.m_awaitingInPlaceReload);
    QVERIFY(backend.variantCommands.contains(
        QVariantList({QStringLiteral("set_property"), QStringLiteral("sid"), 1})));
    backend.emitPosition(3.0);
    QCOMPARE(controller.m_currentPosition, 501.0);
    playbackService.failReplan(QStringLiteral("session-1"), QStringLiteral("refused"));

    // A sidecar's cues are on the source clock: shift them onto mpv's.
    backend.variantCommands.clear();
    controller.setSelectedSubtitleTrack(-1000);
    controller.m_externalSubtitleTrackMap.insert(-1000, 2);
    controller.applySubtitleDelay();
    QCOMPARE(backend.variantCommands.last(),
             QVariantList({QStringLiteral("set_property"), QStringLiteral("sub-delay"), -498.0}));

    // An embedded track the stream lacks is asked for; a refusal restores the
    // previous choice, and a choice made meanwhile runs after it.
    controller.setSelectedSubtitleTrack(2);
    QCOMPARE(playbackService.replans.size(), 3);
    QCOMPARE(playbackService.replans.last().operation, PlaybackReplan::Operation::TrackChange);
    QCOMPARE(playbackService.replans.last().subtitleTrackId, QStringLiteral("1"));
    QCOMPARE(playbackService.replans.last().positionMs, 501000);
    QCOMPARE(controller.selectedSubtitleTrack(), 2);
    controller.setSelectedAudioTrack(1);
    QCOMPARE(playbackService.replans.size(), 3);
    playbackService.failReplan(QStringLiteral("session-1"), QStringLiteral("refused"));
    QCOMPARE(controller.selectedSubtitleTrack(), -1000);
    QCOMPARE(playbackService.replans.size(), 4);
    QCOMPARE(playbackService.replans.last().audioTrackIndex, 1);
    QCOMPARE(playbackService.replans.last().operation, PlaybackReplan::Operation::TrackChange);

    // A choice made while that request runs survives its answer: the answer
    // grants audio 1, and the queued request still asks for audio 0.
    controller.setSelectedAudioTrack(0);
    QCOMPARE(playbackService.replans.size(), 4);
    Bloom::PlaybackDescriptor granted = packaged(500000, 501000);
    granted.selectedAudioTrackId = QStringLiteral("1");
    granted.stream.pinnedAudioTrackId = QStringLiteral("1");
    granted.stream.deliveredAudioTracks = {1};
    playbackService.completeReplan(QStringLiteral("session-1"), granted);
    QCOMPARE(playbackService.replans.size(), 5);
    QCOMPARE(playbackService.replans.last().audioTrackIndex, 0);
    QCOMPARE(controller.selectedAudioTrack(), 0);
    // Refusing it restores what the stream carries, not the older choice.
    playbackService.failReplan(QStringLiteral("session-1"), QStringLiteral("refused"));
    QCOMPARE(controller.selectedAudioTrack(), 1);
}

void PlayerControllerAutoplayContextTest::audioStartupPlanOverridesProfileOutputArgs()
{
    ConfigManager config;
    QVariantMap profile = config.getMpvProfile(QStringLiteral("Medium Quality"));
    profile[QStringLiteral("extraArgs")] = QStringList{
        QStringLiteral("--audio-spdif=ac3"),
        QStringLiteral("--audio-device=profile-device"),
        QStringLiteral("--audio-channels=7.1")
    };
    config.setMpvProfile(QStringLiteral("Medium Quality"), profile);
    config.setDefaultProfileName(QStringLiteral("Medium Quality"));
    config.setAudioExclusiveMode(false);
    config.setAudioPassthroughMode(QStringLiteral("custom"));
    config.setAudioPassthroughCodecs({QStringLiteral("truehd")});

    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 4},
                    {QStringLiteral("codec"), QStringLiteral("truehd")},
                    {QStringLiteral("channels"), 8},
                    {QStringLiteral("channelLayout"), QStringLiteral("7.1")}}
    });
    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/truehd"),
                                 QStringLiteral("truehd-item"), 0, {}, {}, {},
                                 QStringLiteral("source-truehd"), QStringLiteral("session-truehd"),
                                 mediaSource, 4, -1);

    QCOMPARE(backend.startCallCount, 1);
    QCOMPARE(controller.selectedAudioTrack(), 4);
    QCOMPARE(controller.m_audioPlan.path, QStringLiteral("passthrough"));
    QCOMPARE(controller.m_audioPlan.passthroughCodec, QStringLiteral("truehd"));
    QVERIFY(backend.lastStartArgs.indexOf(QStringLiteral("--audio-spdif=ac3"))
            < backend.lastStartArgs.lastIndexOf(QStringLiteral("--audio-spdif=truehd")));
    QVERIFY(backend.lastStartArgs.indexOf(QStringLiteral("--audio-device=profile-device"))
            < backend.lastStartArgs.lastIndexOf(QStringLiteral("--audio-device=auto")));
    QVERIFY(backend.lastStartArgs.indexOf(QStringLiteral("--audio-channels=7.1"))
            < backend.lastStartArgs.lastIndexOf(QStringLiteral("--audio-channels=auto-safe")));
}

void PlayerControllerAutoplayContextTest::autoAudioDeviceChangesReloadAfterInitialBaseline()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    controller.m_playbackState = PlayerController::Playing;
    controller.m_activeMediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 1},
                    {QStringLiteral("codec"), QStringLiteral("aac")}}
    });
    controller.m_selectedAudioTrack = 1;
    controller.updateTrackMappings(controller.m_activeMediaSource);

    backend.setRunning(true);
    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("speaker")}}});
    QVERIFY(!backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));
    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("speaker")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("headset")}}});

    const auto reloadCount = [&backend] {
        return std::count(backend.variantCommands.cbegin(), backend.variantCommands.cend(),
                          QVariantList{QStringLiteral("ao-reload")});
    };
    QTRY_VERIFY(reloadCount() >= 1);
}

void PlayerControllerAutoplayContextTest::explicitAudioDeviceReloadsOnDisappearAndReconnect()
{
    ConfigManager config;
    config.setAudioOutputDevice(QStringLiteral("alsa/device-1"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    controller.m_playbackState = PlayerController::Playing;
    backend.setRunning(true);
    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("alsa/device-1")}}});
    QVERIFY(!backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));

    backend.emitAudioDevices({});
    auto reloadCount = [&backend] {
        return std::count(backend.variantCommands.cbegin(), backend.variantCommands.cend(),
                          QVariantList{QStringLiteral("ao-reload")});
    };
    QTRY_VERIFY(reloadCount() >= 1);
    // A missing endpoint plays on the system default; the saved choice is kept.
    QVERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-device"),
                     QStringLiteral("auto")}));
    QCOMPARE(config.getAudioOutputDevice(), QStringLiteral("alsa/device-1"));
    QVERIFY(!controller.audioOutputStatus().value(QStringLiteral("preferredAvailable")).toBool());

    backend.variantCommands.clear();
    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("alsa/device-1")}}});
    QTRY_VERIFY(reloadCount() >= 1);
    QVERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-device"),
                     QStringLiteral("alsa/device-1")}));
    QVERIFY(controller.audioOutputStatus().value(QStringLiteral("preferredAvailable")).toBool());
}

void PlayerControllerAutoplayContextTest::exclusiveFailureFallsBackToSharedThenSystemDefault()
{
    ConfigManager config;
    config.setAudioOutputDeviceSelection(QStringLiteral("wasapi/{tv}"), QStringLiteral("TV"));
    config.setAudioExclusiveMode(true);
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);
    QSignalSpy noticeSpy(&controller, &PlayerController::audioOutputNotice);

    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("auto")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("wasapi/{tv}")},
                                          {QStringLiteral("description"), QStringLiteral("TV")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("wasapi/{spk}")},
                                          {QStringLiteral("description"), QStringLiteral("Speakers")}}});
    QVERIFY(controller.audioOutputStatus().value(QStringLiteral("exclusiveSupported")).toBool());

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 1},
                    {QStringLiteral("codec"), QStringLiteral("aac")},
                    {QStringLiteral("channels"), 2}}
    });
    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/aac"),
                                 QStringLiteral("aac-item"), 0, {}, {}, {},
                                 QStringLiteral("source-aac"), QStringLiteral("session-aac"),
                                 mediaSource, 1, -1);
    QCOMPARE(backend.startCallCount, 1);
    QVERIFY(controller.m_audioPlan.exclusivePcm);
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--audio-exclusive=yes")));
    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--audio-device=wasapi/{tv}")));
    const int stopCalls = backend.stopCallCount;

    for (bool exclusive : {false, true}) {
        backend.variantCommands.clear();
        controller.m_audioDeviceReloadTimer->start();
        config.setAudioExclusiveMode(exclusive);
        QVERIFY(!controller.m_audioDeviceReloadTimer->isActive());
        QCOMPARE(controller.m_audioPlan.exclusivePcm, exclusive);
        QVERIFY(backend.variantCommands.contains(
            QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-exclusive"),
                         exclusive ? QStringLiteral("yes") : QStringLiteral("no")}));
        QVERIFY(backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));
        QCOMPARE(backend.startCallCount, 1);
        QCOMPARE(backend.stopCallCount, stopCalls);
    }

    const QVariantMap pcm{{QStringLiteral("format"), QStringLiteral("s32")}};
    const QVariantMap active{{QStringLiteral("current-ao"), QStringLiteral("wasapi")},
                             {QStringLiteral("audio-exclusive"), true},
                             {QStringLiteral("audio-out-params"), pcm}};
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration), active);
    QVERIFY(controller.audioOutputStatus().value(QStringLiteral("exclusiveActive")).toBool());
    QCOMPARE(noticeSpy.count(), 0);

    // Device busy in another app's exclusive stream: mpv falls back to the null AO.
    QVariantMap nullOutput{{QStringLiteral("current-ao"), QStringLiteral("null")},
                           {QStringLiteral("audio-exclusive"), true},
                           {QStringLiteral("audio-out-params"), pcm}};
    backend.variantCommands.clear();
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration), nullOutput);
    QVERIFY(controller.m_exclusiveFallback);
    QVERIFY(!controller.m_audioPlan.exclusivePcm);
    QCOMPARE(controller.m_audioPlan.device, QStringLiteral("wasapi/{tv}"));
    QVERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-exclusive"), QStringLiteral("no")}));
    QVERIFY(backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));
    QCOMPARE(noticeSpy.count(), 1);
    QVERIFY(controller.audioOutputStatus().value(QStringLiteral("exclusiveFallback")).toBool());

    // Shared mode on the explicit endpoint also fails: use the system default for the session.
    nullOutput[QStringLiteral("audio-exclusive")] = false;
    backend.variantCommands.clear();
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration), nullOutput);
    QTRY_VERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-device"), QStringLiteral("auto")}));
    QCOMPARE(controller.m_audioPlan.device, QStringLiteral("auto"));
    QCOMPARE(config.getAudioOutputDevice(), QStringLiteral("wasapi/{tv}"));
    QCOMPARE(noticeSpy.count(), 2);
    QCOMPARE(backend.startCallCount, 1);
    QCOMPARE(backend.stopCallCount, stopCalls);
    QVERIFY(std::none_of(backend.commands.cbegin(), backend.commands.cend(), [](const QStringList &command) {
        return !command.isEmpty() && command.first() == QStringLiteral("loadfile");
    }));
}

void PlayerControllerAutoplayContextTest::selectAudioOutputDeviceReopensAudioWithoutRestart()
{
    ConfigManager config;
    config.setAudioOutputDevice(QStringLiteral("auto"));
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    backend.emitAudioDevices({QVariantMap{{QStringLiteral("name"), QStringLiteral("auto")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("pipewire")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("pipewire/dac")},
                                          {QStringLiteral("description"), QStringLiteral("USB DAC")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("alsa/hw:0")},
                                          {QStringLiteral("description"), QStringLiteral("Raw")}}});
    // System Default plus the active driver's endpoints only.
    const QVariantList options = controller.audioOutputOptions();
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("pipewire/dac"));
    QVERIFY(!controller.audioOutputStatus().value(QStringLiteral("exclusiveSupported")).toBool());

    controller.m_playbackState = PlayerController::Playing;
    controller.m_activeMediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 1},
                    {QStringLiteral("codec"), QStringLiteral("aac")}}
    });
    controller.m_selectedAudioTrack = 1;
    controller.updateTrackMappings(controller.m_activeMediaSource);
    backend.setRunning(true);

    controller.selectAudioOutputDevice(QStringLiteral("pipewire/dac"));
    QCOMPARE(config.getAudioOutputDevice(), QStringLiteral("pipewire/dac"));
    QCOMPARE(config.getAudioOutputDeviceDescription(), QStringLiteral("USB DAC"));
    QVERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-device"),
                     QStringLiteral("pipewire/dac")}));
    QVERIFY(backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));
    // Only the audio output reopens: no restart, reload, seek or pause change.
    QCOMPARE(backend.startCallCount, 0);
    QCOMPARE(backend.stopCallCount, 0);
    for (const QVariantList &command : std::as_const(backend.variantCommands)) {
        const QString name = command.value(0).toString();
        QVERIFY2(name != QStringLiteral("loadfile") && name != QStringLiteral("seek"), qPrintable(name));
        QVERIFY(command.value(1).toString() != QStringLiteral("pause"));
        QVERIFY(command.value(1).toString() != QStringLiteral("time-pos"));
    }
    QVERIFY(controller.audioOutputOptions().at(1).toMap().value(QStringLiteral("selected")).toBool());
}

void PlayerControllerAutoplayContextTest::idleAudioDeviceRefreshUsesBackendEnumeration()
{
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    // Startup enumerates once so the saved choice resolves before first playback.
    QTRY_COMPARE(backend.refreshAudioDevicesCount, 1);
    controller.refreshAudioOutputDevices();
    QCOMPARE(backend.refreshAudioDevicesCount, 2);

    // A running player already observes mpv's live list.
    backend.setRunning(true);
    controller.refreshAudioOutputDevices();
    QCOMPARE(backend.refreshAudioDevicesCount, 2);
}

void PlayerControllerAutoplayContextTest::audioDiagnosticsFallbackToPcmOnceAndThrottleNullRetries()
{
    ConfigManager config;
    config.setAudioPassthroughMode(QStringLiteral("custom"));
    config.setAudioPassthroughCodecs({QStringLiteral("truehd")});
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 2},
                    {QStringLiteral("codec"), QStringLiteral("truehd")}}
    });
    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/truehd"),
                                 QStringLiteral("truehd-item"), 0, {}, {}, {},
                                 QStringLiteral("source-truehd"), QStringLiteral("session-truehd"),
                                 mediaSource, 2, -1);
    QCOMPARE(backend.startCallCount, 1);
    QCOMPARE(controller.m_audioPlan.path, QStringLiteral("passthrough"));
    const int stopCalls = backend.stopCallCount;

    const QVariantMap mismatch{{QStringLiteral("current-ao"), QStringLiteral("alsa")},
                               {QStringLiteral("audio-out-params"),
                                QVariantMap{{QStringLiteral("format"), QStringLiteral("spdif-ac3")}}}};
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration),
                                    mismatch);
    QCOMPARE(controller.m_audioPlan.path, QStringLiteral("pcm"));
    QVERIFY(controller.m_forcePcmAudio);
    QVERIFY(backend.variantCommands.contains(
        QVariantList{QStringLiteral("set_property"), QStringLiteral("audio-spdif"), QString()}));
    QCOMPARE(backend.startCallCount, 1);
    QCOMPARE(backend.stopCallCount, stopCalls);
    QVERIFY(std::none_of(backend.commands.cbegin(), backend.commands.cend(), [](const QStringList &command) {
        return !command.isEmpty() && command.first() == QStringLiteral("loadfile");
    }));

    const QVariantMap nullOutput{{QStringLiteral("current-ao"), QStringLiteral("null")},
                                 {QStringLiteral("audio-out-params"),
                                  QVariantMap{{QStringLiteral("format"), QStringLiteral("s16")}}}};
    const auto reloadCount = [&backend] {
        return std::count(backend.variantCommands.cbegin(), backend.variantCommands.cend(),
                          QVariantList{QStringLiteral("ao-reload")});
    };
    const int fallbackReloads = reloadCount();
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration),
                                    nullOutput);
    const int afterFirstNull = reloadCount();
    QVERIFY(afterFirstNull > fallbackReloads);
    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration),
                                    nullOutput);
    QCOMPARE(reloadCount(), afterFirstNull);
    QCOMPARE(backend.startCallCount, 1);
    QCOMPARE(backend.stopCallCount, stopCalls);
}

void PlayerControllerAutoplayContextTest::audioDiagnosticsWithoutOutputEvidenceDoNotFallback()
{
    ConfigManager config;
    config.setAudioPassthroughMode(QStringLiteral("custom"));
    config.setAudioPassthroughCodecs({QStringLiteral("truehd")});
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager(&config);
    AuthenticationService authService(nullptr);
    PlaybackService playbackService(&authService, &config);
    FakeLibraryService libraryService(&authService);
    FakePlayerBackend backend;
    PlayerController controller(&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService);

    const QVariantMap mediaSource = buildMediaSource({
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 3},
                    {QStringLiteral("codec"), QStringLiteral("truehd")}}
    });
    controller.playUrlWithTracks(QStringLiteral("https://example.invalid/truehd"),
                                 QStringLiteral("truehd-item"), 0, {}, {}, {},
                                 QStringLiteral("source-truehd"), QStringLiteral("session-truehd"),
                                 mediaSource, 3, -1);
    QCOMPARE(controller.m_audioPlan.path, QStringLiteral("passthrough"));

    backend.emitPlaybackDiagnostics(QStringLiteral("audio|%1").arg(controller.m_audioPlanGeneration),
                                    {{QStringLiteral("current-ao"), QStringLiteral("alsa")}});
    QVERIFY(!controller.m_forcePcmAudio);
    QCOMPARE(controller.audioDiagnostics().value(QStringLiteral("outputObservable")).toBool(), false);
    QCOMPARE(controller.audioDiagnostics().value(QStringLiteral("passthroughActive")).toBool(), false);
    QCOMPARE(controller.audioDiagnostics().value(QStringLiteral("objectMetadataEvidence")).toString(),
             QStringLiteral("not retained by PCM or unverified output"));
    QVERIFY(!backend.variantCommands.contains(QVariantList{QStringLiteral("ao-reload")}));
    QCOMPARE(backend.startCallCount, 1);
}

void PlayerControllerAutoplayContextTest::externalSubtitleSelectionReaddsUnresolvedTrack()
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

    controller.addExternalSubtitleTrackInternal(
        QStringLiteral("https://example.invalid/subtitle.vtt"),
        QStringLiteral("English"),
        QStringLiteral("eng"),
        -1000,
        false);
    QCOMPARE(controller.m_externalSubtitleTrackMap.value(-1000), -1);
    backend.variantCommands.clear();

    controller.setSelectedSubtitleTrack(-1000);

    QVERIFY(backend.variantCommands.contains(QVariantList{
        QStringLiteral("sub-add"),
        QStringLiteral("https://example.invalid/subtitle.vtt"),
        QStringLiteral("select"),
        QStringLiteral("English"),
        QStringLiteral("eng")
    }));
}


QTEST_MAIN(PlayerControllerAutoplayContextTest)
#include "PlayerControllerAutoplayContextTest.moc"
