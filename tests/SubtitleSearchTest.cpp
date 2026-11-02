#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>

#include "models/SubtitleSearch.h"
#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "network/PlaybackService.h"
#include "player/PlayerController.h"
#include "player/backend/IPlayerBackend.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloPlaybackProvider.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "utils/ConfigManager.h"
#include "utils/DisplayManager.h"
#include "utils/TrackPreferencesManager.h"
#include "viewmodels/SubtitleSearchViewModel.h"

#include "TestConfigIsolation.h"

namespace {

class ControlledReply final : public QNetworkReply
{
public:
    ControlledReply(const QNetworkRequest &request, int statusCode, QByteArray payload,
                    bool delayed, QObject *parent)
        : QNetworkReply(parent)
        , m_statusCode(statusCode)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (statusCode >= 400) {
            setError(statusCode == 401 ? QNetworkReply::AuthenticationRequiredError
                                       : QNetworkReply::UnknownServerError,
                     QStringLiteral("test network error"));
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
    int m_statusCode;
    QByteArray m_payload;
    qint64 m_offset = 0;
};

struct ResponsePlan
{
    int statusCode;
    QByteArray payload;
    bool delayed = false;
};

struct RecordedRequest
{
    QNetworkAccessManager::Operation operation;
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
        requests.append({operation, request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const ResponsePlan plan = responses.isEmpty()
            ? ResponsePlan{500, QByteArrayLiteral(R"({"error":"queue_empty"})"), false}
            : responses.takeFirst();
        auto *reply = new ControlledReply(request, plan.statusCode, plan.payload, plan.delayed, this);
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

    void seed(const QString &serverUrl, const QString &accountId, const QString &accessToken)
    {
        seedSession(serverUrl, accountId, accessToken, accountId);
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
        SubtitleSearchTarget target;
        QString language;
        Bloom::SubtitleSearchResult result;
    };

    bool supportsSubtitleSearch() const override { return true; }
    QString subtitleSearchConnectionId() const override { return connectionId; }
    void loadSubtitleSearchStatus(const QString &requestKey) override
    {
        calls.append({QStringLiteral("status"), requestKey, {}, {}, {}});
    }
    void searchSubtitles(const QString &requestKey, const SubtitleSearchTarget &target,
                         const QString &language) override
    {
        calls.append({QStringLiteral("search"), requestKey, target, language, {}});
    }
    void downloadSubtitle(const QString &requestKey, const SubtitleSearchTarget &target,
                          const Bloom::SubtitleSearchResult &result) override
    {
        calls.append({QStringLiteral("download"), requestKey, target, {}, result});
    }
    void cancelSubtitleSearch(const QString &requestKey) override { cancelled.append(requestKey); }

    Call last(const QString &kind) const
    {
        for (qsizetype i = calls.size() - 1; i >= 0; --i) {
            if (calls.at(i).kind == kind) {
                return calls.at(i);
            }
        }
        return {};
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

PlaybackProviderContext siloContext()
{
    PlaybackProviderContext context;
    context.serverUrl = QUrl(QStringLiteral("https://silo.example.test"));
    context.accessToken = QStringLiteral("access-1");
    context.accountId = QStringLiteral("42");
    context.profileId = QStringLiteral("profile-1");
    return context;
}

SubtitleSearchTarget fileTarget()
{
    SubtitleSearchTarget target;
    target.media.itemId = QStringLiteral("movie-1");
    target.media.connectionId = QStringLiteral("connection-1");
    target.mediaVersionId = QStringLiteral("42");
    target.playbackSessionId = QStringLiteral("session-1");
    return target;
}

QVariantMap resultMap(const QString &id, double score, qint64 downloads, bool hashMatch = false)
{
    Bloom::SubtitleSearchResult result;
    result.id = id;
    result.provider = QStringLiteral("opensubtitles");
    result.language = QStringLiteral("en");
    result.title = QStringLiteral("Release %1").arg(id);
    result.format = QStringLiteral("srt");
    result.score = score;
    result.downloads = downloads;
    result.hashMatch = hashMatch;
    return result.toVariantMap();
}

QStringList resultIds(const QVariantList &results)
{
    QStringList ids;
    for (const QVariant &result : results) {
        ids.append(result.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

} // namespace

class SubtitleSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void resultsSortBestFirst();
    void siloRequestsAndParsing();
    void jellyfinRequestsAndParsing();
    void siloStatusGatesTheSearch();
    void siloSearchAndDownloadOverTheNetwork();
    void jellyfinDownloadReadsTheNewStream();
    void jellyfinDownloadWaitsForTheItemRefresh();
    void cancelledRequestsAnswerNothing();
    void viewModelSearchDownloadSelectAndPersist();
    void viewModelDropsStaleAnswers();
    void viewModelCancelsWhenPlaybackStops();
    void viewModelDefaultsToTheSubtitleLanguage();

private:
    struct PlayerFixture;
};

void SubtitleSearchTest::resultsSortBestFirst()
{
    QList<Bloom::SubtitleSearchResult> results;
    for (const QVariantMap &map : {resultMap(QStringLiteral("a"), 5.0, 10),
                                   resultMap(QStringLiteral("b"), 9.0, 1),
                                   resultMap(QStringLiteral("c"), 1.0, 0, true),
                                   resultMap(QStringLiteral("d"), 9.0, 50),
                                   resultMap(QStringLiteral("e"), 5.0, 10)}) {
        results.append(Bloom::SubtitleSearchResult::fromVariantMap(map));
    }
    QStringList ids;
    for (const Bloom::SubtitleSearchResult &result : Bloom::sortSubtitleSearchResults(results)) {
        ids.append(result.id);
    }
    // File match first, then score, then downloads; ties keep server order.
    QCOMPARE(ids, (QStringList{QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("b"),
                               QStringLiteral("a"), QStringLiteral("e")}));
}

void SubtitleSearchTest::siloRequestsAndParsing()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.supportsSubtitleSearch());
    const PlaybackProviderContext context = siloContext();

    const SubtitleSearchRequest status = provider.createSubtitleSearchStatusRequest(context);
    QCOMPARE(status.endpoint, QStringLiteral("/api/v2/subtitles/providers/status"));
    QCOMPARE(status.method, QStringLiteral("GET"));
    QVERIFY(status.replaySafe);
    const auto statusOf = [&provider](const char *json) {
        return provider.parseSubtitleSearchStatus(QByteArray(json));
    };
    SubtitleSearchStatus parsed = statusOf(
        R"({"schema_version":1,"enabled":true,"providers":["opensubtitles"],"revision":"r","state":"available","allowed":true})");
    QVERIFY(parsed.valid);
    QVERIFY(parsed.available);
    QCOMPARE(parsed.providers, QStringList{QStringLiteral("opensubtitles")});
    QVERIFY(!statusOf(R"({"enabled":true,"providers":[],"state":"not_configured","allowed":true})").available);
    QVERIFY(!statusOf(R"({"enabled":true,"providers":["a"],"state":"available","allowed":false})").available);
    QVERIFY(!statusOf(R"({"enabled":false,"providers":["a"],"state":"available","allowed":true})").available);
    QVERIFY(!statusOf("not json").valid);

    // The picker's ISO 639-2 code goes out as a BCP 47 tag.
    SubtitleSearchTarget target = fileTarget();
    const SubtitleSearchRequest search =
        provider.createSubtitleSearchRequest(context, target, QStringLiteral("fre"));
    QCOMPARE(search.endpoint, QStringLiteral("/api/v2/subtitles/search"));
    QCOMPARE(search.method, QStringLiteral("POST"));
    QVERIFY(search.replaySafe);
    QCOMPARE(search.body, (QJsonObject{{QStringLiteral("media_file_id"), QStringLiteral("42")},
                                       {QStringLiteral("languages"), QJsonArray{QStringLiteral("fr")}}}));
    QCOMPARE(provider.createSubtitleSearchRequest(context, target, QStringLiteral("eng"))
                 .body.value(QStringLiteral("languages")).toArray().first().toString(),
             QStringLiteral("en"));
    SubtitleSearchTarget noFile = target;
    noFile.mediaVersionId.clear();
    QVERIFY(!provider.createSubtitleSearchRequest(context, noFile, QStringLiteral("eng")).isValid());

    const SubtitleSearchParseResult results = provider.parseSubtitleSearchResponse(QByteArrayLiteral(
        R"({"results":[{"id":"9001","provider":"opensubtitles","language":"en","release_name":"Movie.2020.1080p","format":"SRT","score":8.5,"downloads":1200,"hearing_impaired":true,"upload_date":"2024-01-02T03:04:05.000Z"},{"provider":"x"}],"warnings":[]})"));
    QVERIFY(results.valid);
    QCOMPARE(results.results.size(), 1);
    const Bloom::SubtitleSearchResult first = results.results.first();
    QCOMPARE(first.id, QStringLiteral("9001"));
    QCOMPARE(first.provider, QStringLiteral("opensubtitles"));
    QCOMPARE(first.language, QStringLiteral("en"));
    QCOMPARE(first.title, QStringLiteral("Movie.2020.1080p"));
    QCOMPARE(first.format, QStringLiteral("srt"));
    QCOMPARE(first.score, 8.5);
    QCOMPARE(first.downloads, qint64(1200));
    QVERIFY(first.hearingImpaired);
    QVERIFY(!first.hashMatch);
    QVERIFY(first.uploadedAt.isValid());
    QVERIFY(!provider.parseSubtitleSearchResponse(QByteArrayLiteral("[]")).valid);

    // The result goes back exactly as the search sent it; sent once.
    const SubtitleSearchRequest download = provider.createSubtitleDownloadRequest(context, target, first);
    QCOMPARE(download.endpoint, QStringLiteral("/api/v2/subtitles/download"));
    QCOMPARE(download.method, QStringLiteral("POST"));
    QVERIFY(!download.replaySafe);
    QCOMPARE(download.body, (QJsonObject{{QStringLiteral("media_file_id"), QStringLiteral("42")},
                                         {QStringLiteral("provider"), QStringLiteral("opensubtitles")},
                                         {QStringLiteral("subtitle_id"), QStringLiteral("9001")},
                                         {QStringLiteral("language"), QStringLiteral("en")},
                                         {QStringLiteral("release_name"), QStringLiteral("Movie.2020.1080p")},
                                         {QStringLiteral("score"), 8.5},
                                         {QStringLiteral("hearing_impaired"), true}}));

    // The stored subtitle is served by the session's sidecar route pinned to
    // its identity, with the account token mpv needs.
    const QByteArray stored = QByteArrayLiteral(
        R"({"subtitle":{"id":"7","media_file_id":"42","provider":"opensubtitles","language":"en","format":"srt","release_name":"Movie.2020.1080p","score":8.5,"hearing_impaired":true,"created_at":"2024-01-02T03:04:05.000Z","timing":{"offset_ms":0,"scale":1}}})");
    SubtitleDownloadParseResult downloaded =
        provider.parseSubtitleDownloadResponse(context, target, first, stored);
    QVERIFY(downloaded.valid);
    QVERIFY(!downloaded.needsPlaybackInfo);
    QCOMPARE(downloaded.subtitle.sourceIndex, -1);
    QCOMPARE(downloaded.subtitle.title, QStringLiteral("Movie.2020.1080p"));
    QCOMPARE(downloaded.subtitle.url.path(), QStringLiteral("/api/v2/stream/session-1/subtitles/0.vtt"));
    QCOMPARE(downloaded.subtitle.url.host(), QStringLiteral("silo.example.test"));
    const QUrlQuery query(downloaded.subtitle.url);
    QCOMPARE(query.queryItemValue(QStringLiteral("file_id")), QStringLiteral("42"));
    QCOMPARE(query.queryItemValue(QStringLiteral("downloaded_subtitle_id")), QStringLiteral("7"));
    QCOMPARE(query.queryItemValue(QStringLiteral("token")), QStringLiteral("access-1"));

    // ASS keeps its styling.
    QByteArray ass = stored;
    ass.replace("\"format\":\"srt\"", "\"format\":\"ass\"");
    QCOMPARE(provider.parseSubtitleDownloadResponse(context, target, first, ass).subtitle.url.path(),
             QStringLiteral("/api/v2/stream/session-1/subtitles/0.ass"));

    // Without a session the download is kept for the next one.
    SubtitleSearchTarget noSession = target;
    noSession.playbackSessionId.clear();
    downloaded = provider.parseSubtitleDownloadResponse(context, noSession, first, stored);
    QVERIFY(downloaded.valid);
    QVERIFY(!downloaded.subtitle.isValid());
    QVERIFY(!provider.parseSubtitleDownloadResponse(context, target, first, "{}").valid);
}

void SubtitleSearchTest::jellyfinRequestsAndParsing()
{
    JellyfinPlaybackProvider provider;
    QVERIFY(provider.supportsSubtitleSearch());
    PlaybackProviderContext context;
    context.serverUrl = QUrl(QStringLiteral("https://jellyfin.example.test"));
    context.accessToken = QStringLiteral("token-1");
    context.profileId = QStringLiteral("user-1");
    // No status document: always offered.
    QVERIFY(!provider.createSubtitleSearchStatusRequest(context).isValid());

    const SubtitleSearchTarget target = fileTarget();
    const SubtitleSearchRequest search =
        provider.createSubtitleSearchRequest(context, target, QStringLiteral("ENG"));
    QCOMPARE(search.endpoint, QStringLiteral("/Items/movie-1/RemoteSearch/Subtitles/eng"));
    QCOMPARE(search.method, QStringLiteral("GET"));
    QVERIFY(search.replaySafe);
    QVERIFY(!search.endpoint.contains(QStringLiteral("isPerfectMatch")));

    const SubtitleSearchParseResult results = provider.parseSubtitleSearchResponse(QByteArrayLiteral(
        R"([{"Id":"os-1","ProviderName":"Open Subtitles","ThreeLetterISOLanguageName":"eng","Name":"Movie.2020","Format":"SRT","DownloadCount":40,"CommunityRating":7.5,"IsHashMatch":true,"HearingImpaired":false,"Forced":true,"MachineTranslated":true,"AiTranslated":false,"DateCreated":"2024-02-03T04:05:06.000Z"},{"Name":"no id"}])"));
    QVERIFY(results.valid);
    QCOMPARE(results.results.size(), 1);
    const Bloom::SubtitleSearchResult first = results.results.first();
    QCOMPARE(first.id, QStringLiteral("os-1"));
    QCOMPARE(first.provider, QStringLiteral("Open Subtitles"));
    QCOMPARE(first.language, QStringLiteral("eng"));
    QCOMPARE(first.title, QStringLiteral("Movie.2020"));
    QCOMPARE(first.format, QStringLiteral("srt"));
    QCOMPARE(first.downloads, qint64(40));
    QCOMPARE(first.score, 7.5);
    QVERIFY(first.hashMatch);
    QVERIFY(first.forced);
    QVERIFY(first.machineTranslated);
    QVERIFY(!first.aiTranslated);
    QVERIFY(!provider.parseSubtitleSearchResponse(QByteArrayLiteral("{}")).valid);

    const SubtitleSearchRequest download = provider.createSubtitleDownloadRequest(context, target, first);
    QCOMPARE(download.endpoint, QStringLiteral("/Items/movie-1/RemoteSearch/Subtitles/os-1"));
    QCOMPARE(download.method, QStringLiteral("POST"));
    QVERIFY(!download.replaySafe);
    const SubtitleDownloadParseResult downloaded =
        provider.parseSubtitleDownloadResponse(context, target, first, {});
    QVERIFY(downloaded.valid);
    QVERIFY(downloaded.needsPlaybackInfo);
}

void SubtitleSearchTest::siloStatusGatesTheSearch()
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
    QVERIFY(service.supportsSubtitleSearch());
    QSignalSpy status(&service, &PlaybackService::subtitleSearchStatusLoaded);

    manager.responses = {{200, QByteArrayLiteral(
        R"({"schema_version":1,"enabled":true,"providers":[],"revision":"r","state":"not_configured","allowed":true})")}};
    service.loadSubtitleSearchStatus(QStringLiteral("key-1"));
    QTRY_COMPARE_WITH_TIMEOUT(status.count(), 1, 2000);
    QCOMPARE(status.first().at(0).toString(), QStringLiteral("key-1"));
    QVERIFY(!status.first().at(1).toBool());
    QCOMPARE(manager.requests.first().request.url().path(),
             QStringLiteral("/api/v2/subtitles/providers/status"));
    QCOMPARE(manager.requests.first().request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));

    manager.responses = {{200, QByteArrayLiteral(
        R"({"schema_version":1,"enabled":true,"providers":["opensubtitles"],"revision":"r","state":"available","allowed":true})")}};
    service.loadSubtitleSearchStatus(QStringLiteral("key-2"));
    QTRY_COMPARE_WITH_TIMEOUT(status.count(), 2, 2000);
    QVERIFY(status.at(1).at(1).toBool());

    // Jellyfin has no document and answers at once.
    JellyfinProviderAdapter jellyfin;
    ExposedAuthenticationService jellyfinAuth(&transport, &jellyfin);
    jellyfinAuth.seed(QStringLiteral("https://jellyfin.example.test"), QStringLiteral("user-1"),
                      QStringLiteral("token-1"));
    PlaybackService jellyfinService(&jellyfinAuth);
    QSignalSpy jellyfinStatus(&jellyfinService, &PlaybackService::subtitleSearchStatusLoaded);
    const qsizetype sent = manager.requests.size();
    jellyfinService.loadSubtitleSearchStatus(QStringLiteral("key-3"));
    QCOMPARE(jellyfinStatus.count(), 1);
    QVERIFY(jellyfinStatus.first().at(1).toBool());
    QCOMPARE(manager.requests.size(), sent);
}

void SubtitleSearchTest::siloSearchAndDownloadOverTheNetwork()
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
    QSignalSpy loaded(&service, &PlaybackService::subtitleSearchResultsLoaded);
    QSignalSpy downloaded(&service, &PlaybackService::subtitleDownloaded);
    QSignalSpy failed(&service, &PlaybackService::subtitleSearchFailed);

    manager.responses = {{200, QByteArrayLiteral(
        R"({"results":[{"id":"9001","provider":"opensubtitles","language":"en","release_name":"R","format":"srt","score":1,"downloads":2,"hearing_impaired":false}],"warnings":[]})")}};
    service.searchSubtitles(QStringLiteral("search-1"), fileTarget(), QStringLiteral("eng"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 2000);
    QCOMPARE(manager.requests.first().operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(QJsonDocument::fromJson(manager.requests.first().body).object()
                 .value(QStringLiteral("languages")).toArray().first().toString(),
             QStringLiteral("en"));
    const QVariantList results = loaded.first().at(1).toList();
    QCOMPARE(resultIds(results), QStringList{QStringLiteral("9001")});

    // A refused download is not replayed.
    manager.responses = {{503, QByteArrayLiteral(R"({"status":503})")}};
    const Bloom::SubtitleSearchResult result =
        Bloom::SubtitleSearchResult::fromVariantMap(results.first().toMap());
    qsizetype sent = manager.requests.size();
    service.downloadSubtitle(QStringLiteral("download-1"), fileTarget(), result);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), sent + 1);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("download-1"));

    manager.responses = {{200, QByteArrayLiteral(
        R"({"subtitle":{"id":"7","media_file_id":"42","provider":"opensubtitles","language":"en","format":"srt","release_name":"R","score":1,"hearing_impaired":false,"created_at":"2024-01-02T03:04:05.000Z","timing":{"offset_ms":0,"scale":1}}})")}};
    sent = manager.requests.size();
    service.downloadSubtitle(QStringLiteral("download-2"), fileTarget(), result);
    QTRY_COMPARE_WITH_TIMEOUT(downloaded.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), sent + 1);
    QCOMPARE(manager.requests.last().request.url().path(), QStringLiteral("/api/v2/subtitles/download"));
    const Bloom::DownloadedSubtitle subtitle =
        Bloom::DownloadedSubtitle::fromVariantMap(downloaded.first().at(1).toMap());
    QCOMPARE(QUrlQuery(subtitle.url).queryItemValue(QStringLiteral("downloaded_subtitle_id")),
             QStringLiteral("7"));
}

namespace {

QByteArray jellyfinPlaybackInfo(bool withDownload)
{
    QJsonArray streams{
        QJsonObject{{QStringLiteral("Index"), 0}, {QStringLiteral("Type"), QStringLiteral("Video")},
                    {QStringLiteral("Codec"), QStringLiteral("h264")}},
        QJsonObject{{QStringLiteral("Index"), 1}, {QStringLiteral("Type"), QStringLiteral("Subtitle")},
                    {QStringLiteral("Codec"), QStringLiteral("subrip")},
                    {QStringLiteral("Language"), QStringLiteral("eng")},
                    {QStringLiteral("IsExternal"), false}},
    };
    if (withDownload) {
        // An older sidecar in another language, then the download.
        streams.append(QJsonObject{
            {QStringLiteral("Index"), 2}, {QStringLiteral("Type"), QStringLiteral("Subtitle")},
            {QStringLiteral("Codec"), QStringLiteral("srt")},
            {QStringLiteral("Language"), QStringLiteral("ger")},
            {QStringLiteral("IsExternal"), true},
            {QStringLiteral("DeliveryMethod"), QStringLiteral("External")},
            {QStringLiteral("DeliveryUrl"), QStringLiteral("/Videos/movie-1/42/Subtitles/2/0/Stream.srt")}});
        streams.append(QJsonObject{
            {QStringLiteral("Index"), 3}, {QStringLiteral("Type"), QStringLiteral("Subtitle")},
            {QStringLiteral("Codec"), QStringLiteral("srt")},
            {QStringLiteral("Language"), QStringLiteral("eng")},
            {QStringLiteral("DisplayTitle"), QStringLiteral("English - SRT - External")},
            {QStringLiteral("IsExternal"), true},
            {QStringLiteral("DeliveryMethod"), QStringLiteral("External")},
            {QStringLiteral("DeliveryUrl"),
             QStringLiteral("/Videos/movie-1/42/Subtitles/3/0/Stream.srt?ApiKey=stale")}});
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("PlaySessionId"), QStringLiteral("play-1")},
        {QStringLiteral("MediaSources"), QJsonArray{QJsonObject{
             {QStringLiteral("Id"), QStringLiteral("42")},
             {QStringLiteral("Container"), QStringLiteral("mkv")},
             {QStringLiteral("SupportsDirectPlay"), true},
             {QStringLiteral("MediaStreams"), streams}}}}}).toJson(QJsonDocument::Compact);
}

Bloom::SubtitleSearchResult jellyfinResult()
{
    Bloom::SubtitleSearchResult result;
    result.id = QStringLiteral("os-1");
    result.provider = QStringLiteral("Open Subtitles");
    result.language = QStringLiteral("eng");
    result.title = QStringLiteral("Movie.2020");
    result.format = QStringLiteral("srt");
    return result;
}

} // namespace

void SubtitleSearchTest::jellyfinDownloadReadsTheNewStream()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.seed(QStringLiteral("https://jellyfin.example.test"), QStringLiteral("user-1"),
              QStringLiteral("token-1"));
    PlaybackService service(&auth);
    QSignalSpy downloaded(&service, &PlaybackService::subtitleDownloaded);
    QSignalSpy failed(&service, &PlaybackService::subtitleSearchFailed);

    SubtitleSearchTarget target = fileTarget();
    target.knownSubtitleIndexes = {1};
    manager.responses = {{204, QByteArray()}, {200, jellyfinPlaybackInfo(true)}};
    service.downloadSubtitle(QStringLiteral("download-1"), target, jellyfinResult());
    QTRY_COMPARE_WITH_TIMEOUT(downloaded.count(), 1, 3000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(manager.requests.size(), 2);
    QCOMPARE(manager.requests.at(0).operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(manager.requests.at(0).request.url().path(),
             QStringLiteral("/Items/movie-1/RemoteSearch/Subtitles/os-1"));
    QCOMPARE(manager.requests.at(1).request.url().path(), QStringLiteral("/Items/movie-1/PlaybackInfo"));

    // The new external stream in the downloaded language, with its index and
    // the session's credential.
    const Bloom::DownloadedSubtitle subtitle =
        Bloom::DownloadedSubtitle::fromVariantMap(downloaded.first().at(1).toMap());
    QCOMPARE(subtitle.sourceIndex, 3);
    QCOMPARE(subtitle.url.path(), QStringLiteral("/Videos/movie-1/42/Subtitles/3/0/Stream.srt"));
    QCOMPARE(QUrlQuery(subtitle.url).queryItemValue(QStringLiteral("ApiKey")), QStringLiteral("token-1"));
    QCOMPARE(subtitle.title, QStringLiteral("English - SRT - External"));
}

void SubtitleSearchTest::jellyfinDownloadWaitsForTheItemRefresh()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.seed(QStringLiteral("https://jellyfin.example.test"), QStringLiteral("user-1"),
              QStringLiteral("token-1"));
    PlaybackService service(&auth);
    QSignalSpy downloaded(&service, &PlaybackService::subtitleDownloaded);

    SubtitleSearchTarget target = fileTarget();
    target.knownSubtitleIndexes = {1, 2};
    // The refresh has not finished on the first read.
    manager.responses = {{204, QByteArray()},
                         {200, jellyfinPlaybackInfo(false)},
                         {200, jellyfinPlaybackInfo(true)}};
    service.downloadSubtitle(QStringLiteral("download-1"), target, jellyfinResult());
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), 2, 2000);
    QCOMPARE(downloaded.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(downloaded.count(), 1,
                              PlaybackService::kDownloadedSubtitleReadIntervalMs + 3000);
    QCOMPARE(manager.requests.size(), 3);
    QCOMPARE(Bloom::DownloadedSubtitle::fromVariantMap(downloaded.first().at(1).toMap()).sourceIndex, 3);
}

void SubtitleSearchTest::cancelledRequestsAnswerNothing()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.seed(QStringLiteral("https://jellyfin.example.test"), QStringLiteral("user-1"),
              QStringLiteral("token-1"));
    PlaybackService service(&auth);
    QSignalSpy loaded(&service, &PlaybackService::subtitleSearchResultsLoaded);
    QSignalSpy failed(&service, &PlaybackService::subtitleSearchFailed);

    manager.responses = {{200, QByteArrayLiteral("[]"), true}};
    service.searchSubtitles(QStringLiteral("search-1"), fileTarget(), QStringLiteral("eng"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.replies.size(), 1, 1000);
    service.cancelSubtitleSearch(QStringLiteral("search-1"));
    if (manager.replies.first()) {
        manager.replies.first()->complete();
    }
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

struct SubtitleSearchTest::PlayerFixture
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

    void play(const QString &itemId, const QString &seasonId, const QString &sessionId)
    {
        controller.m_currentItemId = itemId;
        controller.m_currentSeriesId = seasonId.isEmpty() ? QString() : QStringLiteral("series-1");
        controller.m_currentSeasonId = seasonId;
        controller.m_mediaSourceId = QStringLiteral("source-1");
        controller.m_playSessionId = sessionId;
        controller.m_availableSubtitleTracks = {
            QVariantMap{{QStringLiteral("index"), 1}, {QStringLiteral("language"), QStringLiteral("ger")}}};
        controller.m_playbackState = PlayerController::Playing;
        emit controller.currentItemIdChanged();
        emit controller.isPlaybackActiveChanged();
    }

    void stop()
    {
        controller.m_playbackState = PlayerController::Idle;
        emit controller.isPlaybackActiveChanged();
    }
};

void SubtitleSearchTest::viewModelSearchDownloadSelectAndPersist()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSearchViewModel model(&service, &fixture.controller, &fixture.config);
    QSignalSpy availableChanged(&model, &SubtitleSearchViewModel::availableChanged);
    QSignalSpy loadedSpy(&model, &SubtitleSearchViewModel::subtitleLoaded);

    fixture.play(QStringLiteral("episode-1"), QStringLiteral("season-1"), QStringLiteral("play-1"));
    QVERIFY(!model.available());
    const FakePlaybackService::Call status = service.last(QStringLiteral("status"));
    QVERIFY(!status.key.isEmpty());
    emit service.subtitleSearchStatusLoaded(status.key, true);
    QVERIFY(model.available());

    model.open();
    QVERIFY(model.isLoading());
    const FakePlaybackService::Call search = service.last(QStringLiteral("search"));
    QCOMPARE(search.language, QStringLiteral("eng"));
    QCOMPARE(search.target.media.itemId, QStringLiteral("episode-1"));
    QCOMPARE(search.target.mediaVersionId, QStringLiteral("source-1"));
    QCOMPARE(search.target.playbackSessionId, QStringLiteral("play-1"));
    QCOMPARE(search.target.knownSubtitleIndexes, QList<int>{1});

    emit service.subtitleSearchResultsLoaded(
        search.key, {resultMap(QStringLiteral("low"), 2.0, 5), resultMap(QStringLiteral("match"), 1.0, 0, true),
                     resultMap(QStringLiteral("high"), 8.0, 5)});
    QVERIFY(!model.isLoading());
    QVERIFY(model.searched());
    QCOMPARE(resultIds(model.results()),
             (QStringList{QStringLiteral("match"), QStringLiteral("high"), QStringLiteral("low")}));
    QCOMPARE(model.rowCount(), 3);
    // Opening again for the same file and language keeps the results.
    const qsizetype searches = service.calls.size();
    model.open();
    QCOMPARE(service.calls.size(), searches);

    model.download(QStringLiteral("high"));
    QVERIFY(model.downloading());
    QCOMPARE(model.downloadingId(), QStringLiteral("high"));
    const FakePlaybackService::Call download = service.last(QStringLiteral("download"));
    QCOMPARE(download.result.id, QStringLiteral("high"));
    // One download at a time.
    model.download(QStringLiteral("low"));
    QCOMPARE(service.last(QStringLiteral("download")).key, download.key);

    Bloom::DownloadedSubtitle subtitle;
    subtitle.url = QUrl(QStringLiteral("https://jellyfin.example.test/Videos/episode-1/source-1/Subtitles/4/0/Stream.srt?ApiKey=t"));
    subtitle.title = QStringLiteral("English - SRT - External");
    subtitle.language = QStringLiteral("eng");
    subtitle.sourceIndex = 4;
    fixture.backend.variantCommands.clear();
    emit service.subtitleDownloaded(download.key, subtitle.toVariantMap());
    QVERIFY(!model.downloading());
    QCOMPARE(loadedSpy.count(), 1);

    // Loaded into mpv with select, selected, and saved for the season.
    QVERIFY(fixture.backend.variantCommands.contains(QVariantList{
        QStringLiteral("sub-add"), subtitle.url.toString(QUrl::FullyEncoded), QStringLiteral("select"),
        QStringLiteral("English - SRT - External"), QStringLiteral("eng")}));
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), 4);
    const ScopedTrackPreferences saved = fixture.trackPrefs.getSeasonPreferences(QStringLiteral("season-1"));
    QCOMPARE(saved.subtitle.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(saved.subtitle.streamIndex, 4);
    // mpv's sid report maps the new track.
    fixture.backend.emitSubtitleTrackId(2);
    QCOMPARE(fixture.controller.selectedSubtitleTrack(), 4);
    QCOMPARE(fixture.controller.m_externalSubtitleTrackMap.value(4), 2);

    // A download without a provider index plays but is not saved.
    model.download(QStringLiteral("low"));
    const FakePlaybackService::Call second = service.last(QStringLiteral("download"));
    Bloom::DownloadedSubtitle sessionOnly;
    sessionOnly.url = QUrl(QStringLiteral("https://silo.example.test/api/v2/stream/play-1/subtitles/0.vtt?file_id=1&downloaded_subtitle_id=7&token=t"));
    sessionOnly.title = QStringLiteral("Release low");
    emit service.subtitleDownloaded(second.key, sessionOnly.toVariantMap());
    QCOMPARE(loadedSpy.count(), 2);
    QVERIFY(fixture.controller.selectedSubtitleTrack() <= -1000);
    QCOMPARE(fixture.trackPrefs.getSeasonPreferences(QStringLiteral("season-1")).subtitle.streamIndex, 4);
    Q_UNUSED(availableChanged)
}

void SubtitleSearchTest::viewModelDropsStaleAnswers()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSearchViewModel model(&service, &fixture.controller, &fixture.config);
    fixture.play(QStringLiteral("movie-1"), QString(), QStringLiteral("play-1"));
    emit service.subtitleSearchStatusLoaded(service.last(QStringLiteral("status")).key, true);

    model.open();
    const FakePlaybackService::Call english = service.last(QStringLiteral("search"));
    model.setLanguage(QStringLiteral("fre"));
    const FakePlaybackService::Call french = service.last(QStringLiteral("search"));
    QVERIFY(english.key != french.key);
    QCOMPARE(french.language, QStringLiteral("fre"));
    QVERIFY(service.cancelled.contains(english.key));

    // The English answer lands after the language changed.
    emit service.subtitleSearchResultsLoaded(english.key, {resultMap(QStringLiteral("en-1"), 1.0, 1)});
    QVERIFY(model.isLoading());
    QCOMPARE(model.count(), 0);
    emit service.subtitleSearchFailed(english.key, QStringLiteral("late"));
    QVERIFY(!model.hasError());
    emit service.subtitleSearchResultsLoaded(french.key, {resultMap(QStringLiteral("fr-1"), 1.0, 1)});
    QCOMPARE(resultIds(model.results()), QStringList{QStringLiteral("fr-1")});

    // A download whose answer arrives after playback moved to another item
    // is not loaded.
    model.download(QStringLiteral("fr-1"));
    const FakePlaybackService::Call download = service.last(QStringLiteral("download"));
    fixture.play(QStringLiteral("movie-2"), QString(), QStringLiteral("play-2"));
    QSignalSpy loadedSpy(&model, &SubtitleSearchViewModel::subtitleLoaded);
    fixture.backend.variantCommands.clear();
    Bloom::DownloadedSubtitle subtitle;
    subtitle.url = QUrl(QStringLiteral("https://silo.example.test/api/v2/stream/play-1/subtitles/0.vtt"));
    emit service.subtitleDownloaded(download.key, subtitle.toVariantMap());
    QCOMPARE(loadedSpy.count(), 0);
    QVERIFY(fixture.backend.variantCommands.isEmpty());
    QCOMPARE(model.count(), 0);

    // A failed search shows its error and a retry asks again.
    emit service.subtitleSearchStatusLoaded(service.last(QStringLiteral("status")).key, true);
    model.open();
    const FakePlaybackService::Call failing = service.last(QStringLiteral("search"));
    emit service.subtitleSearchFailed(failing.key, QStringLiteral("The subtitle provider is busy."));
    QVERIFY(model.hasError());
    QCOMPARE(model.errorMessage(), QStringLiteral("The subtitle provider is busy."));
    model.reload();
    QVERIFY(!model.hasError());
    QVERIFY(service.last(QStringLiteral("search")).key != failing.key);
}

void SubtitleSearchTest::viewModelCancelsWhenPlaybackStops()
{
    PlayerFixture fixture;
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSearchViewModel model(&service, &fixture.controller, &fixture.config);
    fixture.play(QStringLiteral("movie-1"), QString(), QStringLiteral("play-1"));
    const QString statusKey = service.last(QStringLiteral("status")).key;
    emit service.subtitleSearchStatusLoaded(statusKey, true);
    QVERIFY(model.available());

    model.open();
    const FakePlaybackService::Call search = service.last(QStringLiteral("search"));
    fixture.stop();
    QVERIFY(service.cancelled.contains(search.key));
    QVERIFY(!model.available());
    QVERIFY(!model.isLoading());
    emit service.subtitleSearchResultsLoaded(search.key, {resultMap(QStringLiteral("a"), 1.0, 1)});
    QCOMPARE(model.count(), 0);

    // The status is read once per connection.
    const qsizetype statusReads = std::count_if(service.calls.cbegin(), service.calls.cend(),
        [](const FakePlaybackService::Call &call) { return call.kind == QStringLiteral("status"); });
    fixture.play(QStringLiteral("movie-2"), QString(), QStringLiteral("play-2"));
    QVERIFY(model.available());
    QCOMPARE(std::count_if(service.calls.cbegin(), service.calls.cend(),
                           [](const FakePlaybackService::Call &call) {
                               return call.kind == QStringLiteral("status");
                           }),
             statusReads);

    // A server that does not offer it hides the entry.
    service.connectionId = QStringLiteral("connection-2");
    fixture.play(QStringLiteral("movie-3"), QString(), QStringLiteral("play-3"));
    emit service.subtitleSearchStatusLoaded(service.last(QStringLiteral("status")).key, false);
    QVERIFY(!model.available());
    const qsizetype calls = service.calls.size();
    model.open();
    QCOMPARE(service.calls.size(), calls);
}

void SubtitleSearchTest::viewModelDefaultsToTheSubtitleLanguage()
{
    const QVariantList languages{
        QVariantMap{{QStringLiteral("label"), QStringLiteral("English")}, {QStringLiteral("value"), QStringLiteral("eng")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("French")}, {QStringLiteral("value"), QStringLiteral("fre")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("German")}, {QStringLiteral("value"), QStringLiteral("ger")}}};
    QCOMPARE(SubtitleSearchViewModel::defaultLanguage(QStringLiteral("fre"), QStringLiteral("ger"), languages),
             QStringLiteral("fre"));
    QCOMPARE(SubtitleSearchViewModel::defaultLanguage(QStringLiteral("forced"), QStringLiteral("ger"), languages),
             QStringLiteral("ger"));
    QCOMPARE(SubtitleSearchViewModel::defaultLanguage(QStringLiteral("off"), QStringLiteral("jellyfin-default"),
                                                      languages),
             QStringLiteral("eng"));

    PlayerFixture fixture;
    fixture.config.setDefaultSubtitleTrackSelection(QStringLiteral("ger"));
    FakePlaybackService &service = fixture.playbackService;
    SubtitleSearchViewModel model(&service, &fixture.controller, &fixture.config);
    fixture.play(QStringLiteral("movie-1"), QString(), QStringLiteral("play-1"));
    emit service.subtitleSearchStatusLoaded(service.last(QStringLiteral("status")).key, true);
    model.open();
    QCOMPARE(model.language(), QStringLiteral("ger"));
    QCOMPARE(model.languageLabel(), QStringLiteral("German"));
    QCOMPARE(service.last(QStringLiteral("search")).language, QStringLiteral("ger"));

    // Left/Right move through the list and wrap.
    const int position = [&model]() {
        const QVariantList list = model.languages();
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).toMap().value(QStringLiteral("value")).toString() == QStringLiteral("ger")) {
                return i;
            }
        }
        return -1;
    }();
    QVERIFY(position >= 0);
    model.cycleLanguage(1);
    const QString next = model.languages().at((position + 1) % model.languages().size())
                             .toMap().value(QStringLiteral("value")).toString();
    QCOMPARE(model.language(), next);
    QCOMPARE(service.last(QStringLiteral("search")).language, next);
    model.cycleLanguage(-1);
    QCOMPARE(model.language(), QStringLiteral("ger"));
}

QTEST_MAIN(SubtitleSearchTest)
#include "SubtitleSearchTest.moc"
