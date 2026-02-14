#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>

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
        Q_UNUSED(command);
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
};

class FakeLibraryService final : public LibraryService
{
    Q_OBJECT

public:
    explicit FakeLibraryService(AuthenticationService *authService, QObject *parent = nullptr)
        : LibraryService(authService, parent)
    {
    }

    void getNextUnplayedEpisode(const QString &seriesId) override
    {
        requestedSeriesIds.append(seriesId);
    }

    QString getStreamUrl(const QString &itemId) override
    {
        return QStringLiteral("https://example.invalid/") + itemId;
    }

    QStringList requestedSeriesIds;
};

class PlayerControllerAutoplayContextTest : public QObject
{
    Q_OBJECT

private slots:
    void itemMarkedPlayedUsesPendingContext();
    void nextEpisodeNavigationUsesPendingTrackContext();
    void nextEpisodeIgnoresMismatchedSeries();
    void embeddedVideoShrinkToggleEmitsAndPersists();
    void startupTrackSelectionUsesCanonicalMapWhenUrlNotPinned();
    void startupTrackSelectionRespectsPinnedUrlUnlessUserOverride();
    void runtimeTrackSelectionUsesCanonicalMapAndSubtitleNone();
};

void PlayerControllerAutoplayContextTest::itemMarkedPlayedUsesPendingContext()
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
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onItemMarkedPlayed",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("item-1"))));
    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
    QCOMPARE(libraryService.requestedSeriesIds.first(), QStringLiteral("series-1"));

    QVERIFY(QMetaObject::invokeMethod(&controller,
                                      "onItemMarkedPlayed",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("different-item"))));
    QCOMPARE(libraryService.requestedSeriesIds.size(), 1);
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
    controller.m_pendingAutoplayItemId = QStringLiteral("item-1");
    controller.m_pendingAutoplaySeriesId = QStringLiteral("series-1");
    controller.m_pendingAutoplayAudioTrack = 3;
    controller.m_pendingAutoplaySubtitleTrack = 6;

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

    QCOMPARE(navigationSpy.count(), 1);
    const QList<QVariant> signalArgs = navigationSpy.takeFirst();
    QCOMPARE(signalArgs.at(1).toString(), QStringLiteral("series-1"));
    QCOMPARE(signalArgs.at(2).toInt(), 3);
    QCOMPARE(signalArgs.at(3).toInt(), 6);

    QVERIFY(!controller.m_shouldAutoplay);
    QCOMPARE(controller.m_pendingAutoplayItemId, QString());
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QString());
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, -1);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, -1);
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

    QCOMPARE(navigationSpy.count(), 0);
    QVERIFY(controller.m_shouldAutoplay);
    QCOMPARE(controller.m_pendingAutoplaySeriesId, QStringLiteral("series-1"));
    QCOMPARE(controller.m_pendingAutoplayAudioTrack, 4);
    QCOMPARE(controller.m_pendingAutoplaySubtitleTrack, 7);
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
    controller.updateTrackMappings(
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 7}, {QStringLiteral("mpvTrackId"), 2}}
        },
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 11}, {QStringLiteral("mpvTrackId"), 3}}
        });

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
    controller.updateTrackMappings(
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 4}, {QStringLiteral("mpvTrackId"), 1}},
            QVariantMap{{QStringLiteral("jellyfinIndex"), 9}, {QStringLiteral("mpvTrackId"), 2}}
        },
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 8}, {QStringLiteral("mpvTrackId"), 1}},
            QVariantMap{{QStringLiteral("jellyfinIndex"), 10}, {QStringLiteral("mpvTrackId"), 2}}
        });

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
    controller.updateTrackMappings(
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 5}, {QStringLiteral("mpvTrackId"), 2}}
        },
        QVariantList{
            QVariantMap{{QStringLiteral("jellyfinIndex"), 13}, {QStringLiteral("mpvTrackId"), 4}}
        });

    controller.setSelectedAudioTrack(5);
    controller.setSelectedSubtitleTrack(13);
    controller.setSelectedSubtitleTrack(-1);

    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "aid", 2}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", 4}));
    QVERIFY(backend.variantCommands.contains(QVariantList{"set_property", "sid", "no"}));
    QCOMPARE(trackPrefs.getAudioTrack(QStringLiteral("season-42")), 5);
    QCOMPARE(trackPrefs.getSubtitleTrack(QStringLiteral("season-42")), -1);
}

QTEST_MAIN(PlayerControllerAutoplayContextTest)
#include "PlayerControllerAutoplayContextTest.moc"
