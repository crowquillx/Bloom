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
#include "providers/silo/SiloArtworkProvider.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloModelMapper.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

// Silo artwork-bearing reads ask for the widths Bloom draws on a 4K screen.
QString withArtworkWidths(const QString &endpoint)
{
    return endpoint + (endpoint.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?"))
        + QStringLiteral("poster_width=1000&backdrop_width=3840&logo_width=1280&still_width=1280");
}

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
    void playbackStartRenewsOnlyAgingAccessTokens();
    void staleFirstPageCannotReplaceCurrentCursor();
    void hierarchyAndRandomFallbacksUseCatalogRoutes();
    void structuredFiltersUseNativeQueryBody();
    void bookQueriesUseEbookType();
    void unsupportedFiltersAreExplicit();
    void responsePreservesSnapshotAndPaginationTruth();
    void nativeResponseShapesPreserveHomeLibrarySearchAndDetailData();
    void sparseNativeMappingsPreserveFallbacksAndState();
    void episodeCardsMapParentArtwork();
    void seasonPosterRefreshReadsItsOwnField();
    void immediateCatalogRequestSurvivesAuthenticationCompletion();
    void requestHeadersAndGenerationSuppressStaleReplies();
    void theIntroDbV4Requests_data();
    void theIntroDbV4Requests();
    void externalSegmentsUseSeriesIdentityAndOrder_data();
    void externalSegmentsUseSeriesIdentityAndOrder();
    void externalSegmentsContinueAfterV4Errors_data();
    void externalSegmentsContinueAfterV4Errors();
    void homeSectionsKeepCuratedVideoRowsInLayoutOrder();
    void watchlistRequestsUseMembershipRoutes();
    void artworkWidthsOnlyOnRoutesThatDeclareThem();
    void homeSectionsServiceAddsWatchlistRowOnlyWithoutOne();
    void homeSectionsCapRowsOnlyWhenTheServerAcceptsIt();
    void homeSectionsServiceReportsFailure();
    void watchlistWritesReportSuccessAndFailure();
    void jellyfinHasNoWatchlistOrCuratedRows();
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
    QCOMPARE(request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog?source=query")));

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
        request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/items/content%2Fwith%20space")));

    request = provider.createRequest(ProviderCatalogOperation::Versions, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
        withArtworkWidths(QStringLiteral("/api/v2/catalog/items/content%2Fwith%20space/versions")));

    ProviderCatalogQuery hierarchy;
    hierarchy.seriesId = QStringLiteral("series/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons")));
    hierarchy.parentId = QStringLiteral("season/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/items/season%2Fone/episodes")));

    // LibraryService::getSeasons / getSeasonEpisodes send only a parent and
    // one item type; both must use hierarchy routes, never library_id.
    ProviderCatalogQuery seasonEpisodes;
    seasonEpisodes.parentId = QStringLiteral("season/one");
    seasonEpisodes.includeItemTypes = {QStringLiteral("Episode")};
    request = provider.createRequest(ProviderCatalogOperation::Items, seasonEpisodes);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/items/season%2Fone/episodes")));

    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("content-42");
    state.stateValue = true;
    ProviderCatalogQuery season;
    season.parentId = QStringLiteral("series/one");
    season.includeItemTypes = {QStringLiteral("Season")};
    request = provider.createRequest(ProviderCatalogOperation::Items, season);
    QVERIFY(request.supported);
    QCOMPARE(
        request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons")));
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
    QCOMPARE(request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/catalog/query")));
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
    QCOMPARE(nextUp.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/home/sections")));
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

void SiloCatalogServiceTest::episodeCardsMapParentArtwork()
{
    const QString connectionId = QStringLiteral("silo");
    const auto ref = [](const QVariantMap &item, const QString &key) {
        return item.value(key).toMap();
    };

    // A browse card: backdrop_url is the episode still, so the series
    // backdrop stands in for it.
    QJsonObject card{
        {QStringLiteral("content_id"), QStringLiteral("episode:1")},
        {QStringLiteral("type"), QStringLiteral("episode")},
        {QStringLiteral("series_id"), QStringLiteral("series:1")},
        {QStringLiteral("series_title"), QStringLiteral("Show")},
        {QStringLiteral("poster_url"), QStringLiteral("https://cdn.test/season-1.webp?sig=a")},
        {QStringLiteral("backdrop_url"), QStringLiteral("https://cdn.test/still-1.webp?sig=b")},
        {QStringLiteral("backdrop_is_episode_still"), true},
        {QStringLiteral("backdrop_thumbhash"), QStringLiteral("still-hash")},
        {QStringLiteral("backdrop_url_expires_at"), QStringLiteral("2026-11-01T00:00:00Z")},
        {QStringLiteral("series_poster_url"), QStringLiteral("https://cdn.test/series-p.webp?sig=c")},
        {QStringLiteral("series_poster_thumbhash"), QStringLiteral("series-poster-hash")},
        {QStringLiteral("season_poster_url"), QStringLiteral("https://cdn.test/season-1.webp?sig=d")},
        {QStringLiteral("season_poster_thumbhash"), QStringLiteral("season-poster-hash")},
        {QStringLiteral("series_backdrop_url"), QStringLiteral("https://cdn.test/series-b.webp?sig=e")},
        {QStringLiteral("series_backdrop_thumbhash"), QStringLiteral("series-backdrop-hash")}};
    QVariantMap mapped = SiloModelMapper::mediaItem(card, connectionId);

    const QVariantMap seriesPoster = ref(mapped, QStringLiteral("seriesPrimaryArtwork"));
    QCOMPARE(seriesPoster.value(QStringLiteral("itemId")).toString(), QStringLiteral("series:1"));
    QCOMPARE(seriesPoster.value(QStringLiteral("kind")).toString(), QStringLiteral("primary"));
    QCOMPARE(seriesPoster.value(QStringLiteral("tag")).toString(),
             SiloModelMapper::artworkTag(QStringLiteral("https://cdn.test/series-p.webp")));
    QCOMPARE(mapped.value(QStringLiteral("seriesPrimaryArtworkUrl")).toString(),
             QStringLiteral("https://cdn.test/series-p.webp?sig=c"));

    // Cards carry no season ID, so the episode owns its season poster.
    const QVariantMap seasonPoster = ref(mapped, QStringLiteral("parentPrimaryArtwork"));
    QCOMPARE(seasonPoster.value(QStringLiteral("itemId")).toString(), QStringLiteral("episode:1"));
    QCOMPARE(seasonPoster.value(QStringLiteral("kind")).toString(), QStringLiteral("primary"));
    QCOMPARE(seasonPoster.value(QStringLiteral("tag")).toString(),
             ref(mapped, QStringLiteral("primaryArtwork")).value(QStringLiteral("tag")).toString());

    const QVariantMap backdrop = ref(mapped, QStringLiteral("backdropArtwork"));
    QCOMPARE(backdrop.value(QStringLiteral("itemId")).toString(), QStringLiteral("series:1"));
    QCOMPARE(mapped.value(QStringLiteral("backdropArtworkUrl")).toString(),
             QStringLiteral("https://cdn.test/series-b.webp?sig=e"));
    const QVariantMap thumbhashes = mapped.value(QStringLiteral("artworkThumbhashes")).toMap();
    QCOMPARE(thumbhashes.value(QStringLiteral("backdrop")).toString(),
             QStringLiteral("series-backdrop-hash"));
    QCOMPARE(thumbhashes.value(QStringLiteral("seriesPoster")).toString(),
             QStringLiteral("series-poster-hash"));
    QCOMPARE(thumbhashes.value(QStringLiteral("seasonPoster")).toString(),
             QStringLiteral("season-poster-hash"));
    // The still's expiry no longer describes the backdrop.
    QVERIFY(!mapped.value(QStringLiteral("artworkExpiresAt")).toMap()
                 .contains(QStringLiteral("backdrop")));
    // The still stays in the artwork list after the series backdrop.
    QStringList backdropOwners;
    for (const QVariant &value : mapped.value(QStringLiteral("artwork")).toList()) {
        const QVariantMap artwork = value.toMap();
        if (artwork.value(QStringLiteral("kind")).toString() == QStringLiteral("backdrop")) {
            backdropOwners.append(artwork.value(QStringLiteral("itemId")).toString());
        }
    }
    QCOMPARE(backdropOwners, (QStringList{QStringLiteral("series:1"), QStringLiteral("episode:1")}));

    // Continue Watching and Next Up cards already carry the season backdrop
    // as backdrop_url; it stays first.
    card.insert(QStringLiteral("backdrop_url"), QStringLiteral("https://cdn.test/season-b.webp?sig=f"));
    card.insert(QStringLiteral("backdrop_is_episode_still"), false);
    mapped = SiloModelMapper::mediaItem(card, connectionId);
    QCOMPARE(ref(mapped, QStringLiteral("backdropArtwork")).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("episode:1"));
    QCOMPARE(mapped.value(QStringLiteral("backdropArtworkUrl")).toString(),
             QStringLiteral("https://cdn.test/season-b.webp?sig=f"));
    QCOMPARE(mapped.value(QStringLiteral("artworkThumbhashes")).toMap()
                 .value(QStringLiteral("backdrop")).toString(),
             QStringLiteral("still-hash"));

    // Servers before episode_parent_artwork send none of the fields.
    for (const QString &field : {QStringLiteral("series_poster_url"),
                                 QStringLiteral("season_poster_url"),
                                 QStringLiteral("series_backdrop_url")}) {
        card.remove(field);
    }
    card.insert(QStringLiteral("backdrop_url"), QStringLiteral("https://cdn.test/still-1.webp?sig=b"));
    card.insert(QStringLiteral("backdrop_is_episode_still"), true);
    mapped = SiloModelMapper::mediaItem(card, connectionId);
    QVERIFY(!mapped.contains(QStringLiteral("seriesPrimaryArtwork")));
    QVERIFY(!mapped.contains(QStringLiteral("parentPrimaryArtwork")));
    QCOMPARE(ref(mapped, QStringLiteral("backdropArtwork")).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("episode:1"));
    QCOMPARE(mapped.value(QStringLiteral("artworkExpiresAt")).toMap()
                 .value(QStringLiteral("backdrop")).toString(),
             QStringLiteral("2026-11-01T00:00:00Z"));
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

    // Next Up reads the home layout: video cards from Continue Watching
    // (which folds in Next Up by default) and a separate Next Up section,
    // without Continue Listening cards, other sections, or duplicates.
    const ProviderCatalogResponse nextUp = provider.parseResponse(
        ProviderCatalogOperation::NextUp,
        QByteArrayLiteral(
            R"({"sections":[)"
            R"({"id":"default-continue-watching","section_type":"continue_watching","items":[)"
            R"({"content_id":"movie:heat","type":"movie","title":"Heat","position_seconds":1200,"item_source":"in_progress"},)"
            R"({"content_id":"episode:s1e2","type":"episode","title":"Two","item_source":"next_up"}]},)"
            R"({"id":"default-continue-listening","section_type":"continue_watching","items":[)"
            R"({"content_id":"audiobook:one","type":"audiobook","title":"Book"}]},)"
            R"({"id":"system-next-up","section_type":"next_up","items":[)"
            R"({"content_id":"episode:s1e2","type":"episode","title":"Two"},)"
            R"({"content_id":"episode:x1","type":"episode","title":"Other"}]},)"
            R"({"id":"recently_added","section_type":"recently_added","items":[)"
            R"({"content_id":"movie:new","type":"movie","title":"New"}]}]})"));
    QVERIFY(nextUp.valid);
    QStringList nextUpIds;
    for (const QJsonValue &value : nextUp.rawItems) {
        nextUpIds.append(value.toObject().value(QStringLiteral("content_id")).toString());
    }
    QCOMPARE(nextUpIds, (QStringList{QStringLiteral("movie:heat"),
                                     QStringLiteral("episode:s1e2"),
                                     QStringLiteral("episode:x1")}));

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
        { 200, QByteArrayLiteral(R"([{"id":"1","name":"Movies","type":"movies"}])"), false }
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
        { 200, QByteArrayLiteral(R"([{"id":"1","name":"Old account","type":"movies"}])"), true });
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
        { 200, QByteArrayLiteral(R"([{"id":"2","name":"New account","type":"movies"}])"), true });
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
        { 200, QByteArrayLiteral(R"([{"id":"3","name":"Stale profile","type":"movies"}])"), true });
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
        { 200, QByteArrayLiteral(R"([{"id":"4","name":"Current profile","type":"movies"}])"), true });
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

void SiloCatalogServiceTest::playbackStartRenewsOnlyAgingAccessTokens()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":28800,"user":{"id":"account-1","username":"tester"}})"),
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

    // A fresh eight-hour token outlives a four-hour feature: no renewal.
    const qsizetype before = manager.requests.size();
    bool done = false;
    auth.ensureAccessTokenValidFor(4 * 60 * 60, [&done]() { done = true; });
    QVERIFY(done);
    QCOMPARE(manager.requests.size(), before);

    // Asking for more than half the issued lifetime is capped, so a short-lived
    // deployment is not renewed on every start.
    done = false;
    auth.ensureAccessTokenValidFor(24 * 60 * 60, [&done]() { done = true; });
    QVERIFY(done);
    QCOMPARE(manager.requests.size(), before);
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

void SiloCatalogServiceTest::hierarchyAndRandomFallbacksUseCatalogRoutes()
{
    QTemporaryDir directory;
    ScopedConfigIsolation isolation(directory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    const ResponsePlan seasons{200, QByteArrayLiteral(
        R"({"items":[{"content_id":"season:1","season_number":1,"title":"Season 1","episode_count":2},)"
        R"({"content_id":"season:2","season_number":2,"title":"Season 2","episode_count":1}]})")};
    const ResponsePlan seasonOne{200, QByteArrayLiteral(
        R"({"items":[{"content_id":"ep:1","season_number":1,"episode_number":1,"title":"One","runtime":30,"user_data":{"played":true}},)"
        R"({"content_id":"ep:2","season_number":1,"episode_number":2,"title":"Two","runtime":30,"user_data":{"played":false}}]})")};
    const ResponsePlan seasonTwo{200, QByteArrayLiteral(
        R"({"items":[{"content_id":"ep:3","season_number":2,"episode_number":1,"title":"Three","runtime":30,"user_data":{"played":false}}]})")};
    manager.responses = {
        {200, QByteArrayLiteral(R"({"access_token":"token","refresh_token":"refresh","expires_in":900,"user":{"id":"account-1","username":"tester"}})"), false},
        {200, QByteArrayLiteral(R"({"items":[{"id":"profile-1","name":"tester","has_pin":false}]})"), false},
        seasons, seasonOne, seasonTwo,
        seasons, seasonOne, seasonTwo
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

    // Silo has no series-wide episode route: next-unplayed reads each season.
    QSignalSpy next(&library, &LibraryService::canonicalNextUnplayedEpisodeLoaded);
    const qsizetype firstRequest = manager.requests.size();
    library.getNextUnplayedEpisode(QStringLiteral("series:x"));
    QTRY_COMPARE(next.count(), 1);
    QCOMPARE(manager.requests.at(firstRequest).request.url().path(QUrl::FullyEncoded),
             QStringLiteral("/api/v2/catalog/series/series%3Ax/seasons"));
    QCOMPARE(manager.requests.at(firstRequest + 1).request.url().path(QUrl::FullyEncoded),
             QStringLiteral("/api/v2/catalog/items/season%3A1/episodes"));
    QCOMPARE(manager.requests.at(firstRequest + 2).request.url().path(QUrl::FullyEncoded),
             QStringLiteral("/api/v2/catalog/items/season%3A2/episodes"));
    QVariantMap episode = next.first().at(2).toMap();
    QCOMPARE(episode.value(QStringLiteral("itemId")).toString(), QStringLiteral("ep:2"));
    QCOMPARE(episode.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Episode"));

    // Autoplay excludes the episode that just played.
    library.getNextUnplayedEpisode(QStringLiteral("series:x"), QStringLiteral("ep:2"));
    QTRY_COMPARE(next.count(), 2);
    episode = next.at(1).at(2).toMap();
    QCOMPARE(episode.value(QStringLiteral("itemId")).toString(), QStringLiteral("ep:3"));

    // Untyped season cards map as seasons numbered by season_number.
    const QVariantMap season = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(seasons.payload).object()
            .value(QStringLiteral("items")).toArray().at(1).toObject(),
        QStringLiteral("silo"));
    QCOMPARE(season.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Season"));
    QCOMPARE(season.value(QStringLiteral("indexNumber")).toInt(), 2);

    // Hero items: count the library, then read a shuffled window at a random
    // offset that stays inside the total.
    QJsonArray window;
    for (int index = 0; index < 10; ++index) {
        window.append(QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie:%1").arg(index)},
                                  {QStringLiteral("type"), QStringLiteral("movie")},
                                  {QStringLiteral("title"), QStringLiteral("Movie %1").arg(index)}});
    }
    manager.responses = {
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"movie:0","type":"movie","title":"Movie 0"}],"total":250,"page":{"has_more":true,"next_cursor":"next"}})")},
        {200, QJsonDocument(QJsonObject{{QStringLiteral("items"), window},
                                        {QStringLiteral("total"), 250}})
                  .toJson(QJsonDocument::Compact)}
    };
    QSignalSpy hero(&library, &LibraryService::canonicalHeroLibraryItemsLoaded);
    const qsizetype heroRequest = manager.requests.size();
    library.getHeroLibraryItems(5, {QStringLiteral("lib-1")}, false);
    QTRY_COMPARE(hero.count(), 1);
    QCOMPARE(hero.first().at(1).toList().size(), 5);
    const QJsonObject countBody =
        QJsonDocument::fromJson(manager.requests.at(heroRequest).body).object();
    QCOMPARE(countBody.value(QStringLiteral("limit")).toInt(), 1);
    QCOMPARE(countBody.value(QStringLiteral("library_id")).toString(), QStringLiteral("lib-1"));
    const QJsonObject windowBody =
        QJsonDocument::fromJson(manager.requests.at(heroRequest + 1).body).object();
    QCOMPARE(windowBody.value(QStringLiteral("limit")).toInt(), 100);
    const int seek = windowBody.value(QStringLiteral("seek")).toInt(0);
    QVERIFY(seek >= 0 && seek <= 150);
}

namespace {

// Home layout with every kind of row Bloom must keep or drop.
// GET /api/v2/home/capabilities, with or without the per-row card cap.
QByteArray homeCapabilities(bool withItemLimit, int itemLimitMax = 100)
{
    QJsonObject capabilities{{QStringLiteral("revision"), QStringLiteral("r1")},
                             {QStringLiteral("state"), QStringLiteral("available")},
                             {QStringLiteral("allowed"), true}};
    if (withItemLimit) {
        capabilities.insert(QStringLiteral("section_item_limit_max"), itemLimitMax);
    }
    return QJsonDocument(capabilities).toJson(QJsonDocument::Compact);
}

QByteArray curatedHomeLayout(bool withWatchlistRow)
{
    QByteArray body = QByteArrayLiteral(
        R"({"sections":[)"
        R"({"id":"default-continue-watching","section_type":"continue_watching","title":"Continue Watching","items":[)"
        R"({"content_id":"movie:resume","type":"movie","title":"Resume"}]},)"
        R"({"id":"for-you","section_type":"recommended_for_you","title":"For You","featured":false,"items":[)"
        R"({"content_id":"movie:a","type":"movie","title":"Alpha","user_state":{"played":false,"is_favorite":false,"in_watchlist":true,"not_interested":false}},)"
        R"({"content_id":"series:b","type":"series","title":"Bravo"},)"
        R"({"content_id":"audiobook:c","type":"audiobook","title":"Charlie"}]},)"
        R"({"id":"listening","section_type":"custom_filter","title":"Listen Again","items":[)"
        R"({"content_id":"audiobook:d","type":"audiobook","title":"Delta"},)"
        R"({"content_id":"ebook:e","type":"ebook","title":"Echo"}]},)"
        R"({"id":"default-recently-added-movies","section_type":"recently_added","title":"Recently Added Movies","items":[)"
        R"({"content_id":"movie:new","type":"movie","title":"New"}]},)"
        R"({"id":"empty-genre","section_type":"genre","title":"Westerns","items":[]},)"
        R"({"id":"spotlight","section_type":"editorial_spotlight","title":"Spotlight","featured":true,"items":[)"
        R"({"content_id":"episode:s1e1","type":"episode","title":"Pilot","series_title":"Show","season_number":1,"episode_number":1},)"
        R"({"content_id":"season:s1","type":"season","title":"Season 1"}]},)"
        R"({"id":"system-next-up","section_type":"next_up","title":"Next Up","items":[)"
        R"({"content_id":"episode:next","type":"episode","title":"Next"}]},)"
        R"({"id":"fresh","section_type":"new_to_library","title":"New to the Library","items":[)"
        R"({"content_id":"movie:fresh","type":"movie","title":"Fresh"}]},)"
        R"({"id":"classics","section_type":"collection","title":"Classics","items":[)"
        R"({"content_id":"movie:a","type":"movie","title":"Alpha"},)"
        R"({"content_id":"movie:a","type":"movie","title":"Alpha"}]})");
    if (withWatchlistRow) {
        body += QByteArrayLiteral(
            R"(,{"id":"my-list","section_type":"watchlist","title":"My List","items":[)"
            R"({"content_id":"movie:w","type":"movie","title":"Wanted"}]})");
    }
    body += QByteArrayLiteral("]}");
    return body;
}

QStringList rowIds(const QVariantList &rows)
{
    QStringList ids;
    for (const QVariant &row : rows) {
        ids.append(row.toMap().value(QStringLiteral("sectionId")).toString());
    }
    return ids;
}

QStringList itemIds(const QVariant &row)
{
    QStringList ids;
    for (const QVariant &item : row.toMap().value(QStringLiteral("items")).toList()) {
        ids.append(item.toMap().value(QStringLiteral("itemId")).toString());
    }
    return ids;
}

// Signs in to the scripted Silo server, which has one primary profile.
bool signInToSilo(ControlledNetworkAccessManager &manager,
                  ExposedAuthenticationService &auth,
                  ConfigManager &config)
{
    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})"),
            false },
        { 200,
            QByteArrayLiteral(
                R"({"items":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true}]})"),
            false }
    };
    auth.initialize(&config);
    if (!QTest::qWaitFor([&auth]() { return !auth.isRestoringSession(); }, 1000)) {
        return false;
    }
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"),
                      QStringLiteral("Alice"),
                      QStringLiteral("password"));
    return QTest::qWaitFor([&auth]() { return auth.isAuthenticated(); }, 1000);
}

} // namespace

void SiloCatalogServiceTest::homeSectionsKeepCuratedVideoRowsInLayoutOrder()
{
    SiloCatalogProvider provider;

    ProviderCatalogRequest request
        = provider.createRequest(ProviderCatalogOperation::HomeSections, {});
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/home/sections")));
    ProviderCatalogQuery modified;
    modified.limit = 10;
    request = provider.createRequest(ProviderCatalogOperation::HomeSections, modified);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/home/sections?item_limit=10")));
    modified.startIndex = 1;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::HomeSections, modified).supported);
    modified = {};
    modified.limit = -1;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::HomeSections, modified).supported);

    // The Home capabilities report the largest cap only on servers that
    // accept item_limit.
    request = provider.createRequest(ProviderCatalogOperation::HomeCapabilities, {});
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/home/capabilities"));
    ProviderCatalogResponse capabilities = provider.parseResponse(
        ProviderCatalogOperation::HomeCapabilities,
        QByteArrayLiteral(
            R"({"revision":"r1","state":"available","allowed":true,"section_item_limit_max":100})"));
    QVERIFY(capabilities.valid);
    QCOMPARE(capabilities.capabilityMetadata.value(QStringLiteral("sectionItemLimitMax")).toInt(), 100);
    capabilities = provider.parseResponse(
        ProviderCatalogOperation::HomeCapabilities,
        QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})"));
    QVERIFY(capabilities.valid);
    QVERIFY(!capabilities.capabilityMetadata.contains(QStringLiteral("sectionItemLimitMax")));
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::HomeCapabilities,
                                    QByteArrayLiteral(R"({"sections":[]})")).valid);

    // Rows Bloom already shows (Continue Watching, Next Up, Recently Added
    // and its newest-first twin) are skipped; non-video cards are dropped,
    // and so are rows left empty; cards repeat once per row.
    ProviderCatalogResponse response = provider.parseResponse(
        ProviderCatalogOperation::HomeSections, curatedHomeLayout(false));
    QVERIFY(response.valid);
    QVERIFY(!response.capabilityMetadata.value(QStringLiteral("layoutHasWatchlist")).toBool());
    QStringList ids;
    QList<QStringList> cards;
    for (const QJsonValue &value : response.rawItems) {
        const QJsonObject row = value.toObject();
        ids.append(row.value(QStringLiteral("id")).toString());
        QStringList rowCards;
        for (const QJsonValue &item : row.value(QStringLiteral("items")).toArray()) {
            rowCards.append(item.toObject().value(QStringLiteral("content_id")).toString());
        }
        cards.append(rowCards);
    }
    QCOMPARE(ids, (QStringList{QStringLiteral("for-you"), QStringLiteral("spotlight"),
                               QStringLiteral("classics")}));
    QCOMPARE(cards.at(0), (QStringList{QStringLiteral("movie:a"), QStringLiteral("series:b")}));
    QCOMPARE(cards.at(1), QStringList{QStringLiteral("episode:s1e1")});
    QCOMPARE(cards.at(2), QStringList{QStringLiteral("movie:a")});
    QCOMPARE(response.rawItems.at(0).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("For You"));

    response = provider.parseResponse(
        ProviderCatalogOperation::HomeSections, curatedHomeLayout(true));
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value(QStringLiteral("layoutHasWatchlist")).toBool());
    QCOMPARE(response.rawItems.last().toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("my-list"));

    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::HomeSections,
                                    QByteArrayLiteral(R"({"items":[]})")).valid);
}

void SiloCatalogServiceTest::artworkWidthsOnlyOnRoutesThatDeclareThem()
{
    // Silo answers 422 to query parameters a route does not declare.
    SiloCatalogProvider provider;
    const auto hasWidths = [](const ProviderCatalogRequest &request) {
        const QUrlQuery parameters(QUrl(request.relativeEndpoint).query());
        return parameters.queryItemValue(QStringLiteral("poster_width")) == QStringLiteral("1000")
            && parameters.queryItemValue(QStringLiteral("backdrop_width")) == QStringLiteral("3840")
            && parameters.queryItemValue(QStringLiteral("logo_width")) == QStringLiteral("1280")
            && parameters.queryItemValue(QStringLiteral("still_width")) == QStringLiteral("1280");
    };
    const auto mentionsWidths = [](const ProviderCatalogRequest &request) {
        return request.relativeEndpoint.contains(QStringLiteral("_width"));
    };

    ProviderCatalogQuery query;
    QVERIFY(!mentionsWidths(provider.createRequest(ProviderCatalogOperation::Views, query)));
    QVERIFY(!mentionsWidths(
        provider.createRequest(ProviderCatalogOperation::FilterOptions, query)));
    QVERIFY(!mentionsWidths(
        provider.createRequest(ProviderCatalogOperation::ThemeSongsCapability, query)));
    QVERIFY(!mentionsWidths(
        provider.createRequest(ProviderCatalogOperation::PosterBadgeSettings, query)));
    QVERIFY(!mentionsWidths(
        provider.createRequest(ProviderCatalogOperation::PosterBadgeDefaults, query)));

    query.parentId = QStringLiteral("home");
    QVERIFY(hasWidths(provider.createRequest(ProviderCatalogOperation::Views, query)));
    query.parentId = QStringLiteral("library-1");
    QVERIFY(hasWidths(provider.createRequest(ProviderCatalogOperation::Views, query)));
    query.itemId = QStringLiteral("section-1");
    QVERIFY(hasWidths(provider.createRequest(ProviderCatalogOperation::Items, query)));

    ProviderCatalogQuery item;
    item.itemId = QStringLiteral("movie-1");
    item.limit = 12;
    QVERIFY(hasWidths(provider.createRequest(ProviderCatalogOperation::SimilarItems, item)));
    item.limit = 0;
    QVERIFY(!mentionsWidths(provider.createRequest(ProviderCatalogOperation::ThemeSongs, item)));
    item.themeId = QStringLiteral("theme-1");
    const ProviderCatalogRequest grant =
        provider.createRequest(ProviderCatalogOperation::ThemeSongPlayback, item);
    QCOMPARE(grant.relativeEndpoint,
             QStringLiteral("/api/v2/catalog/items/movie-1/themes/theme-1/playback"));
    item.stateValue = true;
    for (const ProviderCatalogOperation write : {ProviderCatalogOperation::SetWatched,
                                                 ProviderCatalogOperation::SetFavorite,
                                                 ProviderCatalogOperation::SetWatchlist}) {
        QVERIFY(!mentionsWidths(provider.createRequest(write, item)));
    }

    ProviderCatalogQuery search;
    search.searchTerm = QStringLiteral("heat");
    QVERIFY(hasWidths(provider.createRequest(ProviderCatalogOperation::Search, search)));
}

void SiloCatalogServiceTest::watchlistRequestsUseMembershipRoutes()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("movie/heat 1995");
    state.stateValue = true;

    // addToWatchlist and deleteWatchlistEntry are natural_idempotent.
    ProviderCatalogRequest request
        = provider.createRequest(ProviderCatalogOperation::SetWatchlist, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/watchlist/movie%2Fheat%201995"));
    QVERIFY(request.body.isEmpty());
    state.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetWatchlist, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/watchlist/movie%2Fheat%201995"));
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetWatchlist, {}).supported);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetWatchlist, {}).valid);

    ProviderCatalogQuery list;
    list.limit = 20;
    request = provider.createRequest(ProviderCatalogOperation::Watchlist, list);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/watchlist?limit=20")));
    list.startIndex = 20;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::Watchlist, list).supported);

    const ProviderCatalogResponse watchlist = provider.parseResponse(
        ProviderCatalogOperation::Watchlist,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"movie:w","type":"movie","title":"Wanted"},)"
            R"({"content_id":"audiobook:x","type":"audiobook","title":"Book"},)"
            R"({"content_id":"series:y","type":"series","title":"Show"}],"page":{"has_more":false}})"));
    QVERIFY(watchlist.valid);
    QCOMPARE(watchlist.rawItems.size(), 2);
    QCOMPARE(watchlist.rawItems.at(1).toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("series:y"));

    // Item details carry the membership as canonical user state.
    const QVariantMap mapped = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:w","type":"movie","title":"Wanted",)"
            R"("user_state":{"played":false,"is_favorite":false,"in_watchlist":true,"not_interested":false}})"))
            .object(),
        QStringLiteral("connection-1"));
    QVERIFY(mapped.value(QStringLiteral("inWatchlist")).toBool());
    QVERIFY(mapped.value(QStringLiteral("userState")).toMap()
                .value(QStringLiteral("inWatchlist")).toBool());
}

void SiloCatalogServiceTest::homeSectionsServiceAddsWatchlistRowOnlyWithoutOne()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));
    const QString connectionId = config.getActiveConnection().value_or(ServerConnection{}).connectionId;
    QVERIFY(!connectionId.isEmpty());

    LibraryService service(&auth);
    QVERIFY(service.supportsWatchlist());
    QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
    QVERIFY(loaded.isValid());

    // Without a Watchlist row in the layout, Bloom adds one first.
    manager.responses = {
        { 200, homeCapabilities(true), false },
        { 200, curatedHomeLayout(false), false },
        { 200,
            QByteArrayLiteral(
                R"({"items":[{"content_id":"movie:w","type":"movie","title":"Wanted"},)"
                R"({"content_id":"audiobook:x","type":"audiobook","title":"Book"}],"page":{"has_more":false}})"),
            false }
    };
    const qsizetype firstRequest = manager.requests.size();
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(manager.requests.size(), firstRequest + 3);
    QCOMPARE(manager.requests.at(firstRequest).request.url().path(),
             QStringLiteral("/api/v2/home/capabilities"));
    const RecordedRequest layoutRequest = manager.requests.at(firstRequest + 1);
    QCOMPARE(layoutRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(layoutRequest.request.url().path(), QStringLiteral("/api/v2/home/sections"));
    QCOMPARE(QUrlQuery(layoutRequest.request.url()).queryItemValue(QStringLiteral("item_limit")),
             QStringLiteral("20"));
    QCOMPARE(layoutRequest.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    const RecordedRequest watchlistRequest = manager.requests.at(firstRequest + 2);
    QCOMPARE(watchlistRequest.request.url().path(), QStringLiteral("/api/v2/watchlist"));
    QCOMPARE(QUrlQuery(watchlistRequest.request.url()).queryItemValue(QStringLiteral("limit")),
             QStringLiteral("20"));
    QCOMPARE(watchlistRequest.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));

    QCOMPARE(loaded.first().at(0).toString(), connectionId);
    QVariantList rows = loaded.first().at(1).toList();
    QCOMPARE(rowIds(rows), (QStringList{QStringLiteral("bloom:watchlist"), QStringLiteral("for-you"),
                                        QStringLiteral("spotlight"), QStringLiteral("classics")}));
    QCOMPARE(rows.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Watchlist"));
    QCOMPARE(itemIds(rows.at(0)), QStringList{QStringLiteral("movie:w")});
    QCOMPARE(itemIds(rows.at(1)), (QStringList{QStringLiteral("movie:a"), QStringLiteral("series:b")}));
    // Rows hold canonical camelCase items scoped to the connection.
    const QVariantMap alpha = rows.at(1).toMap().value(QStringLiteral("items")).toList().first().toMap();
    QCOMPARE(alpha.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(alpha.value(QStringLiteral("connectionId")).toString(), connectionId);
    QVERIFY(alpha.value(QStringLiteral("inWatchlist")).toBool());
    QVERIFY(!alpha.contains(QStringLiteral("content_id")));
    const QVariantMap pilot = rows.at(2).toMap().value(QStringLiteral("items")).toList().first().toMap();
    QCOMPARE(pilot.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Episode"));
    QCOMPARE(pilot.value(QStringLiteral("seriesName")).toString(), QStringLiteral("Show"));

    // The profile's own Watchlist row stays where the layout puts it, and
    // no second watchlist read is made.
    manager.responses = { { 200, curatedHomeLayout(true), false } };
    const qsizetype secondRequest = manager.requests.size();
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
    QCOMPARE(manager.requests.size(), secondRequest + 1);
    rows = loaded.at(1).at(1).toList();
    QCOMPARE(rowIds(rows), (QStringList{QStringLiteral("for-you"), QStringLiteral("spotlight"),
                                        QStringLiteral("classics"), QStringLiteral("my-list")}));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("My List"));

    // An empty watchlist adds no row, and a failed watchlist read keeps the
    // profile's rows.
    manager.responses = {
        { 200, curatedHomeLayout(false), false },
        { 200, QByteArrayLiteral(R"({"items":[],"page":{"has_more":false}})"), false },
        { 200, curatedHomeLayout(false), false },
        { 404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false }
    };
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 1000);
    QCOMPARE(rowIds(loaded.at(2).at(1).toList()).first(), QStringLiteral("for-you"));
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, 1000);
    QCOMPARE(rowIds(loaded.at(3).at(1).toList()).size(), 3);
}

void SiloCatalogServiceTest::seasonPosterRefreshReadsItsOwnField()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));
    const QString connectionId = config.getActiveConnection().value_or(ServerConnection{}).connectionId;
    QVERIFY(!connectionId.isEmpty());

    const QVariantMap mapped = SiloModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("episode-1")},
            {QStringLiteral("type"), QStringLiteral("episode")},
            {QStringLiteral("series_id"), QStringLiteral("series-1")},
            {QStringLiteral("poster_url"), QStringLiteral("https://silo.example.test/img/still.webp?sig=a")},
            {QStringLiteral("season_poster_url"),
             QStringLiteral("https://silo.example.test/img/season.webp?sig=b")}},
        connectionId);
    const QByteArray episode = QJsonDocument(QJsonObject{
        {QStringLiteral("content_id"), QStringLiteral("episode-1")},
        {QStringLiteral("type"), QStringLiteral("episode")},
        {QStringLiteral("poster_url"), QStringLiteral("https://silo.example.test/img/still.webp?sig=c")},
        {QStringLiteral("season_poster_url"),
         QStringLiteral("https://silo.example.test/img/season.webp?sig=d")}}).toJson();
    const auto refresh = [&](const QString &key) {
        manager.responses = {{200, episode, false}};
        const qsizetype first = manager.requests.size();
        SiloArtworkProvider provider(&auth);
        std::optional<QNetworkRequest> refreshed;
        bool finished = false;
        provider.refreshArtwork(
            Bloom::ArtworkRef::fromVariantMap(mapped.value(key).toMap()),
            [&](std::optional<QNetworkRequest> request) {
                refreshed = std::move(request);
                finished = true;
            });
        if (!QTest::qWaitFor([&finished] { return finished; }, 1000)
            || manager.requests.size() != first + 1
            || manager.requests.at(first).request.url().path()
                != QStringLiteral("/api/v2/catalog/items/episode-1")) {
            return QStringLiteral("unexpected refresh read");
        }
        return refreshed.has_value() ? refreshed->url().toString() : QString();
    };

    // Both are the episode's primary artwork; each refreshes to its own field.
    QCOMPARE(refresh(QStringLiteral("parentPrimaryArtwork")),
             QStringLiteral("https://silo.example.test/img/season.webp?sig=d"));
    QCOMPARE(refresh(QStringLiteral("primaryArtwork")),
             QStringLiteral("https://silo.example.test/img/still.webp?sig=c"));
}

void SiloCatalogServiceTest::homeSectionsCapRowsOnlyWhenTheServerAcceptsIt()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));

    const auto layoutReads = [&manager](qsizetype from) {
        QStringList reads;
        for (qsizetype index = from; index < manager.requests.size(); ++index) {
            const QUrl url = manager.requests.at(index).request.url();
            const QString limit = QUrlQuery(url).queryItemValue(QStringLiteral("item_limit"));
            reads.append(url.path() + (limit.isEmpty() ? QString() : QLatin1Char('=') + limit));
        }
        return reads;
    };
    const QString capabilities = QStringLiteral("/api/v2/home/capabilities");
    const QString layout = QStringLiteral("/api/v2/home/sections");

    // A server without the cap rejects item_limit: the layout is read
    // uncapped, and its capabilities are read once.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        manager.responses = {{200, homeCapabilities(false), false},
                             {200, curatedHomeLayout(true), false},
                             {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout, layout}));
    }

    // A smaller server cap wins over Bloom's row size.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        manager.responses = {{200, homeCapabilities(true, 12), false},
                             {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout + QStringLiteral("=12")}));
    }

    // A failed capability read still loads the rows, uncapped, and the next
    // load asks again.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        manager.responses = {
            {404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false},
            {200, curatedHomeLayout(true), false},
            {200, homeCapabilities(true), false},
            {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout, capabilities,
                                                  layout + QStringLiteral("=20")}));
    }
}

void SiloCatalogServiceTest::homeSectionsServiceReportsFailure()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));

    LibraryService service(&auth);
    QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalHomeSectionsFailed);

    // A newer request supersedes an older one still in flight.
    manager.responses = {
        { 200, homeCapabilities(true), false },
        { 200, curatedHomeLayout(true), true },
        { 404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), true }
    };
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(manager.replies.size(), 4, 1000);
    QPointer<ControlledReply> olderReply = manager.replies.constLast();
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(manager.replies.size(), 5, 1000);
    QPointer<ControlledReply> newerReply = manager.replies.constLast();
    QVERIFY(olderReply && newerReply);
    newerReply->complete();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(0).toString(),
             config.getActiveConnection().value_or(ServerConnection{}).connectionId);
    olderReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(failed.count(), 1);
}

void SiloCatalogServiceTest::watchlistWritesReportSuccessAndFailure()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    QVERIFY(signInToSilo(manager, auth, config));

    LibraryService service(&auth);
    QSignalSpy changed(&service, &LibraryService::watchlistStatusChanged);
    QSignalSpy failed(&service, &LibraryService::watchlistStatusFailed);

    manager.responses = { { 204, QByteArray(), false }, { 204, QByteArray(), false } };
    const qsizetype firstRequest = manager.requests.size();
    service.setItemWatchlisted(QStringLiteral("movie:heat-1995"), true);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 1000);
    QCOMPARE(changed.first().at(0).toString(), QStringLiteral("movie:heat-1995"));
    QCOMPARE(changed.first().at(1).toBool(), true);
    const RecordedRequest add = manager.requests.at(firstRequest);
    QCOMPARE(add.operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(add.request.url().path(), QStringLiteral("/api/v2/watchlist/movie:heat-1995"));
    QCOMPARE(add.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(add.request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-1"));
    QVERIFY(add.body.isEmpty());

    service.setItemWatchlisted(QStringLiteral("movie:heat-1995"), false);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 1000);
    QCOMPARE(changed.at(1).at(1).toBool(), false);
    QCOMPARE(manager.requests.at(firstRequest + 1).operation,
             QNetworkAccessManager::DeleteOperation);

    manager.responses = {
        { 404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false }
    };
    service.setItemWatchlisted(QStringLiteral("movie:gone"), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("movie:gone"));
    QCOMPARE(failed.first().at(1).toBool(), true);
    QVERIFY(!failed.first().at(2).toString().isEmpty());
    QCOMPARE(changed.count(), 2);
    // A 404 is not replayed.
    QCOMPARE(manager.requests.size(), firstRequest + 3);
}

void SiloCatalogServiceTest::jellyfinHasNoWatchlistOrCuratedRows()
{
    SiloProviderAdapter silo;
    QVERIFY(silo.supportsCapability(ProviderCapability::Watchlist));
    QVERIFY(silo.supportsCapability(ProviderCapability::HomeSections));

    JellyfinProviderAdapter jellyfin;
    QVERIFY(!jellyfin.supportsCapability(ProviderCapability::Watchlist));
    QVERIFY(!jellyfin.supportsCapability(ProviderCapability::HomeSections));
    const ICatalogProvider *catalog = jellyfin.catalogProvider();
    QVERIFY(catalog);
    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("item-1");
    state.stateValue = true;
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::SetWatchlist, state).supported);
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::Watchlist, {}).supported);
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::HomeSections, {}).supported);

    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &jellyfin);
    LibraryService service(&auth);
    QVERIFY(!service.supportsWatchlist());
    QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
    service.getHomeSections();
    QCOMPARE(loaded.count(), 1);
    QVERIFY(loaded.first().at(1).toList().isEmpty());
    QVERIFY(manager.requests.isEmpty());
}


QTEST_MAIN(SiloCatalogServiceTest)
#include "SiloCatalogServiceTest.moc"
