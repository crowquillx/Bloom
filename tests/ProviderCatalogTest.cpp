#include <QtTest/QtTest>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <limits>
#include <tuple>

#include "models/MediaExtras.h"
#include "providers/ICatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloModelMapper.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloCatalogProvider.h"

namespace {

// Silo artwork-bearing reads ask for the widths Bloom draws on a 4K screen.
QString withArtworkWidths(const QString &endpoint)
{
    return endpoint + QStringLiteral("?poster_width=1000&backdrop_width=3840&logo_width=1280&still_width=1280");
}

} // namespace

class ProviderCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinItemsRequestRetainsNativeContract();
    void jellyfinCanonicalSortKeysMapAtProviderBoundary();
    void jellyfinReservedIdentifiersAndLimitsRemainOpaque();
    void jellyfinMutationsAndPaginationRemainCompatible();
    void jellyfinRequestsUseOpenApiRoutesAndFields();
    void jellyfinNextUpOptionsAndResumeList();
    void siloTrackIndicesAndMultipartFallbackStayContiguous();
    void siloCanonicalIdentityVersionsMultipartAndStateUseMilliseconds();
    void siloPlaybackFallbacksAndExternalSubtitleMetadata();
    void canonicalArtworkAndPlaybackChapterMetadataRemainProviderNeutral();
    void siloEpisodeParentAndBoundedTimes();
    void siloArtworkCacheIdentityFollowsStoredVariant();
    void siloNumericSeasonRoutesAndEnvelopeOperation();
    void siloThemeMetadataCapabilitiesAndGrants();
    void jellyfinExtrasUseOpenApiRoutesAndMapKinds();
    void siloExtrasMapFromCatalogItemDetail();
    void preferredTrailerPrefersTrailerThenTeaser();

};

void ProviderCatalogTest::jellyfinCanonicalSortKeysMapAtProviderBoundary()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    const QList<QPair<QString, QString>> sortCases = {
        {QString(), QStringLiteral("ParentIndexNumber,IndexNumber,SortName")},
        {QStringLiteral("title"), QStringLiteral("SortName")},
        {QStringLiteral("releaseDate"), QStringLiteral("PremiereDate")},
        {QStringLiteral("dateAdded"), QStringLiteral("DateCreated")},
        {QStringLiteral("rating"), QStringLiteral("CommunityRating")},
        {QStringLiteral("year"), QStringLiteral("ProductionYear")},
        {QStringLiteral("random"), QStringLiteral("Random")},
        {QStringLiteral("datePlayed"), QStringLiteral("DatePlayed,SortName")},
    };
    for (const auto &[canonical, native] : sortCases) {
        ProviderCatalogQuery query;
        query.userId = QStringLiteral("user-1");
        query.sortBy = canonical;
        query.sortOrder = QStringLiteral("descending");
        const ProviderCatalogRequest request = catalog->createRequest(
            ProviderCatalogOperation::Items, query);
        QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
        const QUrlQuery parameters{QUrl(request.relativeEndpoint)};
        QCOMPARE(parameters.queryItemValue(QStringLiteral("SortBy")), native);
        // The name tie-break of datePlayed stays A to Z (one order per key).
        QCOMPARE(parameters.queryItemValue(QStringLiteral("SortOrder")),
                 canonical == QStringLiteral("datePlayed")
                     ? QStringLiteral("Descending,Ascending")
                     : QStringLiteral("Descending"));
    }

    ProviderCatalogQuery invalid;
    invalid.userId = QStringLiteral("user-1");
    invalid.sortBy = QStringLiteral("PremiereDate");
    const ProviderCatalogRequest rejected = catalog->createRequest(
        ProviderCatalogOperation::Items, invalid);
    QVERIFY(!rejected.supported);
    QVERIFY(!rejected.unsupportedReason.isEmpty());
}

void ProviderCatalogTest::jellyfinItemsRequestRetainsNativeContract()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user-1");
    query.parentId = QStringLiteral("library-1");
    query.startIndex = 20;
    query.limit = 25;
    query.searchTerm = QStringLiteral("Alien");
    query.genres = {QStringLiteral("Science Fiction"), QStringLiteral("Drama")};
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.sortBy = QStringLiteral("releaseDate");
    query.sortOrder = QStringLiteral("descending");
    query.watched = ProviderCatalogTriState::No;
    query.recursive = true;
    query.minPremiereDate = QDate(2020, 2, 3);
    query.maxPremiereDate = QDate(2024, 11, 12);
    query.minDateLastSaved = QDate(2026, 8, 7);
    query.useCacheValidation = true;
    query.etag = QByteArrayLiteral("\"catalog-v2\"");
    query.lastModified = QByteArrayLiteral("Fri, 07 Aug 2026 10:00:00 GMT");

    const ProviderCatalogRequest request = catalog->createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QVERIFY(request.body.isEmpty());
    QCOMPARE(request.extraHeaders.value(QByteArrayLiteral("If-None-Match")),
             QByteArrayLiteral("\"catalog-v2\""));
    QCOMPARE(request.extraHeaders.value(QByteArrayLiteral("If-Modified-Since")),
             QByteArrayLiteral("Fri, 07 Aug 2026 10:00:00 GMT"));

    const QUrl url(request.relativeEndpoint);
    const QUrlQuery parameters(url);
    QCOMPARE(url.path(), QStringLiteral("/Items"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("ParentId")),
             QStringLiteral("library-1"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("StartIndex")),
             QStringLiteral("20"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("Limit")),
             QStringLiteral("25"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("SearchTerm")),
             QStringLiteral("Alien"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("Genres")),
             QStringLiteral("Drama,Science Fiction"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("PremiereDate"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("SortOrder")),
             QStringLiteral("Descending"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("IsPlayed")),
             QStringLiteral("false"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("Recursive")),
             QStringLiteral("true"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("MinPremiereDate")),
             QStringLiteral("2020-02-03T00:00:00Z"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("MaxPremiereDate")),
             QStringLiteral("2024-11-12T23:59:59Z"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("MinDateLastSaved")),
             QStringLiteral("2026-08-07T00:00:00Z"));
    QVERIFY(parameters.queryItemValue(QStringLiteral("Fields"))
                .contains(QStringLiteral("MediaSources")));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("EnableImageTypes")),
             QStringLiteral("Primary,Backdrop,Thumb,Logo"));
}

void ProviderCatalogTest::jellyfinReservedIdentifiersAndLimitsRemainOpaque()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user/&?=");
    query.parentId = QStringLiteral("parent&next=bad");
    query.itemId = QStringLiteral("item/child?x=1");
    query.seriesId = QStringLiteral("series#fragment");

    const ProviderCatalogRequest items = catalog->createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(items.supported);
    const QUrl itemsUrl(items.relativeEndpoint);
    QCOMPARE(itemsUrl.path(QUrl::FullyEncoded), QStringLiteral("/Items"));
    QCOMPARE(QUrlQuery(itemsUrl).queryItemValue(QStringLiteral("UserId")), query.userId);
    QCOMPARE(QUrlQuery(itemsUrl).queryItemValue(QStringLiteral("ParentId")),
             query.parentId);

    const ProviderCatalogRequest item = catalog->createRequest(
        ProviderCatalogOperation::Item, query);
    QVERIFY(item.supported);
    QCOMPARE(QUrl(item.relativeEndpoint).path(QUrl::FullyEncoded), QStringLiteral("/Items"));
    QCOMPARE(QUrlQuery(QUrl(item.relativeEndpoint)).queryItemValue(QStringLiteral("UserId")),
             query.userId);
    QCOMPARE(QUrlQuery(QUrl(item.relativeEndpoint)).queryItemValue(QStringLiteral("Ids")),
             query.itemId);

    query.limit = 0;
    const ProviderCatalogRequest searchDefault = catalog->createRequest(
        ProviderCatalogOperation::Search, query);
    QVERIFY(searchDefault.supported);
    QCOMPARE(QUrlQuery(QUrl(searchDefault.relativeEndpoint))
                 .queryItemValue(QStringLiteral("Limit")),
             QStringLiteral("50"));

    query.limit = -7;
    const ProviderCatalogRequest randomDefault = catalog->createRequest(
        ProviderCatalogOperation::RandomItems, query);
    QVERIFY(randomDefault.supported);
    QCOMPARE(QUrlQuery(QUrl(randomDefault.relativeEndpoint))
                 .queryItemValue(QStringLiteral("Limit")),
             QStringLiteral("50"));
}

void ProviderCatalogTest::jellyfinMutationsAndPaginationRemainCompatible()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user-1");
    query.itemId = QStringLiteral("item-9");
    query.stateValue = true;

    const ProviderCatalogRequest markWatched = catalog->createRequest(
        ProviderCatalogOperation::SetWatched, query);
    QVERIFY(markWatched.supported);
    QCOMPARE(markWatched.method, ProviderHttpMethod::Post);
    QCOMPARE(markWatched.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(QUrl(markWatched.relativeEndpoint).path(),
             QStringLiteral("/UserPlayedItems/item-9"));
    QCOMPARE(QUrlQuery(QUrl(markWatched.relativeEndpoint))
                 .queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));
    QCOMPARE(markWatched.extraHeaders.value(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/json"));

    query.stateValue = false;
    const ProviderCatalogRequest clearFavorite = catalog->createRequest(
        ProviderCatalogOperation::SetFavorite, query);
    QVERIFY(clearFavorite.supported);
    QCOMPARE(clearFavorite.method, ProviderHttpMethod::Delete);
    QCOMPARE(clearFavorite.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(QUrl(clearFavorite.relativeEndpoint).path(),
             QStringLiteral("/UserFavoriteItems/item-9"));
    QCOMPARE(QUrlQuery(QUrl(clearFavorite.relativeEndpoint))
                 .queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));

    const ProviderCatalogResponse response = catalog->parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"Items":[{"Id":"item-21"},{"Id":"item-22"}],"StartIndex":20,"TotalRecordCount":23})"),
        {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"items-v3\"")},
         {QByteArrayLiteral("LAST-MODIFIED"),
          QByteArrayLiteral("Fri, 07 Aug 2026 11:00:00 GMT")}});
    QVERIFY(response.valid);
    QCOMPARE(response.rawItems.size(), 2);
    QCOMPARE(response.total, 23);
    QVERIFY(response.hasMore);
    QCOMPARE(response.snapshot.value(QStringLiteral("etag")).toString(),
             QStringLiteral("\"items-v3\""));
    QCOMPARE(response.snapshot.value(QStringLiteral("lastModified")).toString(),
             QStringLiteral("Fri, 07 Aug 2026 11:00:00 GMT"));
}

void ProviderCatalogTest::jellyfinRequestsUseOpenApiRoutesAndFields()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user-1");
    query.itemId = QStringLiteral("item-1");
    query.parentId = QStringLiteral("library-1");
    query.seriesId = QStringLiteral("series-1");
    query.limit = 12;
    query.recursive = true;

    const QHash<ProviderCatalogOperation, QString> expectedPaths{
        {ProviderCatalogOperation::Views, QStringLiteral("/UserViews")},
        {ProviderCatalogOperation::Items, QStringLiteral("/Items")},
        {ProviderCatalogOperation::NextUp, QStringLiteral("/Shows/NextUp")},
        {ProviderCatalogOperation::ResumeItems, QStringLiteral("/UserItems/Resume")},
        {ProviderCatalogOperation::LatestMedia, QStringLiteral("/Items/Latest")},
        {ProviderCatalogOperation::Item, QStringLiteral("/Items")},
        {ProviderCatalogOperation::Chapters, QStringLiteral("/Items")},
        {ProviderCatalogOperation::SimilarItems, QStringLiteral("/Items/item-1/Similar")},
        {ProviderCatalogOperation::NextUnplayedEpisode, QStringLiteral("/Items")},
        {ProviderCatalogOperation::Search, QStringLiteral("/Items")},
        {ProviderCatalogOperation::RandomItems, QStringLiteral("/Items")},
        {ProviderCatalogOperation::HeroItems, QStringLiteral("/Items")},
        {ProviderCatalogOperation::HeroOverviews, QStringLiteral("/Items")},
        {ProviderCatalogOperation::Versions, QStringLiteral("/Items")},
        {ProviderCatalogOperation::SetWatched, QStringLiteral("/UserPlayedItems/item-1")},
        {ProviderCatalogOperation::SetFavorite, QStringLiteral("/UserFavoriteItems/item-1")}
    };
    for (auto it = expectedPaths.constBegin(); it != expectedPaths.constEnd(); ++it) {
        const ProviderCatalogRequest request = catalog->createRequest(it.key(), query);
        QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
        const QUrl url(request.relativeEndpoint);
        QCOMPARE(url.path(), it.value());
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("UserId")), query.userId);
        if (it.key() == ProviderCatalogOperation::Item
            || it.key() == ProviderCatalogOperation::Chapters
            || it.key() == ProviderCatalogOperation::HeroOverviews
            || it.key() == ProviderCatalogOperation::Versions) {
            QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("Ids")),
                     it.key() == ProviderCatalogOperation::HeroOverviews
                         ? query.seriesId : query.itemId);
        }
    }

    QFile openApiManifest(QStringLiteral(BLOOM_JELLYFIN_OPENAPI_MANIFEST));
    QVERIFY2(openApiManifest.open(QIODevice::ReadOnly),
             qPrintable(openApiManifest.errorString()));
    const QJsonObject manifest = QJsonDocument::fromJson(
        openApiManifest.readAll()).object();
    const QJsonArray itemFieldValues = manifest.value(QStringLiteral("schemas"))
                                               .toObject()
                                               .value(QStringLiteral("ItemFields"))
                                               .toObject()
                                               .value(QStringLiteral("enum"))
                                               .toArray();
    QVERIFY(!itemFieldValues.isEmpty());
    QSet<QString> openApiItemFields;
    for (const QJsonValue &field : itemFieldValues) {
        openApiItemFields.insert(field.toString());
    }
    for (ProviderCatalogOperation operation : {
             ProviderCatalogOperation::Items, ProviderCatalogOperation::NextUp,
             ProviderCatalogOperation::ResumeItems, ProviderCatalogOperation::LatestMedia, ProviderCatalogOperation::HomeBackdrops,
             ProviderCatalogOperation::ScreensaverItems, ProviderCatalogOperation::SimilarItems,
             ProviderCatalogOperation::NextUnplayedEpisode, ProviderCatalogOperation::Search,
             ProviderCatalogOperation::RandomItems, ProviderCatalogOperation::HeroItems}) {
        const ProviderCatalogRequest request = catalog->createRequest(operation, query);
        QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
        const QString fields = QUrlQuery(QUrl(request.relativeEndpoint))
                                   .queryItemValue(QStringLiteral("Fields"));
        for (const QString &field : fields.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QVERIFY2(openApiItemFields.contains(field), qPrintable(field));
        }
    }

    query.filterFacet = QStringLiteral("filters");
    const ProviderCatalogRequest filters = catalog->createRequest(
        ProviderCatalogOperation::FilterOptions, query);
    QCOMPARE(QUrl(filters.relativeEndpoint).path(), QStringLiteral("/Items/Filters2"));
    QVERIFY(!QUrlQuery(QUrl(filters.relativeEndpoint)).hasQueryItem(QStringLiteral("Limit")));
    query.filterFacet = QStringLiteral("genres");
    const ProviderCatalogRequest genres = catalog->createRequest(
        ProviderCatalogOperation::FilterOptions, query);
    QCOMPARE(QUrl(genres.relativeEndpoint).path(), QStringLiteral("/Genres"));
    QVERIFY(!QUrlQuery(QUrl(genres.relativeEndpoint)).hasQueryItem(QStringLiteral("Recursive")));

    const ProviderCatalogResponse detail = catalog->parseResponse(
        ProviderCatalogOperation::Item,
        QByteArrayLiteral(R"({"Items":[{"Id":"item-1","Name":"Refreshed"}],"TotalRecordCount":1})"));
    QVERIFY(detail.valid);
    QCOMPARE(detail.rawItem.value(QStringLiteral("Id")).toString(), QStringLiteral("item-1"));
    QCOMPARE(detail.rawItem.value(QStringLiteral("Name")).toString(),
             QStringLiteral("Refreshed"));
}

void ProviderCatalogTest::jellyfinNextUpOptionsAndResumeList()
{
    JellyfinProviderAdapter adapter;
    QVERIFY(adapter.supportsCapability(ProviderCapability::NextUpOptions));
    QVERIFY(adapter.supportsCapability(ProviderCapability::ResumeItems));
    QVERIFY(!adapter.supportsCapability(ProviderCapability::Watchlist));
    SiloProviderAdapter silo;
    QVERIFY(!silo.supportsCapability(ProviderCapability::NextUpOptions));
    QVERIFY(!silo.supportsCapability(ProviderCapability::ResumeItems));

    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);
    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user-1");

    // Next Up keeps part-watched episodes (the resume copy wins later) and
    // sends no option the viewer did not choose.
    ProviderCatalogRequest request = catalog->createRequest(ProviderCatalogOperation::NextUp, query);
    QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QUrlQuery parameters{QUrl(request.relativeEndpoint)};
    QCOMPARE(QUrl(request.relativeEndpoint).path(), QStringLiteral("/Shows/NextUp"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("Limit")), QStringLiteral("20"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("Fields")),
             QStringLiteral("Path,Overview,ParentId"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("EnableUserData")), QStringLiteral("true"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("EnableResumable")), QStringLiteral("true"));
    QVERIFY(!parameters.hasQueryItem(QStringLiteral("EnableRewatching")));
    QVERIFY(!parameters.hasQueryItem(QStringLiteral("NextUpDateCutoff")));

    query.nextUpRewatching = true;
    query.nextUpCutoff = QDate(2026, 8, 3);
    request = catalog->createRequest(ProviderCatalogOperation::NextUp, query);
    parameters = QUrlQuery(QUrl(request.relativeEndpoint));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("EnableRewatching")), QStringLiteral("true"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("NextUpDateCutoff")),
             QStringLiteral("2026-08-03T00:00:00Z"));

    // The resume list asks for the same card shape, video movies and
    // episodes only.
    ProviderCatalogQuery resumeQuery;
    resumeQuery.userId = QStringLiteral("user-1");
    resumeQuery.limit = 20;
    request = catalog->createRequest(ProviderCatalogOperation::ResumeItems, resumeQuery);
    QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    QCOMPARE(QUrl(request.relativeEndpoint).path(), QStringLiteral("/UserItems/Resume"));
    const QUrlQuery resume{QUrl(request.relativeEndpoint)};
    QCOMPARE(resume.queryItemValue(QStringLiteral("UserId")), QStringLiteral("user-1"));
    QCOMPARE(resume.queryItemValue(QStringLiteral("Limit")), QStringLiteral("20"));
    QCOMPARE(resume.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Episode"));
    QCOMPARE(resume.queryItemValue(QStringLiteral("MediaTypes")), QStringLiteral("Video"));
    QCOMPARE(resume.queryItemValue(QStringLiteral("Fields")),
             parameters.queryItemValue(QStringLiteral("Fields")));
    QCOMPARE(resume.queryItemValue(QStringLiteral("EnableImageTypes")),
             parameters.queryItemValue(QStringLiteral("EnableImageTypes")));
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::ResumeItems, {}).supported);

    // Every parameter is a current one of the pinned OpenAPI operation.
    QFile openApiManifest(QStringLiteral(BLOOM_JELLYFIN_OPENAPI_MANIFEST));
    QVERIFY2(openApiManifest.open(QIODevice::ReadOnly),
             qPrintable(openApiManifest.errorString()));
    const QJsonArray operations = QJsonDocument::fromJson(openApiManifest.readAll())
                                      .object().value(QStringLiteral("operations")).toArray();
    const auto declaredParameters = [&operations](const QString &path) {
        QHash<QString, bool> deprecatedByName;
        for (const QJsonValue &value : operations) {
            const QJsonObject operation = value.toObject();
            if (operation.value(QStringLiteral("method")).toString() != QStringLiteral("GET")
                || operation.value(QStringLiteral("path")).toString() != path) {
                continue;
            }
            for (const QJsonValue &parameter : operation.value(QStringLiteral("parameters")).toArray()) {
                deprecatedByName.insert(
                    parameter.toObject().value(QStringLiteral("name")).toString().toLower(),
                    parameter.toObject().value(QStringLiteral("deprecated")).toBool());
            }
        }
        return deprecatedByName;
    };
    for (const auto &[path, sent] : {std::pair{QStringLiteral("/Shows/NextUp"), parameters},
                                     std::pair{QStringLiteral("/UserItems/Resume"), resume}}) {
        const QHash<QString, bool> declared = declaredParameters(path);
        QVERIFY2(!declared.isEmpty(), qPrintable(path));
        for (const auto &[name, value] : sent.queryItems()) {
            Q_UNUSED(value)
            QVERIFY2(declared.contains(name.toLower()), qPrintable(path + QLatin1Char(' ') + name));
            QVERIFY2(!declared.value(name.toLower()), qPrintable(path + QLatin1Char(' ') + name));
        }
    }

    const ProviderCatalogResponse parsed = catalog->parseResponse(
        ProviderCatalogOperation::ResumeItems,
        QByteArrayLiteral(R"({"Items":[{"Id":"movie-1","Type":"Movie","Name":"Halfway",)"
                          R"("UserData":{"PlaybackPositionTicks":6000000000,)"
                          R"("LastPlayedDate":"2026-10-30T20:15:00.1234567Z"}}],)"
                          R"("TotalRecordCount":1,"StartIndex":0})"));
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.rawItems.size(), 1);
    const QVariantMap mapped = adapter.mapMediaItem(parsed.rawItems.first().toObject(),
                                                    QStringLiteral("connection-1"));
    QCOMPARE(mapped.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(mapped.value(QStringLiteral("positionMs")).toLongLong(), 600000LL);
    QCOMPARE(mapped.value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("connection-1"));

    // Silo sends in-progress cards with Next Up from its Home layout.
    SiloCatalogProvider siloCatalog;
    const ProviderCatalogRequest siloResume = siloCatalog.createRequest(
        ProviderCatalogOperation::ResumeItems, resumeQuery);
    QVERIFY(!siloResume.supported);
    QVERIFY(!siloResume.unsupportedReason.isEmpty());
}

void ProviderCatalogTest::siloTrackIndicesAndMultipartFallbackStayContiguous()
{
    const QJsonObject version{
        {QStringLiteral("file_id"), QStringLiteral("file-1")},
        {QStringLiteral("audio_tracks"), QJsonArray{
             QJsonValue(QStringLiteral("malformed")),
             QJsonObject{{QStringLiteral("index"), 7},
                          {QStringLiteral("language"), QStringLiteral("eng")},
                          {QStringLiteral("codec"), QStringLiteral("truehd")},
                          {QStringLiteral("profile"), QStringLiteral("Dolby Atmos")},
                          {QStringLiteral("sample_rate"), 48000},
                          {QStringLiteral("bit_depth"), 24},
                          {QStringLiteral("channels"), 8},
                          {QStringLiteral("bitrate"), 5000000}},
             QJsonObject{{QStringLiteral("language"), QStringLiteral("deu")}}
         }}
    };
    const QJsonObject secondVersion{
        {QStringLiteral("file_id"), QStringLiteral("file-2")}
    };
    const QJsonObject item{
        {QStringLiteral("content_id"), QStringLiteral("content-1")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Example")},
        {QStringLiteral("versions"), QJsonArray{version}},
        {QStringLiteral("playback_variants"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("variant_id"), QStringLiteral("variant-1")},
                 {QStringLiteral("part_count"), 2},
                 {QStringLiteral("parts"), QJsonArray{
                      QJsonValue(QStringLiteral("malformed")),
                      QJsonObject{
                          {QStringLiteral("versions"), QJsonArray{version}}
                      },
                      QJsonObject{
                          {QStringLiteral("versions"), QJsonArray{secondVersion}}
                      }
                  }}
             }
         }}
    };

    const QVariantMap mapped = SiloModelMapper::mediaItem(
        item, QStringLiteral("connection-silo"));
    const QVariantList tracks = mapped.value(QStringLiteral("versions"))
                                    .toList().constFirst().toMap()
                                    .value(QStringLiteral("audioTracks")).toList();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.at(0).toMap().value(QStringLiteral("index")).toInt(), 7);
    QCOMPARE(tracks.at(1).toMap().value(QStringLiteral("index")).toInt(), 2);

    const QVariantList parts = mapped.value(QStringLiteral("playbackVariants"))
                                   .toList().constFirst().toMap()
                                   .value(QStringLiteral("parts")).toList();
    QCOMPARE(parts.size(), 2);
    QCOMPARE(parts.at(0).toMap().value(QStringLiteral("partIndex")).toInt(), 1);
    QCOMPARE(parts.at(1).toMap().value(QStringLiteral("partIndex")).toInt(), 2);

    const PlaybackInfoResponse playback = SiloModelMapper::playbackInfo(item);
    QCOMPARE(playback.mediaSources.size(), 2);
    const QList<MediaStreamInfo> streams = playback.mediaSources.first().mediaStreams;
    QCOMPARE(streams.size(), 2);
    QCOMPARE(streams.at(0).index, 7);
    QCOMPARE(streams.at(1).index, 2);
    QCOMPARE(streams.at(0).sampleRate, 48000);
    QCOMPARE(streams.at(0).bitDepth, 24);
    QVERIFY(streams.at(0).atmos);

    SiloProviderAdapter adapter;
    QVERIFY(adapter.supportsCapability(ProviderCapability::Catalog));
    QVERIFY(adapter.supportsCapability(ProviderCapability::NativeState));
    QVERIFY(adapter.supportsCapability(ProviderCapability::Playback));
    QVERIFY(adapter.supportsCapability(ProviderCapability::PlaybackReporting));
}

void ProviderCatalogTest::siloCanonicalIdentityVersionsMultipartAndStateUseMilliseconds()
{
    const QJsonObject version{
        {QStringLiteral("file_id"), QStringLiteral("file-99")},
        {QStringLiteral("file_name"), QStringLiteral("movie.mkv")},
        {QStringLiteral("duration"), 123.456},
        {QStringLiteral("edition_raw"), QStringLiteral("Director's Cut")},
        {QStringLiteral("presentation_part_index"), 1},
        {QStringLiteral("presentation_part_total"), 2},
        {QStringLiteral("chapters"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("index"), 3},
                 {QStringLiteral("title"), QStringLiteral("Arrival")},
                 {QStringLiteral("start_seconds"), 1.25},
                 {QStringLiteral("end_seconds"), 4.75}
             }
         }}
    };
    const QJsonObject item{
        {QStringLiteral("content_id"), QStringLiteral("content-42")},
        {QStringLiteral("file_id"), QStringLiteral("wrong-item-identity")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Example")},
        {QStringLiteral("provider_ids"), QJsonObject{
             {QStringLiteral("imdb_id"), QStringLiteral("tt1234567")},
             {QStringLiteral("Tmdb"), QStringLiteral("7654")}
         }},
        {QStringLiteral("user_data"), QJsonObject{
             {QStringLiteral("played"), false},
             {QStringLiteral("is_favorite"), true},
             {QStringLiteral("position_seconds"), 12.345},
             {QStringLiteral("duration_seconds"), 123.456},
             {QStringLiteral("is_in_progress"), true}
         }},
        {QStringLiteral("versions"), QJsonArray{version}},
        {QStringLiteral("playback_variants"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("variant_id"), QStringLiteral("variant-main")},
                 {QStringLiteral("part_count"), 2},
                 {QStringLiteral("total_duration"), 246.912},
                 {QStringLiteral("default_file_id"), QStringLiteral("file-99")},
                 {QStringLiteral("parts"), QJsonArray{
                      QJsonObject{
                          {QStringLiteral("part_index"), 1},
                          {QStringLiteral("default_file_id"), QStringLiteral("file-99")},
                          {QStringLiteral("total_duration"), 123.456},
                          {QStringLiteral("versions"), QJsonArray{version}}
                      }
                  }}
             }
         }}
    };

    const QVariantMap mapped = SiloModelMapper::mediaItem(
        item, QStringLiteral("connection-silo"));
    QCOMPARE(mapped.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("content-42"));
    QVERIFY(mapped.value(QStringLiteral("itemId")).toString()
            != QStringLiteral("file-99"));
    QCOMPARE(mapped.value(QStringLiteral("providerIds")).toMap()
                 .value(QStringLiteral("Imdb")).toString(),
             QStringLiteral("tt1234567"));
    QCOMPARE(mapped.value(QStringLiteral("providerIds")).toMap()
                 .value(QStringLiteral("Tmdb")).toString(),
             QStringLiteral("7654"));
    QCOMPARE(mapped.value(QStringLiteral("positionMs")).toLongLong(), 12345);
    QCOMPARE(mapped.value(QStringLiteral("durationMs")).toLongLong(), 123456);
    QVERIFY(mapped.value(QStringLiteral("favorite")).toBool());
    QVERIFY(mapped.value(QStringLiteral("isInProgress")).toBool());

    const QVariantList mappedVersions = mapped.value(QStringLiteral("versions")).toList();
    QCOMPARE(mappedVersions.size(), 1);
    const QVariantMap mappedVersion = mappedVersions.constFirst().toMap();
    QCOMPARE(mappedVersion.value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(mappedVersion.value(QStringLiteral("durationMs")).toLongLong(), 123456);
    QCOMPARE(mappedVersion.value(QStringLiteral("presentationPartIndex")).toInt(), 1);
    QCOMPARE(mappedVersion.value(QStringLiteral("presentationPartTotal")).toInt(), 2);
    const QVariantList mappedChapters = mappedVersion.value(QStringLiteral("chapters")).toList();
    QCOMPARE(mappedChapters.size(), 1);
    const QVariantMap chapter = mappedChapters.constFirst().toMap();
    QCOMPARE(chapter.value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(chapter.value(QStringLiteral("startMs")).toLongLong(), 1250);
    QCOMPARE(chapter.value(QStringLiteral("endMs")).toLongLong(), 4750);

    const QVariantList playbackVariants = mapped.value(QStringLiteral("playbackVariants"))
                                              .toList();
    QCOMPARE(playbackVariants.size(), 1);
    const QVariantMap playbackVariant = playbackVariants.constFirst().toMap();
    QCOMPARE(playbackVariant.value(QStringLiteral("defaultFileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(playbackVariant.value(QStringLiteral("partCount")).toInt(), 2);
    QCOMPARE(playbackVariant.value(QStringLiteral("totalDurationMs")).toLongLong(),
             246912);
    const QVariantList parts = playbackVariant.value(QStringLiteral("parts")).toList();
    QCOMPARE(parts.size(), 1);
    const QVariantMap part = parts.constFirst().toMap();
    QCOMPARE(part.value(QStringLiteral("partIndex")).toInt(), 1);
    QCOMPARE(part.value(QStringLiteral("defaultFileId")).toString(),
             QStringLiteral("file-99"));
    const QVariantList partVersions = part.value(QStringLiteral("versions")).toList();
    QCOMPARE(partVersions.size(), 1);
    QCOMPARE(partVersions.constFirst().toMap().value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));

    const QVariantMap state = SiloModelMapper::nativeState(
        QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("content-42")},
            {QStringLiteral("user_state"), QJsonObject{
                 {QStringLiteral("played"), true},
                 {QStringLiteral("position_seconds"), 98.765},
                 {QStringLiteral("duration_seconds"), 123.456},
                 {QStringLiteral("last_file_id"), QStringLiteral("file-99")}
             }}
        },
        QStringLiteral("connection-silo"));
    QCOMPARE(state.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("content-42"));
    QCOMPARE(state.value(QStringLiteral("lastFileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(state.value(QStringLiteral("positionMs")).toLongLong(), 98765);
    QCOMPARE(state.value(QStringLiteral("durationMs")).toLongLong(), 123456);
    QVERIFY(state.value(QStringLiteral("watched")).toBool());
}

void ProviderCatalogTest::siloPlaybackFallbacksAndExternalSubtitleMetadata()
{
    const QJsonObject version{
        {QStringLiteral("file_id"), QStringLiteral("file-subtitles")},
        {QStringLiteral("audio_tracks"), QJsonArray{
             QJsonObject{{QStringLiteral("url"), QStringLiteral("https://audio.invalid")}}
         }},
        {QStringLiteral("subtitle_tracks"), QJsonArray{}},
        {QStringLiteral("subtitles"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("language"), QStringLiteral("eng")},
                 {QStringLiteral("downloaded_url"),
                  QStringLiteral("https://subtitles.invalid/downloaded.vtt")}
             },
             QJsonObject{
                 {QStringLiteral("language"), QStringLiteral("deu")},
                 {QStringLiteral("external_url"),
                  QStringLiteral("https://subtitles.invalid/external.vtt")}
             }
         }},
        {QStringLiteral("stream_url"), QString()},
        {QStringLiteral("download_url"), QStringLiteral("https://media.invalid/download")}
    };

    const QVariantMap mappedVersion = SiloModelMapper::mediaVersion(
        version, QStringLiteral("silo"), QStringLiteral("item"));
    const QVariantList audioTracks = mappedVersion.value(QStringLiteral("audioTracks")).toList();
    QVERIFY(!audioTracks.constFirst().toMap().contains(QStringLiteral("externalUrl")));
    const QVariantList subtitleTracks =
        mappedVersion.value(QStringLiteral("subtitleTracks")).toList();
    QCOMPARE(subtitleTracks.size(), 2);
    QCOMPARE(subtitleTracks.at(0).toMap().value(QStringLiteral("externalUrl")).toString(),
             QStringLiteral("https://subtitles.invalid/downloaded.vtt"));
    QCOMPARE(subtitleTracks.at(1).toMap().value(QStringLiteral("externalUrl")).toString(),
             QStringLiteral("https://subtitles.invalid/external.vtt"));

    const PlaybackInfoResponse nestedVersions = SiloModelMapper::playbackInfo(
        QJsonObject{
            {QStringLiteral("media_sources"), QJsonArray{}},
            {QStringLiteral("playback_info"), QJsonObject{
                 {QStringLiteral("versions"), QJsonArray{version}}
             }}
        });
    QCOMPARE(nestedVersions.mediaSources.size(), 1);
    QCOMPARE(nestedVersions.mediaSources.first().id, QStringLiteral("file-subtitles"));
    QCOMPARE(nestedVersions.mediaSources.first().directStreamUrl,
             QStringLiteral("https://media.invalid/download"));

    const QJsonObject variantVersion{
        {QStringLiteral("file_id"), QStringLiteral("file-variant")}
    };
    const QJsonArray variants{
        QJsonObject{
            {QStringLiteral("variant_id"), QStringLiteral("variant")},
            {QStringLiteral("parts"), QJsonArray{
                 QJsonObject{
                     {QStringLiteral("versions"), QJsonArray{variantVersion}}
                 }
             }}
        }
    };
    const PlaybackInfoResponse rootVariants = SiloModelMapper::playbackInfo(
        QJsonObject{
            {QStringLiteral("media_sources"), QJsonArray{}},
            {QStringLiteral("playback_variants"), variants}
        });
    QCOMPARE(rootVariants.mediaSources.size(), 1);
    QCOMPARE(rootVariants.mediaSources.first().id, QStringLiteral("file-variant"));

    const PlaybackInfoResponse nestedVariants = SiloModelMapper::playbackInfo(
        QJsonObject{
            {QStringLiteral("media_sources"), QJsonArray{}},
            {QStringLiteral("playback_info"), QJsonObject{
                 {QStringLiteral("playback_variants"), variants}
             }}
        });
    QCOMPARE(nestedVariants.mediaSources.size(), 1);
    QCOMPARE(nestedVariants.mediaSources.first().id, QStringLiteral("file-variant"));

    const QJsonObject fileVersion{
        {QStringLiteral("file_id"), QStringLiteral("file-fallback")}
    };
    const PlaybackInfoResponse files = SiloModelMapper::playbackInfo(
        QJsonObject{
            {QStringLiteral("media_sources"), QJsonArray{}},
            {QStringLiteral("files"), QJsonArray{fileVersion}}
        });
    QCOMPARE(files.mediaSources.size(), 1);
    QCOMPARE(files.mediaSources.first().id, QStringLiteral("file-fallback"));
}


void ProviderCatalogTest::canonicalArtworkAndPlaybackChapterMetadataRemainProviderNeutral()
{
    Bloom::ArtworkRef artwork;
    artwork.connectionId = QStringLiteral("silo-connection");
    artwork.itemId = QStringLiteral("content-42");
    artwork.kind = Bloom::ArtworkKind::Chapter;
    artwork.ownerKind = Bloom::ArtworkOwnerKind::Chapter;
    artwork.index = 4;
    artwork.tag = QStringLiteral("file-99");
    artwork.requestedWidth = 640;
    artwork.sourceUrl = QStringLiteral(
        "https://silo.example.test/api/v2/artwork/file-99/4?signature=rotating");

    const QString cacheKey = artwork.cacheKey();
    const Bloom::ArtworkRef restored = Bloom::ArtworkRef::fromCacheKey(cacheKey);
    QVERIFY(restored.isValid());
    QVERIFY(restored == artwork);
    QCOMPARE(restored.ownerKind, Bloom::ArtworkOwnerKind::Chapter);
    QVERIFY(restored.sourceUrl.isEmpty());
    QVERIFY(!cacheKey.contains(QStringLiteral("signature")));

    Bloom::Chapter chapter;
    chapter.name = QStringLiteral("Arrival");
    chapter.startMs = 1250;
    chapter.artwork = artwork;
    chapter.index = 3;
    chapter.fileId = QStringLiteral("file-99");
    chapter.endMs = 4750;
    chapter.source = QStringLiteral("embedded");
    chapter.thumbnailThumbhash = QStringLiteral("thumbhash");
    const QVariantMap chapterMap = chapter.toVariantMap();
    QCOMPARE(chapterMap.value(QStringLiteral("name")).toString(),
             QStringLiteral("Arrival"));
    QCOMPARE(chapterMap.value(QStringLiteral("startMs")).toLongLong(), 1250);
    QCOMPARE(chapterMap.value(QStringLiteral("index")).toInt(), 3);
    QCOMPARE(chapterMap.value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(chapterMap.value(QStringLiteral("endMs")).toLongLong(), 4750);
    QCOMPARE(chapterMap.value(QStringLiteral("source")).toString(),
             QStringLiteral("embedded"));
    QCOMPARE(chapterMap.value(QStringLiteral("thumbnailThumbhash")).toString(),
             QStringLiteral("thumbhash"));
    QCOMPARE(chapterMap.value(QStringLiteral("artwork")).toMap()
                 .value(QStringLiteral("ownerKind")).toString(),
             QStringLiteral("chapter"));

    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = {QStringLiteral("silo-connection"), QStringLiteral("content-42")};
    descriptor.mediaVersionId = QStringLiteral("file-99");
    descriptor.durationMs = 123456;
    descriptor.startPositionMs = 98765;
    descriptor.stream.url = QUrl(QStringLiteral(
        "https://silo.example.test/api/v2/playback/file-99"));
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.chapters.append(chapter);
    QVERIFY(descriptor.isValid());
    const QVariantMap descriptorMap = descriptor.toVariantMap();
    QCOMPARE(descriptorMap.value(QStringLiteral("mediaVersionId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(descriptorMap.value(QStringLiteral("durationMs")).toLongLong(), 123456);
    QCOMPARE(descriptorMap.value(QStringLiteral("startPositionMs")).toLongLong(), 98765);
    QCOMPARE(descriptorMap.value(QStringLiteral("chapters")).toList().size(), 1);
    QCOMPARE(descriptorMap.value(QStringLiteral("chapters")).toList().first().toMap()
                 .value(QStringLiteral("startMs")).toLongLong(), 1250);
}

void ProviderCatalogTest::siloArtworkCacheIdentityFollowsStoredVariant()
{
    const auto posterKey = [](const QString &posterUrl) {
        const QVariantMap mapped = SiloModelMapper::mediaItem(
            QJsonObject{
                {QStringLiteral("content_id"), QStringLiteral("movie-1")},
                {QStringLiteral("type"), QStringLiteral("movie")},
                {QStringLiteral("poster_url"), posterUrl}
            },
            QStringLiteral("silo"));
        return mapped.value(QStringLiteral("primaryArtwork")).toMap()
            .value(QStringLiteral("cacheKey")).toString();
    };

    // Signed queries change daily without changing the stored image.
    const QString w500 = posterKey(QStringLiteral(
        "https://s3.example/assets/poster/w500.abc.webp?X-Amz-Date=20261027&X-Amz-Signature=1"));
    QVERIFY(!w500.isEmpty());
    QCOMPARE(posterKey(QStringLiteral(
                 "https://s3.example/assets/poster/w500.abc.webp?X-Amz-Date=20261028&X-Amz-Signature=2")),
             w500);
    QVERIFY(posterKey(QStringLiteral(
                "https://s3.example/assets/poster/w1000.abc.webp?X-Amz-Date=20261028")) != w500);
    QVERIFY(posterKey(QStringLiteral(
                "https://s3.example/assets/poster/w500.def.webp?X-Amz-Date=20261028")) != w500);
}

void ProviderCatalogTest::siloEpisodeParentAndBoundedTimes()
{
    const QJsonObject episode{
        {QStringLiteral("content_id"), QStringLiteral("episode-1")},
        {QStringLiteral("type"), QStringLiteral("episode")},
        {QStringLiteral("series_id"), QStringLiteral("series-1")},
        {QStringLiteral("season_id"), QStringLiteral("season-1")}
    };
    const QVariantMap mapped = SiloModelMapper::mediaItem(episode, QStringLiteral("silo"));
    QCOMPARE(mapped.value(QStringLiteral("parentId")).toString(),
             QStringLiteral("season-1"));

    const QVariantMap seriesFallback = SiloModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("episode-2")},
            {QStringLiteral("type"), QStringLiteral("episode")},
            {QStringLiteral("series_id"), QStringLiteral("series-1")}
        },
        QStringLiteral("silo"));
    QCOMPARE(seriesFallback.value(QStringLiteral("parentId")).toString(),
             QStringLiteral("series-1"));

    const QVariantMap zeroChapter = SiloModelMapper::chapter(
        QJsonObject{
            {QStringLiteral("start_seconds"), 0.0},
            {QStringLiteral("end_seconds"), 0.0}
        },
        QStringLiteral("silo"), QStringLiteral("episode-1"), QStringLiteral("file-1"), 0);
    QVERIFY(!zeroChapter.isEmpty());
    QCOMPARE(zeroChapter.value(QStringLiteral("startMs")).toLongLong(), 0);
    QCOMPARE(zeroChapter.value(QStringLiteral("endMs")).toLongLong(), 0);

    const QVariantMap oversizedChapter = SiloModelMapper::chapter(
        QJsonObject{{QStringLiteral("start_seconds"),
                     std::numeric_limits<double>::max()}},
        QStringLiteral("silo"), QStringLiteral("episode-1"), QStringLiteral("file-1"), 0);
    QVERIFY(oversizedChapter.isEmpty());

    const QList<MediaSegmentInfo> validSegments = SiloModelMapper::mediaSegments(
        QStringLiteral("episode-1"),
        QJsonObject{
            {QStringLiteral("file_id"), QStringLiteral("file-1")},
            {QStringLiteral("intro"), QJsonObject{
                 {QStringLiteral("start_seconds"), 0.0},
                 {QStringLiteral("end_seconds"), 1.0}}}
        });
    QCOMPARE(validSegments.size(), 1);
    QCOMPARE(validSegments.first().startMs, 0);

    const QList<MediaSegmentInfo> oversizedSegments = SiloModelMapper::mediaSegments(
        QStringLiteral("episode-1"),
        QJsonObject{
            {QStringLiteral("file_id"), QStringLiteral("file-1")},
            {QStringLiteral("intro"), QJsonObject{
                 {QStringLiteral("start_seconds"), 0.0},
                 {QStringLiteral("end_seconds"), std::numeric_limits<double>::max()}}}
        });
    QVERIFY(oversizedSegments.isEmpty());
}


void ProviderCatalogTest::siloNumericSeasonRoutesAndEnvelopeOperation()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery parentOnlyQuery;
    parentOnlyQuery.parentId = QStringLiteral("series/one");
    parentOnlyQuery.includeItemTypes = {QStringLiteral("Season")};
    const ProviderCatalogRequest seasonsRequest =
        provider.createRequest(ProviderCatalogOperation::Items, parentOnlyQuery);
    QVERIFY(seasonsRequest.supported);
    QCOMPARE(seasonsRequest.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons")));

    // Season listings keyed only by the season id (player episode navigation).
    ProviderCatalogQuery seasonEpisodesQuery;
    seasonEpisodesQuery.parentId = QStringLiteral("season/one");
    seasonEpisodesQuery.includeItemTypes = {QStringLiteral("Episode")};
    const ProviderCatalogRequest seasonEpisodesRequest =
        provider.createRequest(ProviderCatalogOperation::Items, seasonEpisodesQuery);
    QVERIFY(seasonEpisodesRequest.supported);
    QCOMPARE(seasonEpisodesRequest.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/catalog/items/season%2Fone/episodes")));

    ProviderCatalogQuery query;
    query.seriesId = QStringLiteral("series/one");
    query.parentId = QStringLiteral("0");

    ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons/0")));

    const ProviderCatalogResponse season = provider.parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"season":{"content_id":"series/one-S00","season_number":0,"title":"Specials"}})"));
    QVERIFY(season.valid);
    QCOMPARE(season.rawItem.value(QStringLiteral("content_id")).toString(),
             QStringLiteral("series/one-S00"));
    QCOMPARE(season.capabilityMetadata.value(QStringLiteral("envelope")).toString(),
             QStringLiteral("season"));

    query.includeItemTypes = {QStringLiteral("Episode")};
    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             withArtworkWidths(QStringLiteral("/api/v2/catalog/series/series%2Fone/seasons/0/episodes")));

    query.includeItemTypes = {QStringLiteral("Movie")};
    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(!request.supported);
    QVERIFY(!request.unsupportedReason.isEmpty());
}

void ProviderCatalogTest::siloThemeMetadataCapabilitiesAndGrants()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.itemId = QStringLiteral("series/one");
    auto request = provider.createRequest(ProviderCatalogOperation::ThemeSongs, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/catalog/items/series%2Fone"));
    const auto themes = provider.parseResponse(ProviderCatalogOperation::ThemeSongs,
        QByteArrayLiteral(R"({"themes":{"owner_id":"series/one","items":[{"id":"theme-1","title":"Theme","container":"mp3","duration_seconds":29}]},"future_field":true})"));
    QVERIFY(themes.valid);
    QCOMPARE(themes.capabilityMetadata.value("ownerId").toString(), query.itemId);
    QCOMPARE(themes.rawItems.size(), 1);
    query.themeId = QStringLiteral("theme/one");
    request = provider.createRequest(ProviderCatalogOperation::ThemeSongPlayback, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Never);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/catalog/items/series%2Fone/themes/theme%2Fone/playback"));
    QVERIFY(!QJsonDocument::fromJson(request.body).object().value("accepted_formats").toArray().isEmpty());
    const auto capability = provider.parseResponse(ProviderCatalogOperation::ThemeSongsCapability,
        QByteArrayLiteral(R"({"state":"available","allowed":true,"delivery":"routed","future_field":true})"),
        {{"ETag", "opaque-validator"}});
    QVERIFY(capability.valid);
    QVERIFY(capability.capabilityMetadata.value("allowed").toBool());
    QCOMPARE(capability.snapshot.value("etag").toString(), QStringLiteral("opaque-validator"));
    const auto unknown = provider.parseResponse(ProviderCatalogOperation::ThemeSongsCapability,
        QByteArrayLiteral(R"({"state":"future_state","allowed":true,"delivery":"routed"})"));
    QVERIFY(unknown.valid);
    QVERIFY(!unknown.capabilityMetadata.value("allowed").toBool());
    QJsonObject grant{{"url", "/audio?token=secret%2Bvalue"}, {"delivery", "converted"},
        {"expires_at", QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs)},
        {"content_type", "audio/mp4"}};
    auto parsed = provider.parseResponse(ProviderCatalogOperation::ThemeSongPlayback, QJsonDocument(grant).toJson());
    QVERIFY(parsed.valid);
    QVERIFY(parsed.capabilityMetadata.value("requiresFreshGrant").toBool());
    QCOMPARE(parsed.capabilityMetadata.value("url").toString(), QStringLiteral("/audio?token=secret%2Bvalue"));
    grant["delivery"] = "future_delivery";
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::ThemeSongPlayback, QJsonDocument(grant).toJson()).valid);
    grant["delivery"] = "original";
    grant["expires_at"] = QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODateWithMs);
    QVERIFY(!provider.parseResponse(ProviderCatalogOperation::ThemeSongPlayback, QJsonDocument(grant).toJson()).valid);
}

void ProviderCatalogTest::jellyfinExtrasUseOpenApiRoutesAndMapKinds()
{
    JellyfinProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);

    ProviderCatalogQuery query;
    query.userId = QStringLiteral("user-1");
    query.itemId = QStringLiteral("movie 1");

    QFile openApiManifest(QStringLiteral(BLOOM_JELLYFIN_OPENAPI_MANIFEST));
    QVERIFY2(openApiManifest.open(QIODevice::ReadOnly),
             qPrintable(openApiManifest.errorString()));
    const QJsonArray operations = QJsonDocument::fromJson(openApiManifest.readAll())
                                      .object().value(QStringLiteral("operations")).toArray();
    const auto manifestOperation = [&operations](const QString &operationId) {
        for (const QJsonValue &value : operations) {
            if (value.toObject().value(QStringLiteral("operationId")).toString() == operationId) {
                return value.toObject();
            }
        }
        return QJsonObject{};
    };

    // Both reads exist, are current, and take only the userId query.
    for (const auto &[operation, operationId, path] : {
             std::tuple{ProviderCatalogOperation::SpecialFeatures,
                        QStringLiteral("GetSpecialFeatures"),
                        QStringLiteral("/Items/movie 1/SpecialFeatures")},
             std::tuple{ProviderCatalogOperation::LocalTrailers,
                        QStringLiteral("GetLocalTrailers"),
                        QStringLiteral("/Items/movie 1/LocalTrailers")}}) {
        const ProviderCatalogRequest request = catalog->createRequest(operation, query);
        QVERIFY2(request.supported, qPrintable(request.unsupportedReason));
        QCOMPARE(request.method, ProviderHttpMethod::Get);
        QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
        const QUrl url(request.relativeEndpoint);
        QCOMPARE(url.path(), path);
        const QUrlQuery urlQuery(url);
        QCOMPARE(urlQuery.queryItems().size(), 1);
        QCOMPARE(urlQuery.queryItemValue(QStringLiteral("userId")), query.userId);

        const QJsonObject contract = manifestOperation(operationId);
        QVERIFY2(!contract.isEmpty(), qPrintable(operationId));
        QCOMPARE(contract.value(QStringLiteral("method")).toString(), QStringLiteral("GET"));
        QCOMPARE(contract.value(QStringLiteral("deprecated")).toBool(), false);
        QCOMPARE(contract.value(QStringLiteral("path")).toString(),
                 QString(path).replace(QStringLiteral("movie 1"), QStringLiteral("{itemId}")));
        bool userIdDeclared = false;
        for (const QJsonValue &parameter : contract.value(QStringLiteral("parameters")).toArray()) {
            const QJsonObject declared = parameter.toObject();
            if (declared.value(QStringLiteral("name")).toString() == QStringLiteral("userId")) {
                userIdDeclared = !declared.value(QStringLiteral("deprecated")).toBool();
            }
        }
        QVERIFY(userIdDeclared);

        ProviderCatalogQuery missingItem = query;
        missingItem.itemId.clear();
        QVERIFY(!catalog->createRequest(operation, missingItem).supported);
        QVERIFY(!catalog->parseResponse(operation, QByteArrayLiteral("{}")).valid);
    }

    // Local trailers are trailers even when ExtraType is missing.
    const ProviderCatalogResponse trailers = catalog->parseResponse(
        ProviderCatalogOperation::LocalTrailers,
        QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("Id"), QStringLiteral("trailer-1")},
                                             {QStringLiteral("Name"), QStringLiteral("Trailer")}}})
            .toJson());
    QVERIFY(trailers.valid);
    const QVariantList mappedTrailers = adapter.mapExtras(
        trailers.rawItems, QStringLiteral("jf"), QStringLiteral("movie 1"));
    QCOMPARE(mappedTrailers.size(), 1);
    QCOMPARE(mappedTrailers.first().toMap().value(QStringLiteral("extraKind")).toString(),
             Bloom::MediaExtras::kTrailer);

    const auto extra = [](const QString &id, const QJsonValue &type) {
        QJsonObject wire{{QStringLiteral("Id"), id}, {QStringLiteral("Name"), id}};
        if (!type.isUndefined()) {
            wire.insert(QStringLiteral("ExtraType"), type);
        }
        return wire;
    };
    QJsonObject featurette = extra(QStringLiteral("featurette"), QStringLiteral("Featurette"));
    featurette.insert(QStringLiteral("RunTimeTicks"), 1234567890LL);
    featurette.insert(QStringLiteral("ImageTags"),
                      QJsonObject{{QStringLiteral("Primary"), QStringLiteral("tag-1")}});
    const ProviderCatalogResponse features = catalog->parseResponse(
        ProviderCatalogOperation::SpecialFeatures,
        QJsonDocument(QJsonArray{
            featurette,
            extra(QStringLiteral("bts"), QStringLiteral("BehindTheScenes")),
            extra(QStringLiteral("deleted"), QStringLiteral("DeletedScene")),
            extra(QStringLiteral("interview"), QStringLiteral("Interview")),
            extra(QStringLiteral("scene"), QStringLiteral("Scene")),
            extra(QStringLiteral("short"), QStringLiteral("Short")),
            extra(QStringLiteral("clip"), QStringLiteral("Clip")),
            extra(QStringLiteral("unknown"), QStringLiteral("Unknown")),
            extra(QStringLiteral("untyped"), QJsonValue::Null),
            extra(QStringLiteral("theme-song"), QStringLiteral("ThemeSong")),
            extra(QStringLiteral("theme-video"), QStringLiteral("ThemeVideo")),
            extra(QStringLiteral("sample"), QStringLiteral("Sample")),
            QJsonObject{{QStringLiteral("ExtraType"), QStringLiteral("Clip")}}})
            .toJson());
    QVERIFY(features.valid);
    const QVariantList mapped = adapter.mapExtras(
        features.rawItems, QStringLiteral("jf"), QStringLiteral("movie 1"));
    QStringList kinds;
    QStringList ids;
    for (const QVariant &value : mapped) {
        ids.append(value.toMap().value(QStringLiteral("itemId")).toString());
        kinds.append(value.toMap().value(QStringLiteral("extraKind")).toString());
    }
    QCOMPARE(ids, (QStringList{QStringLiteral("featurette"), QStringLiteral("bts"),
                               QStringLiteral("deleted"), QStringLiteral("interview"),
                               QStringLiteral("scene"), QStringLiteral("short"),
                               QStringLiteral("clip"), QStringLiteral("unknown"),
                               QStringLiteral("untyped")}));
    QCOMPARE(kinds, (QStringList{QStringLiteral("featurette"), QStringLiteral("behindTheScenes"),
                                 QStringLiteral("deletedScene"), QStringLiteral("interview"),
                                 QStringLiteral("scene"), QStringLiteral("short"),
                                 QStringLiteral("clip"), QStringLiteral("other"),
                                 QStringLiteral("other")}));

    const QVariantMap first = mapped.first().toMap();
    QCOMPARE(first.value(QStringLiteral("connectionId")).toString(), QStringLiteral("jf"));
    QCOMPARE(first.value(QStringLiteral("parentId")).toString(), QStringLiteral("movie 1"));
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("featurette"));
    QCOMPARE(first.value(QStringLiteral("durationMs")).toLongLong(), 123456LL);
    QCOMPARE(first.value(QStringLiteral("media")).toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("featurette"));
    const QVariantMap still = first.value(QStringLiteral("primaryArtwork")).toMap();
    QCOMPARE(still.value(QStringLiteral("itemId")).toString(), QStringLiteral("featurette"));
    QCOMPARE(still.value(QStringLiteral("kind")).toString(), QStringLiteral("primary"));
    QCOMPARE(still.value(QStringLiteral("tag")).toString(), QStringLiteral("tag-1"));
    // Missing runtime and image stay empty rather than invented.
    const QVariantMap bare = mapped.at(1).toMap();
    QCOMPARE(bare.value(QStringLiteral("durationMs")).toLongLong(), 0LL);
    QVERIFY(!bare.contains(QStringLiteral("primaryArtwork")));

    QVERIFY(adapter.mapExtras({}, QStringLiteral("jf"), QStringLiteral("movie 1")).isEmpty());
}

void ProviderCatalogTest::siloExtrasMapFromCatalogItemDetail()
{
    SiloProviderAdapter adapter;
    const ICatalogProvider *catalog = adapter.catalogProvider();
    QVERIFY(catalog);
    ProviderCatalogQuery query;
    query.itemId = QStringLiteral("movie-1");
    // Extras arrive with the detail; there is no separate extras read.
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::SpecialFeatures, query).supported);
    QVERIFY(!catalog->createRequest(ProviderCatalogOperation::LocalTrailers, query).supported);
    QVERIFY(catalog->createRequest(ProviderCatalogOperation::Item, query).relativeEndpoint
                .contains(QStringLiteral("still_width=1280")));

    const QJsonObject detail{
        {QStringLiteral("content_id"), QStringLiteral("movie-1")},
        {QStringLiteral("type"), QStringLiteral("movie")},
        {QStringLiteral("extras"), QJsonArray{
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-trailer")},
                         {QStringLiteral("kind"), QStringLiteral("trailer")},
                         {QStringLiteral("title"), QStringLiteral(" Official Trailer ")},
                         {QStringLiteral("duration_seconds"), 151},
                         {QStringLiteral("file_id"), 42},
                         {QStringLiteral("still_url"), QStringLiteral(
                              "https://s3.example/local/extras/x-trailer/still/original.a.webp?X-Amz-Signature=1")},
                         {QStringLiteral("still_thumbhash"), QStringLiteral("hash-1")}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-teaser")},
                         {QStringLiteral("kind"), QStringLiteral("teaser")}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-bts")},
                         {QStringLiteral("kind"), QStringLiteral("behind_the_scenes")}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-deleted")},
                         {QStringLiteral("kind"), QStringLiteral("deleted_scene")}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-bloopers")},
                         {QStringLiteral("kind"), QStringLiteral("bloopers")}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("x-new")},
                         {QStringLiteral("kind"), QStringLiteral("making_of")}},
             QJsonObject{{QStringLiteral("kind"), QStringLiteral("featurette")}}}}
    };
    const QVariantMap item = adapter.mapMediaItem(detail, QStringLiteral("silo"));
    const QVariantList extras = item.value(QStringLiteral("extras")).toList();
    QStringList kinds;
    for (const QVariant &value : extras) {
        kinds.append(value.toMap().value(QStringLiteral("extraKind")).toString());
    }
    // An extra without content_id cannot play and is dropped; an unknown
    // kind folds to other.
    QCOMPARE(kinds, (QStringList{QStringLiteral("trailer"), QStringLiteral("teaser"),
                                 QStringLiteral("behindTheScenes"), QStringLiteral("deletedScene"),
                                 QStringLiteral("bloopers"), QStringLiteral("other")}));

    const QVariantMap trailer = extras.first().toMap();
    QCOMPARE(trailer.value(QStringLiteral("itemId")).toString(), QStringLiteral("x-trailer"));
    QCOMPARE(trailer.value(QStringLiteral("connectionId")).toString(), QStringLiteral("silo"));
    QCOMPARE(trailer.value(QStringLiteral("parentId")).toString(), QStringLiteral("movie-1"));
    QCOMPARE(trailer.value(QStringLiteral("name")).toString(), QStringLiteral("Official Trailer"));
    QCOMPARE(trailer.value(QStringLiteral("durationMs")).toLongLong(), 151000LL);
    QCOMPARE(trailer.value(QStringLiteral("fileId")).toString(), QStringLiteral("42"));
    QCOMPARE(trailer.value(QStringLiteral("media")).toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("x-trailer"));
    const QVariantMap still = trailer.value(QStringLiteral("primaryArtwork")).toMap();
    // The parent owns the still; signed query changes keep its cache key.
    QCOMPARE(still.value(QStringLiteral("itemId")).toString(), QStringLiteral("movie-1"));
    QCOMPARE(still.value(QStringLiteral("ownerKind")).toString(), QStringLiteral("extra"));
    QCOMPARE(still.value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(trailer.value(QStringLiteral("artworkThumbhashes")).toMap()
                 .value(QStringLiteral("poster")).toString(),
             QStringLiteral("hash-1"));
    QCOMPARE(Bloom::ArtworkRef::fromVariantMap(still).ownerKind, Bloom::ArtworkOwnerKind::Extra);

    // Missing title, runtime and still stay empty.
    const QVariantMap teaser = extras.at(1).toMap();
    QVERIFY(teaser.value(QStringLiteral("name")).toString().isEmpty());
    QCOMPARE(teaser.value(QStringLiteral("durationMs")).toLongLong(), 0LL);
    QVERIFY(!teaser.contains(QStringLiteral("primaryArtwork")));

    // Cards carry no extras key.
    const QVariantList cards = adapter.mapMediaItems(
        QJsonArray{QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie-1")},
                               {QStringLiteral("type"), QStringLiteral("movie")}}},
        QStringLiteral("silo"));
    QCOMPARE(cards.size(), 1);
    QVERIFY(!cards.first().toMap().contains(QStringLiteral("extras")));
    // A detail always carries the key, empty when Silo sends an empty list
    // or (as it does when there are none) omits `extras`, so the fresh
    // detail replaces extras cached from an earlier one.
    for (const QJsonObject &wire : {
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie-1")},
                         {QStringLiteral("extras"), QJsonArray{}}},
             QJsonObject{{QStringLiteral("content_id"), QStringLiteral("movie-1")}}}) {
        const QVariantMap noExtras = adapter.mapMediaItem(wire, QStringLiteral("silo"));
        QVERIFY(noExtras.contains(QStringLiteral("extras")));
        QVERIFY(noExtras.value(QStringLiteral("extras")).toList().isEmpty());
    }
}

void ProviderCatalogTest::preferredTrailerPrefersTrailerThenTeaser()
{
    namespace Extras = Bloom::MediaExtras;
    const auto extra = [](const QString &id, const QString &kind) {
        return QVariantMap{{QStringLiteral("itemId"), id}, {QStringLiteral("extraKind"), kind}};
    };
    QVERIFY(Extras::preferredTrailer({}).isEmpty());
    QVERIFY(Extras::preferredTrailer({extra(QStringLiteral("a"), Extras::kFeaturette)}).isEmpty());
    QCOMPARE(Extras::preferredTrailer({extra(QStringLiteral("a"), Extras::kFeaturette),
                                       extra(QStringLiteral("b"), Extras::kTeaser)})
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("b"));
    QCOMPARE(Extras::preferredTrailer({extra(QStringLiteral("b"), Extras::kTeaser),
                                       extra(QString(), Extras::kTrailer),
                                       extra(QStringLiteral("c"), Extras::kTrailer)})
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("c"));
    QCOMPARE(Extras::normalizedKind(QStringLiteral("making_of")), Extras::kOther);
    QCOMPARE(Extras::normalizedKind(Extras::kDeletedScene), Extras::kDeletedScene);
}

QTEST_MAIN(ProviderCatalogTest)
#include "ProviderCatalogTest.moc"
