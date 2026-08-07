#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include "providers/ICatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloModelMapper.h"

class ProviderCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinItemsRequestRetainsNativeContract();
    void jellyfinMutationsAndPaginationRemainCompatible();
    void siloCanonicalIdentityVersionsMultipartAndStateUseMilliseconds();
};

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
    query.sortBy = QStringLiteral("PremiereDate");
    query.sortOrder = QStringLiteral("Descending");
    query.watched = ProviderCatalogTriState::No;
    query.recursive = true;
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
    QCOMPARE(url.path(), QStringLiteral("/Users/user-1/Items"));
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
    QVERIFY(parameters.queryItemValue(QStringLiteral("Fields"))
                .contains(QStringLiteral("MediaSources")));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("EnableImageTypes")),
             QStringLiteral("Primary,Backdrop,Thumb,Logo"));
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
    QCOMPARE(markWatched.relativeEndpoint,
             QStringLiteral("/Users/user-1/PlayedItems/item-9"));
    QCOMPARE(markWatched.extraHeaders.value(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/json"));

    query.stateValue = false;
    const ProviderCatalogRequest clearFavorite = catalog->createRequest(
        ProviderCatalogOperation::SetFavorite, query);
    QVERIFY(clearFavorite.supported);
    QCOMPARE(clearFavorite.method, ProviderHttpMethod::Delete);
    QCOMPARE(clearFavorite.relativeEndpoint,
             QStringLiteral("/Users/user-1/FavoriteItems/item-9"));

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
             {QStringLiteral("Tmdb"), 7654}
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

    const QVariantMap mappedVersion = mapped.value(QStringLiteral("versions"))
                                          .toList().constFirst().toMap();
    QCOMPARE(mappedVersion.value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(mappedVersion.value(QStringLiteral("durationMs")).toLongLong(), 123456);
    QCOMPARE(mappedVersion.value(QStringLiteral("presentationPartIndex")).toInt(), 1);
    QCOMPARE(mappedVersion.value(QStringLiteral("presentationPartTotal")).toInt(), 2);
    const QVariantMap chapter = mappedVersion.value(QStringLiteral("chapters"))
                                    .toList().constFirst().toMap();
    QCOMPARE(chapter.value(QStringLiteral("fileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(chapter.value(QStringLiteral("startMs")).toLongLong(), 1250);
    QCOMPARE(chapter.value(QStringLiteral("endMs")).toLongLong(), 4750);

    const QVariantMap playbackVariant = mapped.value(QStringLiteral("playbackVariants"))
                                            .toList().constFirst().toMap();
    QCOMPARE(playbackVariant.value(QStringLiteral("defaultFileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(playbackVariant.value(QStringLiteral("partCount")).toInt(), 2);
    QCOMPARE(playbackVariant.value(QStringLiteral("totalDurationMs")).toLongLong(),
             246912);
    const QVariantMap part = playbackVariant.value(QStringLiteral("parts"))
                                 .toList().constFirst().toMap();
    QCOMPARE(part.value(QStringLiteral("partIndex")).toInt(), 1);
    QCOMPARE(part.value(QStringLiteral("defaultFileId")).toString(),
             QStringLiteral("file-99"));
    QCOMPARE(part.value(QStringLiteral("versions")).toList()
                 .constFirst().toMap().value(QStringLiteral("fileId")).toString(),
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

QTEST_MAIN(ProviderCatalogTest)
#include "ProviderCatalogTest.moc"
