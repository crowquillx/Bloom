#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>

#include "models/SubtitleSearch.h"
#include "models/SubtitleSync.h"
#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "player/PlayerController.h"
#include "player/backend/IPlayerBackend.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"
#include "utils/TrackPreferencesManager.h"
#include "viewmodels/SubtitleSyncViewModel.h"

#include "TestConfigIsolation.h"

namespace {

class ControlledReply final : public QNetworkReply
{
public:
    ControlledReply(const QNetworkRequest &request, int statusCode, QByteArray payload,
                    const QByteArray &etag, bool delayed, QObject *parent)
        : QNetworkReply(parent)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (!etag.isEmpty()) {
            setRawHeader(QByteArrayLiteral("ETag"), etag);
        }
        if (statusCode >= 400) {
            setError(QNetworkReply::UnknownServerError, QStringLiteral("test network error"));
        }
        open(QIODevice::ReadOnly);
        if (!delayed) {
            QTimer::singleShot(0, this, [this]() { complete(); });
        }
    }

    void abort() override {}

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

    void complete()
    {
        if (isFinished()) {
            return;
        }
        setFinished(true);
        if (!m_payload.isEmpty()) {
            emit readyRead();
        }
        emit finished();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, m_payload.size() - m_offset);
        if (count <= 0) {
            return -1;
        }
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_payload;
    qint64 m_offset = 0;
};

struct ResponsePlan
{
    int statusCode;
    QByteArray payload;
    QByteArray etag = {};
    bool delayed = false;
};

struct RecordedRequest
{
    QNetworkAccessManager::Operation operation;
    QByteArray verb;
    QNetworkRequest request;
    QByteArray body;
};

class ControlledNetworkAccessManager final : public QNetworkAccessManager
{
public:
    QList<ResponsePlan> responses;
    QList<RecordedRequest> requests;
    QList<QPointer<ControlledReply>> replies;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        requests.append({operation,
                         request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(),
                         request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const ResponsePlan plan = responses.isEmpty()
            ? ResponsePlan{500, QByteArrayLiteral(R"({"error":"queue_empty"})")}
            : responses.takeFirst();
        auto *reply = new ControlledReply(request, plan.statusCode, plan.payload, plan.etag,
                                          plan.delayed, this);
        replies.append(reply);
        return reply;
    }
};

class ExposedAuthenticationService final : public AuthenticationService
{
public:
    ExposedAuthenticationService(HttpTransport *transport, IProviderAdapter *adapter)
        : AuthenticationService(nullptr, transport, adapter)
    {
    }
};

class FakePlayerBackend final : public IPlayerBackend
{
    Q_OBJECT

public:
    using IPlayerBackend::IPlayerBackend;

    QString backendName() const override { return QStringLiteral("fake-backend"); }
    void startMpv(const QString &, const QStringList &, const QString &) override {}
    void appendUrlsToPlaylist(const QStringList &) override {}
    void stopMpv() override {}
    bool isRunning() const override { return true; }
    void sendCommand(const QStringList &command) override { commands.append(command); }
    void sendVariantCommand(const QVariantList &command) override { variantCommands.append(command); }
    bool supportsEmbeddedVideo() const override { return true; }
    bool attachVideoTarget(QObject *) override { return false; }
    void detachVideoTarget(QObject *) override {}
    void setVideoViewport(const QRectF &) override {}

    void emitSubtitleTrackId(int mpvTrackId) { emit subtitleTrackChanged(mpvTrackId); }

    QList<QVariantList> commandsNamed(const QString &name) const
    {
        QList<QVariantList> found;
        for (const QVariantList &command : variantCommands) {
            if (!command.isEmpty() && command.first().toString() == name) {
                found.append(command);
            }
        }
        return found;
    }

    QList<QVariantList> variantCommands;
    QList<QStringList> commands;
};

// Records the view model's calls; the test answers them with the service's
// own signals.
class FakePlaybackService final : public PlaybackService
{
public:
    using PlaybackService::PlaybackService;

    struct Call
    {
        QString kind;
        QString key;
        QString mediaFileId;
        QString syncKey;
        QByteArray etag;
    };

    bool supportsSubtitleSync() const override { return true; }
    QString subtitleSyncIdentity() const override { return connectionId; }
    void loadSubtitleSyncStatus(const QString &requestKey) override
    {
        calls.append({QStringLiteral("status"), requestKey, {}, {}, {}});
    }
    void readSubtitleSync(const QString &requestKey, const QString &mediaFileId,
                          const QString &syncKey) override
    {
        calls.append({QStringLiteral("read"), requestKey, mediaFileId, syncKey, {}});
    }
    void startSubtitleSync(const QString &requestKey, const QString &mediaFileId,
                           const QString &syncKey) override
    {
        calls.append({QStringLiteral("start"), requestKey, mediaFileId, syncKey, {}});
    }
    void resetSubtitleTiming(const QString &requestKey, const QString &mediaFileId,
                             const QString &syncKey, const QByteArray &etag) override
    {
        calls.append({QStringLiteral("reset"), requestKey, mediaFileId, syncKey, etag});
    }
    void cancelSubtitleSync(const QString &requestKey) override { cancelled.append(requestKey); }

    Call last(const QString &kind) const
    {
        for (qsizetype i = calls.size() - 1; i >= 0; --i) {
            if (calls.at(i).kind == kind) {
                return calls.at(i);
            }
        }
        return {};
    }

    qsizetype count(const QString &kind) const
    {
        qsizetype total = 0;
        for (const Call &call : calls) {
            total += call.kind == kind ? 1 : 0;
        }
        return total;
    }

    QString connectionId = QStringLiteral("connection-1");
    QList<Call> calls;
    QStringList cancelled;
};

bool signInToSilo(ControlledNetworkAccessManager &manager, ExposedAuthenticationService &auth,
                  ConfigManager &config)
{
    manager.responses = {
        {200, QByteArrayLiteral(
                  R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})")},
        {200, QByteArrayLiteral(
                  R"({"items":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true}]})")}};
    auth.initialize(&config);
    if (!QTest::qWaitFor([&auth]() { return !auth.isRestoringSession(); }, 1000)) {
        return false;
    }
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"), QStringLiteral("Alice"),
                      QStringLiteral("password"));
    const bool signedIn = QTest::qWaitFor([&auth]() { return auth.isAuthenticated(); }, 1000);
    manager.requests.clear();
    manager.replies.clear();
    return signedIn;
}

const QString kSyncKey = QStringLiteral("stored-17");
const QString kOtherSyncKey = QStringLiteral("stored-18");
const QString kFileId = QStringLiteral("42");

QByteArray stateBody(const QString &status, qint64 offsetMs = 0, double progress = 0.0,
                     const QString &key = kSyncKey, const QString &failure = QString())
{
    QJsonObject subtitle{{"key", key}, {"media_file_id", kFileId}, {"source", "downloaded"},
                         {"language", "en"}, {"format", "srt"}, {"label", "Release"},
                         {"timing", QJsonObject{{"offset_ms", offsetMs}, {"scale", 1}}}};
    if (!status.isEmpty()) {
        QJsonObject job{{"id", "9"}, {"status", status}, {"trigger", "manual"},
                        {"confidence", QJsonValue::Null},
                        {"created_at", "2026-11-01T10:00:00.000Z"},
                        {"finished_at", QJsonValue::Null}, {"progress", progress}};
        if (status == QStringLiteral("running")) {
            job["phase"] = QStringLiteral("analyzing");
        }
        if (!failure.isEmpty()) {
            job["failure"] = failure;
        }
        subtitle["sync"] = job;
    }
    return QJsonDocument(QJsonObject{{"subtitle", subtitle}}).toJson(QJsonDocument::Compact);
}

Bloom::SubtitleSyncState state(const QString &status, qint64 offsetMs = 0, double progress = 0.0,
                               const QString &key = kSyncKey, const QString &failure = QString())
{
    Bloom::SubtitleSyncState result;
    result.key = key;
    result.mediaFileId = kFileId;
    result.timing.offsetMs = offsetMs;
    if (status == QStringLiteral("pending")) result.job.status = Bloom::SubtitleSyncStatus::Pending;
    if (status == QStringLiteral("running")) result.job.status = Bloom::SubtitleSyncStatus::Running;
    if (status == QStringLiteral("synced")) result.job.status = Bloom::SubtitleSyncStatus::Synced;
    if (status == QStringLiteral("noMatch")) result.job.status = Bloom::SubtitleSyncStatus::NoMatch;
    if (status == QStringLiteral("failed")) result.job.status = Bloom::SubtitleSyncStatus::Failed;
    if (!status.isEmpty()) result.job.id = QStringLiteral("9");
    result.job.progress = progress;
    result.job.failure = failure;
    if (result.job.status == Bloom::SubtitleSyncStatus::Running) {
        result.job.phase = QStringLiteral("analyzing");
    }
    return result;
}

Bloom::SubtitleSyncCapability capability(bool autoSync = false, bool external = true)
{
    return {true, autoSync, external};
}

QVariantMap externalOption(int index, const QString &syncKey, const QString &language)
{
    QVariantMap option{{"index", index}, {"isExternal", true},
                       {"externalUrl", QStringLiteral("https://silo.example.test/api/v2/stream/s/subtitles/%1.srt")
                                           .arg(-index)},
                       {"displayTitle", QStringLiteral("Release %1").arg(language)},
                       {"language", language}};
    if (!syncKey.isEmpty()) {
        option["syncKey"] = syncKey;
    }
    return option;
}

} // namespace

class SubtitleSyncTest : public QObject
{
    Q_OBJECT

private slots:
    void serviceSendsSiloRequests();
    void serviceDropsCancelledAnswers();
    void syncPollsReloadsAndOverridesTheDelay();
    void reloadWaitsForTheNewCopy();
    void failedReloadKeepsTheOldCopy();
    void bufferingDefersTheReload();
    void reanchorLoadsTheNewTimingFresh();
    void hlsOffsetIsAppliedOnceAfterTheReload();
    void pollLimitAndALaterSync();
    void autoSyncIsReadAfterTheSubtitleLoads();
    void externalFlagAndUnknownSubtitlesHideTheRow();
    void identityChangesSettleRequests();
    void downloadedSubtitlesSyncAndReloadInPlace();
    void failedSyncLeavesTheSubtitleAlone();
    void noMatchKeepsTheTimingAndTheDelay();
    void stopCancelsPolling();
    void otherSubtitleCancelsAndRereads();
    void resetRestoresTheOriginalTiming();
    void resetSettlesARefusedReplay();
    void onlySyncableExternalSubtitlesAreOffered();

private:
    struct PlayerFixture;
};

void SubtitleSyncTest::serviceSendsSiloRequests()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));
    PlaybackService service(&auth);
    QVERIFY(service.supportsSubtitleSync());
    QSignalSpy status(&service, &PlaybackService::subtitleSyncStatusLoaded);
    QSignalSpy loaded(&service, &PlaybackService::subtitleSyncStateLoaded);
    QSignalSpy failed(&service, &PlaybackService::subtitleSyncFailed);

    manager.responses = {{200, QByteArrayLiteral(
        R"({"auto_sync":false,"external":true,"revision":"r","state":"available","allowed":true})")}};
    service.loadSubtitleSyncStatus(QStringLiteral("status-1"));
    QTRY_COMPARE_WITH_TIMEOUT(status.count(), 1, 2000);
    QVERIFY(status.first().at(1).value<Bloom::SubtitleSyncCapability>().available);
    QVERIFY(status.first().at(1).value<Bloom::SubtitleSyncCapability>().external);
    QVERIFY(!status.first().at(1).value<Bloom::SubtitleSyncCapability>().autoSync);
    QCOMPARE(manager.requests.last().request.url().path(), QStringLiteral("/api/v2/subtitles/sync/status"));
    QCOMPARE(manager.requests.last().request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));

    // The read hands over the ETag.
    manager.responses = {{200, stateBody(QString()), QByteArrayLiteral("\"v1\"")}};
    service.readSubtitleSync(QStringLiteral("read-1"), kFileId, kSyncKey);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 2000);
    QCOMPARE(manager.requests.last().operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(manager.requests.last().request.url().path(),
             QStringLiteral("/api/v2/subtitles/42/sync/stored-17"));
    QCOMPARE(loaded.first().at(1).value<Bloom::SubtitleSyncState>().key, kSyncKey);
    QCOMPARE(loaded.first().at(2).toByteArray(), QByteArrayLiteral("\"v1\""));

    // startSubtitleSync is coalescing, so a lost answer is replayed.
    manager.responses = {{503, QByteArrayLiteral(R"({"status":503})")},
                         {202, stateBody(QStringLiteral("pending"))}};
    qsizetype sent = manager.requests.size();
    service.startSubtitleSync(QStringLiteral("start-1"), kFileId, kSyncKey);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
    QCOMPARE(manager.requests.size(), sent + 2);
    QCOMPARE(manager.requests.last().operation, QNetworkAccessManager::PostOperation);
    QVERIFY(loaded.last().at(1).value<Bloom::SubtitleSyncState>().isActive());

    // The reset is guarded by the validator; a refusal reaches the caller.
    manager.responses = {{412, QByteArrayLiteral(R"({"status":412})")}};
    sent = manager.requests.size();
    service.resetSubtitleTiming(QStringLiteral("reset-1"), kFileId, kSyncKey, QByteArrayLiteral("\"v1\""));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), sent + 1);
    QCOMPARE(manager.requests.last().operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(manager.requests.last().request.url().path(),
             QStringLiteral("/api/v2/subtitles/42/sync/stored-17/timing"));
    QCOMPARE(manager.requests.last().request.rawHeader("If-Match"), QByteArrayLiteral("\"v1\""));
    const QJsonObject body = QJsonDocument::fromJson(manager.requests.last().body).object();
    QCOMPARE(body.value("offset_ms").toInt(-1), 0);
    QCOMPARE(body.value("scale").toDouble(), 1.0);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("reset-1"));
    QCOMPARE(failed.first().at(1).toInt(), 412);

    // A key the server's path pattern refuses never goes out.
    sent = manager.requests.size();
    service.startSubtitleSync(QStringLiteral("start-2"), kFileId, QStringLiteral("embedded-3"));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(manager.requests.size(), sent);
}

void SubtitleSyncTest::serviceDropsCancelledAnswers()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));
    PlaybackService service(&auth);
    QSignalSpy loaded(&service, &PlaybackService::subtitleSyncStateLoaded);
    QSignalSpy failed(&service, &PlaybackService::subtitleSyncFailed);

    manager.responses = {{200, stateBody(QStringLiteral("running")), {}, true}};
    service.readSubtitleSync(QStringLiteral("poll-1"), kFileId, kSyncKey);
    QTRY_COMPARE_WITH_TIMEOUT(manager.replies.size(), 1, 2000);
    service.cancelSubtitleSync(QStringLiteral("poll-1"));
    if (manager.replies.first()) {
        manager.replies.first()->complete();
    }
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

struct SubtitleSyncTest::PlayerFixture
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation{directory.path()};
    ConfigManager config;
    TrackPreferencesManager trackPrefs;
    DisplayManager displayManager{&config};
    AuthenticationService authService{nullptr};
    FakePlaybackService playbackService{&authService};
    LibraryService libraryService{&authService};
    FakePlayerBackend backend;
    PlayerController controller{&backend, &config, &trackPrefs, &displayManager,
                                &playbackService, &libraryService, &authService};

    // Plays item-1 with two downloaded Silo subtitles and one embedded track;
    // the first download is selected and loaded as mpv sid 3.
    void play(const QString &sessionId = QStringLiteral("session-1"))
    {
        controller.m_currentItemId = QStringLiteral("item-1");
        controller.m_mediaSourceId = kFileId;
        controller.m_playSessionId = sessionId;
        controller.m_availableSubtitleTracks = {
            QVariantMap{{"index", 2}, {"language", "eng"}},
            externalOption(-1000, kSyncKey, QStringLiteral("eng")),
            externalOption(-1001, kOtherSyncKey, QStringLiteral("ita")),
            externalOption(-1002, QString(), QStringLiteral("fre"))};
        controller.m_externalSubtitleTrackMap = {{-1000, 3}, {-1001, -1}, {-1002, -1}};
        controller.m_selectedSubtitleTrack = -1000;
        controller.m_playbackState = PlayerController::Playing;
        emit controller.currentItemIdChanged();
        emit controller.isPlaybackActiveChanged();
    }

    void stop()
    {
        controller.m_playbackState = PlayerController::Idle;
        emit controller.isPlaybackActiveChanged();
    }

    // Answers the capability read and the first read of the selected subtitle.
    void becomeAvailable(SubtitleSyncViewModel &model, const Bloom::SubtitleSyncState &initial,
                         const Bloom::SubtitleSyncCapability &offered = capability())
    {
        const FakePlaybackService::Call status = playbackService.last(QStringLiteral("status"));
        QVERIFY(!status.key.isEmpty());
        emit playbackService.subtitleSyncStatusLoaded(status.key, offered);
        QVERIFY(model.available());
        const FakePlaybackService::Call read = playbackService.last(QStringLiteral("read"));
        QCOMPARE(read.mediaFileId, kFileId);
        QCOMPARE(read.syncKey, kSyncKey);
        emit playbackService.subtitleSyncStateLoaded(read.key, initial, QByteArrayLiteral("\"v1\""));
    }
};

void SubtitleSyncTest::syncPollsReloadsAndOverridesTheDelay()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    QSignalSpy availableChanged(&model, &SubtitleSyncViewModel::availableChanged);

    fixture.controller.m_currentSeasonId = QStringLiteral("season-1");
    fixture.play();
    QVERIFY(!model.available());
    fixture.becomeAvailable(model, state(QString()));
    QCOMPARE(availableChanged.count(), 1);
    QVERIFY(!model.busy());
    QVERIFY(!model.canReset());
    // The season's delay, through the real path.
    fixture.controller.setSubtitleDelayMs(2300);
    QCOMPARE(fixture.trackPrefs.getSeasonPreferences(QStringLiteral("season-1")).subtitleDelayMs, 2300);

    model.startSync();
    QVERIFY(model.busy());
    const FakePlaybackService::Call start = service.last(QStringLiteral("start"));
    QCOMPARE(start.mediaFileId, kFileId);
    QCOMPARE(start.syncKey, kSyncKey);
    emit service.subtitleSyncStateLoaded(start.key, state(QStringLiteral("pending")), {});
    QCOMPARE(model.phase(), SubtitleSyncViewModel::Phase::Syncing);
    QCOMPARE(model.statusText(), QStringLiteral("Waiting to start…"));
    // A second press while the job runs sends nothing.
    model.startSync();
    QCOMPARE(service.count(QStringLiteral("start")), 1);

    // The poll follows the job every few seconds.
    const qsizetype reads = service.count(QStringLiteral("read"));
    QTRY_COMPARE_WITH_TIMEOUT(service.count(QStringLiteral("read")), reads + 1,
                              SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("running"), 0, 0.42), {});
    QVERIFY(model.busy());
    QCOMPARE(model.progress(), 0.42);
    QCOMPARE(model.statusText(), QStringLiteral("Listening to the audio… 42%"));
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-add")).isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(service.count(QStringLiteral("read")), reads + 2,
                              SubtitleSyncViewModel::kPollIntervalMs + 2000);
    fixture.backend.variantCommands.clear();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 2300), {});

    // The same URL is loaded again, selected; the old copy stays until mpv
    // reports the new one, and the outcome waits for it too.
    QCOMPARE(fixture.backend.variantCommands.size(), 1);
    const QVariantList add = fixture.backend.variantCommands.at(0);
    QCOMPARE(add.value(0).toString(), QStringLiteral("sub-add"));
    QCOMPARE(add.value(1).toString(),
             QStringLiteral("https://silo.example.test/api/v2/stream/s/subtitles/1000.srt"));
    QCOMPARE(add.value(2).toString(), QStringLiteral("select"));
    QVERIFY(model.busy());
    QCOMPARE(model.statusText(), QStringLiteral("Loading the new timing…"));
    QCOMPARE(notices.count(), 0);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 2300);

    // mpv's sid report maps the new copy to the same menu entry; only then
    // does the old copy go.
    fixture.backend.emitSubtitleTrackId(7);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), -1000);
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(-1000), 7);
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-remove")),
             (QList<QVariantList>{{QStringLiteral("sub-remove"), 3}}));

    // The correction now comes from the server: this subtitle plays with no
    // delay, and the season keeps its own.
    QCOMPARE(fixture.controller.subtitleDelayMs(), 0);
    QCOMPARE(fixture.controller.m_subtitleDelayMs, 2300);
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(),
             QStringLiteral("Synced to the audio (+2.3 s). This subtitle now plays with no delay."));
    QVERIFY(model.canReset());
    ScopedTrackPreferences saved = fixture.trackPrefs.getSeasonPreferences(QStringLiteral("season-1"));
    QCOMPARE(saved.subtitleDelayMs, 2300);
    QCOMPARE(saved.subtitleDelayOverrides.size(), 1);
    QCOMPARE(saved.subtitleDelayOverrides.first().syncKey, kSyncKey);
    QCOMPARE(saved.subtitleDelayOverrides.first().delayMs, 0);

    // Nudging the delay now edits this subtitle's override.
    fixture.controller.adjustSubtitleDelayMs(100);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 100);
    saved = fixture.trackPrefs.getSeasonPreferences(QStringLiteral("season-1"));
    QCOMPARE(saved.subtitleDelayMs, 2300);
    QCOMPARE(saved.subtitleDelayOverrides.first().delayMs, 100);

    // Another subtitle keeps the season's delay.
    fixture.controller.setSelectedSubtitleTrack(-1001);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 2300);
    fixture.controller.setSelectedSubtitleTrack(-1000);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 100);

    // No more polling once the job ended.
    const qsizetype finalReads = service.count(QStringLiteral("read"));
    QTest::qWait(SubtitleSyncViewModel::kPollIntervalMs + 300);
    QCOMPARE(service.count(QStringLiteral("read")), finalReads);
}

void SubtitleSyncTest::reloadWaitsForTheNewCopy()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("synced"), 1500), {});
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);

    // mpv passes through "no", and reports the old copy: neither ends the
    // selection or the sync.
    fixture.backend.emitSubtitleTrackId(-1);
    fixture.backend.emitSubtitleTrackId(3);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), -1000);
    QVERIFY(model.available());
    QVERIFY(model.busy());
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-remove")).isEmpty());
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(-1000), 3);

    fixture.backend.emitSubtitleTrackId(8);
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(-1000), 8);
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-remove")),
             (QList<QVariantList>{{QStringLiteral("sub-remove"), 3}}));
    QCOMPARE(notices.count(), 1);
    QVERIFY(notices.first().at(0).toString().startsWith(QStringLiteral("Synced to the audio (+1.5 s).")));
}

void SubtitleSyncTest::failedReloadKeepsTheOldCopy()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.controller.m_subtitleReloadTimeoutMs = 50;
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));
    fixture.controller.setSubtitleDelayMs(400);

    // The sub-add fails in mpv (the fetch fails): no sid is ever reported.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("synced"), 1500), {});
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    fixture.backend.variantCommands.clear();
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 2000);

    // The old copy stays loaded and selected, and the delay is unchanged.
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-remove")).isEmpty());
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("set_property")).value(0),
             (QVariantList{QStringLiteral("set_property"), QStringLiteral("sid"), 3}));
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(-1000), 3);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), -1000);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 400);
    QVERIFY(!fixture.controller.hasSubtitleDelayOverride(kSyncKey));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(),
             QStringLiteral("Synced to the audio (+1.5 s). The new timing applies the next time "
                            "this subtitle loads."));

    // A copy that arrives after all is removed; the old one stays selected.
    fixture.backend.variantCommands.clear();
    fixture.backend.emitSubtitleTrackId(9);
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-remove")),
             (QList<QVariantList>{{QStringLiteral("sub-remove"), 9}}));
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("set_property")).value(0),
             (QVariantList{QStringLiteral("set_property"), QStringLiteral("sid"), 3}));
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(-1000), 3);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), -1000);

    // The next read of the subtitle tries the reload again.
    fixture.controller.setSelectedSubtitleTrack(-1001);
    fixture.controller.setSelectedSubtitleTrack(-1000);
    fixture.backend.variantCommands.clear();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 1500), {});
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
}

void SubtitleSyncTest::bufferingDefersTheReload()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    fixture.controller.m_playbackState = PlayerController::Buffering;
    emit fixture.controller.playbackStateChanged();
    QTRY_VERIFY_WITH_TIMEOUT(service.count(QStringLiteral("read")) >= 2,
                             SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), -800), {});
    // Playback cannot take the reload now; the correction is kept for later.
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-add")).isEmpty());
    QVERIFY(model.busy());
    QCOMPARE(notices.count(), 0);

    fixture.controller.m_playbackState = PlayerController::Playing;
    emit fixture.controller.playbackStateChanged();
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    fixture.backend.emitSubtitleTrackId(7);
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Synced to the audio (-0.8 s)."));
    QVERIFY(fixture.controller.hasSubtitleDelayOverride(kSyncKey));
}

void SubtitleSyncTest::reanchorLoadsTheNewTimingFresh()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));
    fixture.controller.setSubtitleDelayMs(600);

    // A seek re-anchor replaces the stream: mpv drops the sidecars, which
    // are added again from their URLs once it loads. A timing change found
    // meanwhile needs no reload.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    fixture.controller.clearExternalSubtitleTracks(true);
    fixture.controller.m_playbackState = PlayerController::Buffering;
    emit fixture.controller.playbackStateChanged();
    QTRY_VERIFY_WITH_TIMEOUT(service.count(QStringLiteral("read")) >= 2,
                             SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 1200), {});
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-add")).isEmpty());
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(),
             QStringLiteral("Synced to the audio (+1.2 s). This subtitle now plays with no delay."));
    QCOMPARE(fixture.controller.subtitleDelayMs(), 0);

    // A re-anchor while a reload is loading: the stream's new copy is
    // fetched fresh, so the reload counts as loaded.
    fixture.controller.m_playbackState = PlayerController::Playing;
    fixture.controller.m_externalSubtitleTrackMap = {{-1000, 4}, {-1001, -1}, {-1002, -1}};
    model.resetTiming();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 1200),
                                         QByteArrayLiteral("\"v2\""));
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("reset")).key,
                                         state(QStringLiteral("synced"), 0), {});
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    QVERIFY(model.busy());
    fixture.controller.clearExternalSubtitleTracks(true);
    QVERIFY(!model.busy());
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Original timing restored."));
    // Reset Timing brings the season's delay back.
    QVERIFY(!fixture.controller.hasSubtitleDelayOverride(kSyncKey));
    QCOMPARE(fixture.controller.subtitleDelayMs(), 600);
}

void SubtitleSyncTest::hlsOffsetIsAppliedOnceAfterTheReload()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    fixture.play();
    // A server stream whose clock starts 10 s into the source.
    fixture.controller.m_streamTimelineOffsetSeconds = 10.0;
    fixture.becomeAvailable(model, state(QString()));
    fixture.controller.setSubtitleDelayMs(500);
    const auto lastSubDelay = [&fixture]() {
        double value = 0.0;
        for (const QVariantList &command : fixture.backend.commandsNamed(QStringLiteral("set_property"))) {
            if (command.value(1).toString() == QStringLiteral("sub-delay")) {
                value = command.value(2).toDouble();
            }
        }
        return value;
    };
    QCOMPARE(lastSubDelay(), 0.5 - 10.0);

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("synced"), 2000), {});
    fixture.backend.emitSubtitleTrackId(7);
    // The override replaces the season's delay; the offset stays, once.
    QCOMPARE(lastSubDelay(), 0.0 - 10.0);
    fixture.controller.adjustSubtitleDelayMs(100);
    QCOMPARE(lastSubDelay(), 0.1 - 10.0);
    // Off: no sidecar, so no offset and the season's delay.
    fixture.controller.setSelectedSubtitleTrack(-1);
    QCOMPARE(fixture.controller.subtitleDelayMs(), 500);
    QCOMPARE(lastSubDelay(), 0.5);
}

void SubtitleSyncTest::pollLimitAndALaterSync()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    model.setPollLimitMs(0);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    QTRY_VERIFY_WITH_TIMEOUT(service.count(QStringLiteral("read")) >= 2,
                             SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("running"), 0, 0.5), {});
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).toString(),
             QStringLiteral("Still syncing on the server. Select Sync to Audio later to check on it."));
    const qsizetype reads = service.count(QStringLiteral("read"));
    QTest::qWait(SubtitleSyncViewModel::kPollIntervalMs + 300);
    QCOMPARE(service.count(QStringLiteral("read")), reads);

    // The job finished meanwhile; another one reaches the same timing. The
    // timing mpv had is what tells the outcome, not the job's status.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("synced"), 900), {});
    fixture.backend.emitSubtitleTrackId(7);
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Synced to the audio (+0.9 s)."));

    // Once loaded, a sync that changes nothing says so.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("synced"), 900), {});
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Already in sync with the audio."));
}

void SubtitleSyncTest::autoSyncIsReadAfterTheSubtitleLoads()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    model.setAutoSyncRecheckMs(50);
    fixture.play();
    fixture.controller.setSubtitleDelayMs(700);
    // The first read is before the server started its automatic sync.
    fixture.becomeAvailable(model, state(QString()), capability(true));
    QCOMPARE(service.count(QStringLiteral("read")), 1);

    // mpv has the subtitle loaded, so it is read again shortly.
    QTRY_COMPARE_WITH_TIMEOUT(service.count(QStringLiteral("read")), 2, 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("running"), 0, 0.1), {});
    QVERIFY(model.busy());
    QTRY_COMPARE_WITH_TIMEOUT(service.count(QStringLiteral("read")), 3,
                              SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 1100), {});
    fixture.backend.emitSubtitleTrackId(7);
    QVERIFY(!model.busy());
    // Not started here: not announced, but the row shows it and the delay
    // follows the sync.
    QCOMPARE(notices.count(), 0);
    QVERIFY(model.statusText().startsWith(QStringLiteral("Synced to the audio (+1.1 s).")));
    QCOMPARE(fixture.controller.subtitleDelayMs(), 0);

    // A subtitle mpv has not loaded yet is read again once it has. Each is
    // looked at once per playback.
    fixture.controller.setSelectedSubtitleTrack(-1001);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QString(), 0, 0.0, kOtherSyncKey), {});
    const qsizetype reads = service.count(QStringLiteral("read"));
    QTest::qWait(200);
    QCOMPARE(service.count(QStringLiteral("read")), reads);
    fixture.backend.emitSubtitleTrackId(9);
    QTRY_COMPARE_WITH_TIMEOUT(service.count(QStringLiteral("read")), reads + 1, 2000);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QString(), 0, 0.0, kOtherSyncKey), {});
    QTest::qWait(200);
    QCOMPARE(service.count(QStringLiteral("read")), reads + 1);

    // Without automatic sync nothing is read again.
    PlayerFixture manual;
    SubtitleSyncViewModel manualModel(&manual.playbackService, &manual.controller);
    manualModel.setAutoSyncRecheckMs(50);
    manual.play();
    manual.becomeAvailable(manualModel, state(QString()));
    QTest::qWait(200);
    QCOMPARE(manual.playbackService.count(QStringLiteral("read")), 1);
}

void SubtitleSyncTest::externalFlagAndUnknownSubtitlesHideTheRow()
{
    const QString externalKey = QStringLiteral("external-") + QString(64, QLatin1Char('b'));
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    fixture.play();
    fixture.controller.m_availableSubtitleTracks.append(
        externalOption(-1003, externalKey, QStringLiteral("spa")));
    fixture.controller.m_externalSubtitleTrackMap.insert(-1003, -1);
    emit fixture.controller.availableTracksChanged();
    fixture.becomeAvailable(model, state(QString()), capability(false, false));
    QVERIFY(model.available());

    // The server syncs downloads only: a file beside the media is not offered.
    fixture.controller.m_selectedSubtitleTrack = -1003;
    emit fixture.controller.selectedSubtitleTrackChanged();
    QVERIFY(!model.available());

    // A subtitle the server answers 404 for is hidden.
    fixture.controller.m_selectedSubtitleTrack = -1001;
    emit fixture.controller.selectedSubtitleTrackChanged();
    QVERIFY(model.available());
    emit service.subtitleSyncFailed(service.last(QStringLiteral("read")).key, 404, QStringLiteral("x"));
    QVERIFY(!model.available());
    fixture.controller.m_selectedSubtitleTrack = -1000;
    emit fixture.controller.selectedSubtitleTrackChanged();
    QVERIFY(model.available());
}

void SubtitleSyncTest::identityChangesSettleRequests()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();

    // A session change drops the capability read without an answer; it is
    // sent again instead of waiting forever.
    const FakePlaybackService::Call firstStatus = service.last(QStringLiteral("status"));
    emit service.subtitleSyncIdentityChanged();
    QVERIFY(service.cancelled.contains(firstStatus.key));
    QCOMPARE(service.count(QStringLiteral("status")), 2);
    emit service.subtitleSyncStatusLoaded(firstStatus.key, capability());
    QVERIFY(!model.available());
    fixture.becomeAvailable(model, state(QString()));

    // The same account and profile: the start is sent again (it coalesces).
    model.startSync();
    const FakePlaybackService::Call start = service.last(QStringLiteral("start"));
    emit service.subtitleSyncIdentityChanged();
    QCOMPARE(service.count(QStringLiteral("start")), 2);
    QVERIFY(service.cancelled.contains(start.key));
    QVERIFY(model.busy());
    QVERIFY(model.available());
    emit service.subtitleSyncStatusLoaded(service.last(QStringLiteral("status")).key, capability());
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    QCOMPARE(model.phase(), SubtitleSyncViewModel::Phase::Syncing);

    // Another profile: the job is no longer followed, and the capability
    // is read for the new one.
    const qsizetype statusReads = service.count(QStringLiteral("status"));
    service.connectionId = QStringLiteral("connection-1/profile-2");
    emit service.subtitleSyncIdentityChanged();
    QVERIFY(!model.busy());
    QVERIFY(!model.available());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).toString(),
             QStringLiteral("Lost track of the sync. Select Sync to Audio to check on it."));
    QCOMPARE(service.count(QStringLiteral("status")), statusReads + 1);
    const qsizetype reads = service.count(QStringLiteral("read"));
    QTest::qWait(SubtitleSyncViewModel::kPollIntervalMs + 300);
    QCOMPARE(service.count(QStringLiteral("read")), reads);
    emit service.subtitleSyncStatusLoaded(service.last(QStringLiteral("status")).key, capability());
    QVERIFY(model.available());
    QCOMPARE(service.count(QStringLiteral("read")), reads + 1);

    // An answer for another subtitle never leaves the row waiting.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running"), 0, 0.0, kOtherSyncKey), {});
    QVERIFY(!model.busy());
}

void SubtitleSyncTest::failedSyncLeavesTheSubtitleAlone()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));
    fixture.controller.m_subtitleDelayMs = 500;
    fixture.backend.variantCommands.clear();

    // The server refuses the start.
    model.startSync();
    emit service.subtitleSyncFailed(service.last(QStringLiteral("start")).key, 503, QStringLiteral("x"));
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).toString(),
             QStringLiteral("The server is busy. Try again in a few minutes."));

    // The job fails on the server.
    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    QTRY_VERIFY_WITH_TIMEOUT(service.count(QStringLiteral("read")) >= 2,
                             SubtitleSyncViewModel::kPollIntervalMs + 2000);
    emit service.subtitleSyncStateLoaded(
        service.last(QStringLiteral("read")).key,
        state(QStringLiteral("failed"), 0, 0.0, kSyncKey, QStringLiteral("noAudio")), {});
    QCOMPARE(notices.count(), 2);
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("The server can't read this video's audio."));
    QCOMPARE(model.statusText(), QStringLiteral("The server can't read this video's audio."));
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-add")).isEmpty());
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-remove")).isEmpty());
    QCOMPARE(fixture.controller.subtitleDelayMs(), 500);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), -1000);
}

void SubtitleSyncTest::noMatchKeepsTheTimingAndTheDelay()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));
    fixture.controller.m_subtitleDelayMs = 800;
    fixture.backend.variantCommands.clear();

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("noMatch")), {});
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QVERIFY(notices.first().at(0).toString().contains(QStringLiteral("another release")));
    QVERIFY(fixture.backend.variantCommands.isEmpty());
    QCOMPARE(fixture.controller.subtitleDelayMs(), 800);
    QVERIFY(!model.canReset());
}

void SubtitleSyncTest::stopCancelsPolling()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    model.startSync();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("start")).key,
                                         state(QStringLiteral("running")), {});
    QTRY_VERIFY_WITH_TIMEOUT(service.count(QStringLiteral("read")) >= 2,
                             SubtitleSyncViewModel::kPollIntervalMs + 2000);
    const FakePlaybackService::Call poll = service.last(QStringLiteral("read"));

    fixture.stop();
    QVERIFY(service.cancelled.contains(poll.key));
    QVERIFY(!model.busy());
    QVERIFY(!model.available());
    // A late answer is dropped, and nothing polls on.
    fixture.backend.variantCommands.clear();
    emit service.subtitleSyncStateLoaded(poll.key, state(QStringLiteral("synced"), 1000), {});
    QVERIFY(fixture.backend.variantCommands.isEmpty());
    QCOMPARE(notices.count(), 0);
    const qsizetype reads = service.count(QStringLiteral("read"));
    QTest::qWait(SubtitleSyncViewModel::kPollIntervalMs + 300);
    QCOMPARE(service.count(QStringLiteral("read")), reads);
}

void SubtitleSyncTest::otherSubtitleCancelsAndRereads()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    model.startSync();
    const FakePlaybackService::Call start = service.last(QStringLiteral("start"));

    // Another subtitle: the start is dropped and that one is read.
    fixture.controller.m_selectedSubtitleTrack = -1001;
    emit fixture.controller.selectedSubtitleTrackChanged();
    QVERIFY(service.cancelled.contains(start.key));
    QVERIFY(!model.busy());
    const FakePlaybackService::Call read = service.last(QStringLiteral("read"));
    QCOMPARE(read.syncKey, kOtherSyncKey);
    emit service.subtitleSyncStateLoaded(start.key, state(QStringLiteral("pending")), {});
    QVERIFY(!model.busy());

    // A subtitle without a sync key offers nothing.
    fixture.controller.m_selectedSubtitleTrack = -1002;
    emit fixture.controller.selectedSubtitleTrackChanged();
    QVERIFY(!model.available());

    // Another subtitle of the same playback uses the capability read.
    QCOMPARE(service.count(QStringLiteral("status")), 1);

    // A new session of the same file starts over and reads the capability
    // again, so a server that turned the sync on is picked up.
    fixture.controller.m_selectedSubtitleTrack = -1000;
    fixture.play(QStringLiteral("session-2"));
    QCOMPARE(service.count(QStringLiteral("status")), 2);
    emit service.subtitleSyncStatusLoaded(service.last(QStringLiteral("status")).key, capability());
    QCOMPARE(service.last(QStringLiteral("read")).syncKey, kSyncKey);
}

void SubtitleSyncTest::resetRestoresTheOriginalTiming()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QStringLiteral("synced"), -1200));
    QVERIFY(model.canReset());
    QCOMPARE(model.statusText(), QStringLiteral("Synced -1.2 s"));
    fixture.controller.m_subtitleDelayMs = 300;
    fixture.backend.variantCommands.clear();

    // A fresh read supplies the validator, then the PUT restores the identity.
    model.resetTiming();
    QVERIFY(model.busy());
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), -1200),
                                         QByteArrayLiteral("\"v2\""));
    const FakePlaybackService::Call reset = service.last(QStringLiteral("reset"));
    QCOMPARE(reset.etag, QByteArrayLiteral("\"v2\""));
    QCOMPARE(reset.syncKey, kSyncKey);
    emit service.subtitleSyncStateLoaded(reset.key, state(QStringLiteral("synced"), 0),
                                         QByteArrayLiteral("\"v3\""));
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    QVERIFY(model.busy());
    fixture.backend.emitSubtitleTrackId(7);
    QVERIFY(!model.busy());
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Original timing restored."));
    QVERIFY(!model.canReset());
    // The user's own delay is theirs to keep.
    QCOMPARE(fixture.controller.subtitleDelayMs(), 300);
}

void SubtitleSyncTest::resetSettlesARefusedReplay()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    QSignalSpy notices(&model, &SubtitleSyncViewModel::notice);
    fixture.play();
    fixture.becomeAvailable(model, state(QStringLiteral("synced"), 900));

    model.resetTiming();
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 900),
                                         QByteArrayLiteral("\"v2\""));
    // 412: either this reset went through and its replay was refused, or
    // another change won. A read tells which.
    const qsizetype reads = service.count(QStringLiteral("read"));
    emit service.subtitleSyncFailed(service.last(QStringLiteral("reset")).key, 412, QStringLiteral("x"));
    QVERIFY(model.busy());
    QCOMPARE(service.count(QStringLiteral("read")), reads + 1);
    emit service.subtitleSyncStateLoaded(service.last(QStringLiteral("read")).key,
                                         state(QStringLiteral("synced"), 0), {});
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    fixture.backend.emitSubtitleTrackId(7);
    QVERIFY(!model.busy());
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Original timing restored."));
}

void SubtitleSyncTest::onlySyncableExternalSubtitlesAreOffered()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    fixture.play();
    // The server cannot sync: nothing is offered and nothing is read.
    emit service.subtitleSyncStatusLoaded(service.last(QStringLiteral("status")).key, {});
    QVERIFY(!model.available());
    QCOMPARE(service.count(QStringLiteral("read")), 0);
    model.startSync();
    QCOMPARE(service.count(QStringLiteral("start")), 0);

    // An embedded track never carries a key.
    PlayerFixture embedded;
    SubtitleSyncViewModel embeddedModel(&embedded.playbackService, &embedded.controller);
    embedded.play();
    embedded.controller.m_selectedSubtitleTrack = 2;
    emit embedded.controller.selectedSubtitleTrackChanged();
    emit embedded.playbackService.subtitleSyncStatusLoaded(
        embedded.playbackService.last(QStringLiteral("status")).key, capability());
    QVERIFY(!embeddedModel.available());
    QCOMPARE(embedded.controller.reloadSelectedExternalSubtitle(),
             PlayerController::SubtitleReload::Unavailable);
}

void SubtitleSyncTest::downloadedSubtitlesSyncAndReloadInPlace()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSyncViewModel model(&service, &fixture.controller);
    fixture.play();
    fixture.becomeAvailable(model, state(QString()));

    // A subtitle the server just downloaded (subtitle search) carries its
    // sync key, so it offers Sync to Audio once selected.
    Bloom::DownloadedSubtitle download;
    download.url = QUrl(QStringLiteral(
        "https://silo.example.test/api/v2/stream/s/subtitles/0.srt?downloaded_subtitle_id=21"));
    download.title = QStringLiteral("Release");
    download.language = QStringLiteral("en");
    download.storedId = QStringLiteral("21");
    download.syncKey = QStringLiteral("stored-21");
    const int index = fixture.controller.addDownloadedSubtitleTrack(download.toVariantMap(), true);
    QVERIFY(index != PlayerController::kNoSubtitleTrackIndex);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), index);
    fixture.backend.emitSubtitleTrackId(11);
    QVERIFY(model.available());
    QCOMPARE(service.last(QStringLiteral("read")).syncKey, QStringLiteral("stored-21"));

    // A download saved over a stream mpv has selected reloads in place: the
    // old copy goes only once mpv reports the new one.
    Bloom::DownloadedSubtitle stream;
    stream.url = QUrl(QStringLiteral("https://silo.example.test/Videos/item-1/Subtitles/5/Stream.srt"));
    stream.language = QStringLiteral("en");
    stream.sourceIndex = 5;
    QCOMPARE(fixture.controller.addDownloadedSubtitleTrack(stream.toVariantMap(), true), 5);
    fixture.backend.emitSubtitleTrackId(12);
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(5), 12);
    fixture.backend.variantCommands.clear();
    QCOMPARE(fixture.controller.addDownloadedSubtitleTrack(stream.toVariantMap(), true), 5);
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-add")).size(), 1);
    QVERIFY(fixture.backend.commandsNamed(QStringLiteral("sub-remove")).isEmpty());
    fixture.backend.emitSubtitleTrackId(-1);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), 5);
    fixture.backend.emitSubtitleTrackId(13);
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(5), 13);
    QCOMPARE(fixture.backend.commandsNamed(QStringLiteral("sub-remove")),
             (QList<QVariantList>{{QStringLiteral("sub-remove"), 12}}));
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), 5);
}

QTEST_MAIN(SubtitleSyncTest)
#include "SubtitleSyncTest.moc"
