#include <QCoreApplication>
#include <QEventLoop>
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
#include <QUrl>
#include <QUrlQuery>
#include <cstring>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "network/MediaSegmentProviderService.h"
#include "network/PlaybackService.h"
#include "providers/ICatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

class ControlledReply final : public QNetworkReply
{
public:
    ControlledReply(const QNetworkRequest &request, int statusCode, QByteArray payload,
        bool delayed, QObject *parent, const QHash<QByteArray, QByteArray> &headers = {})
        : QNetworkReply(parent)
        , m_statusCode(statusCode)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        for (auto it = headers.cbegin(); it != headers.cend(); ++it)
            setRawHeader(it.key(), it.value());
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (statusCode >= 400) {
            setError(statusCode == 401
                         ? QNetworkReply::AuthenticationRequiredError
                         : QNetworkReply::UnknownServerError,
                     QStringLiteral("test network error"));
        }
        open(QIODevice::ReadOnly);
        if (!delayed) {
            QTimer::singleShot(0, this, [this]() { complete(); });
        }
    }

    void abort() override
    {
        m_aborted = true;
    }

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

    void complete()
    {
        if (isFinished()) {
            return;
        }
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_statusCode);
        setFinished(true);
        if (!m_payload.isEmpty()) {
            emit readyRead();
        }
        emit finished();
    }

    bool wasAborted() const { return m_aborted; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_payload.size() - m_offset;
        const qint64 count = qMin(maxSize, remaining);
        if (count <= 0) {
            return -1;
        }
        memcpy(data, m_payload.constData() + m_offset,
               static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    int m_statusCode;
    QByteArray m_payload;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

struct ResponsePlan
{
    int statusCode;
    QByteArray payload;
    bool delayed = false;
    QHash<QByteArray, QByteArray> headers;
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
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        requests.append(
            {operation, request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const ResponsePlan plan = responses.isEmpty()
            ? ResponsePlan{500, QByteArrayLiteral(R"({"error":"queue_empty"})"), false}
            : responses.takeFirst();
        auto *reply = new ControlledReply(
            request, plan.statusCode, plan.payload, plan.delayed, this, plan.headers);
        replies.append(reply);
        return reply;
    }
};

class ExposedAuthenticationService final : public AuthenticationService
{
public:
    ExposedAuthenticationService(HttpTransport *transport,
                                 IProviderAdapter *adapter)
        : AuthenticationService(nullptr, transport, adapter)
    {
    }

    void replaceAccount(const QString &accountId, const QString &accessToken)
    {
        seedSession(QStringLiteral("https://silo.example.test"),
                    accountId,
                    accessToken,
                    accountId);
    }
};

} // namespace

class SiloCatalogServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void nativeRoutesUseContentIdentityAndCorrectMethods();
    void themeGrantFlowUsesProfileAndFreshGrant();
    void playbackSurvivesAccessRefreshDuringCapabilityRead();
    void staleFirstPageCannotReplaceCurrentCursor();
    void structuredFiltersUseNativeQueryBody();
    void bookQueriesUseEbookType();
    void unsupportedFiltersAreExplicit();
    void responsePreservesSnapshotAndPaginationTruth();
    void nativeResponseShapesPreserveHomeLibrarySearchAndDetailData();
    void sparseNativeMappingsPreserveFallbacksAndState();
    void immediateCatalogRequestSurvivesAuthenticationCompletion();
    void requestHeadersAndGenerationSuppressStaleReplies();
    void theIntroDbV4Requests_data();
    void theIntroDbV4Requests();
    void externalSegmentsUseSeriesIdentityAndOrder_data();
    void externalSegmentsUseSeriesIdentityAndOrder();
    void externalSegmentsContinueAfterV4Errors_data();
    void externalSegmentsContinueAfterV4Errors();
};

void SiloCatalogServiceTest::theIntroDbV4Requests_data()
{
    QTest::addColumn<QString>("tvdbId");
    QTest::addColumn<QString>("tmdbId");
    QTest::addColumn<QString>("imdbId");
    QTest::addColumn<QString>("mediaType");
    QTest::addColumn<int>("season");
    QTest::addColumn<int>("episode");
    QTest::addColumn<qint64>("duration");
    QTest::addColumn<QString>("identityKey");
    QTest::newRow("tvdb-first") << " 81189 " << "1396" << "tt0903747" << "Episode" << 1 << 1 << qint64(3600000) << "tvdb_id";
    QTest::newRow("tmdb-fallback") << "" << "1396" << "tt0903747" << "Episode" << 1 << 1 << qint64(3600000) << "tmdb_id";
    QTest::newRow("imdb-fallback") << "" << "" << "tt0903747" << "Episode" << 1 << 1 << qint64(0) << "imdb_id";
    QTest::newRow("special") << "81189" << "" << "" << "Episode" << 0 << 1 << qint64(0) << "tvdb_id";
    QTest::newRow("episode-zero") << "81189" << "" << "" << "Episode" << 1 << 0 << qint64(0) << "tvdb_id";
    QTest::newRow("movie") << "169" << "" << "" << "Movie" << 1 << 1 << qint64(7200000) << "tvdb_id";
    QTest::newRow("missing-season") << "81189" << "" << "" << "Episode" << -1 << 1 << qint64(0) << "";
    QTest::newRow("missing-episode") << "81189" << "" << "" << "Episode" << 1 << -1 << qint64(0) << "";
    QTest::newRow("invalid-season") << "81189" << "" << "" << "Episode" << 1001 << 1 << qint64(0) << "";
    QTest::newRow("invalid-episode") << "81189" << "" << "" << "Episode" << 1 << 10001 << qint64(0) << "";
    QTest::newRow("no-identity") << " " << "" << "" << "Movie" << -1 << -1 << qint64(0) << "";
    QTest::newRow("unsupported-type") << "81189" << "" << "" << "Series" << -1 << -1 << qint64(0) << "";
}

void SiloCatalogServiceTest::theIntroDbV4Requests()
{
    QFETCH(QString, tvdbId);
    QFETCH(QString, tmdbId);
    QFETCH(QString, imdbId);
    QFETCH(QString, mediaType);
    QFETCH(int, season);
    QFETCH(int, episode);
    QFETCH(qint64, duration);
    QFETCH(QString, identityKey);
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    config.setMediaSegmentProviderOrder({QStringLiteral("theintrodb")});
    ControlledNetworkAccessManager manager;
    manager.responses = {{200, QByteArrayLiteral(R"({"tvdb_id":81189,"type":"tv","intro":[{"start_ms":1000,"end_ms":60000}]})")}};
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    MediaSegmentProviderService service(&auth, &config);
    MediaSegmentLookupContext context;
    context.itemId = QStringLiteral("item-1");
    context.tvdbId = tvdbId;
    context.tmdbId = tmdbId;
    context.imdbId = imdbId;
    context.type = mediaType;
    context.seasonNumber = season;
    context.episodeNumber = episode;
    context.episodeOrder = QStringLiteral("altdvd");
    context.durationMs = duration;
    bool completed = false;
    QList<MediaSegmentInfo> segments;
    service.fetchExternalSegments(context, {}, [&](const auto &result) {
        segments = result;
        completed = true;
    });
    QTRY_VERIFY(completed);
    if (identityKey.isEmpty()) {
        QVERIFY(manager.requests.isEmpty());
        QVERIFY(segments.isEmpty());
        return;
    }
    QCOMPARE(manager.requests.size(), 1);
    const auto request = manager.requests.first().request;
    QCOMPARE(manager.requests.first().operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(request.url().scheme(), QStringLiteral("https"));
    QCOMPARE(request.url().host(), QStringLiteral("api.theintrodb.org"));
    QCOMPARE(request.url().path(), QStringLiteral("/v4/media"));
    QVERIFY(!request.hasRawHeader(QByteArrayLiteral("Authorization")));
    QVERIFY(!request.hasRawHeader(QByteArrayLiteral("X-Emby-Token")));
    QVERIFY(request.transferTimeout() > 0);
    const QUrlQuery query(request.url());
    const QMap<QString, QString> identities{
        {QStringLiteral("tvdb_id"), tvdbId.trimmed()},
        {QStringLiteral("tmdb_id"), tmdbId.trimmed()},
        {QStringLiteral("imdb_id"), imdbId.trimmed()}};
    for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
        QCOMPARE(query.hasQueryItem(it.key()), it.key() == identityKey);
        if (it.key() == identityKey) QCOMPARE(query.queryItemValue(it.key()), it.value());
    }
    const bool isEpisode = mediaType == QStringLiteral("Episode");
    QCOMPARE(query.hasQueryItem(QStringLiteral("season")), isEpisode);
    QCOMPARE(query.hasQueryItem(QStringLiteral("episode")), isEpisode);
    QCOMPARE(query.hasQueryItem(QStringLiteral("mapping")), isEpisode);
    if (isEpisode) {
        QCOMPARE(query.queryItemValue(QStringLiteral("season")), QString::number(season));
        QCOMPARE(query.queryItemValue(QStringLiteral("episode")), QString::number(episode));
        QCOMPARE(query.queryItemValue(QStringLiteral("mapping")), QStringLiteral("altdvd"));
    }
    QCOMPARE(query.hasQueryItem(QStringLiteral("duration_ms")), duration > 0);
    if (duration > 0) QCOMPARE(query.queryItemValue(QStringLiteral("duration_ms")), QString::number(duration));
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().itemId, context.itemId);
}

void SiloCatalogServiceTest::externalSegmentsUseSeriesIdentityAndOrder_data()
{
    QTest::addColumn<bool>("nativeProvider");
    QTest::addColumn<bool>("seriesAvailable");
    QTest::newRow("jellyfin") << false << true;
    QTest::newRow("silo") << true << true;
    QTest::newRow("jellyfin-missing-series") << false << false;
    QTest::newRow("silo-missing-series") << true << false;
}

void SiloCatalogServiceTest::externalSegmentsUseSeriesIdentityAndOrder()
{
    QFETCH(bool, nativeProvider);
    QFETCH(bool, seriesAvailable);
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    config.setMediaSegmentProviderOrder({QStringLiteral("theintrodb")});
    ControlledNetworkAccessManager manager;
    manager.responses = {
        {200, QByteArrayLiteral(R"({"Items":[]})")},
        {200, nativeProvider
             ? QByteArrayLiteral(R"({"content_id":"episode-1","type":"episode","series_id":"series-1","season_number":0,"episode_number":1,"tvdb_id":"999999","tmdb_id":"999999","imdb_id":"tt9999999","duration_seconds":3600})")
             : QByteArrayLiteral(R"({"Items":[{"Id":"episode-1","Type":"Episode","SeriesId":"series-1","ParentIndexNumber":0,"IndexNumber":1,"RunTimeTicks":36000000000,"ProviderIds":{"Tvdb":"999999","Tmdb":"999999","Imdb":"tt9999999"}}]})")},
        {200, nativeProvider
             ? QByteArrayLiteral(R"({"content_id":"series-1","type":"series","tvdb_id":"81189","tmdb_id":"1396","imdb_id":"tt0903747"})")
             : QByteArrayLiteral(R"({"Items":[{"Id":"series-1","Type":"Series","DisplayOrder":"altdvd","ProviderIds":{"Tvdb":"81189","Tmdb":"1396","Imdb":"tt0903747"}}]})")},
        {200, QByteArrayLiteral(R"({"tvdb_id":81189,"type":"tv","season":0,"episode":1,"intro":[{"start_ms":1000,"end_ms":60000}]})")}
    };
    if (!seriesAvailable) {
        manager.responses[2] = {404, QByteArrayLiteral(R"({"error":"not found"})")};
    }
    HttpTransport transport(&manager);
    SiloProviderAdapter silo;
    JellyfinProviderAdapter jellyfin;
    ExposedAuthenticationService auth(&transport, nativeProvider
        ? static_cast<IProviderAdapter *>(&silo) : &jellyfin);
    auth.replaceAccount(QStringLiteral("account-1"), QStringLiteral("private-token"));
    QVERIFY(auth.isAuthenticated());
    MediaSegmentProviderService external(&auth, &config);
    PlaybackService service(&auth, &config, &external);
    QSignalSpy spy(&service, &PlaybackService::mediaSegmentsLoaded);
    service.getMediaSegments(QStringLiteral("episode-1"));
    QTRY_COMPARE(spy.size(), 1);
    const auto segments = qvariant_cast<QList<MediaSegmentInfo>>(spy.first().at(1));
    if (!seriesAvailable) {
        QCOMPARE(manager.requests.size(), 3);
        QVERIFY(segments.isEmpty());
        return;
    }
    QCOMPARE(manager.requests.size(), 4);
    const QUrl seriesUrl = manager.requests.at(2).request.url();
    if (nativeProvider) {
        QVERIFY(seriesUrl.path().contains(QStringLiteral("series-1")));
    } else {
        QCOMPARE(QUrlQuery(seriesUrl).queryItemValue(QStringLiteral("Ids")), QStringLiteral("series-1"));
    }
    const auto request = manager.requests.last().request;
    QCOMPARE(request.url().host(), QStringLiteral("api.theintrodb.org"));
    const QUrlQuery query(request.url());
    QCOMPARE(query.queryItemValue(QStringLiteral("tvdb_id")), QStringLiteral("81189"));
    QCOMPARE(query.queryItemValue(QStringLiteral("season")), QStringLiteral("0"));
    QCOMPARE(query.queryItemValue(QStringLiteral("episode")), QStringLiteral("1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("mapping")), nativeProvider ? QString() : QStringLiteral("altdvd"));
    QCOMPARE(query.queryItemValue(QStringLiteral("duration_ms")), QStringLiteral("3600000"));
    QVERIFY(!request.hasRawHeader(QByteArrayLiteral("Authorization")));
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().itemId, QStringLiteral("episode-1"));
}

void SiloCatalogServiceTest::externalSegmentsContinueAfterV4Errors_data()
{
    QTest::addColumn<int>("statusCode");
    QTest::newRow("unsupported-mapping") << 400;
    QTest::newRow("not-found") << 404;
    QTest::newRow("rate-limited") << 429;
    QTest::newRow("server-error") << 500;
}

void SiloCatalogServiceTest::externalSegmentsContinueAfterV4Errors()
{
    QFETCH(int, statusCode);
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    config.setMediaSegmentProviderOrder({QStringLiteral("theintrodb"), QStringLiteral("introdb")});
    ControlledNetworkAccessManager manager;
    manager.responses = {
        {statusCode, QByteArrayLiteral(R"({"error":"unavailable"})")},
        {200, QByteArrayLiteral(R"({"recap":{"start_ms":1000,"end_ms":10000}})")}
    };
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    MediaSegmentProviderService service(&auth, &config);
    MediaSegmentLookupContext context;
    context.itemId = QStringLiteral("episode-1");
    context.type = QStringLiteral("Episode");
    context.tvdbId = QStringLiteral("81189");
    context.imdbId = QStringLiteral("tt0903747");
    context.seasonNumber = 1;
    context.episodeNumber = 1;
    MediaSegmentInfo serverIntro;
    serverIntro.type = MediaSegmentType::Intro;
    serverIntro.source = QStringLiteral("server");
    serverIntro.startMs = 2000;
    serverIntro.endMs = 60000;
    bool completed = false;
    QList<MediaSegmentInfo> segments;
    service.fetchExternalSegments(context, {serverIntro}, [&](const auto &result) {
        segments = result;
        completed = true;
    });
    QTRY_VERIFY(completed);
    QCOMPARE(manager.requests.size(), 2);
    QCOMPARE(manager.requests.first().request.url().path(), QStringLiteral("/v4/media"));
    QCOMPARE(manager.requests.last().request.url().host(), QStringLiteral("api.introdb.app"));
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.first().source, QStringLiteral("server"));
    QCOMPARE(segments.first().startMs, 2000);
    QCOMPARE(segments.last().type, MediaSegmentType::Recap);
}

void SiloCatalogServiceTest::nativeRoutesUseContentIdentityAndCorrectMethods()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;

    ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Views, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/user/libraries"));
    QVERIFY(request.body.isEmpty());

    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/catalog?source=query"));

    request = provider.createRequest(ProviderCatalogOperation::LatestMedia, query);
    QVERIFY(request.supported);
    const QUrlQuery latestParameters{QUrl(request.relativeEndpoint)};
    QCOMPARE(latestParameters.queryItemValue(QStringLiteral("sort")), QStringLiteral("-added_at"));
    QVERIFY(!latestParameters.hasQueryItem(QStringLiteral("order")));

    ProviderCatalogQuery paged;
    paged.cursor = QStringLiteral("opaque/snapshot + token");
    request = provider.createRequest(ProviderCatalogOperation::Items, paged);
    QVERIFY(request.supported);
    const QUrlQuery pagedParameters(QUrl(request.relativeEndpoint));
    QCOMPARE(pagedParameters.queryItemValue(QStringLiteral("cursor")),
        QStringLiteral("opaque/snapshot + token"));

    query.itemId = QStringLiteral("content/with space");
    request = provider.createRequest(ProviderCatalogOperation::Item, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(
        request.relativeEndpoint, QStringLiteral("/api/v2/catalog/items/content%2Fwith%20space"));

    request = provider.createRequest(ProviderCatalogOperation::Versions, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
        QStringLiteral("/api/v2/catalog/items/content%2Fwith%20space/versions"));

    ProviderCatalogQuery hierarchy;
    hierarchy.seriesId = QStringLiteral("series/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons"));
    hierarchy.parentId = QStringLiteral("season/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, QStringLiteral("/api/v2/catalog/items/season%2Fone/episodes"));

    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("content-42");
    state.stateValue = true;
    ProviderCatalogQuery season;
    season.parentId = QStringLiteral("series/one");
    season.includeItemTypes = {QStringLiteral("Season")};
    request = provider.createRequest(ProviderCatalogOperation::Items, season);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons"));
    request = provider.createRequest(ProviderCatalogOperation::SetWatched, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/watched/content-42"));
    request = provider.createRequest(ProviderCatalogOperation::SetFavorite, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/favorites/content-42"));

    state.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetWatched, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/watched/content-42"));
    request = provider.createRequest(ProviderCatalogOperation::SetFavorite, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/favorites/content-42"));
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetWatched, {}).valid);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetFavorite, {}).valid);
}

void SiloCatalogServiceTest::structuredFiltersUseNativeQueryBody()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.parentId = QStringLiteral("17");
    query.startIndex = 40;
    query.limit = 250;
    query.genres = {QStringLiteral("Science Fiction")};
    query.studios = {QStringLiteral("Example Studio")};
    query.years = {1982, 2026};
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    query.watched = ProviderCatalogTriState::No;
    query.favorite = ProviderCatalogTriState::Yes;
    query.sortBy = QStringLiteral("releaseDate");
    query.sortOrder = QStringLiteral("descending");

    const ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/catalog/query"));
    QCOMPARE(request.extraHeaders.value(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/json"));

    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("library_id")).toString(), QStringLiteral("17"));
    QCOMPARE(body.value(QStringLiteral("seek")).toInt(), 40);
    QCOMPARE(body.value(QStringLiteral("limit")).toInt(), 100);
    QCOMPARE(body.value(QStringLiteral("sort")).toString(),
             QStringLiteral("release_date"));
    QCOMPARE(body.value(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    QCOMPARE(body.value(QStringLiteral("match")).toString(), QStringLiteral("all"));

    const QJsonArray groups = body.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groups.size(), 3);
    const QJsonObject allGroup = groups.at(0).toObject();
    QCOMPARE(allGroup.value(QStringLiteral("match")).toString(), QStringLiteral("all"));
    const QJsonArray allRules = allGroup.value(QStringLiteral("rules")).toArray();
    QCOMPARE(allRules.size(), 4);
    QCOMPARE(allRules.at(0).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("genre"));
    QCOMPARE(allRules.at(0).toObject().value(QStringLiteral("op")).toString(),
             QStringLiteral("contains"));
    QCOMPARE(allRules.at(1).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("studio"));
    QCOMPARE(allRules.at(2).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("watched"));
    QCOMPARE(allRules.at(2).toObject().value(QStringLiteral("value")).toBool(), false);
    QCOMPARE(allRules.at(3).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("favorited"));
    QCOMPARE(allRules.at(3).toObject().value(QStringLiteral("value")).toBool(), true);

    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("match")).toString(),
             QStringLiteral("any"));
    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("rules")).toArray().size(), 2);
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("match")).toString(),
             QStringLiteral("any"));
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("rules")).toArray().size(), 2);
    query.parentId.clear();
    const ProviderCatalogRequest unscopedRequest = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(unscopedRequest.supported);
    const QJsonObject unscopedBody = QJsonDocument::fromJson(unscopedRequest.body).object();
    QVERIFY(!unscopedBody.contains(QStringLiteral("library_id")));

    ProviderCatalogQuery search;
    search.parentId = QStringLiteral("17");
    search.searchTerm = QStringLiteral("Alien & Aliens");
    search.limit = 30;
    const ProviderCatalogRequest searchRequest = provider.createRequest(
        ProviderCatalogOperation::Search, search);
    QVERIFY(searchRequest.supported);
    QCOMPARE(searchRequest.method, ProviderHttpMethod::Get);
    QVERIFY(searchRequest.body.isEmpty());
    const QUrl searchUrl(searchRequest.relativeEndpoint);
    const QUrlQuery parameters(searchUrl);
    QCOMPARE(searchUrl.path(), QStringLiteral("/api/v2/catalog"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("source")), QStringLiteral("query"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("library_id")), QStringLiteral("17"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("q")), QStringLiteral("Alien & Aliens"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("limit")), QStringLiteral("30"));
}

void SiloCatalogServiceTest::bookQueriesUseEbookType()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.includeItemTypes = {QStringLiteral("Book")};

    const ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QJsonArray groups = body.value(QStringLiteral("groups")).toArray();
    QVERIFY(!groups.isEmpty());
    const QJsonArray rules = groups.first().toObject().value(QStringLiteral("rules")).toArray();
    QVERIFY(!rules.isEmpty());
    QCOMPARE(rules.first().toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("type"));
    QCOMPARE(rules.first().toObject().value(QStringLiteral("value")).toString(),
             QStringLiteral("ebook"));
}

void SiloCatalogServiceTest::unsupportedFiltersAreExplicit()
{
    SiloCatalogProvider provider;
    const auto verifyUnsupported = [&provider](ProviderCatalogOperation operation,
                                               const ProviderCatalogQuery &query) {
        const ProviderCatalogRequest request = provider.createRequest(operation, query);
        QVERIFY(!request.supported);
        QVERIFY2(!request.unsupportedReason.trimmed().isEmpty(),
                 "Unsupported provider semantics must include a reason");
        QVERIFY(request.relativeEndpoint.isEmpty());
        QVERIFY(request.body.isEmpty());
    };

    ProviderCatalogQuery query;
    query.tags = {QStringLiteral("award-winner")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.includeItemTypes = {QStringLiteral("Audio")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.minPremiereDate = QDate(2020, 1, 1);
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.sortBy = QStringLiteral("unsupported-sort");
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.sortBy = QStringLiteral("PremiereDate");
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.sortOrder = QStringLiteral("desc");
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.sortOrder = QStringLiteral("sideways");
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.itemIds = {QStringLiteral("content-1")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    const ProviderCatalogRequest nextUp = provider.createRequest(
        ProviderCatalogOperation::NextUp, query);
    QVERIFY(nextUp.supported);
    QCOMPARE(nextUp.method, ProviderHttpMethod::Get);
    QCOMPARE(nextUp.relativeEndpoint, QStringLiteral("/api/v2/home/sections/system-next-up/items"));
    verifyUnsupported(ProviderCatalogOperation::RandomItems, query);
    verifyUnsupported(ProviderCatalogOperation::ResolveLibrary, query);
}

void SiloCatalogServiceTest::sparseNativeMappingsPreserveFallbacksAndState()
{
    const QJsonObject item{
        {QStringLiteral("content_id"), QStringLiteral("content-1")},
        {QStringLiteral("title"), QStringLiteral("Sparse")},
        {QStringLiteral("poster_url"), QString()},
        {QStringLiteral("still_url"), QStringLiteral("still-url")},
        {QStringLiteral("poster_thumbhash"), QString()},
        {QStringLiteral("still_thumbhash"), QStringLiteral("still-hash")},
        {QStringLiteral("user_state"), QJsonObject{
             {QStringLiteral("position_seconds"), 12.5},
             {QStringLiteral("is_in_progress"), false}}},
        {QStringLiteral("versions"), QJsonArray{
             QJsonObject{{QStringLiteral("file_id"), QStringLiteral("file-1")},
                         {QStringLiteral("chapters"), QJsonArray{
                              QJsonObject{{QStringLiteral("start_seconds"), 0.0}}}}}}}};
    const QVariantMap mapped = SiloModelMapper::mediaItem(item, QStringLiteral("silo"));
    QCOMPARE(mapped.value(QStringLiteral("primaryArtworkUrl")).toString(),
             QStringLiteral("still-url"));
    QCOMPARE(mapped.value(QStringLiteral("artworkThumbhashes")).toMap()
                 .value(QStringLiteral("poster")).toString(),
             QStringLiteral("still-hash"));
    QVERIFY(mapped.value(QStringLiteral("isInProgress")).toBool());
    const QVariantList versions = mapped.value(QStringLiteral("versions")).toList();
    QCOMPARE(versions.size(), 1);
    const QVariantList chapters = versions.constFirst().toMap()
                                      .value(QStringLiteral("chapters")).toList();
    QCOMPARE(chapters.size(), 1);
    QCOMPARE(chapters.constFirst().toMap().value(QStringLiteral("endMs")).toLongLong(), -1);

    const QVariantMap native = SiloModelMapper::nativeState(
        QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("content-1")},
            {QStringLiteral("state"), QJsonObject{
                 {QStringLiteral("position_seconds"), 4.0},
                 {QStringLiteral("played"), false}}}},
        QStringLiteral("silo"));
    QCOMPARE(native.value(QStringLiteral("itemId")).toString(), QStringLiteral("content-1"));
    QVERIFY(native.value(QStringLiteral("isInProgress")).toBool());
}

void SiloCatalogServiceTest::responsePreservesSnapshotAndPaginationTruth()
{
    SiloCatalogProvider provider;
    const ProviderCatalogResponse page = provider.parseResponse(ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-1"},{"content_id":"content-2"}],"total":912,"total_exact":false,"has_more":true,"window_cursor":"window-1","page":{"has_more":true,"next_cursor":"opaque-next"}})"),
        { { QByteArrayLiteral("ETAG"), QByteArrayLiteral("\"silo-page-v1\"") },
            { QByteArrayLiteral("last-modified"),
                QByteArrayLiteral("Fri, 07 Aug 2026 11:22:33 GMT") } });
    QVERIFY(page.valid);
    QCOMPARE(page.rawItems.size(), 2);
    const QString firstId = page.rawItems.at(0).toObject()
                                .value(QStringLiteral("content_id")).toString();
    const QString secondId = page.rawItems.at(1).toObject()
                                 .value(QStringLiteral("content_id")).toString();
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());
    QVERIFY(firstId != secondId);
    QCOMPARE(page.total, 912);
    QVERIFY(page.hasMore);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("totalPresent")).toBool(), true);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(), false);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("hasMorePresent")).toBool(), true);
    QCOMPARE(page.nextCursor, QStringLiteral("opaque-next"));
    QCOMPARE(page.snapshot.value(QStringLiteral("etag")).toByteArray(),
             QByteArrayLiteral("\"silo-page-v1\""));
    QCOMPARE(page.snapshot.value(QStringLiteral("lastModified")).toByteArray(),
             QByteArrayLiteral("Fri, 07 Aug 2026 11:22:33 GMT"));

    const ProviderCatalogResponse queryWindow = provider.parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-3"}],"total":1,"total_exact":false,"page":{"has_more":false}})"));
    QVERIFY(queryWindow.valid);
    QCOMPARE(queryWindow.rawItems.size(), 1);
    const QString nextId = queryWindow.rawItems.first().toObject()
                               .value(QStringLiteral("content_id")).toString();
    QVERIFY(!nextId.isEmpty());
    QVERIFY(nextId != firstId);
    QVERIFY(nextId != secondId);
    QCOMPARE(queryWindow.total, 1);
    QVERIFY(!queryWindow.hasMore);
    QCOMPARE(queryWindow.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(),
             false);
    QVERIFY(!queryWindow.snapshot.contains(QStringLiteral("snapshot")));

    const ProviderCatalogResponse detail = provider.parseResponse(
        ProviderCatalogOperation::Item,
        QByteArrayLiteral(
            R"({"content_id":"content-42","versions":[{"file_id":"file-99"}]})"));
    QVERIFY(detail.valid);
    QCOMPARE(detail.rawItem.value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-42"));
    QCOMPARE(detail.rawItem.value(QStringLiteral("versions")).toArray()
                 .at(0).toObject().value(QStringLiteral("file_id")).toString(),
             QStringLiteral("file-99"));
}

void SiloCatalogServiceTest::nativeResponseShapesPreserveHomeLibrarySearchAndDetailData()
{
    SiloCatalogProvider provider;

    const ProviderCatalogResponse home = provider.parseResponse(
        ProviderCatalogOperation::Views,
        QByteArrayLiteral(
            R"({"sections":[{"id":"system-next-up","title":"Next Up","type":"next_up"},{"id":"library-17","title":"Movies","type":"library"}]})"));
    QVERIFY(home.valid);
    QCOMPARE(home.rawItems.size(), 2);
    QCOMPARE(home.rawItems.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("system-next-up"));
    QCOMPARE(home.rawItems.at(1).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("library"));

    const ProviderCatalogResponse library = provider.parseResponse(ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-1","type":"movie","title":"One"},{"content_id":"content-2","type":"movie","title":"Two"}],"total":2,"total_exact":true,"page":{"has_more":false}})"));
    QVERIFY(library.valid);
    QCOMPARE(library.rawItems.size(), 2);
    QCOMPARE(library.rawItems.at(1).toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-2"));
    QCOMPARE(library.total, 2);
    QCOMPARE(library.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(), true);
    QVERIFY(!library.hasMore);

    const ProviderCatalogResponse search = provider.parseResponse(
        ProviderCatalogOperation::Search,
        QByteArrayLiteral("{\"items\":[{\"content_id\":\"content-9\",\"type\":\"series\",\"title\":\"Alien\"}],\"total\":1,\"total_exact\":true,\"has_more\":false,\"search_diagnostics\":{\"term\":\"Alien\"}}"));
    QVERIFY(search.valid);
    QCOMPARE(search.rawItems.size(), 1);
    QCOMPARE(search.rawItems.first().toObject()
                 .value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-9"));
    QCOMPARE(search.capabilityMetadata.value(QStringLiteral("searchDiagnostics")).toMap()
                 .value(QStringLiteral("term")).toString(),
             QStringLiteral("Alien"));

    const ProviderCatalogResponse detailResponse = provider.parseResponse(
        ProviderCatalogOperation::Item,
        QByteArrayLiteral(
            R"JSON({"content_id":"content-42","type":"movie","title":"Example","provider_ids":{"imdb_id":"tt123"},"user_state":{"played":true,"is_favorite":true},"versions":[{"file_id":"file-99","duration":123.456}],"playback_variants":[{"variant_id":"variant-1","part_count":2}],"subtitles":[{"language":"en"}]})JSON"));
    QVERIFY(detailResponse.valid);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-42"));
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("provider_ids")).toObject()
                 .value(QStringLiteral("imdb_id")).toString(),
             QStringLiteral("tt123"));
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("versions")).toArray().size(), 1);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("playback_variants")).toArray()
                 .first().toObject().value(QStringLiteral("part_count")).toInt(),
             2);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("subtitles")).toArray().size(), 1);
}

void SiloCatalogServiceTest::immediateCatalogRequestSurvivesAuthenticationCompletion()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;

    ControlledNetworkAccessManager manager;
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})"),
            false },
        { 200,
            QByteArrayLiteral(
                R"({"items":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true}]})"),
            false },
        { 200, QByteArrayLiteral(R"([{"id":"1","name":"Movies"}])"), false }
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    QTRY_VERIFY_WITH_TIMEOUT(!auth.isRestoringSession(), 1000);

    LibraryService *servicePtr = nullptr;
    connect(&auth, &AuthenticationService::authenticationStepChanged,
            &auth, [&]() {
        if (auth.isAuthenticated() && servicePtr) {
            servicePtr->getViews();
        }
    });

    LibraryService service(&auth);
    servicePtr = &service;
    QSignalSpy viewsSpy(&service, &LibraryService::canonicalViewsLoadedForConnection);
    QVERIFY(viewsSpy.isValid());

    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"),
                      QStringLiteral("Alice"),
                      QStringLiteral("password"));

    QTRY_COMPARE_WITH_TIMEOUT(viewsSpy.count(), 1, 1000);
    QCOMPARE(manager.requests.constLast().request.url().path(),
        QStringLiteral("/api/v2/user/libraries"));
    const QVariantList views = viewsSpy.first().at(1).toList();
    QCOMPARE(views.size(), 1);
    QCOMPARE(views.constFirst().toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("1"));
}

void SiloCatalogServiceTest::requestHeadersAndGenerationSuppressStaleReplies()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;

    ControlledNetworkAccessManager manager;
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})"),
            false },
        { 200,
            QByteArrayLiteral(
                R"({"items":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true},{"id":"profile-2","name":"Cinema","has_pin":false,"is_child":false,"is_primary":false}]})"),
            false }
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    // Wait out async restore so later auth state is deterministic.
    QTRY_VERIFY_WITH_TIMEOUT(!auth.isRestoringSession(), 1000);
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"),
                      QStringLiteral("Alice"),
                      QStringLiteral("password"));
    QTRY_COMPARE_WITH_TIMEOUT(auth.authenticationStep(), QStringLiteral("profiles"), 1000);
    auth.selectProfile(QStringLiteral("profile-1"));
    QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 1000);
    QCOMPARE(config.getActiveConnection().value_or(ServerConnection{}).profileId,
             QStringLiteral("profile-1"));

    LibraryService service(&auth);
    QSignalSpy viewsSpy(&service, &LibraryService::canonicalViewsLoadedForConnection);
    QVERIFY(viewsSpy.isValid());

    manager.responses.append(
        { 200, QByteArrayLiteral(R"([{"id":"1","name":"Old account"}])"), true });
    const qsizetype oldRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), oldRequestIndex + 1, 1000);
    const RecordedRequest oldRequest = manager.requests.at(oldRequestIndex);
    QCOMPARE(oldRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(oldRequest.request.url().path(), QStringLiteral("/api/v2/user/libraries"));
    QCOMPARE(oldRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(oldRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QVERIFY(oldRequest.body.isEmpty());
    QPointer<ControlledReply> staleAccountReply = manager.replies.constLast();
    QVERIFY(staleAccountReply);

    auth.replaceAccount(QStringLiteral("account-2"), QStringLiteral("access-2"));
    QTRY_VERIFY_WITH_TIMEOUT(staleAccountReply->wasAborted(), 1000);
    staleAccountReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(viewsSpy.count(), 0);

    manager.responses.append(
        { 200, QByteArrayLiteral(R"([{"id":"2","name":"New account"}])"), true });
    const qsizetype newRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), newRequestIndex + 1, 1000);
    const RecordedRequest newRequest = manager.requests.at(newRequestIndex);
    QCOMPARE(newRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(newRequest.request.url().path(), QStringLiteral("/api/v2/user/libraries"));
    QCOMPARE(newRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
    QCOMPARE(newRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QPointer<ControlledReply> currentReply = manager.replies.constLast();
    QVERIFY(currentReply);
    currentReply->complete();
    QTRY_COMPARE_WITH_TIMEOUT(viewsSpy.count(), 1, 1000);
    const QVariantList currentViews = viewsSpy.first().at(1).toList();
    QCOMPARE(currentViews.size(), 1);
    QCOMPARE(currentViews.constFirst().toMap()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("2"));

    manager.responses.append(
        { 200, QByteArrayLiteral(R"([{"id":"3","name":"Stale profile"}])"), true });
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), newRequestIndex + 2, 1000);
    QPointer<ControlledReply> staleProfileReply = manager.replies.constLast();
    QVERIFY(staleProfileReply);
    auth.selectProfile(QStringLiteral("profile-2"));
    QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 1000);
    QCOMPARE(config.getActiveConnection().value_or(ServerConnection{}).profileId,
             QStringLiteral("profile-2"));
    QTRY_VERIFY_WITH_TIMEOUT(staleProfileReply->wasAborted(), 1000);
    staleProfileReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(viewsSpy.count(), 1);

    manager.responses.append(
        { 200, QByteArrayLiteral(R"([{"id":"4","name":"Current profile"}])"), true });
    const qsizetype profileRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), profileRequestIndex + 1, 1000);
    const RecordedRequest profileRequest = manager.requests.at(profileRequestIndex);
    QCOMPARE(profileRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(profileRequest.request.url().path(), QStringLiteral("/api/v2/user/libraries"));
    QCOMPARE(profileRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
    QCOMPARE(profileRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-2"));
    QVERIFY(profileRequest.body.isEmpty());
    QPointer<ControlledReply> profileReply = manager.replies.constLast();
    QVERIFY(profileReply);
    profileReply->complete();
    QTRY_COMPARE_WITH_TIMEOUT(viewsSpy.count(), 2, 1000);
}

void SiloCatalogServiceTest::themeGrantFlowUsesProfileAndFreshGrant()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    const QByteArray grant = QJsonDocument(
        QJsonObject {
            { "url", "/api/v2/catalog/items/series-1/themes/theme-1/audio?token=secret%2Bvalue" },
            { "delivery", "converted" }, { "content_type", "audio/mp4" },
            { "expires_at",
                QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs) } })
                                 .toJson();
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"token","refresh_token":"refresh","expires_in":900,"user":{"id":"account-1","username":"tester"}})"),
            false },
        { 200,
            QByteArrayLiteral(R"({"items":[{"id":"profile-1","name":"tester","has_pin":false}]})"),
            false },
        { 200, QByteArrayLiteral(R"({"state":"available","allowed":true,"delivery":"routed"})"),
            false, { { "ETag", "opaque-theme-validator" } } },
        { 200,
            QByteArrayLiteral(R"({"themes":{"owner_id":"series-1","items":[{"id":"theme-1"}]}})"),
            false },
        { 200, grant, false }
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    QTRY_VERIFY(!auth.isRestoringSession());
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"), QStringLiteral("tester"),
        QStringLiteral("password"));
    QTRY_VERIFY(auth.isAuthenticated());
    LibraryService service(&auth);
    QSignalSpy loaded(&service, &LibraryService::themeSongPlaybackLoaded);
    service.getThemeSongs(QStringLiteral("series-1"));
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(1).toStringList().first(),
        QStringLiteral("https://silo.example.test/api/v2/catalog/items/series-1/themes/theme-1/"
                       "audio?token=secret%2Bvalue"));
    QVERIFY(loaded.first().at(2).toBool());
    manager.responses = { { 304, {}, false },
        { 200,
            QByteArrayLiteral(R"({"themes":{"owner_id":"series-1","items":[{"id":"theme-1"}]}})"),
            false },
        { 200, grant, false } };
    service.getThemeSongs(QStringLiteral("series-1"));
    QTRY_COMPARE(loaded.count(), 2);
    QCOMPARE(manager.requests.at(5).request.rawHeader("If-None-Match"),
        QByteArrayLiteral("opaque-theme-validator"));
    QCOMPARE(manager.requests.last().operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(manager.requests.last().request.url().path(),
        QStringLiteral("/api/v2/catalog/items/series-1/themes/theme-1/playback"));
    for (int i = 2; i < manager.requests.size(); ++i)
    {
        QCOMPARE(manager.requests.at(i).request.rawHeader("X-Profile-Id"),
            QByteArrayLiteral("profile-1"));
    }
}

void SiloCatalogServiceTest::playbackSurvivesAccessRefreshDuringCapabilityRead()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"account-1","username":"tester"}})"),
            false },
        { 200,
            QByteArrayLiteral(R"({"items":[{"id":"profile-1","name":"tester","has_pin":false}]})"),
            false }
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    QTRY_VERIFY(!auth.isRestoringSession());
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"), QStringLiteral("tester"),
        QStringLiteral("password"));
    QTRY_VERIFY(auth.isAuthenticated());
    const QJsonObject decision { { "protocol_version", 3 }, { "outcome", "playable" },
        { "session_id", "session-1" },
        { "playback_plan",
            QJsonObject { { "protocol_version", 3 }, { "plan_id", "plan:00000001" },
                { "plan_attempt_key", "v3:00000001" }, { "session_id", "session-1" },
                { "delivery", "original_http" }, { "effective_media_file_id", "file-1" },
                { "requested_media_file_id", "file-1" },
                { "stream",
                    QJsonObject { { "url", "/api/v2/stream/session-1?st=opaque%2Bvalue" },
                        { "protocol", "http_progressive" }, { "header_refresh", "none" },
                        { "headers", QJsonObject {} } } },
                { "timeline",
                    QJsonObject { { "timeline_offset_seconds", 0 }, { "can_seek_anywhere", true },
                        { "player_start_seconds", 0 } } },
                { "subtitle",
                    QJsonObject { { "mode", "off" }, { "inventory", QJsonArray {} } } } } } };
    const QJsonObject problem { { "type",
                                    "https://siloserver.org/docs/api/v2/problems/invalid_token" },
        { "title", "Invalid token" }, { "status", 401 }, { "detail", "Expired" },
        { "instance", "urn:silo:request:test" } };
    manager.responses = { { 401, QJsonDocument(problem).toJson(), false },
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":900})"),
            false },
        { 200,
            QByteArrayLiteral(
                R"({"state":"available","allowed":true,"installation_id":"installation-1","protocol_versions":[3],"features":["playback_plan_v3"],"deliveries":["original_http"]})"),
            false },
        { 201, QJsonDocument(decision).toJson(), false } };
    PlaybackService playback(&auth, &config);
    QSignalSpy loaded(&playback, &PlaybackService::playbackDescriptorLoadedForRequest);
    QSignalSpy failed(&playback, &PlaybackService::playbackDescriptorFailedForRequest);
    playback.requestPlaybackDescriptor(QStringLiteral("item-1"), { { "fileId", "file-1" } }, 0, -1,
        0, QString(), QStringLiteral("refresh-test"));
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(auth.getAccessToken(), QStringLiteral("access-2"));
    const auto descriptor = qvariant_cast<Bloom::PlaybackDescriptor>(loaded.first().at(1));
    QVERIFY(descriptor.stream.url.toEncoded().endsWith("st=opaque%2Bvalue&token=access-2"));
    QCOMPARE(manager.requests.last().request.rawHeader("Authorization"),
        QByteArrayLiteral("Bearer access-2"));
}

void SiloCatalogServiceTest::staleFirstPageCannotReplaceCurrentCursor()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    manager.responses = {
        {200, QByteArrayLiteral(R"({"access_token":"token","refresh_token":"refresh","expires_in":900,"user":{"id":"account-1","username":"tester"}})"), false},
        {200, QByteArrayLiteral(R"({"items":[{"id":"profile-1","name":"tester","has_pin":false}]})"), false},
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"old-item","title":"Old"}],"page":{"has_more":true,"next_cursor":"stale-cursor"},"window_cursor":"old-window","total":2})"), true},
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"new-item","title":"New"}],"page":{"has_more":true,"next_cursor":"current-cursor"},"window_cursor":"new-window","total":2})"), false},
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"next-item","title":"Next"}],"page":{"has_more":false},"window_cursor":"new-window","total":2})"), false}
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    QTRY_VERIFY(!auth.isRestoringSession());
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"), QStringLiteral("tester"), QStringLiteral("password"));
    QTRY_VERIFY(auth.isAuthenticated());
    LibraryService library(&auth);
    QSignalSpy loaded(&library, &LibraryService::canonicalItemsLoadedForConnection);
    LibraryItemQuery query; query.parentId = QStringLiteral("opaque-library"); query.limit = 1;
    library.getItems(query);
    library.getItems(query);
    QTRY_COMPARE(loaded.count(), 1);
    manager.replies.at(2)->complete();
    QTRY_COMPARE(loaded.count(), 2);
    query.startIndex = 1;
    library.getItems(query);
    QCOMPARE(QUrlQuery(manager.requests.last().request.url()).queryItemValue(QStringLiteral("cursor")), QStringLiteral("current-cursor"));
    QTRY_COMPARE(loaded.count(), 3);
}

QTEST_MAIN(SiloCatalogServiceTest)
#include "SiloCatalogServiceTest.moc"
