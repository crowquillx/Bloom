#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QCoreApplication>

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
        Q_UNUSED(args);
        Q_UNUSED(mediaUrl);
        m_running = true;
        emit stateChanged(true);
    }

    void stopMpv() override
    {
        if (!m_running) {
            return;
        }
        m_running = false;
        emit stateChanged(false);
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
    QList<QVariantList> variantCommands;
    QList<QStringList> commands;

    void emitAudioTrackId(int mpvTrackId)
    {
        emit audioTrackChanged(mpvTrackId);
    }

    void emitSubtitleTrackId(int mpvTrackId)
    {
        emit subtitleTrackChanged(mpvTrackId);
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

    void getNextUnplayedEpisode(const QString &seriesId, const QString &excludeItemId = QString()) override
    {
        Q_UNUSED(excludeItemId);
        requestedSeriesIds.append(seriesId);
    }

    QString getStreamUrl(const QString &itemId) override
    {
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QStringList requestedSeriesIds;
};

static QVariantMap buildMediaSource(const QList<QVariantMap> &streams,
                                    int defaultAudio = -1,
                                    int defaultSubtitle = -1)
{
    QVariantList mediaStreams;
    for (const QVariantMap &stream : streams) {
        mediaStreams.append(stream);
    }

    return QVariantMap{
        {QStringLiteral("id"), QStringLiteral("media-source-1")},
        {QStringLiteral("defaultAudioStreamIndex"), defaultAudio},
        {QStringLiteral("defaultSubtitleStreamIndex"), defaultSubtitle},
        {QStringLiteral("mediaStreams"), mediaStreams}
    };
}

class PlayerControllerAutoplayContextTest : public QObject
{
    Q_OBJECT

private slots:
    void thresholdMetRequestsNextEpisodeDirectly();
    void userStopPastThresholdRequestsNextEpisode();
    void nextEpisodeNavigationUsesPendingTrackContext();
    void nextEpisodeIgnoresMismatchedSeries();
    void autoplayPlaybackInfoErrorFallsBackToBasicPlayback();
    void autoplayPlaybackInfoUsesStoredSubtitlePreferenceWhenOverrideUnset();
    void staleAutoplayPlaybackInfoResponseFallsBackAfterTimeout();
    void playUrlWithTracksKeepsNewSessionMetadataWhenReplacingPlayback();
    void embeddedVideoShrinkToggleEmitsAndPersists();
    void startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned();
    void startupTrackSelectionRespectsPinnedUrlUnlessUserOverride();
    void runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone();
    void backendTrackSyncUsesReverseMap();
};

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

    controller.handlePlaybackStopAndAutoplay(PlayerController::Event::PlaybackEnd);

    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedSeriesIds.first(), QStringLiteral("series-1"));
    QVERIFY(controller.m_shouldAutoplay);
    QVERIFY(controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QCOMPARE(controller.m_pendingAutoplayItemId, QStringLiteral("item-1"));
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));

    controller.m_hasEvaluatedCompletionForAttempt = true;
    controller.handlePlaybackStopAndAutoplay(PlayerController::Event::PlaybackEnd);
    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
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

    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedSeriesIds.first(), QStringLiteral("series-1"));
    QVERIFY(controller.m_shouldAutoplay);
    QVERIFY(controller.m_waitingForNextEpisodeAtPlaybackEnd);
    QCOMPARE(controller.m_pendingAutoplayItemId, QStringLiteral("item-1"));
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
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
                                      Q_ARG(QJsonObject, episodeData)));
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
                                      Q_ARG(QJsonObject, episodeData)));
    QCoreApplication::processEvents();

    QCOMPARE(navigationSpy.count(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, 4);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, 7);
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
    QCOMPARE(controller.selectedSubtitleTrack(), 12);
    QCOMPARE(controller.m_mediaSourceId, QStringLiteral("media-source-11"));
    QCOMPARE(controller.m_playSessionId, QStringLiteral("play-session-11"));
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
