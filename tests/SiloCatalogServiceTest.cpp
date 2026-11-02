#include <QCoreApplication>
#include <QEventLoop>
#include <QtTest/QtTest>
#include <QScopeGuard>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <tuple>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "network/MediaSegmentProviderService.h"
#include "network/PlaybackService.h"
#include "models/HeroBackdrops.h"
#include "providers/ICatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloArtworkProvider.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloModelMapper.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloSubtitleAppearanceMapper.h"
#include "utils/ConfigManager.h"
#include "viewmodels/UserRatingState.h"

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
    // Answers by URL path, ahead of the queue and never used up: for reads
    // that run side by side in no fixed order.
    QHash<QString, ResponsePlan> responsesByPath;
    QList<RecordedRequest> requests;
    QList<QPointer<ControlledReply>> replies;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        requests.append(
            {operation, request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const auto byPath = responsesByPath.constFind(request.url().path());
        const ResponsePlan plan = byPath != responsesByPath.cend() ? *byPath
            : responses.isEmpty()
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

    void seedJellyfin()
    {
        seedSession(QStringLiteral("https://jellyfin.example.test"),
                    QStringLiteral("user-1"),
                    QStringLiteral("token-1"),
                    QStringLiteral("Alice"));
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
    void addedSinceUsesFixedRange();
    void libraryTitleQueriesAcceptRecursiveScope();
    void libraryQueryCapabilitiesFollowProvider();
    void responsePreservesSnapshotAndPaginationTruth();
    void nativeResponseShapesPreserveHomeLibrarySearchAndDetailData();
    void sparseNativeMappingsPreserveFallbacksAndState();
    void episodeCardsMapParentArtwork();
    void extendedItemArtworkMapsOnlyWithTheCapability();
    void imageCapabilitiesGateDetailArtworkOncePerProfile();
    void extendedBackdropRefreshFindsItsPosition();
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
    void subtitleAppearanceDocumentStaysInLibraryService();
    void homeDismissalRequestsCarryTheirAnchor();
    void homeDismissalWritesReportSuccessAndFailure();
    void homeCardCapabilitiesFollowTheCardsProvider();
    void homeCardWriteAnswersWhenTheSessionChanges();
    void jellyfinHasNoWatchlistOrCuratedRows();
    void watchHistoryAndWatchTonightRequests();
    void watchHistoryServiceReadsPagesAndRemoves();
    void homeShowsWatchTonightFirstUnlessColdOrOff();
    void jellyfinItemExtrasPutTrailersFirstAndFlagPartialAnswers();
    void siloItemExtrasAnswerEmptyWithoutRequests();
    void personReadsMapCanonicalPeopleAndTitles();
    void collectionReadsMapCollectionsAndStopWithoutPartOf();
    void jellyfinCollectionsBeforeApi12ScanTheLibrary();
    void jellyfinCollectionsFromApi12AreServerScoped();
    void peopleSearchFollowsTheSearchCapability();
    void notInterestedRequestsAndCapabilityParse();
    void notInterestedServiceGatesOnProfileCapability();
    void notInterestedReadCancelledWithSessionIsAskedAgain();
    void becauseWatchedAndRatingRequestsParse();
    void starRatingsNeverReplayAndHideWhenUnavailable();
    void staleRatingWriteFailsAfterSessionChange();
    void becauseWatchedReadsNativeRouteOrJellyfinSimilar();
    void jellyfinLikesWriteAndClear();
    void nextAiringSharesAndCachesOneCalendarRead();
    void nextAiringRetriesUnknownZoneAsOffset();
    void upcomingAiringsReadTheViewerCalendar();
    void upcomingPageReadsThePresetAndFailsOnSessionChange();
    void nextAiringPicksEarliestAiringStillAhead();
    void jellyfinUpcomingProbesThenPagesTheCalendar();
    void markerSegmentsKeepRepeatedRanges();
    void serverRatingsFollowServerOrder();
    void librarySectionsShareTheHomeRowCap();
    void libraryLettersReadTheCapabilityOnce();
    void searchCapabilitiesServePeopleAndLettersOnce();
    void jellyfinHasNoLibraryRowsAndIndexesItsOwnList();
    void collectionShuffleFollowsTheProviderScopeKinds();
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

void SiloCatalogServiceTest::addedSinceUsesFixedRange()
{
    // The local start of the minimum date as a UTC instant, up to a fixed far
    // bound: identical on every page, whatever the clock says.
    const QDate minDate(2026, 7, 1);
    const QJsonArray range = SiloCatalogProvider::addedSinceRange(minDate);
    QCOMPARE(range.size(), 2);
    QCOMPARE(QDateTime::fromString(range.at(0).toString(), Qt::ISODate), minDate.startOfDay());
    QVERIFY(range.at(0).toString().endsWith(QLatin1Char('Z')));
    QCOMPARE(range.at(1).toString(), QStringLiteral("9999-12-31T23:59:59Z"));
    QVERIFY(SiloCatalogProvider::addedSinceRange(QDate()).isEmpty());

    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.parentId = QStringLiteral("17");
    query.minDateLastSaved = minDate;
    const ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QJsonArray groups = body.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groups.size(), 1);
    const QJsonArray rules = groups.first().toObject().value(QStringLiteral("rules")).toArray();
    QCOMPARE(rules.size(), 1);
    const QJsonObject rule = rules.first().toObject();
    QCOMPARE(rule.value(QStringLiteral("field")).toString(), QStringLiteral("added_at"));
    QCOMPARE(rule.value(QStringLiteral("op")).toString(), QStringLiteral("between"));
    QCOMPARE(rule.value(QStringLiteral("value")).toArray(), range);

    // A later page of the same query sends the same rules, so Silo accepts
    // the cursor it issued for the first page.
    query.startIndex = 100;
    const ProviderCatalogRequest later = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY2(later.supported, qPrintable(later.unsupportedReason));
    QCOMPARE(QJsonDocument::fromJson(later.body).object().value(QStringLiteral("groups")),
             body.value(QStringLiteral("groups")));
}

void SiloCatalogServiceTest::libraryTitleQueriesAcceptRecursiveScope()
{
    // The library view asks for a recursive, type-restricted query whenever a
    // sort or filter is active (and always for "All Movies"); Silo's library
    // catalog is already that flat list of titles.
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.parentId = QStringLiteral("17");
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.recursive = true;
    query.sortBy = QStringLiteral("title");
    query.minDateLastSaved = QDate(2026, 7, 1);
    query.genres = {QStringLiteral("Drama")};
    ProviderCatalogRequest request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY2(request.supported, qPrintable(request.unsupportedReason));

    query.includeItemTypes = {QStringLiteral("Series")};
    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY2(request.supported, qPrintable(request.unsupportedReason));

    // Other recursive scopes stay unsupported.
    query.includeItemTypes = {QStringLiteral("Episode")};
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::Items, query).supported);
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.parentId.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::Items, query).supported);
}

void SiloCatalogServiceTest::libraryQueryCapabilitiesFollowProvider()
{
    SiloProviderAdapter silo;
    QVERIFY(silo.catalogProvider());
    const ProviderLibraryQueryCapabilities siloCapabilities =
        silo.catalogProvider()->libraryQueryCapabilities();
    QVERIFY(!siloCapabilities.sorts.contains(QStringLiteral("random")));
    QVERIFY(!siloCapabilities.filters.contains(QStringLiteral("tags")));
    QVERIFY(siloCapabilities.filters.contains(QStringLiteral("addedSince")));

    // Every sort and filter Silo reports is one its catalog request accepts.
    SiloCatalogProvider provider;
    for (const QString &sort : siloCapabilities.sorts) {
        ProviderCatalogQuery query;
        query.sortBy = sort;
        query.sortOrder = QStringLiteral("descending");
        QVERIFY2(provider.createRequest(ProviderCatalogOperation::Items, query).supported,
                 qPrintable(sort));
    }
    ProviderCatalogQuery random;
    random.sortBy = QStringLiteral("random");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::Items, random).supported);

    JellyfinProviderAdapter jellyfin;
    QVERIFY(jellyfin.catalogProvider());
    QVERIFY(jellyfin.catalogProvider()->libraryQueryCapabilities()
            == ProviderLibraryQueryCapabilities{});

    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &silo);
    LibraryService service(&auth);
    QVERIFY(service.libraryQueryCapabilities() == siloCapabilities);
    LibraryService detached(nullptr);
    QVERIFY(detached.libraryQueryCapabilities() == ProviderLibraryQueryCapabilities{});
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
        {QStringLiteral("season_poster_url"), QStringLiteral("https://cdn.test/season-1-poster.webp?sig=d")},
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
             SiloModelMapper::artworkTag(QStringLiteral("https://cdn.test/season-1-poster.webp")));
    QVERIFY(seasonPoster.value(QStringLiteral("tag")).toString()
            != ref(mapped, QStringLiteral("primaryArtwork")).value(QStringLiteral("tag")).toString());

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

const QString kWatchTonightCardsPath = QStringLiteral("/api/v2/recommendations/watch-tonight/cards");

// One Watch Tonight pick, movie:pick.
QByteArray watchTonightPicks()
{
    return QByteArrayLiteral(
        R"({"items":[{"content_id":"movie:pick","type":"movie","title":"Pick","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"}],)"
        R"("has_more":false,"paging_limited":false,"is_cold":false})");
}

// The requests from `from` on, without the Watch Tonight reads, which run
// beside the layout in no fixed order.
QList<RecordedRequest> withoutWatchTonight(const QList<RecordedRequest> &requests, qsizetype from)
{
    QList<RecordedRequest> kept;
    for (qsizetype index = from; index < requests.size(); ++index) {
        if (requests.at(index).request.url().path() != kWatchTonightCardsPath) {
            kept.append(requests.at(index));
        }
    }
    return kept;
}

int watchTonightReads(const QList<RecordedRequest> &requests, qsizetype from)
{
    return static_cast<int>(requests.size() - from - withoutWatchTonight(requests, from).size());
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
    // The production default: the Watch Tonight row is read beside the
    // layout (answered by path), once.
    QVERIFY(config.getHomeWatchTonightEnabled());
    manager.responsesByPath.insert(kWatchTonightCardsPath, {200, watchTonightPicks(), false, {}});
    const QString connectionId = config.getActiveConnection().value_or(ServerConnection{}).connectionId;
    QVERIFY(!connectionId.isEmpty());

    LibraryService service(&auth);
    QVERIFY(service.supportsWatchlist());
    QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
    QSignalSpy updated(&service, &LibraryService::canonicalHomeSectionsUpdated);
    QVERIFY(loaded.isValid());
    // The rows last published, loaded or updated.
    QVariantList shown;
    connect(&service, &LibraryService::canonicalHomeSectionsLoaded, this,
            [&shown](const QString &, const QVariantList &rows) { shown = rows; });
    connect(&service, &LibraryService::canonicalHomeSectionsUpdated, this,
            [&shown](const QString &, const QVariantList &rows) { shown = rows; });

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
    QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty()
                                 && rowIds(shown).first() == LibraryService::WatchTonightRowId,
                             1000);
    QCOMPARE(watchTonightReads(manager.requests, firstRequest), 1);
    const QList<RecordedRequest> homeReads = withoutWatchTonight(manager.requests, firstRequest);
    QCOMPARE(homeReads.size(), 3);
    QCOMPARE(homeReads.at(0).request.url().path(), QStringLiteral("/api/v2/home/capabilities"));
    const RecordedRequest layoutRequest = homeReads.at(1);
    QCOMPARE(layoutRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(layoutRequest.request.url().path(), QStringLiteral("/api/v2/home/sections"));
    QCOMPARE(QUrlQuery(layoutRequest.request.url()).queryItemValue(QStringLiteral("item_limit")),
             QStringLiteral("20"));
    QCOMPARE(layoutRequest.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    const RecordedRequest watchlistRequest = homeReads.at(2);
    QCOMPARE(watchlistRequest.request.url().path(), QStringLiteral("/api/v2/watchlist"));
    QCOMPARE(QUrlQuery(watchlistRequest.request.url()).queryItemValue(QStringLiteral("limit")),
             QStringLiteral("20"));
    QCOMPARE(watchlistRequest.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));

    QCOMPARE(loaded.first().at(0).toString(), connectionId);
    // Watch Tonight first, then Bloom's Watchlist row and the profile's.
    QVariantList rows = shown;
    QCOMPARE(rowIds(rows), (QStringList{LibraryService::WatchTonightRowId,
                                        QStringLiteral("bloom:watchlist"), QStringLiteral("for-you"),
                                        QStringLiteral("spotlight"), QStringLiteral("classics")}));
    QCOMPARE(itemIds(rows.at(0)), QStringList{QStringLiteral("movie:pick")});
    rows.removeFirst();
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
    // no second watchlist read is made. A refresh that is not a new Home
    // (a favorite or watchlist change) reuses the Watch Tonight row.
    manager.responses = { { 200, curatedHomeLayout(true), false } };
    const qsizetype secondRequest = manager.requests.size();
    const qsizetype updatesBefore = updated.count();
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
    QCOMPARE(manager.requests.size(), secondRequest + 1);
    rows = loaded.at(1).at(1).toList();
    QCOMPARE(rowIds(rows), (QStringList{LibraryService::WatchTonightRowId, QStringLiteral("for-you"),
                                        QStringLiteral("spotlight"), QStringLiteral("classics"),
                                        QStringLiteral("my-list")}));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("My List"));
    QCOMPARE(updated.count(), updatesBefore);

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
    QCOMPARE(rowIds(loaded.at(2).at(1).toList()).at(1), QStringLiteral("for-you"));
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, 1000);
    QCOMPARE(rowIds(loaded.at(3).at(1).toList()).size(), 4);
    QCOMPARE(watchTonightReads(manager.requests, secondRequest), 0);
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
    // The production default: each service reads the Watch Tonight row
    // (answered by path) once, beside the layout reads below.
    QVERIFY(config.getHomeWatchTonightEnabled());
    manager.responsesByPath.insert(kWatchTonightCardsPath, {200, watchTonightPicks(), false, {}});

    const auto layoutReads = [&manager](qsizetype from) {
        QStringList reads;
        for (const RecordedRequest &read : withoutWatchTonight(manager.requests, from)) {
            const QUrl url = read.request.url();
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
            {403, QByteArrayLiteral(R"({"type":"about:blank","title":"Forbidden","status":403})"), false},
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

    // A server without the capabilities route has no cap; that is remembered.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        manager.responses = {
            {404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false},
            {200, curatedHomeLayout(true), false},
            {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout, layout}));
    }

    // A remembered cap the server now rejects is dropped, and the rows are
    // read once more without it.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        QSignalSpy failed(&service, &LibraryService::canonicalHomeSectionsFailed);
        manager.responses = {
            {200, homeCapabilities(true), false},
            {400, QByteArrayLiteral(R"({"type":"about:blank","title":"Bad Request","status":400})"), false},
            {200, curatedHomeLayout(true), false},
            {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QCOMPARE(failed.count(), 0);
        // The retried layout still comes with the Watch Tonight row.
        QCOMPARE(rowIds(loaded.first().at(1).toList()).first(), LibraryService::WatchTonightRowId);
        QCOMPARE(rowIds(loaded.first().at(1).toList()).last(), QStringLiteral("my-list"));
        service.getHomeSections();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout + QStringLiteral("=20"),
                                                  layout, layout}));
        QCOMPARE(watchTonightReads(manager.requests, first), 1);
    }

    // A 422 is answered the same way, and a new Home reads Watch Tonight
    // again.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
        QSignalSpy failed(&service, &LibraryService::canonicalHomeSectionsFailed);
        manager.responses = {
            {200, homeCapabilities(true), false},
            {422, QByteArrayLiteral(R"({"type":"about:blank","title":"Unprocessable","status":422})"), false},
            {200, curatedHomeLayout(true), false},
            {200, curatedHomeLayout(true), false}};
        const qsizetype first = manager.requests.size();
        service.getHomeSections({}, true);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QCOMPARE(failed.count(), 0);
        service.getHomeSections({}, true);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(layoutReads(first), (QStringList{capabilities, layout + QStringLiteral("=20"),
                                                  layout, layout}));
        QCOMPARE(watchTonightReads(manager.requests, first), 2);
        QCOMPARE(rowIds(loaded.last().at(1).toList()).first(), LibraryService::WatchTonightRowId);
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
    // These reads answer in queue order; the Watch Tonight read beside
    // the layout has its own test.
    config.setHomeWatchTonightEnabled(false);

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

// LibraryService keeps Silo's subtitle appearance document per scope: a
// write sends the local appearance over the last read's document, nothing
// outside LibraryService sees it, and a request whose session changes is
// still answered.
void SiloCatalogServiceTest::subtitleAppearanceDocumentStaysInLibraryService()
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
    const QString scope = service.subtitleAppearanceScope();
    QVERIFY(!scope.isEmpty());
    QVERIFY(!service.hasSubtitleAppearanceDocument(scope));

    const QJsonObject serverDocument{
        {QStringLiteral("fontSize"), QStringLiteral("large")},
        {QStringLiteral("textOpacity"), 60},
        {QStringLiteral("futureField"), QStringLiteral("kept")},
    };
    const auto effective = [](const QJsonObject &document) {
        return QJsonDocument(QJsonObject{
            {QStringLiteral("key"), QStringLiteral("subtitle_appearance")},
            {QStringLiteral("profile_id"), QStringLiteral("profile-1")},
            {QStringLiteral("effective_value"),
             QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact))},
            {QStringLiteral("has_device_override"), false},
        }).toJson(QJsonDocument::Compact);
    };

    manager.responses = {{200, effective(serverDocument), false}};
    std::optional<QVariantMap> read;
    service.getSubtitleAppearance([&read](bool ok, const QVariantMap &result) {
        QVERIFY(ok);
        read = result;
    });
    QTRY_VERIFY_WITH_TIMEOUT(read.has_value(), 1000);
    QVERIFY(read->value(QStringLiteral("hasValue")).toBool());
    QVERIFY(!read->contains(QStringLiteral("document")));
    QVERIFY(service.hasSubtitleAppearanceDocument(scope));

    manager.responses = {{200, effective(serverDocument), false}};
    const qsizetype writeIndex = manager.requests.size();
    bool written = false;
    service.setSubtitleAppearance({{QStringLiteral("sizePercent"), 150}},
                                  [&written](bool ok, const QVariantMap &result) {
        written = ok && !result.contains(QStringLiteral("document"));
    });
    QTRY_VERIFY_WITH_TIMEOUT(written, 1000);
    const RecordedRequest put = manager.requests.at(writeIndex);
    QCOMPARE(put.operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(put.request.url().path(), QStringLiteral("/api/v2/settings/device/subtitle-appearance"));
    const QJsonObject value = SiloSubtitleAppearanceMapper::parseDocument(
        QJsonDocument::fromJson(put.body).object().value(QStringLiteral("value")).toString());
    QCOMPARE(value.value(QStringLiteral("fontSize")).toString(), QStringLiteral("xxlarge"));
    QCOMPARE(value.value(QStringLiteral("textOpacity")).toInt(), 60);
    QCOMPARE(value.value(QStringLiteral("futureField")).toString(), QStringLiteral("kept"));

    // Removing the override drops the document; the next read brings the
    // profile-wide one.
    manager.responses = {{204, QByteArray(), false}};
    bool removed = false;
    service.resetSubtitleAppearance([&removed](bool ok, const QVariantMap &) { removed = ok; });
    QTRY_VERIFY_WITH_TIMEOUT(removed, 1000);
    QCOMPARE(manager.requests.constLast().operation, QNetworkAccessManager::DeleteOperation);
    QVERIFY(!service.hasSubtitleAppearanceDocument(scope));

    // A session change answers a request still out as stale.
    manager.responses = {{200, effective(serverDocument), true}};
    std::optional<QVariantMap> stale;
    service.getSubtitleAppearance([&stale](bool ok, const QVariantMap &result) {
        QVERIFY(!ok);
        stale = result;
    });
    QTRY_VERIFY_WITH_TIMEOUT(!manager.replies.isEmpty() && manager.replies.constLast(), 1000);
    auth.replaceAccount(QStringLiteral("account-2"), QStringLiteral("access-2"));
    QTRY_VERIFY_WITH_TIMEOUT(stale.has_value(), 1000);
    QVERIFY(stale->value(QStringLiteral("stale")).toBool());
}

void SiloCatalogServiceTest::homeDismissalRequestsCarryTheirAnchor()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.itemId = QStringLiteral("episode:severance/1/2");
    query.homeSurface = QStringLiteral("continue_watching");
    query.progressUpdatedAt = QStringLiteral("2026-10-30T20:15:00.000Z");
    query.seriesId = QStringLiteral("series:severance");
    query.stateValue = true;

    // dismissHomeItem: PUT with the card's progress stamp only. The route is
    // natural_idempotent, so a lost response may be replayed.
    ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/home/dismissals/continue_watching/episode%3Aseverance%2F1%2F2"));
    QCOMPARE(QJsonDocument::fromJson(request.body).object(),
             QJsonObject({{QStringLiteral("progress_updated_at"),
                           QStringLiteral("2026-10-30T20:15:00.000Z")}}));

    // Next Up anchors on the series.
    query.homeSurface = QStringLiteral("next_up");
    request = provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/home/dismissals/next_up/episode%3Aseverance%2F1%2F2"));
    QCOMPARE(QJsonDocument::fromJson(request.body).object(),
             QJsonObject({{QStringLiteral("series_id"), QStringLiteral("series:severance")}}));

    // undismissHomeItem: DELETE without a body, also natural_idempotent.
    query.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QVERIFY(request.body.isEmpty());

    // A dismissal without its anchor, surface or item is not sent.
    query.stateValue = true;
    query.seriesId.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query).supported);
    query.homeSurface = QStringLiteral("continue_watching");
    query.progressUpdatedAt.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query).supported);
    query.progressUpdatedAt = QStringLiteral("2026-10-30T20:15:00.000Z");
    query.homeSurface = QStringLiteral("recently_added");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query).supported);
    query.homeSurface = QStringLiteral("continue_watching");
    query.itemId.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetHomeDismissal, query).supported);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetHomeDismissal, {}).valid);
    // Silo hides cards with dismissals; it never clears a resume position.
    query.itemId = QStringLiteral("movie:heat-1995");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::ClearResumePosition, query).supported);

    // Cards carry their progress stamp verbatim for the dismissal body.
    const QVariantMap card = SiloModelMapper::mediaItem(
        QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie:heat-1995")},
                    {QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("title"), QStringLiteral("Heat")},
                    {QStringLiteral("position_seconds"), 600},
                    {QStringLiteral("progress_updated_at"),
                     QStringLiteral("2026-10-30T20:15:00.000Z")}},
        QStringLiteral("silo"));
    QCOMPARE(card.value(QStringLiteral("progressUpdatedAt")).toString(),
             QStringLiteral("2026-10-30T20:15:00.000Z"));
    const QVariantMap unstamped = SiloModelMapper::mediaItem(
        QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie:heat-1995")},
                    {QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("title"), QStringLiteral("Heat")}},
        QStringLiteral("silo"));
    QVERIFY(!unstamped.contains(QStringLiteral("progressUpdatedAt")));
}

void SiloCatalogServiceTest::homeDismissalWritesReportSuccessAndFailure()
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
    QSignalSpy finished(&service, &LibraryService::homeCardWriteFinished);

    manager.responses = { { 204, QByteArray(), false }, { 204, QByteArray(), false } };
    const qsizetype firstRequest = manager.requests.size();
    service.setHomeItemDismissed(QStringLiteral("key-1"), QStringLiteral("continue_watching"),
                                 QStringLiteral("movie:heat-1995"), QString(),
                                 QStringLiteral("2026-10-30T20:15:00.000Z"), true);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("key-1"));
    QVERIFY(finished.first().at(2).toBool());
    const RecordedRequest dismiss = manager.requests.at(firstRequest);
    QCOMPARE(dismiss.operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(dismiss.request.url().path(),
             QStringLiteral("/api/v2/home/dismissals/continue_watching/movie:heat-1995"));
    QCOMPARE(dismiss.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(dismiss.request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(QJsonDocument::fromJson(dismiss.body).object()
                 .value(QStringLiteral("progress_updated_at")).toString(),
             QStringLiteral("2026-10-30T20:15:00.000Z"));

    service.setHomeItemDismissed(QStringLiteral("key-2"), QStringLiteral("continue_watching"),
                                 QStringLiteral("movie:heat-1995"), QString(), QString(), false);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 1000);
    QCOMPARE(finished.at(1).at(1).toString(), QStringLiteral("key-2"));
    QCOMPARE(manager.requests.at(firstRequest + 1).operation,
             QNetworkAccessManager::DeleteOperation);

    manager.responses = {
        { 404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false }
    };
    service.setHomeItemDismissed(QStringLiteral("key-3"), QStringLiteral("next_up"),
                                 QStringLiteral("episode:gone"), QStringLiteral("series:gone"),
                                 QString(), true);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 1000);
    QCOMPARE(finished.at(2).at(1).toString(), QStringLiteral("key-3"));
    QVERIFY(!finished.at(2).at(2).toBool());
    QVERIFY(!finished.at(2).at(3).toString().isEmpty());
    // A refusal is not replayed.
    QCOMPARE(manager.requests.size(), firstRequest + 3);

    // A dismissal the provider cannot build answers at once, unsent.
    service.setHomeItemDismissed(QStringLiteral("key-4"), QStringLiteral("next_up"),
                                 QStringLiteral("episode:x"), QString(), QString(), true);
    QCOMPARE(finished.count(), 4);
    QVERIFY(!finished.at(3).at(2).toBool());
    QCOMPARE(manager.requests.size(), firstRequest + 3);
}

void SiloCatalogServiceTest::homeCardWriteAnswersWhenTheSessionChanges()
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
    QSignalSpy finished(&service, &LibraryService::homeCardWriteFinished);
    QSignalSpy unconfirmed(&service, &LibraryService::homeCardWriteUnconfirmed);
    QStringList order;
    connect(&service, &LibraryService::homeCardWriteUnconfirmed, this,
            [&order]() { order.append(QStringLiteral("unconfirmed")); });
    connect(&service, &LibraryService::homeCardWriteFinished, this,
            [&order]() { order.append(QStringLiteral("finished")); });
    const QString connectionId = auth.activeConnectionId();

    // The write never answers before the account changes.
    manager.responses = { { 204, QByteArray(), true } };
    const qsizetype firstRequest = manager.requests.size();
    service.setHomeItemState(QStringLiteral("key-1"), QStringLiteral("favorite"),
                             QStringLiteral("movie:heat-1995"), true);
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), firstRequest + 1, 1000);
    QPointer<ControlledReply> staleReply = manager.replies.constLast();
    QVERIFY(staleReply);
    QCOMPARE(finished.count(), 0);

    auth.replaceAccount(QStringLiteral("account-2"), QStringLiteral("access-2"));
    // Answered once, as a failure, for the connection that sent it.
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toString(), connectionId);
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("key-1"));
    QVERIFY(!finished.first().at(2).toBool());
    QVERIFY(!finished.first().at(3).toString().isEmpty());
    // It may have landed: said first, for callers that cannot just undo.
    QCOMPARE(unconfirmed.count(), 1);
    QCOMPARE(unconfirmed.first().at(0).toString(), connectionId);
    QCOMPARE(unconfirmed.first().at(1).toString(), QStringLiteral("key-1"));
    QCOMPARE(order, (QStringList{QStringLiteral("unconfirmed"), QStringLiteral("finished")}));

    // A reply that still lands changes nothing.
    staleReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(finished.count(), 1);
}

void SiloCatalogServiceTest::homeCardCapabilitiesFollowTheCardsProvider()
{
    SiloProviderAdapter silo;
    JellyfinProviderAdapter jellyfin;
    QVERIFY(silo.supportsCapability(ProviderCapability::HomeDismissals));
    QVERIFY(!silo.supportsCapability(ProviderCapability::ResumeItems));
    QVERIFY(!jellyfin.supportsCapability(ProviderCapability::HomeDismissals));
    QVERIFY(jellyfin.supportsCapability(ProviderCapability::ResumeItems));

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &silo);
    QVERIFY(signInToSilo(manager, auth, config));
    LibraryService service(&auth);
    const HomeCardCapabilities live = service.homeCardCapabilities(auth.activeConnectionId());
    QVERIFY(live.dismissals);
    QVERIFY(!live.resumeReset);
    QVERIFY(live.watchlist);
    const HomeCardCapabilities unknown = service.homeCardCapabilities(QStringLiteral("nowhere"));
    QVERIFY(!unknown.dismissals && !unknown.resumeReset && !unknown.watchlist);
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

// LibraryService::getItemExtras reads LocalTrailers and SpecialFeatures,
// puts trailers first whatever order the answers arrive in, drops an extra
// both list, and flags an answer with one failed read as incomplete.
void SiloCatalogServiceTest::jellyfinItemExtrasPutTrailersFirstAndFlagPartialAnswers()
{
    JellyfinProviderAdapter jellyfin;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &jellyfin);
    auth.replaceAccount(QStringLiteral("user-1"), QStringLiteral("token-1"));
    LibraryService service(&auth);
    QSignalSpy loaded(&service, &LibraryService::canonicalItemExtrasLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalItemExtrasFailed);

    const QByteArray trailers = QJsonDocument(QJsonArray{
        QJsonObject{{QStringLiteral("Id"), QStringLiteral("trailer-1")},
                    {QStringLiteral("Name"), QStringLiteral("Trailer")},
                    {QStringLiteral("Type"), QStringLiteral("Trailer")}}}).toJson();
    const QByteArray features = QJsonDocument(QJsonArray{
        QJsonObject{{QStringLiteral("Id"), QStringLiteral("featurette-1")},
                    {QStringLiteral("Name"), QStringLiteral("Making Of")},
                    {QStringLiteral("ExtraType"), QStringLiteral("Featurette")}},
        // Jellyfin lists a local trailer among the special features too.
        QJsonObject{{QStringLiteral("Id"), QStringLiteral("trailer-1")},
                    {QStringLiteral("Name"), QStringLiteral("Trailer")},
                    {QStringLiteral("ExtraType"), QStringLiteral("Trailer")}}}).toJson();
    const QByteArray notFound =
        QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})");

    const auto extraIds = [](const QVariantList &extras) {
        QStringList ids;
        for (const QVariant &extra : extras) {
            ids.append(extra.toMap().value(QStringLiteral("itemId")).toString());
        }
        return ids;
    };

    // Both reads succeed; special features answer first.
    manager.responses = {{200, trailers, true}, {200, features, true}};
    service.getItemExtras(QStringLiteral("movie-1"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.replies.size(), 2, 1000);
    QVERIFY(manager.requests.at(0).request.url().path().endsWith(
        QStringLiteral("/Items/movie-1/LocalTrailers")));
    QVERIFY(manager.requests.at(1).request.url().path().endsWith(
        QStringLiteral("/Items/movie-1/SpecialFeatures")));
    manager.replies.at(1)->complete();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(loaded.count(), 0);
    manager.replies.at(0)->complete();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("movie-1"));
    QCOMPARE(extraIds(loaded.first().at(2).toList()),
             (QStringList{QStringLiteral("trailer-1"), QStringLiteral("featurette-1")}));
    QVERIFY(loaded.first().at(3).toBool());

    // The trailer read fails: what arrived is published, flagged incomplete.
    loaded.clear();
    manager.responses = {{404, notFound, false}, {200, features, false}};
    service.getItemExtras(QStringLiteral("movie-1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(extraIds(loaded.first().at(2).toList()),
             (QStringList{QStringLiteral("featurette-1"), QStringLiteral("trailer-1")}));
    QVERIFY(!loaded.first().at(3).toBool());
    QCOMPARE(failed.count(), 0);

    // Both reads fail: a failure, not an empty list.
    loaded.clear();
    manager.responses = {{404, notFound, false}, {404, notFound, false}};
    service.getItemExtras(QStringLiteral("movie-1"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("movie-1"));
    QCOMPARE(loaded.count(), 0);
}

// Silo details carry their extras, so getItemExtras answers an empty,
// complete list without a request.
void SiloCatalogServiceTest::siloItemExtrasAnswerEmptyWithoutRequests()
{
    SiloProviderAdapter silo;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &silo);
    auth.replaceAccount(QStringLiteral("account-1"), QStringLiteral("token-1"));
    LibraryService service(&auth);
    QSignalSpy loaded(&service, &LibraryService::canonicalItemExtrasLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalItemExtrasFailed);

    service.getItemExtras(QStringLiteral("movie-1"));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("movie-1"));
    QVERIFY(loaded.first().at(2).toList().isEmpty());
    QVERIFY(loaded.first().at(3).toBool());
    QCOMPARE(failed.count(), 0);
    QVERIFY(manager.requests.isEmpty());
}

void SiloCatalogServiceTest::personReadsMapCanonicalPeopleAndTitles()
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
    const QString connectionId = LibraryService(&auth).getActiveConnectionId();
    QVERIFY(!connectionId.isEmpty());

    LibraryService service(&auth);
    QSignalSpy personLoaded(&service, &LibraryService::canonicalPersonLoaded);
    QSignalSpy itemsLoaded(&service, &LibraryService::canonicalPersonItemsLoaded);
    manager.responses = {
        {200, QByteArrayLiteral("{\"id\":\"7\",\"name\":\"Al Pacino\",\"bio\":\"Actor.\",\"birth_date\":\"1940-04-25\",\"birthplace\":\"New York City\",\"photo_url\":\"https://cdn.example.test/people/7.jpg?sig=a\",\"imdb_id\":\"nm0000199\",\"tmdb_id\":\"1158\"}"), false},
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"m1","type":"movie","title":"Heat","year":1995},{"content_id":"s1","type":"series","title":"Angels"}],"total":3,"page":{"has_more":true,"next_cursor":"c2"}})"), false}};
    const qsizetype first = manager.requests.size();
    service.getPerson(QStringLiteral("7"));
    service.getPersonItems(QStringLiteral("7"), 0, QString(), 60, QStringLiteral("page-1"));
    QTRY_COMPARE_WITH_TIMEOUT(personLoaded.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(itemsLoaded.count(), 1, 1000);

    QCOMPARE(manager.requests.at(first).request.url().path(),
             QStringLiteral("/api/v2/catalog/people/7"));
    const QUrl itemsUrl = manager.requests.at(first + 1).request.url();
    QCOMPARE(itemsUrl.path(), QStringLiteral("/api/v2/catalog"));
    const QUrlQuery itemsQuery(itemsUrl);
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("source")), QStringLiteral("person"));
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("person_id")), QStringLiteral("7"));
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("type")), QStringLiteral("video"));

    const QVariantList personArgs = personLoaded.first();
    QCOMPARE(personArgs.at(0).toString(), connectionId);
    QCOMPARE(personArgs.at(1).toString(), QStringLiteral("7"));
    const QVariantMap person = personArgs.at(2).toMap();
    QCOMPARE(person.value(QStringLiteral("name")).toString(), QStringLiteral("Al Pacino"));
    QCOMPARE(person.value(QStringLiteral("connectionId")).toString(), connectionId);
    QCOMPARE(person.value(QStringLiteral("birthDate")).toString(), QStringLiteral("1940-04-25"));
    QVERIFY(person.value(QStringLiteral("artwork")).toMap().value(QStringLiteral("cacheKey"))
                .toString().startsWith(QStringLiteral("artwork:")));

    const QVariantList itemArgs = itemsLoaded.first();
    QCOMPARE(itemArgs.at(2).toString(), QStringLiteral("page-1"));
    const QVariantList items = itemArgs.at(3).toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("mediaType")).toString(), QStringLiteral("Series"));
    QCOMPARE(itemArgs.at(5).toBool(), true);
    QCOMPARE(itemArgs.at(6).toString(), QStringLiteral("c2"));

    // The next page continues from the cursor.
    manager.responses = {{200, QByteArrayLiteral(R"({"items":[],"total":3,"page":{"has_more":false}})"), false}};
    service.getPersonItems(QStringLiteral("7"), 2, QStringLiteral("c2"), 60, QStringLiteral("page-2"));
    QTRY_COMPARE_WITH_TIMEOUT(itemsLoaded.count(), 2, 1000);
    QCOMPARE(QUrlQuery(manager.requests.last().request.url()).queryItemValue(QStringLiteral("cursor")),
             QStringLiteral("c2"));
    QCOMPARE(itemsLoaded.last().at(5).toBool(), false);

    // A person without a name cannot be shown.
    QSignalSpy personFailed(&service, &LibraryService::canonicalPersonFailed);
    manager.responses = {{200, QByteArrayLiteral(R"({"id":"8"})"), false}};
    service.getPerson(QStringLiteral("8"));
    QTRY_COMPARE_WITH_TIMEOUT(personFailed.count(), 1, 1000);
    QCOMPARE(personFailed.first().at(1).toString(), QStringLiteral("8"));
}

// LibraryService's collection reads map the library tab, page a
// collection's titles, and stop asking for "Part of" once the server
// rejects the read.
void SiloCatalogServiceTest::collectionReadsMapCollectionsAndStopWithoutPartOf()
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
    QVERIFY(service.supportsCollections());
    const QString connectionId = service.getActiveConnectionId();
    QVERIFY(!connectionId.isEmpty());
    QSignalSpy libraryLoaded(&service, &LibraryService::canonicalLibraryCollectionsLoaded);
    QSignalSpy itemsLoaded(&service, &LibraryService::canonicalCollectionItemsLoaded);
    QSignalSpy partOfLoaded(&service, &LibraryService::canonicalItemCollectionsLoaded);
    QSignalSpy partOfFailed(&service, &LibraryService::canonicalItemCollectionsFailed);

    manager.responses = {{200, QByteArrayLiteral(
        R"({"library_id":"lib-1","groups":[],"collections":[)"
        R"({"id":"c1","title":"Alien","description":"Six films.","poster_url":"https://cdn.example.test/c1.webp?sig=a",)"
        R"("backdrop_url":"","item_count":6}]})"), false}};
    service.getLibraryCollections(QStringLiteral("lib-1"), 0, 100, QStringLiteral("library-key"));
    QTRY_COMPARE_WITH_TIMEOUT(libraryLoaded.count(), 1, 1000);
    QCOMPARE(manager.requests.last().request.url().path(), QStringLiteral("/api/v2/library/lib-1/collections"));
    QCOMPARE(libraryLoaded.first().at(0).toString(), connectionId);
    QCOMPARE(libraryLoaded.first().at(2).toString(), QStringLiteral("library-key"));
    const QVariantList collections = libraryLoaded.first().at(3).toList();
    QCOMPARE(collections.size(), 1);
    const QVariantMap collection = collections.first().toMap();
    QCOMPARE(collection.value(QStringLiteral("collectionKind")).toString(), QStringLiteral("library"));
    QCOMPARE(collection.value(QStringLiteral("connectionId")).toString(), connectionId);
    QVERIFY(collection.value(QStringLiteral("primaryArtwork")).toMap()
                .value(QStringLiteral("cacheKey")).toString().startsWith(QStringLiteral("artwork:")));
    QCOMPARE(libraryLoaded.first().at(5).toBool(), false);
    QCOMPARE(libraryLoaded.first().at(6).toBool(), false);

    manager.responses = {{200, QByteArrayLiteral(
        R"({"items":[{"content_id":"m1","type":"movie","title":"Alien"},{"content_id":"e1","type":"episode","title":"Pilot"}],)"
        R"("total":6,"page":{"has_more":true,"next_cursor":"n2"}})"), false}};
    service.getCollectionItems(QStringLiteral("c1"), QStringLiteral("library"), QStringLiteral("releaseDate"),
                               0, QString(), 100, QStringLiteral("items-key"));
    QTRY_COMPARE_WITH_TIMEOUT(itemsLoaded.count(), 1, 1000);
    const QUrlQuery itemsQuery(manager.requests.last().request.url());
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("source")), QStringLiteral("library_collection"));
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("collection_id")), QStringLiteral("c1"));
    QCOMPARE(itemsQuery.queryItemValue(QStringLiteral("sort")), QStringLiteral("release_date"));
    QCOMPARE(itemsLoaded.first().at(1).toString(), QStringLiteral("c1"));
    QCOMPARE(itemsLoaded.first().at(3).toList().size(), 1);
    QCOMPARE(itemsLoaded.first().at(5).toBool(), true);
    QCOMPARE(itemsLoaded.first().at(6).toString(), QStringLiteral("n2"));

    // "Part of": the profile's own collections that hold the title.
    manager.responses = {{200, QByteArrayLiteral(
        R"({"groups":[],"items":[{"id":"u1","name":"Rainy days","poster_url":"","item_count":3,"contains":true}]})"), false}};
    service.getItemCollections(QStringLiteral("m1"), QStringLiteral("part-1"));
    QTRY_COMPARE_WITH_TIMEOUT(partOfLoaded.count(), 1, 1000);
    QCOMPARE(QUrlQuery(manager.requests.last().request.url()).queryItemValue(QStringLiteral("contains_item")),
             QStringLiteral("m1"));
    QCOMPARE(partOfLoaded.first().at(3).toList().size(), 1);

    // A server error is a failure, and the read is asked again next time.
    manager.responses = {{500, QByteArrayLiteral(R"({"title":"boom","status":500})"), false},
                         {500, QByteArrayLiteral(R"({"title":"boom","status":500})"), false},
                         {500, QByteArrayLiteral(R"({"title":"boom","status":500})"), false}};
    service.getItemCollections(QStringLiteral("m2"), QStringLiteral("part-2"));
    QTRY_COMPARE_WITH_TIMEOUT(partOfFailed.count(), 1, 10000);
    QCOMPARE(partOfFailed.first().at(2).toString(), QStringLiteral("part-2"));

    // A 404 is about that title: an empty answer, and the next title is
    // still asked.
    manager.responses = {{404, QByteArrayLiteral(R"({"title":"Not Found","status":404})"), false}};
    service.getItemCollections(QStringLiteral("gone"), QStringLiteral("part-404"));
    QTRY_COMPARE_WITH_TIMEOUT(partOfLoaded.count(), 2, 1000);
    QCOMPARE(partOfLoaded.last().at(2).toString(), QStringLiteral("part-404"));
    QVERIFY(partOfLoaded.last().at(3).toList().isEmpty());
    QCOMPARE(partOfFailed.count(), 1);

    // A server without contains_item rejects it: an empty answer, then no
    // more reads for this connection.
    manager.responses = {{422, QByteArrayLiteral(R"({"title":"Unprocessable","status":422})"), false}};
    service.getItemCollections(QStringLiteral("m3"), QStringLiteral("part-3"));
    QTRY_COMPARE_WITH_TIMEOUT(partOfLoaded.count(), 3, 1000);
    QVERIFY(partOfLoaded.last().at(3).toList().isEmpty());
    QCOMPARE(partOfFailed.count(), 1);
    const qsizetype sent = manager.requests.size();
    service.getItemCollections(QStringLiteral("m4"), QStringLiteral("part-4"));
    QCOMPARE(partOfLoaded.count(), 4);
    QCOMPARE(partOfLoaded.last().at(2).toString(), QStringLiteral("part-4"));
    QVERIFY(partOfLoaded.last().at(3).toList().isEmpty());
    QCOMPARE(manager.requests.size(), sent);
}

namespace {

// A signed-in Jellyfin session on connection-jellyfin.
struct JellyfinSession {
    explicit JellyfinSession(const QString &configPath)
        : isolation(configPath)
        , transport(&manager)
        , auth(&transport, &adapter)
    {
        ServerConnection connection;
        connection.connectionId = QStringLiteral("connection-jellyfin");
        connection.providerKind = ProviderKind::Jellyfin;
        connection.protocolMode = ProtocolMode::Native;
        connection.baseUrl = QStringLiteral("https://jellyfin.example.test");
        connection.accountId = QStringLiteral("user-1");
        connection.credentialReference = QStringLiteral("credential-jellyfin");
        config.upsertConnection(connection, true);
        auth.initialize(&config);
        restored = QTest::qWaitFor([this]() { return !auth.isRestoringSession(); }, 1000);
        auth.seedJellyfin();
    }

    ScopedConfigIsolation isolation;
    ConfigManager config;
    ControlledNetworkAccessManager manager;
    HttpTransport transport;
    JellyfinProviderAdapter adapter;
    ExposedAuthenticationService auth;
    bool restored = false;
};

QByteArray jellyfinItems(const QByteArray &items, int total, int startIndex = 0)
{
    return QByteArrayLiteral(R"({"Items":[)") + items + QByteArrayLiteral(R"(],"TotalRecordCount":)")
        + QByteArray::number(total) + QByteArrayLiteral(R"(,"StartIndex":)")
        + QByteArray::number(startIndex) + QByteArrayLiteral("}");
}

QStringList collectionNames(const QVariantList &collections)
{
    QStringList names;
    for (const QVariant &value : collections) {
        names.append(value.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

} // namespace

// Jellyfin 10.11 drops ParentId from a BoxSet-only query, so a library's
// box sets come from its titles collapsed into box sets, read once and
// kept. A scan that finds none, or a library too large to scan, answers
// the server's list and says so. "Part of" is never asked before API 12.
void SiloCatalogServiceTest::jellyfinCollectionsBeforeApi12ScanTheLibrary()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    JellyfinSession session(temporaryDirectory.path());
    QVERIFY(session.restored);
    QVERIFY(session.auth.isAuthenticated());
    ControlledNetworkAccessManager &manager = session.manager;
    LibraryService service(&session.auth);
    QVERIFY(service.supportsCollections());
    QCOMPARE(service.getActiveConnectionId(), QStringLiteral("connection-jellyfin"));
    QSignalSpy loaded(&service, &LibraryService::canonicalLibraryCollectionsLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalLibraryCollectionsFailed);
    QSignalSpy partOf(&service, &LibraryService::canonicalItemCollectionsLoaded);

    manager.responses = {
        {200, QByteArrayLiteral(R"({"Version":"10.11.10","ProductName":"Jellyfin Server"})"), false},
        // Titles in a box set come back as the box set.
        {200, jellyfinItems(QByteArrayLiteral(
             R"({"Id":"m1","Name":"Heat","Type":"Movie"},)"
             R"({"Id":"b1","Name":"Zed Collection","Type":"BoxSet"},)"
             R"({"Id":"b2","Name":"Alien Collection","Type":"BoxSet"})"), 5), false},
        {200, jellyfinItems(QByteArrayLiteral(
             R"({"Id":"b1","Name":"Zed Collection","Type":"BoxSet"},)"
             R"({"Id":"m2","Name":"Ronin","Type":"Movie"})"), 5, 3), false},
        {200, jellyfinItems(QByteArrayLiteral(
             R"({"Id":"b2","Name":"Alien Collection","Type":"BoxSet","ChildCount":4,"ImageTags":{"Primary":"p2"}},)"
             R"({"Id":"b1","Name":"Zed Collection","Type":"BoxSet","ChildCount":2})"), 2), false}};
    const qsizetype first = manager.requests.size();
    service.getLibraryCollections(QStringLiteral("lib-1"), 0, 100, QStringLiteral("k1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 2000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(manager.requests.size(), first + 4);
    QCOMPARE(manager.requests.at(first).request.url().path(), QStringLiteral("/System/Info/Public"));
    for (int page = 1; page <= 2; ++page) {
        const QUrl url = manager.requests.at(first + page).request.url();
        const QUrlQuery scan(url);
        QCOMPARE(url.path(), QStringLiteral("/Items"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("lib-1"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Movie,Series"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("CollapseBoxSetItems")), QStringLiteral("true"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("EnableImages")), QStringLiteral("false"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("EnableUserData")), QStringLiteral("false"));
        QVERIFY(!scan.hasQueryItem(QStringLiteral("Fields")));
        QCOMPARE(scan.queryItemValue(QStringLiteral("StartIndex")), page == 1 ? QStringLiteral("0")
                                                                              : QStringLiteral("3"));
        QCOMPARE(scan.queryItemValue(QStringLiteral("Limit")),
                 QString::number(LibraryService::LibraryCollectionScanPageSize));
    }
    const QUrl byIdUrl = manager.requests.at(first + 3).request.url();
    const QUrlQuery byId(byIdUrl);
    QCOMPARE(byIdUrl.path(), QStringLiteral("/Items"));
    QCOMPARE(byId.queryItemValue(QStringLiteral("Ids")), QStringLiteral("b1,b2"));
    QCOMPARE(byId.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("BoxSet"));
    QVERIFY(!byId.hasQueryItem(QStringLiteral("ParentId")));
    QCOMPARE(loaded.first().at(0).toString(), QStringLiteral("connection-jellyfin"));
    QCOMPARE(loaded.first().at(2).toString(), QStringLiteral("k1"));
    QCOMPARE(collectionNames(loaded.first().at(3).toList()),
             (QStringList{QStringLiteral("Alien Collection"), QStringLiteral("Zed Collection")}));
    QCOMPARE(loaded.first().at(4).toInt(), 2);
    QCOMPARE(loaded.first().at(5).toBool(), false);
    QCOMPARE(loaded.first().at(6).toBool(), false);

    // Later pages come from the kept scan.
    service.getLibraryCollections(QStringLiteral("lib-1"), 1, 1, QStringLiteral("k2"));
    QCOMPARE(loaded.count(), 2);
    QCOMPARE(manager.requests.size(), first + 4);
    QCOMPARE(collectionNames(loaded.last().at(3).toList()),
             (QStringList{QStringLiteral("Zed Collection")}));
    QCOMPARE(loaded.last().at(5).toBool(), false);

    // "Part of" needs API 12: answered empty without a request.
    service.getItemCollections(QStringLiteral("m1"), QStringLiteral("p1"));
    QCOMPARE(partOf.count(), 1);
    QVERIFY(partOf.last().at(3).toList().isEmpty());
    QCOMPARE(manager.requests.size(), first + 4);

    // No title collapsed (the server does not group movies into
    // collections): the server's list, marked server-wide.
    manager.responses = {
        {200, jellyfinItems(QByteArrayLiteral(R"({"Id":"m3","Name":"Up","Type":"Movie"})"), 1), false},
        {200, jellyfinItems(QByteArrayLiteral(R"({"Id":"b9","Name":"Pixar","Type":"BoxSet"})"), 1), false}};
    service.getLibraryCollections(QStringLiteral("lib-2"), 0, 100, QStringLiteral("k3"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 2000);
    QCOMPARE(manager.requests.size(), first + 6);
    const QUrlQuery serverList(manager.requests.last().request.url());
    QCOMPARE(serverList.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("BoxSet"));
    QVERIFY(!serverList.hasQueryItem(QStringLiteral("ParentId")));
    QVERIFY(!serverList.hasQueryItem(QStringLiteral("Ids")));
    QCOMPARE(collectionNames(loaded.last().at(3).toList()), QStringList{QStringLiteral("Pixar")});
    QCOMPARE(loaded.last().at(6).toBool(), true);

    // A library past the scan limit is not paged through.
    manager.responses = {
        {200, jellyfinItems(QByteArrayLiteral(R"({"Id":"m4","Name":"Big","Type":"Movie"})"),
                            LibraryService::LibraryCollectionScanMaxTitles + 1), false},
        {200, jellyfinItems(QByteArrayLiteral(R"({"Id":"b9","Name":"Pixar","Type":"BoxSet"})"), 1), false}};
    service.getLibraryCollections(QStringLiteral("lib-3"), 0, 100, QStringLiteral("k4"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, 2000);
    QCOMPARE(manager.requests.size(), first + 8);
    QVERIFY(!QUrlQuery(manager.requests.last().request.url()).hasQueryItem(QStringLiteral("ParentId")));
    QCOMPARE(loaded.last().at(6).toBool(), true);

    // A failed scan fails the page and is tried again next time.
    manager.responses = {{400, QByteArrayLiteral(R"({"title":"Bad"})"), false}};
    service.getLibraryCollections(QStringLiteral("lib-4"), 0, 100, QStringLiteral("k5"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
    QCOMPARE(failed.last().at(2).toString(), QStringLiteral("k5"));
    QCOMPARE(manager.requests.size(), first + 9);
}

// From API 12 the server keeps a BoxSet query to the library's box sets,
// and "Part of" is asked for every title: a 404 for one never stops it.
void SiloCatalogServiceTest::jellyfinCollectionsFromApi12AreServerScoped()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    JellyfinSession session(temporaryDirectory.path());
    QVERIFY(session.restored);
    QVERIFY(session.auth.isAuthenticated());
    ControlledNetworkAccessManager &manager = session.manager;
    LibraryService service(&session.auth);
    QSignalSpy loaded(&service, &LibraryService::canonicalLibraryCollectionsLoaded);
    QSignalSpy partOf(&service, &LibraryService::canonicalItemCollectionsLoaded);
    QSignalSpy partOfFailed(&service, &LibraryService::canonicalItemCollectionsFailed);

    manager.responses = {
        {200, QByteArrayLiteral(R"({"Version":"12.0.0","ProductName":"Jellyfin Server"})"), false},
        {200, jellyfinItems(QByteArrayLiteral(R"({"Id":"b1","Name":"Alien Collection","Type":"BoxSet"})"), 1), false}};
    const qsizetype first = manager.requests.size();
    service.getLibraryCollections(QStringLiteral("lib-1"), 0, 100, QStringLiteral("k1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), first + 2);
    const QUrlQuery boxSets(manager.requests.last().request.url());
    QCOMPARE(boxSets.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("lib-1"));
    QCOMPARE(boxSets.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("BoxSet"));
    QVERIFY(!boxSets.hasQueryItem(QStringLiteral("CollapseBoxSetItems")));
    QCOMPARE(collectionNames(loaded.last().at(3).toList()),
             QStringList{QStringLiteral("Alien Collection")});
    QCOMPARE(loaded.last().at(6).toBool(), false);

    // The version is read once per connection.
    manager.responses = {
        {404, QByteArrayLiteral(R"({"title":"Not Found"})"), false},
        {200, QByteArrayLiteral(R"({"Items":[{"Id":"b1","Name":"Alien Collection","Type":"BoxSet"}],"TotalRecordCount":1})"), false}};
    service.getItemCollections(QStringLiteral("gone"), QStringLiteral("p1"));
    QTRY_COMPARE_WITH_TIMEOUT(partOf.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), first + 3);
    QCOMPARE(manager.requests.last().request.url().path(), QStringLiteral("/Items/gone/Collections"));
    QVERIFY(partOf.last().at(3).toList().isEmpty());
    service.getItemCollections(QStringLiteral("m1"), QStringLiteral("p2"));
    QTRY_COMPARE_WITH_TIMEOUT(partOf.count(), 2, 2000);
    QCOMPARE(manager.requests.size(), first + 4);
    QCOMPARE(manager.requests.last().request.url().path(), QStringLiteral("/Items/m1/Collections"));
    QCOMPARE(collectionNames(partOf.last().at(3).toList()),
             QStringList{QStringLiteral("Alien Collection")});
    QCOMPARE(partOfFailed.count(), 0);
}

void SiloCatalogServiceTest::peopleSearchFollowsTheSearchCapability()
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

    const auto paths = [&manager](qsizetype from) {
        QStringList result;
        for (qsizetype index = from; index < manager.requests.size(); ++index) {
            result.append(manager.requests.at(index).request.url().path());
        }
        return result;
    };
    const QString capabilities = QStringLiteral("/api/v2/catalog/search/capabilities");
    const QString people = QStringLiteral("/api/v2/catalog/people");
    const QByteArray found = QByteArrayLiteral(
        "{\"items\":[{\"id\":\"7\",\"name\":\"Al Pacino\",\"photo_url\":\"https://cdn.example.test/7.jpg\"},{\"id\":\"9\"}]}");

    // Scoped search: the capability is read once per connection, and the
    // search asks for movie and series credits only.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalPeopleSearchLoaded);
        manager.responses = {
            {200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true,"people_media_scope":true})"), false},
            {200, found, false},
            {200, found, false}};
        const qsizetype first = manager.requests.size();
        service.searchPeople(QStringLiteral("  pacino "), 20);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.searchPeople(QStringLiteral("pacino"), 20);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(paths(first), (QStringList{capabilities, people, people}));
        const QUrlQuery search(manager.requests.at(first + 1).request.url());
        QCOMPARE(search.queryItemValue(QStringLiteral("q")), QStringLiteral("pacino"));
        QCOMPARE(search.queryItemValue(QStringLiteral("limit")), QStringLiteral("20"));
        QCOMPARE(search.queryItemValue(QStringLiteral("media_scope")), QStringLiteral("video"));
        QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("pacino"));
        // The nameless entry is dropped.
        const QVariantList results = loaded.first().at(2).toList();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().toMap().value(QStringLiteral("personId")).toString(),
                 QStringLiteral("7"));
    }

    // A server that cannot scope people search shows no people and is not
    // asked for any.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalPeopleSearchLoaded);
        manager.responses = {
            {200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})"), false}};
        const qsizetype first = manager.requests.size();
        service.searchPeople(QStringLiteral("pacino"), 20);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.searchPeople(QStringLiteral("pacino"), 20);
        QCOMPARE(loaded.count(), 2);
        QVERIFY(loaded.last().at(2).toList().isEmpty());
        QCOMPARE(paths(first), QStringList{capabilities});
    }

    // A failed capability read fails the search. Later searches (every
    // debounced keystroke) do not read it again for a while: they show no
    // people at once instead.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalPeopleSearchLoaded);
        QSignalSpy failed(&service, &LibraryService::canonicalPeopleSearchFailed);
        manager.responses = {
            {403, QByteArrayLiteral(R"({"type":"about:blank","title":"Forbidden","status":403})"), false},
            {200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true,"people_media_scope":true})"), false},
            {200, found, false}};
        const qsizetype first = manager.requests.size();
        service.searchPeople(QStringLiteral("pacino"), 20);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        service.searchPeople(QStringLiteral("pacin"), 20);
        service.searchPeople(QStringLiteral("pacino"), 20);
        QCOMPARE(loaded.count(), 2);
        QVERIFY(loaded.last().at(2).toList().isEmpty());
        QCOMPARE(failed.count(), 1);
        QTest::qWait(50);
        QCOMPARE(paths(first), QStringList{capabilities});
    }

    // A newer search replaces an older one still in flight.
    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalPeopleSearchLoaded);
        manager.responses = {
            {200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true,"people_media_scope":true})"), false},
            {200, found, true},
            {200, found, false}};
        service.searchPeople(QStringLiteral("pac"), 20);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.replies.isEmpty()
                                     && manager.replies.last()
                                     && manager.requests.last().request.url().path() == people,
                                 1000);
        QPointer<ControlledReply> stale = manager.replies.last();
        service.searchPeople(QStringLiteral("pacino"), 20);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("pacino"));
        if (stale) {
            stale->complete();
        }
        QTest::qWait(50);
        QCOMPARE(loaded.count(), 1);
    }
}

void SiloCatalogServiceTest::notInterestedRequestsAndCapabilityParse()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("series/the wire");
    state.stateValue = true;

    // markNotInterested and unmarkNotInterested are natural_idempotent.
    ProviderCatalogRequest request
        = provider.createRequest(ProviderCatalogOperation::SetNotInterested, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/not-interested/series%2Fthe%20wire"));
    QVERIFY(request.body.isEmpty());
    state.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetNotInterested, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetNotInterested, {}).supported);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetNotInterested, {}).valid);

    request = provider.createRequest(ProviderCatalogOperation::NotInterestedCapability, {});
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/capabilities/not-interested"));

    const auto capability = [&provider](const QByteArray &body) {
        return provider.parseResponse(ProviderCatalogOperation::NotInterestedCapability, body);
    };
    ProviderCatalogResponse response = capability(
        QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})"));
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value(QStringLiteral("allowed")).toBool());
    response = capability(QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":false})"));
    QVERIFY(response.valid);
    QVERIFY(!response.capabilityMetadata.value(QStringLiteral("allowed")).toBool());
    response = capability(QByteArrayLiteral(R"({"revision":"r1","state":"disabled","allowed":true})"));
    QVERIFY(response.valid);
    QVERIFY(!response.capabilityMetadata.value(QStringLiteral("allowed")).toBool());
    QVERIFY(!capability(QByteArrayLiteral(R"({"state":"available"})")).valid);

    // Item details carry the mark as canonical user state.
    const QVariantMap mapped = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"series:wire","type":"series","title":"The Wire",)"
            R"("user_state":{"played":false,"is_favorite":false,"in_watchlist":false,"not_interested":true}})"))
            .object(),
        QStringLiteral("connection-1"));
    QVERIFY(mapped.value(QStringLiteral("notInterested")).toBool());
    QVERIFY(mapped.value(QStringLiteral("userState")).toMap()
                .value(QStringLiteral("notInterested")).toBool());

    SiloProviderAdapter silo;
    QVERIFY(silo.supportsCapability(ProviderCapability::NotInterested));
    JellyfinProviderAdapter jellyfin;
    QVERIFY(!jellyfin.supportsCapability(ProviderCapability::NotInterested));
    QVERIFY(!jellyfin.catalogProvider()
                 ->createRequest(ProviderCatalogOperation::SetNotInterested, state).supported);
}

void SiloCatalogServiceTest::notInterestedServiceGatesOnProfileCapability()
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
    QSignalSpy support(&service, &LibraryService::notInterestedSupportChanged);
    QSignalSpy changed(&service, &LibraryService::notInterestedStatusChanged);
    QSignalSpy failed(&service, &LibraryService::notInterestedStatusFailed);
    // Hidden until the profile's capability document allows it.
    QVERIFY(!service.supportsNotInterested());

    manager.responses = {
        { 200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})"), false }
    };
    qsizetype firstRequest = manager.requests.size();
    service.loadNotInterestedCapability();
    // A second page asking while the read is in flight shares it.
    service.loadNotInterestedCapability();
    QTRY_COMPARE_WITH_TIMEOUT(support.count(), 1, 1000);
    QVERIFY(service.supportsNotInterested());
    QCOMPARE(manager.requests.size(), firstRequest + 1);
    const RecordedRequest read = manager.requests.at(firstRequest);
    QCOMPARE(read.request.url().path(), QStringLiteral("/api/v2/capabilities/not-interested"));
    QCOMPARE(read.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    // Read once per connection and profile.
    service.loadNotInterestedCapability();
    QCOMPARE(manager.requests.size(), firstRequest + 1);

    manager.responses = { { 204, QByteArray(), false }, { 204, QByteArray(), false } };
    firstRequest = manager.requests.size();
    service.setItemNotInterested(QStringLiteral("movie:heat-1995"), true);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 1000);
    QCOMPARE(changed.first().at(0).toString(), QStringLiteral("movie:heat-1995"));
    QCOMPARE(changed.first().at(1).toBool(), true);
    const RecordedRequest mark = manager.requests.at(firstRequest);
    QCOMPARE(mark.operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(mark.request.url().path(), QStringLiteral("/api/v2/not-interested/movie:heat-1995"));
    QCOMPARE(mark.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QVERIFY(mark.body.isEmpty());

    service.setItemNotInterested(QStringLiteral("movie:heat-1995"), false);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 1000);
    QCOMPARE(changed.at(1).at(1).toBool(), false);
    QCOMPARE(manager.requests.at(firstRequest + 1).operation,
             QNetworkAccessManager::DeleteOperation);

    manager.responses = {
        { 422, QByteArrayLiteral(R"({"type":"about:blank","title":"Unprocessable","status":422})"), false }
    };
    service.setItemNotInterested(QStringLiteral("episode:x"), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("episode:x"));
    QCOMPARE(failed.first().at(1).toBool(), true);
    QCOMPARE(changed.count(), 2);
}

void SiloCatalogServiceTest::notInterestedReadCancelledWithSessionIsAskedAgain()
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
    QSignalSpy support(&service, &LibraryService::notInterestedSupportChanged);
    const QByteArray allowed = QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})");
    manager.responses = { { 200, allowed, true } };
    const qsizetype firstRequest = manager.requests.size();
    service.loadNotInterestedCapability();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), firstRequest + 1, 1000);

    // The session changes while the read is out, which cancels it; the
    // next page asks again instead of waiting on it forever.
    emit auth.userIdChanged();
    manager.responses = { { 200, allowed, false } };
    service.loadNotInterestedCapability();
    QTRY_COMPARE_WITH_TIMEOUT(support.count(), 1, 1000);
    QVERIFY(service.supportsNotInterested());
    QCOMPARE(manager.requests.size(), firstRequest + 2);
    // The cancelled read finishing late changes nothing.
    if (manager.replies.at(firstRequest)) {
        manager.replies.at(firstRequest)->complete();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(support.count(), 1);
}

void SiloCatalogServiceTest::becauseWatchedAndRatingRequestsParse()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.itemId = QStringLiteral("movie:heat-1995");
    query.limit = 80;

    // listBecauseWatched answers at most 50 cards.
    ProviderCatalogRequest request
        = provider.createRequest(ProviderCatalogOperation::BecauseWatched, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    const QUrl becauseWatched(QStringLiteral("https://silo.test") + request.relativeEndpoint);
    QCOMPARE(becauseWatched.path(),
             QStringLiteral("/api/v2/recommendations/because-watched/movie:heat-1995"));
    QCOMPARE(QUrlQuery(becauseWatched).queryItemValue(QStringLiteral("limit")), QStringLiteral("50"));
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::BecauseWatched, {}).supported);

    const ProviderCatalogResponse cards = provider.parseResponse(
        ProviderCatalogOperation::BecauseWatched,
        QByteArrayLiteral(R"({"items":[{"content_id":"movie:ronin-1998","type":"movie","title":"Ronin"},)"
                          R"({"content_id":"series:the-wire","type":"series","title":"The Wire"}],)"
                          R"("page":{"next_cursor":null}})"));
    QVERIFY(cards.valid);
    QCOMPARE(cards.rawItems.size(), 2);
    QCOMPARE(cards.rawItems.first().toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("movie:ronin-1998"));

    // setRating and deleteRating are non_retryable.
    query.ratingValue = 4;
    request = provider.createRequest(ProviderCatalogOperation::SetUserRating, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/ratings/movie%3Aheat-1995"));
    QCOMPARE(QJsonDocument::fromJson(request.body).object().keys(), QStringList{QStringLiteral("rating")});
    QCOMPARE(QJsonDocument::fromJson(request.body).object().value(QStringLiteral("rating")).toInt(), 4);
    query.ratingValue = 0;
    request = provider.createRequest(ProviderCatalogOperation::SetUserRating, query);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QVERIFY(request.body.isEmpty());
    query.ratingValue = 6;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetUserRating, query).supported);
    query.ratingValue = -1;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetUserRating, query).supported);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetUserRating, {}).valid);

    // CatalogItemDetail.user_rating is the canonical userRating.
    QVariantMap mapped = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:heat-1995","type":"movie","title":"Heat","user_rating":4})"))
            .object(),
        QStringLiteral("connection-1"));
    QCOMPARE(mapped.value(QStringLiteral("userRating")).toInt(), 4);
    QCOMPARE(mapped.value(QStringLiteral("userState")).toMap()
                 .value(QStringLiteral("userRating")).toInt(), 4);
    mapped = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:heat-1995","type":"movie","title":"Heat"})")).object(),
        QStringLiteral("connection-1"));
    QCOMPARE(mapped.value(QStringLiteral("userRating")).toInt(), 0);

    SiloProviderAdapter silo;
    QVERIFY(silo.supportsCapability(ProviderCapability::StarRatings));
    QVERIFY(!silo.supportsCapability(ProviderCapability::LikeRatings));
    JellyfinProviderAdapter jellyfin;
    QVERIFY(jellyfin.supportsCapability(ProviderCapability::LikeRatings));
    QVERIFY(!jellyfin.supportsCapability(ProviderCapability::StarRatings));
    QVERIFY(!jellyfin.catalogProvider()
                 ->createRequest(ProviderCatalogOperation::BecauseWatched, query).supported);
}

void SiloCatalogServiceTest::starRatingsNeverReplayAndHideWhenUnavailable()
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
    QSignalSpy support(&service, &LibraryService::userRatingSupportChanged);
    QSignalSpy changed(&service, &LibraryService::userRatingChanged);
    QSignalSpy failed(&service, &LibraryService::userRatingFailed);
    // Silo has no personal-ratings capability document: stars at once, and
    // getRatingsCapability (the external rating sources) is never read.
    const qsizetype beforeScale = manager.requests.size();
    QCOMPARE(service.userRatingScale(), QStringLiteral("stars"));
    QCOMPARE(manager.requests.size(), beforeScale);
    const QString connectionId = service.getActiveConnectionId();
    QVERIFY(!connectionId.isEmpty());

    manager.responses = { { 204, QByteArray(), false }, { 204, QByteArray(), false } };
    qsizetype firstRequest = manager.requests.size();
    service.setUserRating(QStringLiteral("movie:heat-1995"), 4);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 1000);
    QCOMPARE(changed.first().at(0).toString(), connectionId);
    QCOMPARE(changed.first().at(1).toString(), QStringLiteral("movie:heat-1995"));
    QCOMPARE(changed.first().at(2).toInt(), 4);
    const RecordedRequest rate = manager.requests.at(firstRequest);
    QCOMPARE(rate.operation, QNetworkAccessManager::PutOperation);
    QCOMPARE(rate.request.url().path(), QStringLiteral("/api/v2/ratings/movie:heat-1995"));
    QCOMPARE(rate.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(QJsonDocument::fromJson(rate.body).object().keys(), QStringList{QStringLiteral("rating")});
    QCOMPARE(QJsonDocument::fromJson(rate.body).object().value(QStringLiteral("rating")).toInt(), 4);

    service.setUserRating(QStringLiteral("movie:heat-1995"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 1000);
    QCOMPARE(changed.at(1).at(2).toInt(), 0);
    QCOMPARE(manager.requests.at(firstRequest + 1).operation,
             QNetworkAccessManager::DeleteOperation);

    // An ordinary refusal is reported once, never replayed, and keeps stars.
    manager.responses = {
        { 422, QByteArrayLiteral(R"({"type":"about:blank","title":"Unprocessable","status":422})"), false },
        { 204, QByteArray(), false }
    };
    firstRequest = manager.requests.size();
    service.setUserRating(QStringLiteral("movie:heat-1995"), 2);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(failed.first().at(0).toString(), connectionId);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("movie:heat-1995"));
    QCOMPARE(failed.first().at(2).toInt(), 2);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(manager.requests.size(), firstRequest + 1);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(support.count(), 0);
    QCOMPARE(service.userRatingScale(), QStringLiteral("stars"));

    // 503 (no ratings service) fails once, never replays, and hides stars.
    manager.responses = {
        { 503, QByteArrayLiteral(R"({"type":"about:blank","title":"Unavailable","status":503})"), false },
        { 204, QByteArray(), false }
    };
    firstRequest = manager.requests.size();
    service.setUserRating(QStringLiteral("movie:heat-1995"), 3);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 3000);
    QCOMPARE(failed.at(1).at(2).toInt(), 3);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(manager.requests.size(), firstRequest + 1);
    QCOMPARE(support.count(), 1);
    QCOMPARE(service.userRatingScale(), QString());
}

void SiloCatalogServiceTest::staleRatingWriteFailsAfterSessionChange()
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
    UserRatingState state(&service);
    state.setItem(QStringLiteral("movie:heat-1995"));
    QSignalSpy failed(&service, &LibraryService::userRatingFailed);
    QSignalSpy stateFailed(&state, &UserRatingState::updateFailed);
    const QString connectionId = service.getActiveConnectionId();

    // The write is held; the session changes before the server answers.
    manager.responses = { { 204, QByteArray(), true } };
    state.rate(4);
    QVERIFY(state.pending());
    QCOMPARE(state.value(), 4);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.replies.isEmpty(), 1000);
    QPointer<ControlledReply> reply = manager.replies.constLast();
    QVERIFY(reply);

    auth.replaceAccount(QStringLiteral("account-2"), QStringLiteral("access-2"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(0).toString(), connectionId);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("movie:heat-1995"));
    // The control is usable again, back on the rating it had.
    QVERIFY(!state.pending());
    QCOMPARE(state.value(), 0);
    QCOMPARE(stateFailed.count(), 1);

    // The aborted reply finishing later reports nothing more.
    if (reply) {
        reply->complete();
    }
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(failed.count(), 1);
}

void SiloCatalogServiceTest::becauseWatchedReadsNativeRouteOrJellyfinSimilar()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    {
        ControlledNetworkAccessManager manager;
        HttpTransport transport(&manager);
        SiloProviderAdapter adapter;
        ExposedAuthenticationService auth(&transport, &adapter);
        QVERIFY(signInToSilo(manager, auth, config));
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalBecauseWatchedLoadedForConnection);
        QSignalSpy similar(&service, &LibraryService::canonicalSimilarItemsLoadedForConnection);

        manager.responses = {
            { 200, QByteArrayLiteral(R"({"items":[{"content_id":"movie:ronin-1998","type":"movie","title":"Ronin"}],)"
                                     R"("page":{"next_cursor":null}})"), false }
        };
        const qsizetype firstRequest = manager.requests.size();
        service.getBecauseWatchedItems(QStringLiteral("movie:heat-1995"), 12);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        const RecordedRequest read = manager.requests.at(firstRequest);
        QCOMPARE(read.request.url().path(),
                 QStringLiteral("/api/v2/recommendations/because-watched/movie:heat-1995"));
        QCOMPARE(QUrlQuery(read.request.url()).queryItemValue(QStringLiteral("limit")),
                 QStringLiteral("12"));
        QCOMPARE(read.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
        QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("movie:heat-1995"));
        const QVariantList items = loaded.first().at(2).toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("movie:ronin-1998"));
        QCOMPARE(similar.count(), 0);
    }
    {
        // Jellyfin has no because-watched read; its similar items stand in.
        JellyfinProviderAdapter jellyfin;
        ControlledNetworkAccessManager manager;
        HttpTransport transport(&manager);
        ExposedAuthenticationService auth(&transport, &jellyfin);
        auth.replaceAccount(QStringLiteral("user-1"), QStringLiteral("token-1"));
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::canonicalBecauseWatchedLoadedForConnection);
        QSignalSpy failed(&service, &LibraryService::canonicalBecauseWatchedFailedForConnection);
        manager.responses = {
            { 200, QByteArrayLiteral(R"({"Items":[{"Id":"movie-2","Name":"Ronin","Type":"Movie"}],"TotalRecordCount":1})"), false },
            { 404, QByteArrayLiteral(R"({"title":"Not Found"})"), false }
        };
        service.getBecauseWatchedItems(QStringLiteral("movie-1"), 6);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QVERIFY(manager.requests.first().request.url().path().endsWith(
            QStringLiteral("/Items/movie-1/Similar")));
        QCOMPARE(QUrlQuery(manager.requests.first().request.url())
                     .queryItemValue(QStringLiteral("Limit")), QStringLiteral("6"));
        const QVariantList items = loaded.first().at(2).toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("movie-2"));

        service.getBecauseWatchedItems(QStringLiteral("movie-1"), 6);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("movie-1"));
    }
}

void SiloCatalogServiceTest::jellyfinLikesWriteAndClear()
{
    JellyfinProviderAdapter jellyfin;
    const ICatalogProvider *catalog = jellyfin.catalogProvider();
    ProviderCatalogQuery query;
    query.itemId = QStringLiteral("movie-1");
    query.userId = QStringLiteral("user-1");

    // UpdateUserItemRating (Likes) and DeleteUserItemRating; never retried.
    query.ratingValue = 1;
    ProviderCatalogRequest request = catalog->createRequest(ProviderCatalogOperation::SetUserRating, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QUrl url(QStringLiteral("https://jellyfin.test") + request.relativeEndpoint);
    QCOMPARE(url.path(), QStringLiteral("/UserItems/movie-1/Rating"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("Likes")), QStringLiteral("true"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));
    query.ratingValue = -1;
    request = catalog->createRequest(ProviderCatalogOperation::SetUserRating, query);
    url = QUrl(QStringLiteral("https://jellyfin.test") + request.relativeEndpoint);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("Likes")), QStringLiteral("false"));
    query.ratingValue = 0;
    request = catalog->createRequest(ProviderCatalogOperation::SetUserRating, query);
    url = QUrl(QStringLiteral("https://jellyfin.test") + request.relativeEndpoint);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(url.path(), QStringLiteral("/UserItems/movie-1/Rating"));
    QVERIFY(!QUrlQuery(url).hasQueryItem(QStringLiteral("Likes")));
    query.ratingValue = 4;
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::SetUserRating, query).supported);
    QVERIFY(catalog->parseResponse(ProviderCatalogOperation::SetUserRating,
                                   QByteArrayLiteral(R"({"Likes":true})")).valid);

    // UserData.Likes is the canonical userLikes.
    const auto likesOf = [&jellyfin](const QByteArray &userData) {
        const QJsonObject item = QJsonDocument::fromJson(
            QByteArrayLiteral(R"({"Id":"movie-1","Name":"Heat","Type":"Movie","UserData":)") + userData
            + QByteArrayLiteral("}")).object();
        return jellyfin.mapMediaItem(item, QStringLiteral("connection-1"))
            .value(QStringLiteral("userLikes")).toInt();
    };
    QCOMPARE(likesOf(QByteArrayLiteral(R"({"Likes":true})")), 1);
    QCOMPARE(likesOf(QByteArrayLiteral(R"({"Likes":false})")), -1);
    QCOMPARE(likesOf(QByteArrayLiteral(R"({"Likes":null})")), 0);
    QCOMPARE(likesOf(QByteArrayLiteral(R"({})")), 0);

    // Through the service, as a like scale with no capability read.
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &jellyfin);
    auth.replaceAccount(QStringLiteral("user-1"), QStringLiteral("token-1"));
    LibraryService service(&auth);
    QCOMPARE(service.userRatingScale(), QStringLiteral("likes"));
    QCOMPARE(manager.requests.size(), 0);
    QSignalSpy changed(&service, &LibraryService::userRatingChanged);
    manager.responses = { { 200, QByteArrayLiteral(R"({"Likes":false})"), false } };
    service.setUserRating(QStringLiteral("movie-1"), -1);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 1000);
    QCOMPARE(changed.first().at(1).toString(), QStringLiteral("movie-1"));
    QCOMPARE(changed.first().at(2).toInt(), -1);
    QCOMPARE(manager.requests.first().operation, QNetworkAccessManager::PostOperation);
    QVERIFY(manager.requests.first().request.url().path().endsWith(
        QStringLiteral("/UserItems/movie-1/Rating")));
}

namespace {

QByteArray calendarBody(const QDate &day)
{
    const QString date = day.toString(Qt::ISODate);
    const auto event = [&date](const QString &series, int episode) {
        return QJsonObject{{QStringLiteral("content_id"), QStringLiteral("episode:%1-%2").arg(series).arg(episode)},
                           {QStringLiteral("type"), QStringLiteral("episode")},
                           {QStringLiteral("title"), series},
                           {QStringLiteral("series_id"), QStringLiteral("series:") + series},
                           {QStringLiteral("season_number"), 1},
                           {QStringLiteral("episode_number"), episode},
                           {QStringLiteral("air_date"), date},
                           {QStringLiteral("local_air_date"), date},
                           {QStringLiteral("watched"), false},
                           {QStringLiteral("badges"), QJsonArray{}}};
    };
    return QJsonDocument(QJsonObject{{QStringLiteral("events"), QJsonArray{QJsonObject{
        {QStringLiteral("date"), date},
        {QStringLiteral("items"), QJsonArray{event(QStringLiteral("a"), 2),
                                             event(QStringLiteral("b"), 5)}}}}}})
        .toJson(QJsonDocument::Compact);
}

} // namespace

void SiloCatalogServiceTest::nextAiringSharesAndCachesOneCalendarRead()
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
    QVERIFY(service.supportsNextAiring());
    QSignalSpy loaded(&service, &LibraryService::nextAiringLoaded);
    const QDate today = QDate::currentDate();
    const auto airingFor = [&loaded](const QString &seriesId) {
        for (const QList<QVariant> &arguments : std::as_const(loaded)) {
            if (arguments.at(0).toString() == seriesId) {
                return arguments.at(1).toMap();
            }
        }
        return QVariantMap{{QStringLiteral("missing"), true}};
    };

    // A failed read answers every waiting page with nothing and is not kept.
    qsizetype firstRequest = manager.requests.size();
    manager.responses = { { 404, QByteArrayLiteral(R"({"type":"about:blank","status":404})"), false } };
    service.getNextAiring(QStringLiteral("series:a"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QVERIFY(airingFor(QStringLiteral("series:a")).isEmpty());
    loaded.clear();

    // Pages asking while the calendar is read share the one read.
    manager.responses = { { 200, calendarBody(today.addDays(3)), true } };
    firstRequest = manager.requests.size();
    service.getNextAiring(QStringLiteral("series:a"));
    service.getNextAiring(QStringLiteral("series:b"));
    service.getNextAiring(QStringLiteral("series:a"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), firstRequest + 1, 1000);
    const QUrl url = manager.requests.at(firstRequest).request.url();
    QCOMPARE(url.path(), QStringLiteral("/api/v2/calendar"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("start")), today.toString(Qt::ISODate));
    QCOMPARE(manager.requests.at(firstRequest).request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QCOMPARE(loaded.count(), 0);
    manager.replies.at(firstRequest)->complete();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
    QCOMPARE(airingFor(QStringLiteral("series:a")).value(QStringLiteral("episodeNumber")).toInt(), 2);
    QCOMPARE(airingFor(QStringLiteral("series:b")).value(QStringLiteral("episodeNumber")).toInt(), 5);

    // Another series page answers from the kept calendar, at once.
    service.getNextAiring(QStringLiteral("series:c"));
    QCOMPARE(loaded.count(), 3);
    QVERIFY(airingFor(QStringLiteral("series:c")).isEmpty());
    service.getNextAiring(QStringLiteral("series:b"));
    QCOMPARE(loaded.count(), 4);
    QCOMPARE(manager.requests.size(), firstRequest + 1);

    // A new session drops the kept calendar.
    emit auth.userIdChanged();
    manager.responses = { { 200, calendarBody(today.addDays(3)), false } };
    service.getNextAiring(QStringLiteral("series:a"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), firstRequest + 2, 1000);
}

void SiloCatalogServiceTest::upcomingPageReadsThePresetAndFailsOnSessionChange()
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
    QVERIFY(service.supportsUpcomingAirings());
    QSignalSpy loaded(&service, &LibraryService::canonicalUpcomingAiringsLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalUpcomingAiringsFailed);
    const QString connectionId = service.getActiveConnectionId();
    QVERIFY(!connectionId.isEmpty());
    const QDate today = QDate::currentDate();

    // The page reads its preset from today, profile scoped, as a safe read.
    qsizetype firstRequest = manager.requests.size();
    manager.responses = { { 200, calendarBody(today.addDays(1)), false } };
    service.getUpcomingAirings(QStringLiteral("following"), QStringLiteral("upcoming:1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    const QUrl url = manager.requests.at(firstRequest).request.url();
    QCOMPARE(url.path(), QStringLiteral("/api/v2/calendar"));
    const QUrlQuery parameters(url);
    QCOMPARE(parameters.queryItemValue(QStringLiteral("start")), today.toString(Qt::ISODate));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("end")), today.addDays(30).toString(Qt::ISODate));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("filter")), QStringLiteral("following"));
    QVERIFY(parameters.hasQueryItem(QStringLiteral("timezone")));
    QCOMPARE(manager.requests.at(firstRequest).request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QCOMPARE(loaded.first().at(0).toString(), connectionId);
    QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("upcoming:1"));
    const QVariantList airings = loaded.first().at(2).toList();
    QCOMPARE(airings.size(), 2);
    QCOMPARE(airings.first().toMap().value(QStringLiteral("connectionId")).toString(), connectionId);
    QCOMPARE(airings.first().toMap().value(QStringLiteral("seriesName")).toString(),
             QStringLiteral("a"));
    QVERIFY(loaded.first().at(3).toBool());
    QCOMPARE(failed.count(), 0);

    // A refused read answers failed with the request key.
    manager.responses = { { 404, QByteArrayLiteral(R"({"type":"about:blank","status":404})"), false } };
    service.getUpcomingAirings(QStringLiteral("trending"), QStringLiteral("upcoming:2"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("upcoming:2"));

    // A read the session moves past is answered as failed, not left waiting.
    failed.clear();
    firstRequest = manager.requests.size();
    manager.responses = { { 200, calendarBody(today), true } };
    service.getUpcomingAirings(QStringLiteral("everything"), QStringLiteral("upcoming:3"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), firstRequest + 1, 1000);
    emit auth.userIdChanged();
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("upcoming:3"));
    manager.replies.at(firstRequest)->complete();
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 1);
}

void SiloCatalogServiceTest::nextAiringRetriesUnknownZoneAsOffset()
{
    // A zone with a fixed whole-hour offset (UTC+9) that the server is
    // made to reject.
    const QByteArray previousZone = qgetenv("TZ");
    const bool hadZone = qEnvironmentVariableIsSet("TZ");
    qputenv("TZ", "Asia/Tokyo");
    const auto restoreZone = qScopeGuard([&]() {
        if (hadZone) {
            qputenv("TZ", previousZone);
        } else {
            qunsetenv("TZ");
        }
    });
    if (QTimeZone::systemTimeZoneId() != QByteArrayLiteral("Asia/Tokyo")) {
        QSKIP("The system zone does not follow TZ here");
    }
    // Without zone data (the Nix build sandbox) Bloom already sends the
    // offset zone first, so there is nothing to retry.
    if (!QTimeZone::isTimeZoneIdAvailable(QByteArrayLiteral("Asia/Tokyo"))) {
        QSKIP("No time zone data for Asia/Tokyo here");
    }

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
    QSignalSpy loaded(&service, &LibraryService::nextAiringLoaded);
    const qsizetype firstRequest = manager.requests.size();
    manager.responses = {
        { 422, QByteArrayLiteral(R"({"type":"https://silo.example.test/problems/validation_failed","status":422,)"
                                 R"("errors":[{"location":"query.timezone","code":"invalid"}]})"), false },
        { 200, calendarBody(QDate::currentDate().addDays(2)), false } };
    service.getNextAiring(QStringLiteral("series:b"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(manager.requests.size(), firstRequest + 2);
    QCOMPARE(QUrlQuery(manager.requests.at(firstRequest).request.url())
                 .queryItemValue(QStringLiteral("timezone"), QUrl::FullyDecoded),
             QStringLiteral("Asia/Tokyo"));
    // Etc/GMT zones invert the sign: UTC+9 is Etc/GMT-9.
    QCOMPARE(QUrlQuery(manager.requests.at(firstRequest + 1).request.url())
                 .queryItemValue(QStringLiteral("timezone"), QUrl::FullyDecoded),
             QStringLiteral("Etc/GMT-9"));
    QCOMPARE(loaded.first().at(1).toMap().value(QStringLiteral("episodeNumber")).toInt(), 5);
}

void SiloCatalogServiceTest::upcomingAiringsReadTheViewerCalendar()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query).supported);
    query.calendarStart = QDate(2026, 11, 1);
    query.timeZoneId = QStringLiteral("Etc/GMT+5");
    const ProviderCatalogRequest request
        = provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    // At most 31 days, and the '+' of the zone survives the query string.
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/calendar?start=2026-11-01&end=2026-12-01&timezone=Etc%2FGMT%2B5"));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    query.timeZoneId.clear();
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query).relativeEndpoint,
             QStringLiteral("/api/v2/calendar?start=2026-11-01&end=2026-12-01"));
    // The Upcoming page's presets; a value the pin does not list is not sent.
    query.calendarFilter = QStringLiteral("following");
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query).relativeEndpoint,
             QStringLiteral("/api/v2/calendar?start=2026-11-01&end=2026-12-01&filter=following"));
    query.calendarFilter = QStringLiteral("everything");
    query.timeZoneId = QStringLiteral("America/New_York");
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query).relativeEndpoint,
             QStringLiteral("/api/v2/calendar?start=2026-11-01&end=2026-12-01&filter=everything"
                            "&timezone=America%2FNew_York"));
    query.calendarFilter = QStringLiteral("mine");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::UpcomingAirings, query).supported);

    const ProviderCatalogResponse response = provider.parseResponse(
        ProviderCatalogOperation::UpcomingAirings,
        QByteArrayLiteral(
            R"({"events":[{"date":"2026-11-05","items":[)"
            R"({"content_id":"movie:dune-3","type":"movie","title":"Dune","air_date":"2026-11-05","local_air_date":"2026-11-05","watched":false,"badges":[]},)"
            R"({"content_id":"episode:sev-s02e05","type":"episode","title":"Severance","series_id":"series:severance",)"
            R"("season_number":2,"episode_number":5,"episode_title":"Trojan Horse","air_date":"2026-11-05",)"
            R"("air_time":"21:00","air_timezone":"America/New_York","air_at":"2026-11-06T02:00:00Z",)"
            R"("local_air_date":"2026-11-05","watched":false,"badges":["finale"],)"
            R"("poster_url":"/images/posters/sev.jpg?sig=abc"}]},)"
            R"({"date":"2026-11-07","items":[)"
            R"({"content_id":"season:sev-s03","type":"season_premiere","title":"Severance","series_id":"series:severance",)"
            R"("season_number":3,"air_date":"2026-11-07","air_time":"20:00","watched":true,"badges":["season_premiere"],)"
            R"("poster_url":"/images/posters/sev-s3.jpg"}]}]})"));
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value(QStringLiteral("filterable")).toBool());
    // One entry per event; one without its own local day takes its day's.
    QCOMPARE(response.rawItems.size(), 3);
    QCOMPARE(response.rawItems.at(2).toObject().value(QStringLiteral("local_air_date")).toString(),
             QStringLiteral("2026-11-07"));
    SiloProviderAdapter siloAdapter;
    const QVariantList airings = siloAdapter.mapAirings(response.rawItems,
                                                        QStringLiteral("connection-1"));
    QCOMPARE(airings.size(), 2);
    const QVariantMap airing = airings.first().toMap();
    QCOMPARE(airing.value(QStringLiteral("connectionId")).toString(), QStringLiteral("connection-1"));
    QCOMPARE(airing.value(QStringLiteral("seriesName")).toString(), QStringLiteral("Severance"));
    QCOMPARE(airing.value(QStringLiteral("type")).toString(), QStringLiteral("episode"));
    QVERIFY(!airing.value(QStringLiteral("watched")).toBool());
    // The poster is the series', owned by the series and token-free.
    const QVariantMap poster = airing.value(QStringLiteral("seriesPrimaryArtwork")).toMap();
    QCOMPARE(poster.value(QStringLiteral("itemId")).toString(), QStringLiteral("series:severance"));
    QCOMPARE(poster.value(QStringLiteral("connectionId")).toString(), QStringLiteral("connection-1"));
    QCOMPARE(airing.value(QStringLiteral("artwork")).toList().size(), 1);
    // A season premiere has no episode, and its poster is the season's.
    const QVariantMap premiere = airings.at(1).toMap();
    QCOMPARE(premiere.value(QStringLiteral("type")).toString(), QStringLiteral("season_premiere"));
    QCOMPARE(premiere.value(QStringLiteral("airDate")).toString(), QStringLiteral("2026-11-07"));
    QCOMPARE(premiere.value(QStringLiteral("seasonNumber")).toInt(), 3);
    QCOMPARE(premiere.value(QStringLiteral("episodeNumber")).toInt(), -1);
    QVERIFY(!premiere.contains(QStringLiteral("airAt")));
    // An air_time without its zone stays the network's wall clock.
    QCOMPARE(premiere.value(QStringLiteral("airTime")).toString(), QStringLiteral("20:00"));
    QVERIFY(!airing.contains(QStringLiteral("airTime")));
    QVERIFY(premiere.value(QStringLiteral("watched")).toBool());
    QCOMPARE(premiere.value(QStringLiteral("seriesPrimaryArtwork")).toMap()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("season:sev-s03"));
    QCOMPARE(airing.value(QStringLiteral("seriesId")).toString(), QStringLiteral("series:severance"));
    QCOMPARE(airing.value(QStringLiteral("itemId")).toString(), QStringLiteral("episode:sev-s02e05"));
    QCOMPARE(airing.value(QStringLiteral("seasonNumber")).toInt(), 2);
    QCOMPARE(airing.value(QStringLiteral("episodeNumber")).toInt(), 5);
    QCOMPARE(airing.value(QStringLiteral("episodeTitle")).toString(), QStringLiteral("Trojan Horse"));
    QCOMPARE(airing.value(QStringLiteral("airDate")).toString(), QStringLiteral("2026-11-05"));
    QCOMPARE(airing.value(QStringLiteral("airAt")).toString(), QStringLiteral("2026-11-06T02:00:00Z"));
    QCOMPARE(airing.value(QStringLiteral("badges")).toStringList(), QStringList{QStringLiteral("finale")});
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::UpcomingAirings,
                                    QByteArrayLiteral(R"({"events":null})")).valid);

    // A card's upcoming_event has no zone, so it maps without an instant.
    const QVariantMap card = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"series:severance","type":"series","title":"Severance",)"
            R"("upcoming_event":{"type":"episode","air_date":"2026-11-05","air_time":"21:00",)"
            R"("season_number":2,"episode_number":5,"badges":[]}})"))
            .object(),
        QStringLiteral("connection-1"));
    const QVariantMap next = card.value(QStringLiteral("nextAiring")).toMap();
    QCOMPARE(next.value(QStringLiteral("airDate")).toString(), QStringLiteral("2026-11-05"));
    QCOMPARE(next.value(QStringLiteral("seasonNumber")).toInt(), 2);
    QVERIFY(!next.contains(QStringLiteral("airAt")));

    SiloProviderAdapter silo;
    QVERIFY(silo.supportsCapability(ProviderCapability::AiringCalendar));
    // Jellyfin's upcoming episodes are its calendar (ProviderCatalogTest).
    JellyfinProviderAdapter jellyfin;
    QVERIFY(jellyfin.supportsCapability(ProviderCapability::AiringCalendar));
}

void SiloCatalogServiceTest::nextAiringPicksEarliestAiringStillAhead()
{
    const auto airing = [](const QString &seriesId, int episode, const QString &day,
                           const QString &instant) {
        QVariantMap map{{QStringLiteral("seriesId"), seriesId},
                        {QStringLiteral("seasonNumber"), 1},
                        {QStringLiteral("episodeNumber"), episode},
                        {QStringLiteral("airDate"), day}};
        if (!instant.isEmpty()) {
            map.insert(QStringLiteral("airAt"), instant);
        }
        return map;
    };
    const QDateTime now(QDate(2026, 11, 5), QTime(12, 0), QTimeZone::UTC);
    const QVariantList airings{
        // Aired earlier today.
        airing(QStringLiteral("series:a"), 1, QStringLiteral("2026-11-05"),
               QStringLiteral("2026-11-05T02:00:00Z")),
        airing(QStringLiteral("series:b"), 7, QStringLiteral("2026-11-05"),
               QStringLiteral("2026-11-05T20:00:00Z")),
        airing(QStringLiteral("series:a"), 3, QStringLiteral("2026-11-12"),
               QStringLiteral("2026-11-12T20:00:00Z")),
        airing(QStringLiteral("series:a"), 2, QStringLiteral("2026-11-06"), QString())};
    QCOMPARE(LibraryService::nextAiringFor(airings, QStringLiteral("series:a"), now)
                 .value(QStringLiteral("episodeNumber")).toInt(), 2);
    QCOMPARE(LibraryService::nextAiringFor(airings, QStringLiteral("series:b"), now)
                 .value(QStringLiteral("episodeNumber")).toInt(), 7);
    QVERIFY(LibraryService::nextAiringFor(airings, QStringLiteral("series:c"), now).isEmpty());
    // An airing without an instant counts for the whole of its day.
    const QVariantList dayOnly{airing(QStringLiteral("series:d"), 4,
                                      now.toLocalTime().date().toString(Qt::ISODate), QString())};
    QCOMPARE(LibraryService::nextAiringFor(dayOnly, QStringLiteral("series:d"), now)
                 .value(QStringLiteral("episodeNumber")).toInt(), 4);

    // The chip's window: nothing after the local day 30 days from today.
    const QDate today = now.toLocalTime().date();
    const QVariantList far{
        airing(QStringLiteral("series:e"), 1, today.addDays(31).toString(Qt::ISODate), QString()),
        airing(QStringLiteral("series:f"), 1, today.addDays(30).toString(Qt::ISODate), QString()),
        airing(QStringLiteral("series:g"), 1, today.addDays(40).toString(Qt::ISODate),
               QDateTime(today.addDays(40), QTime(12, 0)).toUTC().toString(Qt::ISODate))};
    QVERIFY(LibraryService::nextAiringFor(far, QStringLiteral("series:e"), now, 30).isEmpty());
    QVERIFY(!LibraryService::nextAiringFor(far, QStringLiteral("series:e"), now).isEmpty());
    QVERIFY(!LibraryService::nextAiringFor(far, QStringLiteral("series:f"), now, 30).isEmpty());
    QVERIFY(LibraryService::nextAiringFor(far, QStringLiteral("series:g"), now, 30).isEmpty());
}

void SiloCatalogServiceTest::jellyfinUpcomingProbesThenPagesTheCalendar()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    JellyfinSession session(temporaryDirectory.path());
    QVERIFY(session.restored);
    QVERIFY(session.auth.isAuthenticated());
    ControlledNetworkAccessManager &manager = session.manager;
    LibraryService service(&session.auth);
    QSignalSpy availability(&service, &LibraryService::upcomingAiringsAvailabilityChanged);
    const QDate today = QDate::currentDate();
    const auto episode = [](const QString &id, const QString &series, const QDate &day) {
        return QStringLiteral(R"({"Id":"%1","Name":"E","Type":"Episode","SeriesId":"%2",)"
                              R"("SeriesName":"%2","ParentIndexNumber":1,"IndexNumber":2,)"
                              R"("PremiereDate":"%3T00:00:00.0000000Z"})")
            .arg(id, series, day.toString(Qt::ISODate))
            .toUtf8();
    };
    const auto episodes = [&episode](int count, const QString &prefix, const QDate &day) {
        QByteArrayList list;
        for (int index = 0; index < count; ++index) {
            list.append(episode(prefix + QString::number(index), QStringLiteral("s-") + prefix, day));
        }
        return list.join(',');
    };

    // Until a probe finds an airing, the tab and the chip stay away.
    QVERIFY(!service.supportsUpcomingAirings());
    QVERIFY(!service.supportsNextAiring());

    // A session change probes: one small page, here empty.
    qsizetype firstRequest = manager.requests.size();
    manager.responses = { { 200, jellyfinItems({}, 0), false } };
    emit session.auth.userIdChanged();
    QTRY_COMPARE_WITH_TIMEOUT(availability.count(), 1, 1000);
    QCOMPARE(manager.requests.size(), firstRequest + 1);
    QUrl url = manager.requests.at(firstRequest).request.url();
    QCOMPARE(url.path(), QStringLiteral("/Shows/Upcoming"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("Limit")), QStringLiteral("20"));
    QVERIFY(!QUrlQuery(url).hasQueryItem(QStringLiteral("StartIndex")));
    QVERIFY(!service.supportsUpcomingAirings());
    // Kept: asking again reads nothing.
    service.getNextAiring(QStringLiteral("s-a"));
    QTest::qWait(20);
    QCOMPARE(manager.requests.size(), firstRequest + 1);

    // The next session probes again. A full page of yesterday's episodes
    // reads on until one in the window shows up.
    firstRequest = manager.requests.size();
    manager.responses = {
        { 200, jellyfinItems(episodes(20, QStringLiteral("old"), today.addDays(-1)), 20), false },
        { 200, jellyfinItems(episode(QStringLiteral("new"), QStringLiteral("s-new"),
                                     today.addDays(2)), 1, 20), false } };
    emit session.auth.userIdChanged();
    QTRY_COMPARE_WITH_TIMEOUT(availability.count(), 2, 1000);
    QCOMPARE(manager.requests.size(), firstRequest + 2);
    QCOMPARE(QUrlQuery(manager.requests.at(firstRequest + 1).request.url())
                 .queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("20"));
    QVERIFY(service.supportsUpcomingAirings());
    QVERIFY(service.supportsNextAiring());

    // The page reads full pages until one ends past the window.
    QSignalSpy loaded(&service, &LibraryService::canonicalUpcomingAiringsLoaded);
    firstRequest = manager.requests.size();
    const int pageSize = LibraryService::kAiringCalendarPageSize;
    manager.responses = {
        { 200, jellyfinItems(episodes(pageSize, QStringLiteral("a"), today), pageSize), false },
        { 200, jellyfinItems(episode(QStringLiteral("near"), QStringLiteral("s-near"),
                                     today.addDays(5))
                                 + ',' + episode(QStringLiteral("far"), QStringLiteral("s-far"),
                                                 today.addDays(40)),
                             2, pageSize), false } };
    service.getUpcomingAirings(QString(), QStringLiteral("upcoming:1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 2000);
    QCOMPARE(manager.requests.size(), firstRequest + 2);
    QCOMPARE(QUrlQuery(manager.requests.at(firstRequest).request.url())
                 .queryItemValue(QStringLiteral("Limit")), QString::number(pageSize));
    QCOMPARE(QUrlQuery(manager.requests.at(firstRequest + 1).request.url())
                 .queryItemValue(QStringLiteral("StartIndex")), QString::number(pageSize));
    QCOMPARE(loaded.first().at(2).toList().size(), pageSize + 2);
    QVERIFY(!loaded.first().at(3).toBool());

    // A calendar that keeps coming back full stops after the page cap.
    loaded.clear();
    firstRequest = manager.requests.size();
    manager.responses.clear();
    for (int page = 0; page < LibraryService::kAiringCalendarMaxPages + 1; ++page) {
        manager.responses.append(
            { 200, jellyfinItems(episodes(pageSize, QStringLiteral("p%1-").arg(page), today),
                                 pageSize, page * pageSize), false });
    }
    service.getUpcomingAirings(QString(), QStringLiteral("upcoming:2"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 4000);
    QCOMPARE(manager.requests.size(), firstRequest + LibraryService::kAiringCalendarMaxPages);
    QCOMPARE(loaded.first().at(2).toList().size(),
             pageSize * LibraryService::kAiringCalendarMaxPages);

    // The chip reads the same pages, but names nothing past 30 days.
    manager.responses = {
        { 200, jellyfinItems(episode(QStringLiteral("near"), QStringLiteral("s-near"),
                                     today.addDays(5))
                                 + ',' + episode(QStringLiteral("far"), QStringLiteral("s-far"),
                                                 today.addDays(40)),
                             2), false } };
    QSignalSpy chips(&service, &LibraryService::nextAiringLoaded);
    service.getNextAiring(QStringLiteral("s-near"));
    QTRY_COMPARE_WITH_TIMEOUT(chips.count(), 1, 1000);
    QCOMPARE(chips.first().at(1).toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("near"));
    service.getNextAiring(QStringLiteral("s-far"));
    QCOMPARE(chips.count(), 2);
    QVERIFY(chips.at(1).at(1).toMap().isEmpty());
}

void SiloCatalogServiceTest::markerSegmentsKeepRepeatedRanges()
{
    // marker_segments lists every occurrence; the singular fields keep one
    // per kind and their provenance.
    const QList<MediaSegmentInfo> segments = SiloModelMapper::mediaSegments(
        QStringLiteral("episode-1"),
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"file_id":"file-1",)"
            R"("intro":{"start_seconds":60,"end_seconds":90,"source":"scanner","confidence":0.9},)"
            R"("credits":{"start_seconds":2400,"end_seconds":2500,"source":"online"},)"
            R"("recap":null,"preview":null,)"
            R"("marker_segments":[)"
            R"({"kind":"recap","start_seconds":0,"end_seconds":30},)"
            R"({"kind":"intro","start_seconds":60,"end_seconds":90},)"
            R"({"kind":"intro","start_seconds":1300,"end_seconds":1330},)"
            R"({"kind":"credits","start_seconds":2400,"end_seconds":2500},)"
            R"({"kind":"bogus","start_seconds":2600,"end_seconds":2700},)"
            R"({"kind":"intro","start_seconds":1500,"end_seconds":1400}]})"))
            .object());
    QCOMPARE(segments.size(), 4);
    QCOMPARE(segments.at(0).type, MediaSegmentType::Recap);
    QCOMPARE(segments.at(1).type, MediaSegmentType::Intro);
    QCOMPARE(segments.at(1).startMs, 60000);
    QCOMPARE(segments.at(1).id, QStringLiteral("file-1:intro"));
    QCOMPARE(segments.at(1).source, QStringLiteral("scanner"));
    QCOMPARE(segments.at(2).type, MediaSegmentType::Intro);
    QCOMPARE(segments.at(2).startMs, 1300000);
    QCOMPARE(segments.at(2).endMs, 1330000);
    QCOMPARE(segments.at(2).id, QStringLiteral("file-1:intro:1"));
    QVERIFY(segments.at(2).source.isEmpty());
    QCOMPARE(segments.at(3).type, MediaSegmentType::Outro);
    QCOMPARE(segments.at(3).source, QStringLiteral("online"));

    // Without occurrences the singular fields stand.
    const QList<MediaSegmentInfo> singular = SiloModelMapper::mediaSegments(
        QStringLiteral("episode-1"),
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"file_id":"file-1","intro":{"start_seconds":5,"end_seconds":35},)"
            R"("credits":null,"recap":null,"preview":null,"marker_segments":[]})"))
            .object());
    QCOMPARE(singular.size(), 1);
    QCOMPARE(singular.first().type, MediaSegmentType::Intro);
    QCOMPARE(singular.first().id, QStringLiteral("file-1:intro"));
}

void SiloCatalogServiceTest::serverRatingsFollowServerOrder()
{
    // The detail's ratings list is authoritative and keeps its order.
    const QVariantMap detail = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:heat","type":"movie","title":"Heat",)"
            R"("rating_imdb":8.3,"rating_tmdb":7.9,"rating_rt_critic":83,)"
            R"("ratings":[)"
            R"({"source":"rt_critic","name":"Rotten Tomatoes","score":83,"display":"83%"},)"
            R"({"source":"imdb","name":"IMDb","score":83,"display":"8.3"},)"
            R"({"source":"letterboxd","name":"Letterboxd","score":82,"display":"4.1"},)"
            R"({"source":"broken","name":"Broken","display":"?"}]})"))
            .object(),
        QStringLiteral("connection-1"));
    const QVariantList ratings = detail.value(QStringLiteral("serverRatings")).toList();
    QCOMPARE(ratings.size(), 3);
    QCOMPARE(ratings.at(0).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("rottenTomatoesCritic"));
    QCOMPARE(ratings.at(0).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("83%"));
    QCOMPARE(ratings.at(1).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("imdb"));
    QCOMPARE(ratings.at(1).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("8.3"));
    QCOMPARE(ratings.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Letterboxd"));
    QCOMPARE(ratings.at(2).toMap().value(QStringLiteral("score")).toDouble(), 82.0);
    // The fixed fields still feed poster badges.
    QCOMPARE(detail.value(QStringLiteral("ratings")).toMap()
                 .value(QStringLiteral("rottenTomatoesCritic")).toInt(), 83);

    // Without the list (cards, older servers) the fixed fields stand in.
    const QVariantMap card = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:heat","type":"movie","title":"Heat",)"
            R"("rating_imdb":8.3,"rating_tmdb":0,"rating_rt_audience":94})"))
            .object(),
        QStringLiteral("connection-1"));
    const QVariantList fallback = card.value(QStringLiteral("serverRatings")).toList();
    QCOMPARE(fallback.size(), 2);
    QCOMPARE(fallback.at(0).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("imdb"));
    QCOMPARE(fallback.at(0).toMap().value(QStringLiteral("score")).toDouble(), 83.0);
    QCOMPARE(fallback.at(0).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("8.3"));
    QCOMPARE(fallback.at(1).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("rottenTomatoesAudience"));
    QCOMPARE(fallback.at(1).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("94%"));

    // An empty list means the server shows none.
    const QVariantMap none = SiloModelMapper::mediaItem(
        QJsonDocument::fromJson(QByteArrayLiteral(
            R"({"content_id":"movie:heat","type":"movie","title":"Heat","rating_imdb":8.3,"ratings":[]})"))
            .object(),
        QStringLiteral("connection-1"));
    QVERIFY(!none.contains(QStringLiteral("serverRatings")));
}


namespace {

QByteArray librarySections()
{
    return QByteArrayLiteral(
        R"({"sections":[)"
        R"({"id":"recent","section_type":"recently_added","title":"Recently Added","featured":false,"item_limit":20,"total_count":45,"is_custom":false,"customized":false,"items":[)"
        R"({"content_id":"movie:a","type":"movie","title":"Alpha"},{"content_id":"episode:x","type":"episode","title":"Pilot"}]},)"
        R"({"id":"tracks","section_type":"genre","title":"Soundtracks","featured":false,"item_limit":20,"total_count":1,"is_custom":false,"customized":false,"items":[)"
        R"({"content_id":"album:z","type":"album","title":"Zeta"}]},)"
        R"({"id":"crime","section_type":"genre","title":"Crime","featured":true,"item_limit":20,"total_count":1,"is_custom":false,"customized":false,"items":[)"
        R"({"content_id":"series:b","type":"series","title":"Bravo"}]}]})");
}

QStringList requestPaths(const ControlledNetworkAccessManager &manager, qsizetype from)
{
    QStringList paths;
    for (qsizetype index = from; index < manager.requests.size(); ++index) {
        const QUrl url = manager.requests.at(index).request.url();
        const QString limit = QUrlQuery(url).queryItemValue(QStringLiteral("item_limit"));
        paths.append(url.path() + (limit.isEmpty() ? QString() : QLatin1Char('=') + limit));
    }
    return paths;
}

QByteArray searchCapabilities(bool letterIndex)
{
    QJsonObject capabilities{{QStringLiteral("revision"), QStringLiteral("r1")},
                             {QStringLiteral("state"), QStringLiteral("available")},
                             {QStringLiteral("allowed"), true}};
    if (letterIndex) {
        capabilities.insert(QStringLiteral("letter_index"), true);
    }
    return QJsonDocument(capabilities).toJson(QJsonDocument::Compact);
}

LibraryItemQuery titleQuery(const QString &requestKey)
{
    LibraryItemQuery query;
    query.parentId = QStringLiteral("lib-1");
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.recursive = true;
    query.sortBy = QStringLiteral("title");
    query.startIndex = 100;
    query.limit = 100;
    query.requestKey = requestKey;
    return query;
}

} // namespace

// Library rows read the Home capabilities' cap (once per connection, shared
// with Home), keep movie and series cards in the server's order, and a row's
// remaining cards come from its own read.
void SiloCatalogServiceTest::librarySectionsShareTheHomeRowCap()
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
    // These reads answer in queue order; the Watch Tonight read beside
    // the layout has its own test.
    config.setHomeWatchTonightEnabled(false);

    LibraryService service(&auth);
    QVERIFY(service.supportsLibrarySections());
    QSignalSpy loaded(&service, &LibraryService::canonicalLibrarySectionsLoaded);
    QSignalSpy homeLoaded(&service, &LibraryService::canonicalHomeSectionsLoaded);
    // A cap of 2: "recent" comes back full at the cap and counts more, so
    // it has more; "crime" sent fewer cards than the cap.
    manager.responses = {{200, homeCapabilities(true, 2), false},
                         {200, librarySections(), false},
                         {200, curatedHomeLayout(true), false}};
    qsizetype first = manager.requests.size();
    service.getLibrarySections(QStringLiteral("lib-1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("lib-1"));
    const QVariantList rows = loaded.first().at(2).toList();
    QCOMPARE(rowIds(rows), (QStringList{QStringLiteral("recent"), QStringLiteral("crime")}));
    QCOMPARE(itemIds(rows.first()), (QStringList{QStringLiteral("movie:a")}));
    QVERIFY(rows.first().toMap().value(QStringLiteral("hasMore")).toBool());
    QCOMPARE(rows.first().toMap().value(QStringLiteral("totalCount")).toInt(), 45);
    QVERIFY(!rows.last().toMap().value(QStringLiteral("hasMore")).toBool());
    QVERIFY(rows.last().toMap().value(QStringLiteral("featured")).toBool());
    // Home reuses the cap read for the library.
    service.getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(homeLoaded.count(), 1, 1000);
    QCOMPARE(requestPaths(manager, first),
             (QStringList{QStringLiteral("/api/v2/home/capabilities"),
                          QStringLiteral("/api/v2/library/lib-1/sections=2"),
                          QStringLiteral("/api/v2/home/sections=2")}));

    // A row's every card.
    QSignalSpy itemsLoaded(&service, &LibraryService::canonicalLibrarySectionItemsLoaded);
    manager.responses = {{200, QByteArrayLiteral(
        R"({"id":"recent","section_type":"recently_added","title":"Recently Added","featured":false,"item_limit":100,"total_count":3,"is_custom":false,"customized":false,"items":[)"
        R"({"content_id":"movie:a","type":"movie"},{"content_id":"movie:c","type":"movie"},{"content_id":"episode:x","type":"episode"}]})"), false}};
    first = manager.requests.size();
    service.getLibrarySectionItems(QStringLiteral("lib-1"), QStringLiteral("recent"));
    QTRY_COMPARE_WITH_TIMEOUT(itemsLoaded.count(), 1, 1000);
    QCOMPARE(requestPaths(manager, first),
             (QStringList{QStringLiteral("/api/v2/library/lib-1/sections/recent/items")}));
    QCOMPARE(itemsLoaded.first().at(2).toString(), QStringLiteral("recent"));
    QCOMPARE(itemsLoaded.first().at(3).toList().size(), 2);

    // A newer read supersedes an older one; a remembered cap the server
    // now rejects is dropped and the rows read once more without it.
    QSignalSpy failed(&service, &LibraryService::canonicalLibrarySectionsFailed);
    manager.responses = {{400, QByteArrayLiteral(R"({"type":"about:blank","title":"Bad Request","status":400})"), false},
                         {200, librarySections(), false}};
    first = manager.requests.size();
    loaded.clear();
    service.getLibrarySections(QStringLiteral("lib-1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(requestPaths(manager, first),
             (QStringList{QStringLiteral("/api/v2/library/lib-1/sections=2"),
                          QStringLiteral("/api/v2/library/lib-1/sections")}));
    // Without item_limit each section answers its own limit, which its
    // items read does not exceed: total_count 45 claims no more cards.
    const QVariantList uncapped = loaded.first().at(2).toList();
    QCOMPARE(uncapped.first().toMap().value(QStringLiteral("totalCount")).toInt(), 45);
    QVERIFY(!uncapped.first().toMap().value(QStringLiteral("hasMore")).toBool());
}

// Scoped people search and the letter index share one read of the search
// capability document: callers that ask while it is in flight wait for it,
// and later ones use the answer.
void SiloCatalogServiceTest::searchCapabilitiesServePeopleAndLettersOnce()
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
    const QByteArray letters = QByteArrayLiteral(
        R"({"total":1,"letters":[{"letter":"A","count":1,"position":0}],"runs":[{"letter":"A","position":0}]})");
    const QByteArray found = QByteArrayLiteral(R"({"items":[{"id":"7","name":"Al Pacino"}]})");

    LibraryService service(&auth);
    QSignalSpy people(&service, &LibraryService::canonicalPeopleSearchLoaded);
    QSignalSpy lettersLoaded(&service, &LibraryService::libraryLettersLoaded);
    manager.responses = {
        {200, QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true,)"
                                R"("people_media_scope":true,"letter_index":true})"), true},
        {200, found, false},
        {200, letters, false}};
    const qsizetype first = manager.requests.size();
    service.searchPeople(QStringLiteral("pacino"), 20);
    service.getLibraryLetters(titleQuery(QStringLiteral("first")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size() - first, qsizetype(1), 1000);
    QCOMPARE(manager.requests.at(first).request.url().path(),
             QStringLiteral("/api/v2/catalog/search/capabilities"));
    manager.replies.last()->complete();
    QTRY_COMPARE_WITH_TIMEOUT(people.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(lettersLoaded.count(), 1, 1000);
    QCOMPARE(requestPaths(manager, first),
             (QStringList{QStringLiteral("/api/v2/catalog/search/capabilities"),
                          QStringLiteral("/api/v2/catalog/people"),
                          QStringLiteral("/api/v2/catalog/letters")}));

    // Both use the cached answer afterwards.
    manager.responses = {{200, found, false}, {200, letters, false}};
    service.searchPeople(QStringLiteral("pacin"), 20);
    QTRY_COMPARE_WITH_TIMEOUT(people.count(), 2, 1000);
    service.getLibraryLetters(titleQuery(QStringLiteral("second")));
    QTRY_COMPARE_WITH_TIMEOUT(lettersLoaded.count(), 2, 1000);
    QCOMPARE(requestPaths(manager, first + 3),
             (QStringList{QStringLiteral("/api/v2/catalog/people"),
                          QStringLiteral("/api/v2/catalog/letters")}));
}

// The letter index is read only when the catalog search capabilities report
// it, once per connection; a failed read is not repeated on every load.
void SiloCatalogServiceTest::libraryLettersReadTheCapabilityOnce()
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
    const QByteArray letters = QByteArrayLiteral(
        R"({"total":3,"letters":[{"letter":"A","count":2,"position":0},{"letter":"B","count":1,"position":2}],)"
        R"("runs":[{"letter":"A","position":0},{"letter":"B","position":2}]})");

    {
        LibraryService service(&auth);
        QSignalSpy loaded(&service, &LibraryService::libraryLettersLoaded);
        QSignalSpy failed(&service, &LibraryService::libraryLettersFailed);
        manager.responses = {{200, searchCapabilities(true), false},
                             {200, letters, false},
                             {200, letters, false}};
        const qsizetype first = manager.requests.size();
        service.getLibraryLetters(titleQuery(QStringLiteral("first")));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        service.getLibraryLetters(titleQuery(QStringLiteral("second")));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 1000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(requestPaths(manager, first),
                 (QStringList{QStringLiteral("/api/v2/catalog/search/capabilities"),
                              QStringLiteral("/api/v2/catalog/letters"),
                              QStringLiteral("/api/v2/catalog/letters")}));
        const RecordedRequest &post = manager.requests.at(first + 1);
        QCOMPARE(post.operation, QNetworkAccessManager::PostOperation);
        const QJsonObject body = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(body.value(QStringLiteral("library_id")).toString(), QStringLiteral("lib-1"));
        QVERIFY(!body.contains(QStringLiteral("seek")));
        QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("first"));
        const QVariantMap index = loaded.first().at(2).toMap();
        QCOMPARE(index.value(QStringLiteral("total")).toInt(), 3);
        QCOMPARE(index.value(QStringLiteral("letters")).toList().size(), 2);

        // Other sorts fail at once, without a request.
        LibraryItemQuery added = titleQuery(QStringLiteral("added"));
        added.sortBy = QStringLiteral("dateAdded");
        const qsizetype before = manager.requests.size();
        service.getLibraryLetters(added);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("added"));
        QCOMPARE(manager.requests.size(), before);
    }

    // A server without the index: the capability read answers no, and the
    // index is never asked for.
    {
        LibraryService service(&auth);
        QSignalSpy failed(&service, &LibraryService::libraryLettersFailed);
        manager.responses = {{200, searchCapabilities(false), false}};
        const qsizetype first = manager.requests.size();
        service.getLibraryLetters(titleQuery(QStringLiteral("none")));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        service.getLibraryLetters(titleQuery(QStringLiteral("none-again")));
        QCOMPARE(failed.count(), 2);
        QCOMPARE(requestPaths(manager, first),
                 (QStringList{QStringLiteral("/api/v2/catalog/search/capabilities")}));
    }

    // A failed capability read is not repeated on the next load.
    {
        LibraryService service(&auth);
        QSignalSpy failed(&service, &LibraryService::libraryLettersFailed);
        manager.responses = {
            {503, QByteArrayLiteral(R"({"type":"about:blank","title":"Unavailable","status":503})"), false},
            {503, QByteArrayLiteral(R"({"type":"about:blank","title":"Unavailable","status":503})"), false},
            {503, QByteArrayLiteral(R"({"type":"about:blank","title":"Unavailable","status":503})"), false}};
        const qsizetype first = manager.requests.size();
        service.getLibraryLetters(titleQuery(QStringLiteral("down")));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        const qsizetype afterFailure = manager.requests.size();
        QVERIFY(afterFailure > first);
        service.getLibraryLetters(titleQuery(QStringLiteral("down-again")));
        QCOMPARE(failed.count(), 2);
        QCOMPARE(manager.requests.size(), afterFailure);
        for (qsizetype index = first; index < afterFailure; ++index) {
            QCOMPARE(manager.requests.at(index).request.url().path(),
                     QStringLiteral("/api/v2/catalog/search/capabilities"));
        }
    }
}

void SiloCatalogServiceTest::jellyfinHasNoLibraryRowsAndIndexesItsOwnList()
{
    JellyfinProviderAdapter jellyfin;
    ControlledNetworkAccessManager manager;
    HttpTransport transport(&manager);
    ExposedAuthenticationService auth(&transport, &jellyfin);
    LibraryService service(&auth);
    QVERIFY(!service.supportsLibrarySections());
    QSignalSpy loaded(&service, &LibraryService::canonicalLibrarySectionsLoaded);
    service.getLibrarySections(QStringLiteral("lib-1"));
    QCOMPARE(loaded.count(), 1);
    QVERIFY(loaded.first().at(2).toList().isEmpty());
    QVERIFY(manager.requests.isEmpty());

    // The letter index counts the list first. Past the threshold it is not
    // downloaded: the rail falls back to the loaded letters.
    auth.replaceAccount(QStringLiteral("user-1"), QStringLiteral("token-1"));
    QSignalSpy lettersLoaded(&service, &LibraryService::libraryLettersLoaded);
    QSignalSpy lettersFailed(&service, &LibraryService::libraryLettersFailed);
    manager.responses = {{200, QByteArrayLiteral(R"({"Items":[],"TotalRecordCount":5001})"), false}};
    service.getLibraryLetters(titleQuery(QStringLiteral("large")));
    QTRY_COMPARE_WITH_TIMEOUT(lettersFailed.count(), 1, 1000);
    QCOMPARE(lettersFailed.first().at(1).toString(), QStringLiteral("large"));
    QCOMPARE(manager.requests.size(), 1);
    QCOMPARE(QUrlQuery(manager.requests.first().request.url())
                 .queryItemValue(QStringLiteral("Limit")), QStringLiteral("0"));
    QCOMPARE(lettersLoaded.count(), 0);

    // A small list is read whole, in one attempt: a failed read is not
    // downloaded again.
    manager.responses = {
        {200, QByteArrayLiteral(R"({"Items":[],"TotalRecordCount":2})"), false},
        {503, QByteArrayLiteral(R"({"title":"Unavailable"})"), false},
        {503, QByteArrayLiteral(R"({"title":"Unavailable"})"), false}};
    service.getLibraryLetters(titleQuery(QStringLiteral("down")));
    QTRY_COMPARE_WITH_TIMEOUT(lettersFailed.count(), 2, 5000);
    QCOMPARE(manager.requests.size(), 3);
    const QUrlQuery list(manager.requests.last().request.url());
    QVERIFY(!list.hasQueryItem(QStringLiteral("Limit")));
    QCOMPARE(list.queryItemValue(QStringLiteral("Fields")), QStringLiteral("SortName"));

    manager.responses = {
        {200, QByteArrayLiteral(R"({"Items":[],"TotalRecordCount":2})"), false},
        {200, QByteArrayLiteral(R"({"Items":[{"Id":"1","SortName":"alien"},{"Id":"2","SortName":"brazil"}],"TotalRecordCount":2})"), false}};
    service.getLibraryLetters(titleQuery(QStringLiteral("small")));
    QTRY_COMPARE_WITH_TIMEOUT(lettersLoaded.count(), 1, 1000);
    QCOMPARE(lettersLoaded.first().at(1).toString(), QStringLiteral("small"));
    QCOMPARE(lettersLoaded.first().at(2).toMap().value(QStringLiteral("letters")).toList().size(), 2);
    QCOMPARE(manager.requests.size(), 5);
}

void SiloCatalogServiceTest::watchHistoryAndWatchTonightRequests()
{
    SiloCatalogProvider provider;

    // listHistory: the profile's whole history by cursor, with the artwork
    // widths; no library or offset.
    ProviderCatalogQuery query;
    query.limit = 60;
    ProviderCatalogRequest request = provider.createRequest(ProviderCatalogOperation::WatchHistory, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint, withArtworkWidths(QStringLiteral("/api/v2/history?limit=60")));
    query.cursor = QStringLiteral("eyJvIjo1MH0");
    request = provider.createRequest(ProviderCatalogOperation::WatchHistory, query);
    QCOMPARE(request.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/history?limit=60&cursor=eyJvIjo1MH0")));
    query.startIndex = 60;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::WatchHistory, query).supported);
    query.startIndex = 0;
    query.parentId = QStringLiteral("lib-1");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::WatchHistory, query).supported);

    // HistoryCard pages keep movie, series and episode cards once each;
    // the cursor says whether there is more.
    ProviderCatalogResponse page = provider.parseResponse(
        ProviderCatalogOperation::WatchHistory,
        QByteArrayLiteral(
            R"({"items":[)"
            R"({"content_id":"series:sev","type":"series","title":"Severance","genres":[],"keywords":[],"status":"matched",)"
            R"("watch":{"media_item_id":"episode:sev-s1e2","watched_at":"2026-10-31T20:00:00.000Z","duration_seconds":3000,"completed":true}},)"
            R"({"content_id":"audiobook:x","type":"audiobook","title":"Book","genres":[],"keywords":[],"status":"matched",)"
            R"("watch":{"media_item_id":"audiobook:x","watched_at":"2026-10-31T19:00:00.000Z","duration_seconds":0,"completed":false}},)"
            R"({"content_id":"movie:heat","type":"movie","title":"Heat","genres":[],"keywords":[],"status":"matched",)"
            R"("watch":{"media_item_id":"movie:heat","watched_at":"2026-10-30T19:00:00.000Z","duration_seconds":10200,"completed":true}},)"
            R"({"content_id":"movie:heat","type":"movie","title":"Heat","genres":[],"keywords":[],"status":"matched",)"
            R"("watch":{"media_item_id":"movie:heat","watched_at":"2026-10-29T19:00:00.000Z","duration_seconds":10200,"completed":true}}],)"
            R"("page":{"has_more":true,"next_cursor":"next-1"}})"));
    QVERIFY(page.valid);
    QCOMPARE(page.rawItems.size(), 2);
    QCOMPARE(page.rawItems.at(0).toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("series:sev"));
    QCOMPARE(page.rawItems.at(1).toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("movie:heat"));
    QVERIFY(page.hasMore);
    QCOMPARE(page.nextCursor, QStringLiteral("next-1"));
    // An empty page may still have more.
    page = provider.parseResponse(
        ProviderCatalogOperation::WatchHistory,
        QByteArrayLiteral(R"({"items":[],"page":{"has_more":true,"next_cursor":"next-2"}})"));
    QVERIFY(page.valid);
    QVERIFY(page.rawItems.isEmpty());
    QVERIFY(page.hasMore);

    // removeHistoryEntries: one target by content identity, never
    // replayed, no artwork widths.
    query = {};
    query.itemId = QStringLiteral("series:sev");
    request = provider.createRequest(ProviderCatalogOperation::RemoveFromWatchHistory, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/history/remove"));
    QCOMPARE(QJsonDocument::fromJson(request.body).object(),
             QJsonObject({{QStringLiteral("targets"),
                           QJsonArray{QJsonObject{{QStringLiteral("content_id"),
                                                   QStringLiteral("series:sev")}}}}}));
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::RemoveFromWatchHistory, {}).valid);
    query.itemId.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::RemoveFromWatchHistory, query).supported);

    // listWatchTonightCards in discover mode, at most 20 cards.
    query = {};
    query.limit = 50;
    request = provider.createRequest(ProviderCatalogOperation::WatchTonight, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(request.relativeEndpoint,
             withArtworkWidths(QStringLiteral(
                 "/api/v2/recommendations/watch-tonight/cards?mode=discover&limit=20")));
    ProviderCatalogResponse picks = provider.parseResponse(
        ProviderCatalogOperation::WatchTonight,
        QByteArrayLiteral(
            R"({"items":[)"
            R"({"content_id":"movie:a","type":"movie","title":"Alpha","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"},)"
            R"({"content_id":"podcast:p","type":"podcast","title":"Pod","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"},)"
            R"({"content_id":"series:b","type":"series","title":"Bravo","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"}],)"
            R"("has_more":true,"paging_limited":false,"is_cold":false})"));
    QVERIFY(picks.valid);
    QCOMPARE(picks.rawItems.size(), 2);
    QVERIFY(!picks.capabilityMetadata.value(QStringLiteral("isCold")).toBool());
    QVERIFY(picks.hasMore);
    picks = provider.parseResponse(
        ProviderCatalogOperation::WatchTonight,
        QByteArrayLiteral(R"({"items":[],"has_more":false,"paging_limited":false,"is_cold":true})"));
    QVERIFY(picks.valid);
    QVERIFY(picks.capabilityMetadata.value(QStringLiteral("isCold")).toBool());
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::WatchTonight,
                                    QByteArrayLiteral(R"({"cards":[]})")).valid);

    // The adapter offers both, and pages history for the whole profile.
    SiloProviderAdapter adapter;
    QVERIFY(adapter.supportsCapability(ProviderCapability::WatchHistoryRemoval));
    QVERIFY(adapter.supportsCapability(ProviderCapability::WatchTonight));
    QVERIFY(!adapter.supportsCapability(ProviderCapability::LibraryScopedHistory));
}

void SiloCatalogServiceTest::watchHistoryServiceReadsPagesAndRemoves()
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

    LibraryService service(&auth);
    QVERIFY(!service.watchHistoryByLibrary());
    QVERIFY(service.supportsWatchHistoryRemoval());
    QSignalSpy loaded(&service, &LibraryService::canonicalWatchHistoryLoaded);
    QSignalSpy failed(&service, &LibraryService::canonicalWatchHistoryFailed);

    manager.responses = {
        { 200,
            QByteArrayLiteral(
                R"({"items":[{"content_id":"movie:heat","type":"movie","title":"Heat","genres":[],"keywords":[],"status":"matched",)"
                R"("watch":{"media_item_id":"movie:heat","watched_at":"2026-10-30T19:00:00.000Z","duration_seconds":10200,"completed":true}}],)"
                R"("page":{"has_more":true,"next_cursor":"next-1"}})"),
            false },
        { 400, QByteArrayLiteral(R"({"type":"invalid_cursor","title":"Invalid cursor","status":400})"), false }
    };
    qsizetype first = manager.requests.size();
    // The library is ignored: Silo has one list for the profile.
    service.getWatchHistory(QStringLiteral("lib-1"), 0, QStringLiteral("cursor-0"), 60,
                            QStringLiteral("key-1"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    const RecordedRequest read = manager.requests.at(first);
    QCOMPARE(read.request.url().path(), QStringLiteral("/api/v2/history"));
    QCOMPARE(QUrlQuery(read.request.url()).queryItemValue(QStringLiteral("cursor")),
             QStringLiteral("cursor-0"));
    QCOMPARE(read.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(loaded.first().at(0).toString(), connectionId);
    QCOMPARE(loaded.first().at(1).toString(), QStringLiteral("key-1"));
    const QVariantList items = loaded.first().at(2).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(items.first().toMap().value(QStringLiteral("connectionId")).toString(), connectionId);
    QVERIFY(loaded.first().at(4).toBool());
    QCOMPARE(loaded.first().at(5).toString(), QStringLiteral("next-1"));

    service.getWatchHistory(QString(), 0, QStringLiteral("next-1"), 60, QStringLiteral("key-2"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("key-2"));
    // The status reaches the page, which starts over on a rejected cursor.
    QCOMPARE(failed.first().at(3).toInt(), 400);

    // Removal: one non_retryable POST, answered as a card write; the
    // server also clears the watched state.
    QSignalSpy finished(&service, &LibraryService::homeCardWriteFinished);
    QSignalSpy played(&service, &LibraryService::itemPlayedStatusChanged);
    QSignalSpy removed(&service, &LibraryService::watchHistoryItemRemoved);
    manager.responses = {
        { 204, QByteArray(), false },
        { 500, QByteArrayLiteral(R"({"type":"about:blank","title":"Error","status":500})"), false }
    };
    first = manager.requests.size();
    service.removeFromWatchHistory(QStringLiteral("remove-1"), QStringLiteral("series:sev"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QVERIFY(finished.first().at(2).toBool());
    const RecordedRequest remove = manager.requests.at(first);
    QCOMPARE(remove.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(remove.request.url().path(), QStringLiteral("/api/v2/history/remove"));
    QCOMPARE(remove.request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(QJsonDocument::fromJson(remove.body).object()
                 .value(QStringLiteral("targets")).toArray().first().toObject()
                 .value(QStringLiteral("content_id")).toString(),
             QStringLiteral("series:sev"));
    QCOMPARE(played.count(), 1);
    QCOMPARE(played.first().at(0).toString(), QStringLiteral("series:sev"));
    QVERIFY(!played.first().at(1).toBool());
    // Home reads Continue Watching and Next Up again on this.
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(0).toString(), connectionId);
    QCOMPARE(removed.first().at(1).toString(), QStringLiteral("series:sev"));

    service.removeFromWatchHistory(QStringLiteral("remove-2"), QStringLiteral("movie:heat"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    QVERIFY(!finished.at(1).at(2).toBool());
    QCOMPARE(played.count(), 1);
    QCOMPARE(removed.count(), 1);
    // A refused removal is never replayed.
    QCOMPARE(manager.requests.size(), first + 2);
}

void SiloCatalogServiceTest::homeShowsWatchTonightFirstUnlessColdOrOff()
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

    auto service = std::make_unique<LibraryService>(&auth);
    QVERIFY(service->supportsWatchTonight());
    QVERIFY(config.getHomeWatchTonightEnabled());
    // The rows last published, loaded or updated, and how often each was.
    QVariantList shown;
    int loads = 0;
    int updates = 0;
    int failures = 0;
    const auto watch = [&](LibraryService *library) {
        connect(library, &LibraryService::canonicalHomeSectionsLoaded, this,
                [&](const QString &, const QVariantList &rows) { shown = rows; ++loads; });
        connect(library, &LibraryService::canonicalHomeSectionsUpdated, this,
                [&](const QString &, const QVariantList &rows) { shown = rows; ++updates; });
        connect(library, &LibraryService::canonicalHomeSectionsFailed, this,
                [&](const QString &, const QString &) { ++failures; });
    };
    watch(service.get());

    const QString cardsPath = kWatchTonightCardsPath;
    const QByteArray picks = QByteArrayLiteral(
        R"({"items":[)"
        R"({"content_id":"movie:pick","type":"movie","title":"Pick","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"},)"
        R"({"content_id":"ebook:e","type":"ebook","title":"Echo","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"}],)"
        R"("has_more":false,"paging_limited":false,"is_cold":false})");
    manager.responsesByPath = {
        {QStringLiteral("/api/v2/home/capabilities"), {200, homeCapabilities(true), false, {}}},
        {QStringLiteral("/api/v2/home/sections"), {200, curatedHomeLayout(true), false, {}}},
        {cardsPath, {200, picks, false, {}}}
    };
    const auto cardsReads = [&manager, &cardsPath](qsizetype from) {
        int count = 0;
        for (qsizetype index = from; index < manager.requests.size(); ++index) {
            const QUrl url = manager.requests.at(index).request.url();
            if (url.path() == cardsPath) {
                ++count;
                if (QUrlQuery(url).queryItemValue(QStringLiteral("mode")) != QStringLiteral("discover")
                    || QUrlQuery(url).queryItemValue(QStringLiteral("limit")) != QStringLiteral("20")
                    || manager.requests.at(index).request.rawHeader("X-Profile-Id") != "profile-1") {
                    return -1;
                }
            }
        }
        return count;
    };
    const auto shownFirst = [&shown]() {
        return shown.isEmpty() ? QString() : rowIds(shown).first();
    };

    // The Watch Tonight row comes first, right after Bloom's own shelves.
    qsizetype first = manager.requests.size();
    service->getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(shownFirst(), LibraryService::WatchTonightRowId, 1000);
    QCOMPARE(cardsReads(first), 1);
    QCOMPARE(rowIds(shown), (QStringList{LibraryService::WatchTonightRowId, QStringLiteral("for-you"),
                                         QStringLiteral("spotlight"), QStringLiteral("classics"),
                                         QStringLiteral("my-list")}));
    QCOMPARE(shown.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Watch Tonight"));
    QCOMPARE(itemIds(shown.first()), QStringList{QStringLiteral("movie:pick")});

    // A refresh that is not a new Home reuses the row; a new Home (or the
    // end of playback) reads it again.
    first = manager.requests.size();
    int before = loads;
    service->getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(loads, before + 1, 1000);
    QCOMPARE(cardsReads(first), 0);
    QCOMPARE(shownFirst(), LibraryService::WatchTonightRowId);

    // A generic fallback (is_cold) or no picks take the row away.
    const QList<ResponsePlan> noRow = {
        {200, QByteArrayLiteral(R"({"items":[{"content_id":"movie:pick","type":"movie","title":"Pick","genres":[],"keywords":[],"status":"matched","cast":[],"watch_tonight_source":"recommendation"}],"has_more":false,"paging_limited":false,"is_cold":true})"), false, {}},
        {200, QByteArrayLiteral(R"({"items":[],"has_more":false,"paging_limited":false,"is_cold":false})"), false, {}}
    };
    for (const ResponsePlan &plan : noRow) {
        manager.responsesByPath.insert(cardsPath, {200, picks, false, {}});
        service->getHomeSections({}, true);
        QTRY_COMPARE_WITH_TIMEOUT(shownFirst(), LibraryService::WatchTonightRowId, 1000);
        manager.responsesByPath.insert(cardsPath, plan);
        first = manager.requests.size();
        service->getHomeSections({}, true);
        QTRY_COMPARE_WITH_TIMEOUT(shownFirst(), QStringLiteral("for-you"), 1000);
        QCOMPARE(cardsReads(first), 1);
    }

    // The layout never waits for Watch Tonight: it publishes at once, and
    // the row is inserted when it lands.
    manager.responsesByPath.insert(cardsPath, {200, picks, true, {}});
    before = loads;
    int updatesBefore = updates;
    service->getHomeSections({}, true);
    QTRY_COMPARE_WITH_TIMEOUT(loads, before + 1, 1000);
    QCOMPARE(shownFirst(), QStringLiteral("for-you"));
    QPointer<ControlledReply> pending;
    for (const QPointer<ControlledReply> &reply : std::as_const(manager.replies)) {
        if (reply && reply->request().url().path() == cardsPath && !reply->isFinished()
            && !reply->wasAborted()) {
            pending = reply;
        }
    }
    QVERIFY(pending);
    pending->complete();
    QTRY_COMPARE_WITH_TIMEOUT(updates, updatesBefore + 1, 1000);
    QCOMPARE(rowIds(shown), (QStringList{LibraryService::WatchTonightRowId, QStringLiteral("for-you"),
                                         QStringLiteral("spotlight"), QStringLiteral("classics"),
                                         QStringLiteral("my-list")}));

    // A failed read keeps the row shown before.
    manager.responsesByPath.insert(
        cardsPath, {500, QByteArrayLiteral(R"({"type":"about:blank","title":"Error","status":500})"), false, {}});
    first = manager.requests.size();
    service->getHomeSections({}, true);
    QTRY_VERIFY_WITH_TIMEOUT(cardsReads(first) >= 1, 1000);
    QTest::qWait(50);
    QCOMPARE(shownFirst(), LibraryService::WatchTonightRowId);

    // A failed layout read still shows the Watch Tonight row: it waits for
    // the read in flight.
    service = std::make_unique<LibraryService>(&auth);
    watch(service.get());
    manager.responsesByPath.insert(cardsPath, {200, picks, true, {}});
    manager.responsesByPath.insert(
        QStringLiteral("/api/v2/home/sections"),
        {404, QByteArrayLiteral(R"({"type":"about:blank","title":"Not Found","status":404})"), false, {}});
    before = loads;
    service->getHomeSections();
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(manager.replies.cbegin(), manager.replies.cend(),
                    [&cardsPath](const QPointer<ControlledReply> &reply) {
                        return reply && reply->request().url().path() == cardsPath
                            && !reply->isFinished() && !reply->wasAborted();
                    }),
        1000);
    QTest::qWait(50);
    QCOMPARE(loads, before);
    for (const QPointer<ControlledReply> &reply : std::as_const(manager.replies)) {
        if (reply && reply->request().url().path() == cardsPath && !reply->isFinished()
            && !reply->wasAborted()) {
            reply->complete();
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(loads, before + 1, 1000);
    QCOMPARE(rowIds(shown), QStringList{LibraryService::WatchTonightRowId});
    const int failuresBefore = failures;

    // Both failing, with no row known, fails the read.
    service = std::make_unique<LibraryService>(&auth);
    watch(service.get());
    manager.responsesByPath.insert(
        cardsPath, {500, QByteArrayLiteral(R"({"type":"about:blank","title":"Error","status":500})"), false, {}});
    service->getHomeSections();
    QTRY_COMPARE_WITH_TIMEOUT(failures, failuresBefore + 1, 10000);

    // With the setting off, no Watch Tonight read is sent.
    config.setHomeWatchTonightEnabled(false);
    manager.responsesByPath.insert(QStringLiteral("/api/v2/home/sections"),
                                   {200, curatedHomeLayout(true), false, {}});
    manager.responsesByPath.insert(cardsPath, {200, picks, false, {}});
    first = manager.requests.size();
    before = loads;
    service->getHomeSections({}, true);
    QTRY_COMPARE_WITH_TIMEOUT(loads, before + 1, 1000);
    QCOMPARE(cardsReads(first), 0);
    QCOMPARE(shownFirst(), QStringLiteral("for-you"));
}

// A collection page's Shuffle shows only for the collection kinds the
// provider can shuffle, and sends the collection as the scope: Silo's
// createShuffle names library_collection or user_collection, Jellyfin reads
// random pages under the box set.
void SiloCatalogServiceTest::collectionShuffleFollowsTheProviderScopeKinds()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;
    {
        ControlledNetworkAccessManager manager;
        HttpTransport transport(&manager);
        SiloProviderAdapter adapter;
        ExposedAuthenticationService auth(&transport, &adapter);
        QVERIFY(signInToSilo(manager, auth, config));
        LibraryService service(&auth);
        QSignalSpy support(&service, &LibraryService::shuffleSupportChanged);
        // Hidden until the profile's capability document arrives.
        QVERIFY(!service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("library")));

        manager.responses = {
            { 200, QByteArrayLiteral(R"({"state":"available","allowed":true,"revision":"r1",)"
                                     R"("scope_kinds":["library","series","season","library_collection"]})"),
              false }
        };
        qsizetype firstRequest = manager.requests.size();
        service.loadShuffleCapability();
        QTRY_COMPARE_WITH_TIMEOUT(support.count(), 1, 1000);
        QCOMPARE(manager.requests.at(firstRequest).request.url().path(),
                 QStringLiteral("/api/v2/shuffles/capabilities"));
        QVERIFY(service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("library")));
        // Not in this server's scope_kinds; box sets are Jellyfin's.
        QVERIFY(!service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("user")));
        QVERIFY(!service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("boxSet")));

        const QList<std::pair<QString, QString>> kinds{
            {QStringLiteral("library"), QStringLiteral("library_collection")},
            {QStringLiteral("user"), QStringLiteral("user_collection")}};
        for (const auto &[collectionKind, wireKind] : kinds) {
            manager.responses = { { 404, QByteArray(), false } };
            firstRequest = manager.requests.size();
            service.createShuffle(QVariantMap{{QStringLiteral("kind"), QStringLiteral("collection")},
                                              {QStringLiteral("id"), QStringLiteral("collection-7")},
                                              {QStringLiteral("collectionKind"), collectionKind}},
                                  QStringLiteral("collection-shuffle"));
            QTRY_VERIFY_WITH_TIMEOUT(manager.requests.size() > firstRequest, 1000);
            const RecordedRequest create = manager.requests.at(firstRequest);
            QCOMPARE(create.operation, QNetworkAccessManager::PostOperation);
            QCOMPARE(create.request.url().path(), QStringLiteral("/api/v2/shuffles"));
            QCOMPARE(QJsonDocument::fromJson(create.body).object(),
                     (QJsonObject{{QStringLiteral("scope"),
                                   QJsonObject{{QStringLiteral("kind"), wireKind},
                                               {QStringLiteral("id"), QStringLiteral("collection-7")}}}}));
        }
    }
    {
        ControlledNetworkAccessManager manager;
        HttpTransport transport(&manager);
        JellyfinProviderAdapter adapter;
        ExposedAuthenticationService auth(&transport, &adapter);
        auth.seedJellyfin();
        QVERIFY(auth.isAuthenticated());
        LibraryService service(&auth);
        QVERIFY(service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("boxSet")));
        QVERIFY(!service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("library")));
        QVERIFY(!service.supportsShuffle(QStringLiteral("collection"), QStringLiteral("user")));

        manager.responses = {
            { 200, QByteArrayLiteral(R"({"Items":[],"TotalRecordCount":0})"), false }
        };
        QSignalSpy loaded(&service, &LibraryService::shuffleLoaded);
        service.getShuffleItems(QVariantMap{{QStringLiteral("kind"), QStringLiteral("collection")},
                                            {QStringLiteral("id"), QStringLiteral("boxset-3")},
                                            {QStringLiteral("collectionKind"), QStringLiteral("boxSet")}},
                                50, QStringLiteral("collection-shuffle"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
        QCOMPARE(manager.requests.size(), 1);
        const QUrlQuery parameters(manager.requests.first().request.url());
        QCOMPARE(parameters.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("boxset-3"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("IncludeItemTypes")),
                 QStringLiteral("Movie,Episode"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("Random"));
        // A box set of series shuffles their episodes, never missing ones.
        QCOMPARE(parameters.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("IsMissing")), QStringLiteral("false"));
    }
}

namespace {

// A movie detail with Silo's extended item artwork.
QJsonObject extendedMovieDetail(const QString &signature)
{
    const auto url = [&signature](const QString &name) {
        return QStringLiteral("https://silo.example.test/api/v2/artwork/%1.webp?sig=%2")
            .arg(name, signature);
    };
    return QJsonObject{
        {QStringLiteral("content_id"), QStringLiteral("movie-1")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Heat")},
        {QStringLiteral("poster_url"), url(QStringLiteral("poster"))},
        {QStringLiteral("backdrop_url"), url(QStringLiteral("backdrop-0"))},
        {QStringLiteral("backdrop_thumbhash"), QStringLiteral("backdrop-hash")},
        {QStringLiteral("backdrop_urls"), QJsonArray{url(QStringLiteral("backdrop-0")),
                                                     url(QStringLiteral("backdrop-1")),
                                                     url(QStringLiteral("backdrop-2"))}},
        {QStringLiteral("thumb_url"), url(QStringLiteral("thumb"))},
        {QStringLiteral("thumb_thumbhash"), QStringLiteral("thumb-hash")},
        {QStringLiteral("banner_url"), url(QStringLiteral("banner"))},
        {QStringLiteral("banner_thumbhash"), QStringLiteral("banner-hash")},
        {QStringLiteral("clearart_url"), url(QStringLiteral("clearart"))},
        {QStringLiteral("disc_url"), url(QStringLiteral("disc"))}};
}

QStringList backdropUrlsOf(const QVariantMap &item)
{
    QStringList urls;
    for (const QVariant &value : Bloom::HeroBackdrops::refs(item)) {
        urls.append(Bloom::ArtworkRef::transientSourceUrlForCacheKey(
            value.toMap().value(QStringLiteral("cacheKey")).toString()));
    }
    return urls;
}

QByteArray imageCapabilities(bool extendedItemArtwork)
{
    QJsonObject document{
        {QStringLiteral("revision"), QStringLiteral("r1")},
        {QStringLiteral("state"), QStringLiteral("available")},
        {QStringLiteral("allowed"), true},
        {QStringLiteral("episode_still_provenance"), true},
        {QStringLiteral("episode_parent_artwork"), true},
        {QStringLiteral("extended_item_artwork"), extendedItemArtwork},
        {QStringLiteral("param"), QStringLiteral("image_size")}};
    return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

} // namespace

void SiloCatalogServiceTest::extendedItemArtworkMapsOnlyWithTheCapability()
{
    const QString connectionId = QStringLiteral("silo");
    const QJsonObject wire = extendedMovieDetail(QStringLiteral("a"));
    const auto artworkUrl = [](const QString &name) {
        return QStringLiteral("https://silo.example.test/api/v2/artwork/%1.webp?sig=a").arg(name);
    };

    // Without the capability the detail maps as before: one backdrop and no
    // extended kinds.
    QVariantMap mapped = SiloModelMapper::mediaDetail(wire, connectionId);
    QCOMPARE(backdropUrlsOf(mapped), QStringList{artworkUrl(QStringLiteral("backdrop-0"))});
    for (const QString &key : {QStringLiteral("thumbArtwork"), QStringLiteral("bannerArtwork"),
                               QStringLiteral("clearArtArtwork"), QStringLiteral("discArtwork")}) {
        QVERIFY2(!mapped.contains(key), qPrintable(key));
    }
    QVERIFY(!mapped.value(QStringLiteral("artworkThumbhashes")).toMap()
                 .contains(QStringLiteral("thumb")));

    // With it every backdrop is listed at its position, backdrop_url first
    // and once.
    mapped = SiloModelMapper::mediaDetail(wire, connectionId, true);
    QCOMPARE(backdropUrlsOf(mapped),
             (QStringList{artworkUrl(QStringLiteral("backdrop-0")),
                          artworkUrl(QStringLiteral("backdrop-1")),
                          artworkUrl(QStringLiteral("backdrop-2"))}));
    QList<int> indexes;
    for (const QVariant &value : mapped.value(QStringLiteral("artwork")).toList()) {
        const QVariantMap ref = value.toMap();
        if (ref.value(QStringLiteral("kind")).toString() == QStringLiteral("backdrop")) {
            indexes.append(ref.value(QStringLiteral("index")).toInt());
            QCOMPARE(ref.value(QStringLiteral("itemId")).toString(),
                     QStringLiteral("movie-1"));
        }
    }
    QCOMPARE(indexes, (QList<int>{0, 1, 2}));
    QCOMPARE(mapped.value(QStringLiteral("backdropArtworkUrl")).toString(),
             artworkUrl(QStringLiteral("backdrop-0")));
    for (const auto &[key, kind, name] : {
             std::tuple{QStringLiteral("thumbArtwork"), QStringLiteral("thumb"),
                        QStringLiteral("thumb")},
             std::tuple{QStringLiteral("bannerArtwork"), QStringLiteral("banner"),
                        QStringLiteral("banner")},
             std::tuple{QStringLiteral("clearArtArtwork"), QStringLiteral("clearArt"),
                        QStringLiteral("clearart")},
             std::tuple{QStringLiteral("discArtwork"), QStringLiteral("disc"),
                        QStringLiteral("disc")}}) {
        const QVariantMap ref = mapped.value(key).toMap();
        QCOMPARE(ref.value(QStringLiteral("kind")).toString(), kind);
        QCOMPARE(ref.value(QStringLiteral("tag")).toString(),
                 SiloModelMapper::artworkTag(artworkUrl(name)));
        QCOMPARE(mapped.value(key + QStringLiteral("Url")).toString(), artworkUrl(name));
        // The canonical kind name round-trips through a cache key.
        QCOMPARE(Bloom::ArtworkRef::fromVariantMap(ref).toVariantMap()
                     .value(QStringLiteral("kind")).toString(), kind);
    }
    const QVariantMap thumbhashes = mapped.value(QStringLiteral("artworkThumbhashes")).toMap();
    QCOMPARE(thumbhashes.value(QStringLiteral("thumb")).toString(), QStringLiteral("thumb-hash"));
    QCOMPARE(thumbhashes.value(QStringLiteral("banner")).toString(), QStringLiteral("banner-hash"));
    QCOMPARE(thumbhashes.value(QStringLiteral("backdrop")).toString(),
             QStringLiteral("backdrop-hash"));

    // A server that sends no backdrop_urls keeps the one backdrop.
    QJsonObject single = wire;
    single.remove(QStringLiteral("backdrop_urls"));
    QCOMPARE(backdropUrlsOf(SiloModelMapper::mediaDetail(single, connectionId, true)),
             QStringList{artworkUrl(QStringLiteral("backdrop-0"))});

    // Seasons and episodes carry none of it; stray fields are ignored.
    QJsonObject episode = wire;
    episode.insert(QStringLiteral("type"), QStringLiteral("episode"));
    mapped = SiloModelMapper::mediaDetail(episode, connectionId, true);
    QCOMPARE(backdropUrlsOf(mapped), QStringList{artworkUrl(QStringLiteral("backdrop-0"))});
    QVERIFY(!mapped.contains(QStringLiteral("thumbArtwork")));

    // The adapter maps a detail page's read with the capability; the plain
    // item mapping stays without it.
    SiloProviderAdapter adapter;
    QCOMPARE(backdropUrlsOf(adapter.mapMediaDetail(wire, connectionId, {.extendedArtwork = true}))
                 .size(),
             qsizetype(3));
    QCOMPARE(backdropUrlsOf(adapter.mapMediaDetail(wire, connectionId, {})).size(), qsizetype(1));
    QCOMPARE(backdropUrlsOf(adapter.mapMediaItem(wire, connectionId)).size(), qsizetype(1));

    // The capability document: the flag alone decides, and older servers
    // leave it out.
    SiloCatalogProvider provider;
    const ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::ImageCapabilities, {});
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/images/capabilities"));
    ProviderCatalogResponse capabilities =
        provider.parseResponse(ProviderCatalogOperation::ImageCapabilities, imageCapabilities(true));
    QVERIFY(capabilities.valid);
    QVERIFY(capabilities.capabilityMetadata.value(QStringLiteral("extendedItemArtwork")).toBool());
    capabilities = provider.parseResponse(
        ProviderCatalogOperation::ImageCapabilities,
        QByteArrayLiteral(R"({"revision":"r1","state":"disabled","allowed":false})"));
    QVERIFY(capabilities.valid);
    QVERIFY(!capabilities.capabilityMetadata.value(QStringLiteral("extendedItemArtwork")).toBool());
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::ImageCapabilities,
                                    QByteArrayLiteral(R"({"sizes":[]})")).valid);
}

// Detail pages read the image capability document once per connection and
// profile, beside the first detail read, and map the extended artwork only
// when it says so.
void SiloCatalogServiceTest::imageCapabilitiesGateDetailArtworkOncePerProfile()
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
    const QString capabilitiesPath = QStringLiteral("/api/v2/images/capabilities");
    const QString itemPath = QStringLiteral("/api/v2/catalog/items/movie-1");
    manager.responsesByPath.insert(
        itemPath,
        {200, QJsonDocument(extendedMovieDetail(QStringLiteral("a"))).toJson(), false, {}});
    const auto capabilityReads = [&manager, &capabilitiesPath](qsizetype from) {
        return static_cast<int>(requestPaths(manager, from).count(capabilitiesPath));
    };

    {
        LibraryService service(&auth);
        QSignalSpy items(&service, qOverload<const QString &, const QVariantMap &>(
                                       &LibraryService::canonicalItemLoaded));
        QSignalSpy series(&service, &LibraryService::canonicalSeriesDetailsLoaded);
        manager.responsesByPath.insert(capabilitiesPath, {200, imageCapabilities(true), false, {}});
        const qsizetype first = manager.requests.size();
        service.getItem(QStringLiteral("movie-1"));
        QTRY_COMPARE_WITH_TIMEOUT(items.count(), 1, 1000);
        QCOMPARE(backdropUrlsOf(items.first().at(1).toMap()).size(), qsizetype(3));
        QVERIFY(items.first().at(1).toMap().contains(QStringLiteral("thumbArtwork")));
        // Both reads went out together.
        QCOMPARE(capabilityReads(first), 1);
        QCOMPARE(requestPaths(manager, first).count(itemPath), qsizetype(1));

        // A later detail page uses the answer it already has.
        service.getSeriesDetails(QStringLiteral("movie-1"));
        QTRY_COMPARE_WITH_TIMEOUT(series.count(), 1, 1000);
        QCOMPARE(backdropUrlsOf(series.first().at(2).toMap()).size(), qsizetype(3));
        QCOMPARE(capabilityReads(first), 1);
    }

    // A server that does not announce it, or has no such document, maps
    // the one backdrop.
    for (const ResponsePlan &plan :
         {ResponsePlan{200, imageCapabilities(false), false, {}},
          ResponsePlan{404, QByteArrayLiteral(R"({"type":"about:blank","status":404})"), false, {}}}) {
        LibraryService service(&auth);
        QSignalSpy items(&service, qOverload<const QString &, const QVariantMap &>(
                                       &LibraryService::canonicalItemLoaded));
        manager.responsesByPath.insert(capabilitiesPath, plan);
        const qsizetype first = manager.requests.size();
        service.getItem(QStringLiteral("movie-1"));
        QTRY_COMPARE_WITH_TIMEOUT(items.count(), 1, 1000);
        QCOMPARE(backdropUrlsOf(items.first().at(1).toMap()).size(), qsizetype(1));
        QVERIFY(!items.first().at(1).toMap().contains(QStringLiteral("thumbArtwork")));
        QCOMPARE(capabilityReads(first), 1);
        service.getItem(QStringLiteral("movie-1"));
        QTRY_COMPARE_WITH_TIMEOUT(items.count(), 2, 1000);
        QCOMPARE(capabilityReads(first), 1);
    }
}

void SiloCatalogServiceTest::extendedBackdropRefreshFindsItsPosition()
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

    const QVariantList refs = Bloom::HeroBackdrops::refs(
        SiloModelMapper::mediaDetail(extendedMovieDetail(QStringLiteral("old")), connectionId, true));
    QCOMPARE(refs.size(), qsizetype(3));
    const QVariantMap thumb = SiloModelMapper::mediaDetail(
        extendedMovieDetail(QStringLiteral("old")), connectionId, true)
        .value(QStringLiteral("thumbArtwork")).toMap();

    const auto refresh = [&](const QVariantMap &ref, const QJsonObject &detail) {
        manager.responses = {{200, QJsonDocument(detail).toJson(), false}};
        const qsizetype first = manager.requests.size();
        SiloArtworkProvider provider(&auth);
        std::optional<QNetworkRequest> refreshed;
        bool finished = false;
        provider.refreshArtwork(Bloom::ArtworkRef::fromVariantMap(ref),
                                [&](std::optional<QNetworkRequest> request) {
                                    refreshed = std::move(request);
                                    finished = true;
                                });
        if (!QTest::qWaitFor([&finished] { return finished; }, 1000)
            || manager.requests.size() != first + 1
            || manager.requests.at(first).request.url().path()
                != QStringLiteral("/api/v2/catalog/items/movie-1")) {
            return QStringLiteral("unexpected refresh read");
        }
        return refreshed.has_value() ? refreshed->url().toString() : QString();
    };
    const QString renewed = QStringLiteral(
        "https://silo.example.test/api/v2/artwork/%1.webp?sig=new");

    // Each backdrop renews to the same image under its new signature.
    QCOMPARE(refresh(refs.at(0).toMap(), extendedMovieDetail(QStringLiteral("new"))),
             renewed.arg(QStringLiteral("backdrop-0")));
    QCOMPARE(refresh(refs.at(2).toMap(), extendedMovieDetail(QStringLiteral("new"))),
             renewed.arg(QStringLiteral("backdrop-2")));
    QCOMPARE(refresh(thumb, extendedMovieDetail(QStringLiteral("new"))),
             renewed.arg(QStringLiteral("thumb")));

    // Reordered on the server: the path still finds it.
    QJsonObject reordered = extendedMovieDetail(QStringLiteral("new"));
    reordered.insert(QStringLiteral("backdrop_urls"),
                     QJsonArray{renewed.arg(QStringLiteral("backdrop-0")),
                                renewed.arg(QStringLiteral("backdrop-2")),
                                renewed.arg(QStringLiteral("backdrop-1"))});
    QCOMPARE(refresh(refs.at(2).toMap(), reordered), renewed.arg(QStringLiteral("backdrop-2")));

    // Replaced artwork falls back to the same position; a position that is
    // gone resolves nothing.
    QJsonObject replaced = extendedMovieDetail(QStringLiteral("new"));
    replaced.insert(QStringLiteral("backdrop_urls"),
                    QJsonArray{renewed.arg(QStringLiteral("backdrop-0")),
                               renewed.arg(QStringLiteral("fresh-1"))});
    QCOMPARE(refresh(refs.at(1).toMap(), replaced), renewed.arg(QStringLiteral("fresh-1")));
    QCOMPARE(refresh(refs.at(2).toMap(), replaced), QString());
}

QTEST_MAIN(SiloCatalogServiceTest)
#include "SiloCatalogServiceTest.moc"
