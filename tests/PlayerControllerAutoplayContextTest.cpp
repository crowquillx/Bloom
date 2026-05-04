#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "player/PlayerController.h"

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
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

    QString getStreamUrl(const QString &itemId) override
    {
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                  int audioStreamIndex = -1, int subtitleStreamIndex = -1) override
    {
        Q_UNUSED(mediaSourceId);
        Q_UNUSED(audioStreamIndex);
        Q_UNUSED(subtitleStreamIndex);
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QStringList requestedSeriesIds;
    QStringList requestedExcludeIds;
    QStringList requestedContexts;
    QStringList requestedSeriesDetailsIds;
    QStringList requestedLibraryResolutionIds;
};

static MediaSourceInfo buildMediaSourceInfo(const QString &id,
                                            const QString &name,
                                            const QString &path,
                                            const QList<QVariantMap> &streams,
                                            int defaultAudio = -1,
                                            int defaultSubtitle = -1,
                                            const QString &container = QStringLiteral("mkv"),
                                            int bitRate = 0,
                                            qint64 runTimeTicks = 0)
{
    MediaSourceInfo info;
    info.id = id;
    info.name = name;
    info.path = path;
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
    void thresholdMetRequestsNextEpisodeDirectly();
    void userStopPastThresholdRequestsNextEpisode();
    void userStopBelowThresholdWaitsForBackendExit();
    void playbackEndedUpgradesQueuedStopFinalization();
    void nextEpisodeNavigationUsesPendingTrackContext();
    void nextEpisodeIgnoresMismatchedSeries();
    void playbackPrefetchIgnoresGenericNextEpisodeResponses();
    void autoplayPlaybackInfoErrorFallsBackToBasicPlayback();
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
    void requestPlaybackPromptsForVersionSelection();
    void requestPlaybackRecoversLibraryProfileFromSeriesDetails();
    void requestPlaybackWaitsForSeriesDetailsParentIdBeforeStarting();
    void requestPlaybackUsesRecoveredLibraryWhenSeriesDetailsArriveBeforePlaybackInfo();
    void requestPlaybackFallsBackWithoutRecoveredLibraryProfileWhenParentIdMissing();
    void requestPlaybackKeepsSeriesProfilePriorityOverRecoveredLibrary();
    void multipartIntermediateEndIsIgnoredUntilFinalSegment();
    void versionAffinityPrefersMatchingParentPath();
    void startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned();
    void startupTrackSelectionRespectsPinnedUrlUnlessUserOverride();
    void runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone();
    void backendTrackSyncUsesReverseMap();

private:
    QTemporaryDir m_configHome;
    QByteArray m_previousConfigHome;
    bool m_hadPreviousConfigHome = false;
};

void PlayerControllerAutoplayContextTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_LINUX
    QVERIFY(m_configHome.isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    m_hadPreviousConfigHome = !m_previousConfigHome.isNull();
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
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
#endif
    QStandardPaths::setTestModeEnabled(false);
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

    QJsonObject episodeData{
        {QStringLiteral("Id"), QStringLiteral("episode-2")},
        {QStringLiteral("Name"), QStringLiteral("Episode 2")},
        {QStringLiteral("SeriesName"), QStringLiteral("Series A")},
        {QStringLiteral("ParentIndexNumber"), 1},
        {QStringLiteral("IndexNumber"), 2}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QJsonObject, episodeData),
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

    QJsonObject episodeData{
        {QStringLiteral("Id"), QStringLiteral("episode-x")},
        {QStringLiteral("Name"), QStringLiteral("Episode X")},
        {QStringLiteral("SeriesName"), QStringLiteral("Unexpected Series")},
        {QStringLiteral("ParentIndexNumber"), 1},
        {QStringLiteral("IndexNumber"), 9}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("series-other")),
                                      Q_ARG(QJsonObject, episodeData),
                                      Q_ARG(QString, QStringLiteral("player:resolve:series-1:item-1"))));
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, 4);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, 7);
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

    const QJsonObject specialEpisode{
        {QStringLiteral("Id"), QStringLiteral("special-s0e1")},
        {QStringLiteral("Name"), QStringLiteral("Special 1")},
        {QStringLiteral("SeriesName"), QStringLiteral("Series A")},
        {QStringLiteral("ParentIndexNumber"), 0},
        {QStringLiteral("IndexNumber"), 1},
        {QStringLiteral("ParentId"), QStringLiteral("season-0")}
    };
    const QJsonObject genericEpisode{
        {QStringLiteral("Id"), QStringLiteral("s2e1")},
        {QStringLiteral("Name"), QStringLiteral("Episode 1")},
        {QStringLiteral("SeriesName"), QStringLiteral("Series A")},
        {QStringLiteral("ParentIndexNumber"), 2},
        {QStringLiteral("IndexNumber"), 1},
        {QStringLiteral("ParentId"), QStringLiteral("season-2")}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QJsonObject, specialEpisode),
                                      Q_ARG(QString, QStringLiteral("player:prefetch:series-1:s1e22"))));
    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onNextEpisodeLoaded",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("series-1")),
                                      Q_ARG(QJsonObject, genericEpisode),
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
    const QJsonObject navigatedEpisode = signalArgs.at(0).toJsonObject();
    QCOMPARE(navigatedEpisode.value(QStringLiteral("Id")).toString(),
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
    controller.m_pendingAutoplayEpisodeData = QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("episode-9")},
        {QStringLiteral("ParentId"), QStringLiteral("season-9")},
        {QStringLiteral("UserData"), QJsonObject{{QStringLiteral("PlaybackPositionTicks"), 4200000000.0}}}
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
    QCOMPARE(controller.m_startPositionTicks, 4200000000LL);
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
    controller.m_pendingAutoplayEpisodeData = QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("episode-11")},
        {QStringLiteral("SeasonId"), QStringLiteral("season-target")}
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
    controller.m_pendingAutoplayEpisodeData = QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("episode-12")},
        {QStringLiteral("SeasonId"), QStringLiteral("season-global")}
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
    controller.m_pendingAutoplayEpisodeData = QJsonObject{
        {QStringLiteral("Id"), QStringLiteral("episode-10")},
        {QStringLiteral("SeasonId"), QStringLiteral("season-10")}
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

void PlayerControllerAutoplayContextTest::requestPlaybackPromptsForVersionSelection()
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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({versionA, versionB}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});

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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({mediaSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});

    QVERIFY(backend.lastStartArgs.isEmpty());
    QVERIFY(backend.lastStartUrl.isEmpty());

    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
    QCOMPARE(backend.lastStartUrl, QStringLiteral("https://example.invalid/episode-1"));
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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({mediaSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});

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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({mediaSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});

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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({mediaSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});

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

    emit playbackService.playbackInfoLoaded(QStringLiteral("episode-1"),
                                            buildPlaybackInfo({mediaSource}));
    emit playbackService.additionalPartsLoaded(QStringLiteral("episode-1"), QJsonArray{});
    emit libraryService.itemLibraryResolved(QStringLiteral("series-1"),
                                            QStringLiteral("library-1"));

    QVERIFY(backend.lastStartArgs.contains(QStringLiteral("--test-series-profile=yes")));
    QVERIFY(!backend.lastStartArgs.contains(QStringLiteral("--test-library-profile=yes")));
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
            {QStringLiteral("runTimeTicks"), 600000000LL},
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
            {QStringLiteral("runTimeTicks"), 600000000LL},
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
    QCOMPARE(controller.m_activePlaybackSegmentOffsetTicks, 600000000LL);
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

    controller.m_pendingUrl = QStringLiteral("https://example.invalid/stream?AudioStreamIndex=4&SubtitleStreamIndex=8");
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
