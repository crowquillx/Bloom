#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "../src/network/LibraryService.h"
#include "../src/network/MediaRequestService.h"
#include "../src/network/SeerrService.h"
#include "../src/providers/jellyfin/JellyfinCatalogProvider.h"
#include "../src/providers/silo/SiloCatalogProvider.h"
#include "../src/providers/silo/SiloMediaRequestMapper.h"
#include "../src/utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

// Answers media request operations from the test instead of the network.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct Call {
        ProviderCatalogOperation operation;
        ProviderCatalogQuery query;
        MediaRequestHandler onLoaded;
        MediaRequestFailureHandler onFailed;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    bool supportsMediaRequests() const override { return silo; }
    void sendMediaRequestOperation(ProviderCatalogOperation operation,
                                   const ProviderCatalogQuery &query,
                                   MediaRequestHandler onLoaded,
                                   MediaRequestFailureHandler onFailed) override
    {
        calls.append({operation, query, std::move(onLoaded), std::move(onFailed)});
    }

    int count(ProviderCatalogOperation operation) const
    {
        int total = 0;
        for (const Call &call : calls) {
            total += call.operation == operation ? 1 : 0;
        }
        return total;
    }

    // Answers and removes the oldest call for the operation.
    void answer(ProviderCatalogOperation operation, const QVariantMap &result)
    {
        for (qsizetype i = 0; i < calls.size(); ++i) {
            if (calls.at(i).operation == operation) {
                const Call call = calls.takeAt(i);
                call.onLoaded(result);
                return;
            }
        }
        QFAIL("No pending call for the operation");
    }

    Call last(ProviderCatalogOperation operation) const
    {
        for (qsizetype i = calls.size() - 1; i >= 0; --i) {
            if (calls.at(i).operation == operation) {
                return calls.at(i);
            }
        }
        return {};
    }

    bool silo = true;
    QList<Call> calls;
};

QVariantMap allowedStatus(bool available = true, bool extras = false)
{
    return {{QStringLiteral("available"), available},
            {QStringLiteral("seasonRequestsSupported"), true},
            {QStringLiteral("followSupported"), extras},
            {QStringLiteral("watchlistTitlesSupported"), extras},
            {QStringLiteral("watchlistRequests"), extras}};
}

QVariantMap discoveryItem(const QString &mediaType, qint64 tmdbId, const QString &libraryItemId = {})
{
    QVariantMap item{
        {QStringLiteral("source"), QStringLiteral("request")},
        {QStringLiteral("requestMediaType"), mediaType},
        {QStringLiteral("requestTmdbId"), tmdbId},
        {QStringLiteral("itemId"), QStringLiteral("request:%1:%2").arg(mediaType).arg(tmdbId)},
        {QStringLiteral("name"), QStringLiteral("Title %1").arg(tmdbId)},
    };
    if (!libraryItemId.isEmpty()) {
        item.insert(QStringLiteral("libraryItemId"), libraryItemId);
    }
    return item;
}

QJsonObject wireResult(const QString &mediaType, qint64 tmdbId, const QString &availability)
{
    return QJsonObject{
        {"media_type", mediaType},
        {"tmdb_id", tmdbId},
        {"title", QStringLiteral("Wire %1").arg(tmdbId)},
        {"availability", availability},
        {"in_watchlist", false},
        {"poster_path", "/poster.jpg"},
        {"backdrop_path", "/backdrop.jpg"},
        {"release_date", "2021-03-04"},
        {"library_content_id", availability == "available" ? "content-9" : ""},
        {"request", QJsonObject{{"requestable", availability != "available"},
                                {"following", false},
                                {"requested_by_viewer", false},
                                {"state", "pending"}}},
    };
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class MediaRequestServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Silo wire boundary
    void siloRequestRoutesAreProfileScopedAndSafelyRetried();
    void siloSearchEncodesTheTermAndPage();
    void siloCreateBodyCarriesTitleAndSeasons();
    void siloResultsMapToDiscoveryItems();
    void siloDetailMapsSeasonsAndReason();
    void siloStatusRequiresEnabledAllowedAndAvailable();
    void siloDiscoverRowsPageAndDropEmptyRows();
    void siloRequestManagementRoutes();
    void siloBrowseRoutes();
    void siloRequestsMapWithCancelRules();
    void siloBrowseResponsesMap();
    void jellyfinHasNoMediaRequests();
    void libraryServiceAcceptsEveryRequestOperation();
    void siloPartialStateUsesThePartialStatus();

    // Routing
    void siloServesRequestsWithoutSeerr();
    void seerrTakesPriorityOverSilo();
    void siloStaysHiddenWhenTheViewerMayNotRequest();
    void siloSearchAndSimilarLeaveOutLibraryTitles();
    void siloCreateRequestUsesThePreparedTitle();
    void viewerChangeAbandonsPendingSiloWork();
    void siloBrowseCategoriesTagKindsAndMediaTypes();
    void siloBrowseAndRequestListRoute();
    void siloFollowAndWatchlistNeedTheirFeatures();
    void backendSwitchAbandonsTheOldBackendsWork();

private:
    std::unique_ptr<MediaRequestService> makeService();

    QTemporaryDir m_configDirectory;
    std::unique_ptr<ScopedConfigIsolation> m_configIsolation;
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<SeerrService> m_seerr;
    std::unique_ptr<ScriptedLibraryService> m_library;
};

void MediaRequestServiceTest::init()
{
    QVERIFY(m_configDirectory.isValid());
    m_configIsolation = std::make_unique<ScopedConfigIsolation>(m_configDirectory.path());
    m_config = std::make_unique<ConfigManager>();
    m_seerr = std::make_unique<SeerrService>(nullptr, m_config.get());
    m_library = std::make_unique<ScriptedLibraryService>();
}

void MediaRequestServiceTest::cleanup()
{
    m_library.reset();
    m_seerr.reset();
    m_config.reset();
    m_configIsolation.reset();
}

std::unique_ptr<MediaRequestService> MediaRequestServiceTest::makeService()
{
    return std::make_unique<MediaRequestService>(nullptr, m_library.get(), m_seerr.get());
}

void MediaRequestServiceTest::siloRequestRoutesAreProfileScopedAndSafelyRetried()
{
    const SiloCatalogProvider provider;
    ProviderCatalogQuery query;

    const auto status = provider.createRequest(ProviderCatalogOperation::MediaRequestStatus, query);
    QVERIFY(status.supported);
    QCOMPARE(status.relativeEndpoint, QStringLiteral("/api/v2/requests/status"));
    QCOMPARE(status.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    query.requestMediaType = QStringLiteral("tv");
    query.tmdbId = 1399;
    const auto detail = provider.createRequest(ProviderCatalogOperation::MediaRequestDetail, query);
    QCOMPARE(detail.relativeEndpoint, QStringLiteral("/api/v2/requests/detail/series/1399"));

    query.requestMedia = {{QStringLiteral("name"), QStringLiteral("Show")}};
    const auto create = provider.createRequest(ProviderCatalogOperation::CreateMediaRequest, query);
    QVERIFY(create.supported);
    QCOMPARE(create.method, ProviderHttpMethod::Post);
    QCOMPARE(create.relativeEndpoint, QStringLiteral("/api/v2/requests"));
    // createRequest is non_retryable: a replay could file a second request.
    QCOMPARE(create.retrySafety, ProviderCatalogRetrySafety::Never);

    const auto discover = provider.createRequest(ProviderCatalogOperation::MediaRequestDiscover, {});
    QCOMPARE(discover.relativeEndpoint, QStringLiteral("/api/v2/requests/discover"));

    ProviderCatalogQuery row;
    row.sectionKey = QStringLiteral("trending_series");
    row.page = 3;
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::MediaRequestDiscoverSection, row)
                 .relativeEndpoint,
             QStringLiteral("/api/v2/requests/discover/trending_series?page=3"));
    row.sectionKey = QStringLiteral("../admin");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::MediaRequestDiscoverSection, row)
                 .supported);

    // Request routes never carry the catalog artwork width parameters.
    QVERIFY(!detail.relativeEndpoint.contains(QStringLiteral("poster_width")));
    QVERIFY(!discover.relativeEndpoint.contains(QStringLiteral("poster_width")));
}

void MediaRequestServiceTest::siloSearchEncodesTheTermAndPage()
{
    const SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.searchTerm = QStringLiteral("  C++ & co  ");
    query.page = 2;
    const auto request = provider.createRequest(ProviderCatalogOperation::MediaRequestSearch, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/requests/search?q=C%2B%2B%20%26%20co&page=2"));

    query.searchTerm = QStringLiteral("   ");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::MediaRequestSearch, query).supported);
}

void MediaRequestServiceTest::siloCreateBodyCarriesTitleAndSeasons()
{
    const QVariantMap media{
        {QStringLiteral("name"), QStringLiteral("Severance")},
        {QStringLiteral("productionYear"), 2022},
        {QStringLiteral("overview"), QStringLiteral("Work.")},
        {QStringLiteral("posterPath"), QStringLiteral("/p.jpg")},
    };
    const QJsonObject series = SiloMediaRequestMapper::createBody(
        QStringLiteral("tv"), 95396, media, {2, 0, 2, 3});
    QCOMPARE(series.value("media_type").toString(), QStringLiteral("series"));
    QCOMPARE(series.value("tmdb_id").toInteger(), qint64(95396));
    QCOMPARE(series.value("title").toString(), QStringLiteral("Severance"));
    QCOMPARE(series.value("year").toInt(), 2022);
    QCOMPARE(series.value("poster_path").toString(), QStringLiteral("/p.jpg"));
    QVERIFY(!series.contains("backdrop_path"));
    // Seasons below 1 are refused by Silo; duplicates collapse.
    QCOMPARE(series.value("seasons").toArray(), (QJsonArray{2, 3}));

    // No seasons: Silo requests every aired season the library lacks.
    QVERIFY(!SiloMediaRequestMapper::createBody(QStringLiteral("tv"), 95396, media, {})
                 .contains("seasons"));
    // Movies never send seasons.
    QVERIFY(!SiloMediaRequestMapper::createBody(QStringLiteral("movie"), 5, media, {1})
                 .contains("seasons"));
    // The title is required.
    QVERIFY(SiloMediaRequestMapper::createBody(QStringLiteral("movie"), 5, {}, {}).isEmpty());
    QVERIFY(SiloMediaRequestMapper::createBody(QStringLiteral("music"), 5, media, {}).isEmpty());
}

void MediaRequestServiceTest::siloResultsMapToDiscoveryItems()
{
    const QJsonObject page{
        {"page", 1}, {"total_pages", 4}, {"total_results", 70},
        {"results", QJsonArray{wireResult("series", 7, "missing"),
                               wireResult("movie", 8, "available"),
                               wireResult("series", 7, "missing"),
                               wireResult("episode", 9, "missing")}},
    };
    const ProviderCatalogResponse response = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::MediaRequestSearch, json(page));
    QVERIFY(response.valid);
    const QVariantList items = response.capabilityMetadata.value("items").toList();
    // Duplicates and non-video types are dropped.
    QCOMPARE(items.size(), 2);

    const QVariantMap series = items.at(0).toMap();
    QCOMPARE(series.value("source").toString(), QStringLiteral("request"));
    QCOMPARE(series.value("requestBackend").toString(), QStringLiteral("silo"));
    QCOMPARE(series.value("requestMediaType").toString(), QStringLiteral("tv"));
    QCOMPARE(series.value("requestTmdbId").toLongLong(), qlonglong(7));
    QCOMPARE(series.value("itemId").toString(), QStringLiteral("request:tv:7"));
    QCOMPARE(series.value("mediaType").toString(), QStringLiteral("Series"));
    QCOMPARE(series.value("productionYear").toInt(), 2021);
    QCOMPARE(series.value("imageUrl").toString(),
             QStringLiteral("https://image.tmdb.org/t/p/w342/poster.jpg"));
    QCOMPARE(series.value("backdropUrl").toString(),
             QStringLiteral("https://image.tmdb.org/t/p/w1280/backdrop.jpg"));
    QCOMPARE(series.value("requestStatus").toString(), QStringLiteral("pending"));
    QVERIFY(series.value("requestable").toBool());
    QVERIFY(!series.contains("libraryItemId"));

    const QVariantMap movie = items.at(1).toMap();
    QCOMPARE(movie.value("requestStatus").toString(), QStringLiteral("available"));
    QCOMPARE(movie.value("libraryItemId").toString(), QStringLiteral("content-9"));
    QVERIFY(!movie.value("requestable").toBool());

    QVERIFY(!SiloCatalogProvider()
                 .parseResponse(ProviderCatalogOperation::MediaRequestSearch, "[]")
                 .valid);
}

void MediaRequestServiceTest::siloDetailMapsSeasonsAndReason()
{
    QJsonObject wire = wireResult("series", 1399, "missing");
    wire.insert("number_of_seasons", 3);
    wire.insert("request", QJsonObject{{"requestable", false},
                                       {"reason", "already_requested"},
                                       {"state", "approved"},
                                       {"requested_by_viewer", true},
                                       {"following", true}});
    wire.insert("seasons", QJsonArray{
        QJsonObject{{"season_number", 1}, {"episode_count", 10}, {"availability", "available"}, {"requested", false}},
        QJsonObject{{"season_number", 2}, {"episode_count", 10}, {"availability", "missing"}, {"requested", true}},
        QJsonObject{{"season_number", 3}, {"episode_count", 8}, {"availability", "missing"}, {"requested", false}},
    });
    wire.insert("recommendations", QJsonArray{wireResult("series", 1400, "missing")});

    const ProviderCatalogResponse response = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::MediaRequestDetail, json(wire));
    QVERIFY(response.valid);
    const QVariantMap detail = response.capabilityMetadata.value("detail").toMap();
    QVERIFY(!detail.value("requestable").toBool());
    QCOMPARE(detail.value("notRequestableReason").toString(), QStringLiteral("already_requested"));
    QCOMPARE(detail.value("requestState").toString(), QStringLiteral("approved"));
    QVERIFY(detail.value("requestedByViewer").toBool());
    QCOMPARE(detail.value("seasonCount").toInt(), 3);
    const QVariantList seasons = detail.value("seasons").toList();
    QCOMPARE(seasons.size(), 3);
    QCOMPARE(seasons.at(0).toMap().value("availability").toString(), QStringLiteral("available"));
    QVERIFY(seasons.at(1).toMap().value("requested").toBool());
    QCOMPARE(detail.value("recommendations").toList().size(), 1);
}

void MediaRequestServiceTest::siloStatusRequiresEnabledAllowedAndAvailable()
{
    const auto features = [](bool enabled, bool allowed, const QString &state) {
        return SiloMediaRequestMapper::features(QJsonObject{
            {"requests_enabled", enabled}, {"allowed", allowed}, {"state", state},
            {"season_requests_supported", true}});
    };
    QVERIFY(features(true, true, "available").value("available").toBool());
    QVERIFY(!features(false, true, "available").value("available").toBool());
    QVERIFY(!features(true, false, "available").value("available").toBool());
    QVERIFY(!features(true, true, "not_configured").value("available").toBool());
    QVERIFY(features(true, true, "available").value("seasonRequestsSupported").toBool());

    QVERIFY(!SiloCatalogProvider()
                 .parseResponse(ProviderCatalogOperation::MediaRequestStatus, R"({"state":"available"})")
                 .valid);
}

void MediaRequestServiceTest::siloDiscoverRowsPageAndDropEmptyRows()
{
    const QJsonObject collection{{"items", QJsonArray{
        QJsonObject{{"key", "trending_series"}, {"title", "Trending Shows"}, {"page", 1},
                    {"total_pages", 5}, {"total_results", 90}, {"next_page", 3},
                    {"results", QJsonArray{wireResult("series", 1, "missing")}}},
        QJsonObject{{"key", "upcoming_movies"}, {"title", "Upcoming"}, {"page", 1},
                    {"total_pages", 1}, {"total_results", 0}, {"results", QJsonArray{}}},
    }}};
    const ProviderCatalogResponse response = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::MediaRequestDiscover, json(collection));
    QVERIFY(response.valid);
    const QVariantList rows = response.capabilityMetadata.value("rows").toList();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value("key").toString(), QStringLiteral("trending_series"));
    QCOMPARE(row.value("title").toString(), QStringLiteral("Trending Shows"));
    // Rating backfill consumed page 2: continue from next_page.
    QCOMPARE(row.value("nextPage").toInt(), 3);
    QVERIFY(row.value("hasMore").toBool());

    const QJsonObject last{{"key", "popular_movies"}, {"title", "Popular"}, {"page", 4},
                           {"total_pages", 4}, {"total_results", 80},
                           {"results", QJsonArray{wireResult("movie", 2, "missing")}}};
    const QVariantMap lastRow = SiloCatalogProvider()
        .parseResponse(ProviderCatalogOperation::MediaRequestDiscoverSection, json(last))
        .capabilityMetadata.value("row").toMap();
    QCOMPARE(lastRow.value("nextPage").toInt(), 5);
    QVERIFY(!lastRow.value("hasMore").toBool());
}

void MediaRequestServiceTest::siloRequestManagementRoutes()
{
    const SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.cursor = QStringLiteral("next 1");
    const auto list = provider.createRequest(ProviderCatalogOperation::MediaRequestList, query);
    QVERIFY(list.supported);
    QCOMPARE(list.relativeEndpoint, QStringLiteral("/api/v2/requests/mine?limit=50&cursor=next%201"));
    QCOMPARE(list.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    ProviderCatalogQuery cancel;
    cancel.itemId = QStringLiteral("req/7");
    const auto cancelRequest = provider.createRequest(ProviderCatalogOperation::CancelMediaRequest, cancel);
    QCOMPARE(cancelRequest.method, ProviderHttpMethod::Post);
    QCOMPARE(cancelRequest.relativeEndpoint, QStringLiteral("/api/v2/requests/req%2F7/cancel"));
    QCOMPARE(cancelRequest.body, QByteArrayLiteral("{}"));
    // cancelRequest is non_retryable.
    QCOMPARE(cancelRequest.retrySafety, ProviderCatalogRetrySafety::Never);
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::CancelMediaRequest, {}).supported);

    ProviderCatalogQuery title;
    title.requestMediaType = QStringLiteral("tv");
    title.tmdbId = 1399;
    title.stateValue = true;
    const auto follow = provider.createRequest(ProviderCatalogOperation::SetMediaRequestFollow, title);
    QCOMPARE(follow.method, ProviderHttpMethod::Put);
    QCOMPARE(follow.relativeEndpoint, QStringLiteral("/api/v2/requests/follows/series/1399"));
    // Following is natural_idempotent.
    QCOMPARE(follow.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    const auto watchlist = provider.createRequest(ProviderCatalogOperation::SetWatchlistTitle, title);
    QCOMPARE(watchlist.method, ProviderHttpMethod::Put);
    QCOMPARE(watchlist.relativeEndpoint, QStringLiteral("/api/v2/watchlist/titles/series/1399"));
    // Watchlist title writes can request the title: never replayed.
    QCOMPARE(watchlist.retrySafety, ProviderCatalogRetrySafety::Never);
    title.stateValue = false;
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::SetMediaRequestFollow, title).method,
             ProviderHttpMethod::Delete);
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::SetWatchlistTitle, title).method,
             ProviderHttpMethod::Delete);
    title.requestMediaType = QStringLiteral("book");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetWatchlistTitle, title).supported);

    // Removing answers 204; adding answers a document Bloom does not need.
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetMediaRequestFollow, "").valid);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetWatchlistTitle, "{}").valid);
}

void MediaRequestServiceTest::siloBrowseRoutes()
{
    const SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.browseKind = QStringLiteral("network");
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::MediaRequestBrowseCategories, query)
                 .relativeEndpoint,
             QStringLiteral("/api/v2/requests/discover/networks"));
    query.browseKind = QStringLiteral("people");
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::MediaRequestBrowseCategories, query)
                 .supported);

    ProviderCatalogQuery genre;
    genre.browseKind = QStringLiteral("genre");
    genre.browseId = QStringLiteral("sci-fi");
    genre.requestMediaType = QStringLiteral("tv");
    genre.browseSort = QStringLiteral("vote_average");
    genre.page = 2;
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::MediaRequestBrowse, genre).relativeEndpoint,
             QStringLiteral("/api/v2/requests/discover/browse/genre/sci-fi?media_type=series&sort=vote_average&page=2"));
    // A genre needs a media type.
    genre.requestMediaType.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::MediaRequestBrowse, genre).supported);

    // Studios and networks take no media type; an unknown sort is left out.
    ProviderCatalogQuery studio;
    studio.browseKind = QStringLiteral("studio");
    studio.browseId = QStringLiteral("a24");
    studio.requestMediaType = QStringLiteral("movie");
    studio.browseSort = QStringLiteral("random");
    QCOMPARE(provider.createRequest(ProviderCatalogOperation::MediaRequestBrowse, studio).relativeEndpoint,
             QStringLiteral("/api/v2/requests/discover/browse/studio/a24"));
    studio.browseId.clear();
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::MediaRequestBrowse, studio).supported);
}

void MediaRequestServiceTest::siloRequestsMapWithCancelRules()
{
    const auto wireRequest = [](const QString &id, const QString &state, const QString &outcome) {
        return QJsonObject{
            {"id", id}, {"provider", "silo"}, {"media_type", "series"}, {"tmdb_id", 1399},
            {"title", "Show"}, {"status", "pending"}, {"outcome", outcome}, {"state", state},
            {"seasons", QJsonArray{1, 2}}, {"season_progress", QJsonArray{}}, {"source", "direct"},
            {"is_anime", false}, {"targets", QJsonArray{}}, {"year", 2011},
            {"poster_path", "/p.jpg"}, {"created_at", "2026-10-01T00:00:00.000Z"},
            {"updated_at", "2026-10-01T00:00:00.000Z"},
        };
    };
    const QJsonObject collection{
        {"items", QJsonArray{wireRequest("a", "pending", "active"),
                             wireRequest("b", "processing", "active"),
                             wireRequest("c", "partially_available", "active"),
                             wireRequest("d", "cancelled", "cancelled")}},
        {"page", QJsonObject{{"has_more", true}, {"next_cursor", "cursor-2"}}},
    };
    const ProviderCatalogResponse response = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::MediaRequestList, json(collection));
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value("hasMore").toBool());
    QCOMPARE(response.capabilityMetadata.value("nextCursor").toString(), QStringLiteral("cursor-2"));
    const QVariantList items = response.capabilityMetadata.value("items").toList();
    QCOMPARE(items.size(), 4);

    const QVariantMap pending = items.at(0).toMap();
    QCOMPARE(pending.value("requestId").toString(), QStringLiteral("a"));
    QCOMPARE(pending.value("requestMediaType").toString(), QStringLiteral("tv"));
    QCOMPARE(pending.value("name").toString(), QStringLiteral("Show"));
    QCOMPARE(pending.value("productionYear").toInt(), 2011);
    QCOMPARE(pending.value("requestedSeasons").toList(), (QVariantList{1, 2}));
    QVERIFY(pending.value("canCancel").toBool());
    // Downloads have started: Bloom does not offer withdrawing.
    QVERIFY(!items.at(1).toMap().value("canCancel").toBool());
    QCOMPARE(items.at(2).toMap().value("requestStatus").toString(), QStringLiteral("partial"));
    QVERIFY(!items.at(3).toMap().value("canCancel").toBool());
    QCOMPARE(items.at(3).toMap().value("requestStatus").toString(), QStringLiteral("cancelled"));

    const ProviderCatalogResponse cancelled = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::CancelMediaRequest, json(wireRequest("a", "cancelled", "cancelled")));
    QVERIFY(cancelled.valid);

    // The title detail says who requested it, whether the viewer follows
    // it, and whether it is on the watchlist.
    QJsonObject wire = wireResult("series", 1399, "missing");
    wire.insert("in_watchlist", true);
    wire.insert("request", QJsonObject{{"requestable", false}, {"reason", "already_requested"},
                                       {"state", "pending"}, {"request_id", "req-1"},
                                       {"requested_by_viewer", false}, {"following", true}});
    const QVariantMap detail = SiloMediaRequestMapper::detail(wire);
    QVERIFY(detail.value("following").toBool());
    QVERIFY(detail.value("inWatchlist").toBool());
    QCOMPARE(detail.value("activeRequestId").toString(), QStringLiteral("req-1"));

    const QVariantMap features = SiloMediaRequestMapper::features(QJsonObject{
        {"requests_enabled", true}, {"allowed", true}, {"state", "available"},
        {"follow_supported", true}, {"watchlist_titles_supported", true}, {"watchlist_requests", false}});
    QVERIFY(features.value("followSupported").toBool());
    QVERIFY(features.value("watchlistTitlesSupported").toBool());
    QVERIFY(!features.value("watchlistRequests").toBool());
}

void MediaRequestServiceTest::siloBrowseResponsesMap()
{
    const QJsonObject brands{{"items", QJsonArray{
        QJsonObject{{"slug", "netflix"}, {"display_name", "Netflix"},
                    {"logo_url", "https://img.test/n.png"}, {"series_supported", true}},
        QJsonObject{{"slug", ""}, {"display_name", "Nameless"}},
    }}};
    const QVariantList categories = SiloCatalogProvider()
        .parseResponse(ProviderCatalogOperation::MediaRequestBrowseCategories, json(brands))
        .capabilityMetadata.value("items").toList();
    QCOMPARE(categories.size(), 1);
    const QVariantMap netflix = categories.first().toMap();
    QCOMPARE(netflix.value("id").toString(), QStringLiteral("netflix"));
    QCOMPARE(netflix.value("name").toString(), QStringLiteral("Netflix"));
    QCOMPARE(netflix.value("logoUrl").toString(), QStringLiteral("https://img.test/n.png"));
    QVERIFY(netflix.value("seriesSupported").toBool());

    const QJsonObject page{{"kind", "network"}, {"slug", "netflix"}, {"display_name", "Netflix"},
                           {"media_type", "series"}, {"sort", "popularity"}, {"page", 2},
                           {"total_pages", 3}, {"results", QJsonArray{wireResult("series", 5, "missing")}}};
    const ProviderCatalogResponse response = SiloCatalogProvider().parseResponse(
        ProviderCatalogOperation::MediaRequestBrowse, json(page));
    QVERIFY(response.valid);
    const QVariantMap row = response.capabilityMetadata.value("row").toMap();
    QCOMPARE(row.value("items").toList().size(), 1);
    QCOMPARE(row.value("page").toInt(), 2);
    QCOMPARE(row.value("nextPage").toInt(), 3);
    QVERIFY(row.value("hasMore").toBool());
}

void MediaRequestServiceTest::jellyfinHasNoMediaRequests()
{
    const JellyfinCatalogProvider provider;
    for (const ProviderCatalogOperation operation : {
             ProviderCatalogOperation::MediaRequestStatus,
             ProviderCatalogOperation::MediaRequestSearch,
             ProviderCatalogOperation::MediaRequestDetail,
             ProviderCatalogOperation::CreateMediaRequest,
             ProviderCatalogOperation::MediaRequestDiscover,
             ProviderCatalogOperation::MediaRequestDiscoverSection,
             ProviderCatalogOperation::MediaRequestList,
             ProviderCatalogOperation::CancelMediaRequest,
             ProviderCatalogOperation::SetMediaRequestFollow,
             ProviderCatalogOperation::SetWatchlistTitle,
             ProviderCatalogOperation::MediaRequestBrowseCategories,
             ProviderCatalogOperation::MediaRequestBrowse}) {
        QVERIFY(!provider.createRequest(operation, {}).supported);
    }
}

void MediaRequestServiceTest::libraryServiceAcceptsEveryRequestOperation()
{
    // The real LibraryService, not the scripted one: every request operation
    // must get past its operation check. Without a Silo session each one then
    // fails on support instead.
    LibraryService library(nullptr);
    const QString unsupported = QStringLiteral("This server does not take media requests.");
    for (const ProviderCatalogOperation operation : {
             ProviderCatalogOperation::MediaRequestStatus,
             ProviderCatalogOperation::MediaRequestSearch,
             ProviderCatalogOperation::MediaRequestDetail,
             ProviderCatalogOperation::CreateMediaRequest,
             ProviderCatalogOperation::MediaRequestDiscover,
             ProviderCatalogOperation::MediaRequestDiscoverSection,
             ProviderCatalogOperation::MediaRequestList,
             ProviderCatalogOperation::CancelMediaRequest,
             ProviderCatalogOperation::SetMediaRequestFollow,
             ProviderCatalogOperation::SetWatchlistTitle,
             ProviderCatalogOperation::MediaRequestBrowseCategories,
             ProviderCatalogOperation::MediaRequestBrowse}) {
        QString error;
        library.sendMediaRequestOperation(operation, {}, {}, [&error](const QString &message) {
            error = message;
        });
        QCOMPARE(error, unsupported);
    }

    QString error;
    library.sendMediaRequestOperation(ProviderCatalogOperation::Views, {}, {},
                                      [&error](const QString &message) { error = message; });
    QCOMPARE(error, QStringLiteral("Not a media request operation."));
}

void MediaRequestServiceTest::siloPartialStateUsesThePartialStatus()
{
    QJsonObject wire = wireResult("series", 7, "missing");
    wire.insert("request", QJsonObject{{"requestable", false}, {"state", "partially_available"},
                                       {"following", false}, {"requested_by_viewer", true}});
    const QVariantMap item = SiloMediaRequestMapper::result(wire);
    QCOMPARE(item.value("requestStatus").toString(), QStringLiteral("partial"));
}

void MediaRequestServiceTest::siloServesRequestsWithoutSeerr()
{
    auto service = makeService();
    QSignalSpy availability(service.get(), &MediaRequestService::availabilityChanged);
    QCOMPARE(m_library->count(ProviderCatalogOperation::MediaRequestStatus), 1);
    QVERIFY(!service->isAvailable());

    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());
    QCOMPARE(availability.size(), 1);
    QCOMPARE(service->backend(), QStringLiteral("silo"));
    QCOMPARE(service->backendName(), QStringLiteral("Silo"));
    QVERIFY(service->isDiscoverAvailable());

    QSignalSpy rows(service.get(), &MediaRequestService::discoverLoaded);
    service->loadDiscover();
    m_library->answer(ProviderCatalogOperation::MediaRequestDiscover,
                      {{QStringLiteral("rows"), QVariantList{QVariantMap{{"key", "trending_movies"}}}}});
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().at(0).toList().size(), 1);
}

void MediaRequestServiceTest::seerrTakesPriorityOverSilo()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());
    QCOMPARE(service->backend(), QStringLiteral("silo"));

    QSignalSpy availability(service.get(), &MediaRequestService::availabilityChanged);
    m_config->setSeerrBaseUrl(QStringLiteral("http://seerr.test"));
    QCOMPARE(availability.size(), 0); // A URL alone is not a configured Seerr.
    m_config->setSeerrApiKey(QStringLiteral("key"));
    QCOMPARE(availability.size(), 1);
    QCOMPARE(service->backend(), QStringLiteral("seerr"));
    QCOMPARE(service->backendName(), QStringLiteral("Seerr"));
    // Discover now comes from Seerr, and Silo-only actions disappear.
    QVERIFY(service->isDiscoverAvailable());
    QVERIFY(!service->followSupported());
    QVERIFY(!service->watchlistTitlesSupported());

    // This Seerr has no network, so every row fails and Discover says so once.
    QSignalSpy discoverFailed(service.get(), &MediaRequestService::discoverFailed);
    service->loadDiscover();
    QCOMPARE(discoverFailed.size(), 1);

    // Seerr browses genres only.
    QSignalSpy browseFailed(service.get(), &MediaRequestService::browseFailed);
    service->browse(QStringLiteral("studio"), QStringLiteral("a24"), QStringLiteral("movie"),
                    QStringLiteral("popularity"), 1);
    QCOMPARE(browseFailed.size(), 1);

    QSignalSpy errors(service.get(), &MediaRequestService::errorOccurred);
    service->setFollowing(QStringLiteral("tv"), 3, true);
    service->setWatchlisted(QStringLiteral("tv"), 3, true);
    QCOMPARE(errors.size(), 2);

    service->search(QStringLiteral("dune"));
    service->getSimilar(QStringLiteral("tv"), 3);
    service->prepareRequest(QStringLiteral("movie"), 4, QStringLiteral("Dune"));
    service->loadMyRequests();
    service->loadBrowseCategories();
    QCOMPARE(m_library->calls.size(), 0);

    // Removing the key hands requests back to Silo without another read.
    m_config->setSeerrApiKey(QString());
    QCOMPARE(availability.size(), 2);
    QCOMPARE(service->backend(), QStringLiteral("silo"));
}

void MediaRequestServiceTest::siloStaysHiddenWhenTheViewerMayNotRequest()
{
    auto service = makeService();
    QSignalSpy availability(service.get(), &MediaRequestService::availabilityChanged);
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus(false));
    QCOMPARE(availability.size(), 0);
    QVERIFY(!service->isAvailable());
    QVERIFY(!service->isDiscoverAvailable());

    QSignalSpy search(service.get(), &MediaRequestService::searchResultsLoaded);
    service->search(QStringLiteral("dune"));
    QCOMPARE(search.size(), 1);
    QVERIFY(search.first().at(1).toList().isEmpty());
    QCOMPARE(m_library->count(ProviderCatalogOperation::MediaRequestSearch), 0);
}

void MediaRequestServiceTest::siloSearchAndSimilarLeaveOutLibraryTitles()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy search(service.get(), &MediaRequestService::searchResultsLoaded);
    service->search(QStringLiteral(" dune "));
    QCOMPARE(m_library->last(ProviderCatalogOperation::MediaRequestSearch).query.searchTerm,
             QStringLiteral("dune"));
    m_library->answer(ProviderCatalogOperation::MediaRequestSearch,
                      {{QStringLiteral("items"), QVariantList{discoveryItem("movie", 1, "content-1"),
                                                              discoveryItem("movie", 2)}}});
    QCOMPARE(search.size(), 1);
    QCOMPARE(search.first().at(0).toString(), QStringLiteral(" dune "));
    const QVariantList results = search.first().at(1).toList();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().toMap().value("requestTmdbId").toLongLong(), qlonglong(2));

    QSignalSpy similar(service.get(), &MediaRequestService::similarResultsLoaded);
    service->getSimilar(QStringLiteral("TV"), 10);
    const auto call = m_library->last(ProviderCatalogOperation::MediaRequestDetail);
    QCOMPARE(call.query.requestMediaType, QStringLiteral("tv"));
    QCOMPARE(call.query.tmdbId, qint64(10));
    m_library->answer(ProviderCatalogOperation::MediaRequestDetail,
                      {{QStringLiteral("detail"),
                        QVariantMap{{QStringLiteral("recommendations"),
                                     QVariantList{discoveryItem("tv", 11, "content-11"),
                                                  discoveryItem("tv", 12)}}}}});
    QCOMPARE(similar.size(), 1);
    QCOMPARE(similar.first().at(0).toString(), QStringLiteral("tv"));
    QCOMPARE(similar.first().at(2).toList().size(), 1);
}

void MediaRequestServiceTest::siloCreateRequestUsesThePreparedTitle()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy errors(service.get(), &MediaRequestService::errorOccurred);
    // Without a prepared title there is no title for the request body.
    service->createRequest(QStringLiteral("tv"), 20, true, {}, -1, -1, {});
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("request"));

    QSignalSpy prepared(service.get(), &MediaRequestService::requestPreparationLoaded);
    service->prepareRequest(QStringLiteral("tv"), 20, QStringLiteral("Fallback"));
    QVariantMap detail = discoveryItem("tv", 20);
    detail.insert(QStringLiteral("seasons"), QVariantList{});
    detail.insert(QStringLiteral("recommendations"), QVariantList{discoveryItem("tv", 21)});
    m_library->answer(ProviderCatalogOperation::MediaRequestDetail, {{QStringLiteral("detail"), detail}});
    QCOMPARE(prepared.size(), 1);
    const QVariantMap payload = prepared.first().at(2).toMap();
    QCOMPARE(payload.value("backend").toString(), QStringLiteral("silo"));
    QVERIFY(payload.value("seasonRequestsSupported").toBool());
    QVERIFY(!payload.contains("recommendations"));

    // Choosing seasons sends them; Seerr's server/profile/folder are ignored.
    service->createRequest(QStringLiteral("tv"), 20, false, {QVariant(1), QVariant(3)}, 4, 5,
                           QStringLiteral("/tv"));
    auto call = m_library->last(ProviderCatalogOperation::CreateMediaRequest);
    QCOMPARE(call.query.requestSeasons, (QList<int>{1, 3}));
    QCOMPARE(call.query.requestMedia.value("name").toString(), QStringLiteral("Title 20"));

    QSignalSpy created(service.get(), &MediaRequestService::requestCreated);
    m_library->answer(ProviderCatalogOperation::CreateMediaRequest,
                      {{QStringLiteral("request"),
                        QVariantMap{{QStringLiteral("requestState"), QStringLiteral("pending")}}}});
    QCOMPARE(created.size(), 1);
    QCOMPARE(created.first().at(2).toMap().value("requestState").toString(), QStringLiteral("pending"));

    // The title's request state changed, so it must be prepared again.
    errors.clear();
    service->createRequest(QStringLiteral("tv"), 20, true, {}, -1, -1, {});
    QCOMPARE(errors.size(), 1);

    // All missing seasons sends none and lets Silo choose.
    service->prepareRequest(QStringLiteral("tv"), 20, QString());
    m_library->answer(ProviderCatalogOperation::MediaRequestDetail, {{QStringLiteral("detail"), detail}});
    service->createRequest(QStringLiteral("tv"), 20, true, {QVariant(1)}, -1, -1, {});
    call = m_library->last(ProviderCatalogOperation::CreateMediaRequest);
    QVERIFY(call.query.requestSeasons.isEmpty());
}

void MediaRequestServiceTest::viewerChangeAbandonsPendingSiloWork()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy search(service.get(), &MediaRequestService::searchResultsLoaded);
    QSignalSpy similarFailed(service.get(), &MediaRequestService::similarResultsFailed);
    QSignalSpy discoverFailed(service.get(), &MediaRequestService::discoverFailed);
    QSignalSpy availability(service.get(), &MediaRequestService::availabilityChanged);
    service->search(QStringLiteral("dune"));
    service->getSimilar(QStringLiteral("tv"), 5);
    service->loadDiscover();
    const QList<ScriptedLibraryService::Call> pending = m_library->calls;
    QCOMPARE(pending.size(), 3);

    // The session leaves Silo: no viewer, no requests.
    m_library->silo = false;
    service->refreshAvailability();
    QCOMPARE(availability.size(), 1);
    QVERIFY(!service->isAvailable());
    QCOMPARE(search.size(), 1);
    QVERIFY(search.first().at(1).toList().isEmpty());
    QCOMPARE(similarFailed.size(), 1);
    QCOMPARE(discoverFailed.size(), 1);

    // A late answer for the old viewer is ignored.
    pending.at(0).onLoaded({{QStringLiteral("items"), QVariantList{discoveryItem("movie", 1)}}});
    QCOMPARE(search.size(), 1);
}

void MediaRequestServiceTest::siloBrowseCategoriesTagKindsAndMediaTypes()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy loaded(service.get(), &MediaRequestService::browseCategoriesLoaded);
    service->loadBrowseCategories();
    QCOMPARE(m_library->count(ProviderCatalogOperation::MediaRequestBrowseCategories), 3);
    QStringList kinds;
    for (const auto &call : std::as_const(m_library->calls)) {
        kinds.append(call.query.browseKind);
    }
    QCOMPARE(kinds, (QStringList{"genre", "studio", "network"}));

    const auto category = [](const QString &id, bool series) {
        return QVariantMap{{"id", id}, {"name", id}, {"logoUrl", QString()}, {"seriesSupported", series}};
    };
    // Answer out of order; groups still come in genre, studio, network order.
    auto calls = m_library->calls;
    m_library->calls.clear();
    calls.at(2).onLoaded({{"items", QVariantList{category("hbo", false)}}});
    calls.at(0).onLoaded({{"items", QVariantList{category("drama", true), category("western", false)}}});
    QCOMPARE(loaded.size(), 0);
    calls.at(1).onFailed(QStringLiteral("down"));
    QCOMPARE(loaded.size(), 1);

    const QVariantList groups = loaded.first().at(0).toList();
    QCOMPARE(groups.size(), 2);
    const QVariantMap genres = groups.at(0).toMap();
    QCOMPARE(genres.value("kind").toString(), QStringLiteral("genre"));
    const QVariantList genreItems = genres.value("items").toList();
    QCOMPARE(genreItems.at(0).toMap().value("movieId").toString(), QStringLiteral("drama"));
    QCOMPARE(genreItems.at(0).toMap().value("tvId").toString(), QStringLiteral("drama"));
    QVERIFY(genreItems.at(1).toMap().value("tvId").toString().isEmpty());
    QVERIFY(!genreItems.at(0).toMap().contains("seriesSupported"));
    // Networks air series whatever the brand says.
    const QVariantMap hbo = groups.at(1).toMap().value("items").toList().first().toMap();
    QCOMPARE(hbo.value("kind").toString(), QStringLiteral("network"));
    QVERIFY(hbo.value("movieId").toString().isEmpty());
    QCOMPARE(hbo.value("tvId").toString(), QStringLiteral("hbo"));
}

void MediaRequestServiceTest::siloBrowseAndRequestListRoute()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy browsed(service.get(), &MediaRequestService::browseLoaded);
    service->browse(QStringLiteral("genre"), QStringLiteral("drama"), QStringLiteral("tv"),
                    QStringLiteral("release_date"), 2);
    const auto call = m_library->last(ProviderCatalogOperation::MediaRequestBrowse);
    QCOMPARE(call.query.browseKind, QStringLiteral("genre"));
    QCOMPARE(call.query.browseId, QStringLiteral("drama"));
    QCOMPARE(call.query.requestMediaType, QStringLiteral("tv"));
    QCOMPARE(call.query.browseSort, QStringLiteral("release_date"));
    QCOMPARE(call.query.page, 2);
    m_library->answer(ProviderCatalogOperation::MediaRequestBrowse,
                      {{"row", QVariantMap{{"items", QVariantList{discoveryItem("tv", 1)}}}}});
    QCOMPARE(browsed.size(), 1);
    QCOMPARE(browsed.first().at(0).toString(),
             service->browseKey("genre", "drama", "tv", "release_date"));
    QCOMPARE(browsed.first().at(1).toInt(), 2);

    QSignalSpy requests(service.get(), &MediaRequestService::myRequestsLoaded);
    service->loadMyRequests();
    m_library->answer(ProviderCatalogOperation::MediaRequestList,
                      {{"items", QVariantList{discoveryItem("movie", 3)}}});
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.first().at(0).toList().size(), 1);

    QSignalSpy cancelled(service.get(), &MediaRequestService::requestCancelled);
    service->cancelRequest(QStringLiteral("req-3"), QStringLiteral("movie"), 3);
    QCOMPARE(m_library->last(ProviderCatalogOperation::CancelMediaRequest).query.itemId,
             QStringLiteral("req-3"));
    m_library->answer(ProviderCatalogOperation::CancelMediaRequest, {});
    QCOMPARE(cancelled.size(), 1);
    QCOMPARE(cancelled.first().at(0).toString(), QStringLiteral("req-3"));
    QCOMPARE(cancelled.first().at(1).toString(), QStringLiteral("movie"));
    QCOMPARE(cancelled.first().at(2).toInt(), 3);
}

void MediaRequestServiceTest::siloFollowAndWatchlistNeedTheirFeatures()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());
    QVERIFY(!service->followSupported());

    QSignalSpy errors(service.get(), &MediaRequestService::errorOccurred);
    service->setFollowing(QStringLiteral("tv"), 9, true);
    service->setWatchlisted(QStringLiteral("tv"), 9, true);
    QCOMPARE(errors.size(), 2);
    QCOMPARE(errors.at(0).at(0).toString(), QStringLiteral("follow"));
    QCOMPARE(errors.at(1).at(0).toString(), QStringLiteral("watchlist"));
    QCOMPARE(m_library->calls.size(), 0);

    // A new capability read turns them on.
    QSignalSpy availability(service.get(), &MediaRequestService::availabilityChanged);
    service->refreshAvailability();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus(true, true));
    QCOMPARE(availability.size(), 1);
    QVERIFY(service->followSupported());
    QVERIFY(service->watchlistTitlesSupported());
    QVERIFY(service->watchlistRequests());

    QSignalSpy following(service.get(), &MediaRequestService::followingChanged);
    service->setFollowing(QStringLiteral("tv"), 9, true);
    const auto followCall = m_library->last(ProviderCatalogOperation::SetMediaRequestFollow);
    QVERIFY(followCall.query.stateValue);
    QCOMPARE(followCall.query.tmdbId, qint64(9));
    m_library->answer(ProviderCatalogOperation::SetMediaRequestFollow, {});
    QCOMPARE(following.size(), 1);
    QVERIFY(following.first().at(2).toBool());

    QSignalSpy watchlist(service.get(), &MediaRequestService::watchlistChanged);
    service->setWatchlisted(QStringLiteral("movie"), 4, false);
    QVERIFY(!m_library->last(ProviderCatalogOperation::SetWatchlistTitle).query.stateValue);
    m_library->answer(ProviderCatalogOperation::SetWatchlistTitle, {});
    QCOMPARE(watchlist.size(), 1);
    QCOMPARE(watchlist.first().at(0).toString(), QStringLiteral("movie"));
    QVERIFY(!watchlist.first().at(2).toBool());
}

void MediaRequestServiceTest::backendSwitchAbandonsTheOldBackendsWork()
{
    auto service = makeService();
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());

    QSignalSpy search(service.get(), &MediaRequestService::searchResultsLoaded);
    service->search(QStringLiteral("dune"));
    const auto siloSearch = m_library->last(ProviderCatalogOperation::MediaRequestSearch);
    // A capability read in flight survives the switch.
    service->refreshAvailability();
    QCOMPARE(m_library->count(ProviderCatalogOperation::MediaRequestStatus), 1);

    // Seerr takes over: the Silo search is abandoned at once...
    m_config->setSeerrBaseUrl(QStringLiteral("http://seerr.test"));
    m_config->setSeerrApiKey(QStringLiteral("key"));
    QCOMPARE(service->backend(), QStringLiteral("seerr"));
    QCOMPARE(search.size(), 1);
    QVERIFY(search.first().at(1).toList().isEmpty());

    // ...and its late answer cannot replace Seerr's results.
    siloSearch.onLoaded({{QStringLiteral("items"), QVariantList{discoveryItem("movie", 1)}}});
    QCOMPARE(search.size(), 1);

    // The capability read still lands, so Silo can take back over.
    m_library->answer(ProviderCatalogOperation::MediaRequestStatus, allowedStatus());
    m_config->setSeerrApiKey(QString());
    QCOMPARE(service->backend(), QStringLiteral("silo"));

    // A Seerr reply arriving after Silo took over is ignored.
    emit m_seerr->searchResultsLoaded(QStringLiteral("dune"), QJsonArray{QJsonObject{{"name", "x"}}});
    QCOMPARE(search.size(), 1);
}

QTEST_MAIN(MediaRequestServiceTest)
#include "MediaRequestServiceTest.moc"
