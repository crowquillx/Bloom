#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <limits>

#include "providers/ICatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloModelMapper.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloCatalogProvider.h"

class ProviderCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinItemsRequestRetainsNativeContract();
    void jellyfinReservedIdentifiersAndLimitsRemainOpaque();
    void jellyfinMutationsAndPaginationRemainCompatible();
    void siloTrackIndicesAndMultipartFallbackStayContiguous();
    void siloCanonicalIdentityVersionsMultipartAndStateUseMilliseconds();
    void canonicalArtworkAndPlaybackChapterMetadataRemainProviderNeutral();
    void siloEpisodeParentAndBoundedTimes();
    void siloNumericSeasonRoutesAndEnvelopeOperation();

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
    QVERIFY(items.relativeEndpoint.contains(QStringLiteral("user%2F%26%3F%3D")));
    const QUrl itemsUrl(items.relativeEndpoint);
    QCOMPARE(itemsUrl.path(QUrl::FullyEncoded),
             QStringLiteral("/Users/user%2F%26%3F%3D/Items"));
    QCOMPARE(QUrlQuery(itemsUrl).queryItemValue(QStringLiteral("ParentId")),
             query.parentId);

    const ProviderCatalogRequest item = catalog->createRequest(
        ProviderCatalogOperation::Item, query);
    QVERIFY(item.supported);
    QVERIFY(item.relativeEndpoint.contains(QStringLiteral("item%2Fchild%3Fx%3D1")));
    QCOMPARE(QUrl(item.relativeEndpoint).path(QUrl::FullyEncoded),
             QStringLiteral("/Users/user%2F%26%3F%3D/Items/item%2Fchild%3Fx%3D1"));

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

void ProviderCatalogTest::siloTrackIndicesAndMultipartFallbackStayContiguous()
{
    const QJsonObject version{
        {QStringLiteral("file_id"), QStringLiteral("file-1")},
        {QStringLiteral("audio_tracks"), QJsonArray{
             QJsonValue(QStringLiteral("malformed")),
             QJsonObject{{QStringLiteral("index"), 7},
                          {QStringLiteral("language"), QStringLiteral("eng")}},
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
        "https://silo.example.test/api/v1/artwork/file-99/4?signature=rotating");

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
        "https://silo.example.test/api/v1/playback/file-99"));
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
    const ProviderCatalogRequest seasonsRequest =
        provider.createRequest(ProviderCatalogOperation::Items, parentOnlyQuery);
    QVERIFY(seasonsRequest.supported);
    QCOMPARE(seasonsRequest.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/series/series%2Fone/seasons"));

    ProviderCatalogQuery query;
    query.seriesId = QStringLiteral("series/one");
    query.parentId = QStringLiteral("0");

    ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/series/series%2Fone/seasons/0"));

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
             QStringLiteral("/api/v1/catalog/series/series%2Fone/seasons/0/episodes"));

    query.includeItemTypes = {QStringLiteral("Movie")};
    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(!request.supported);
    QVERIFY(!request.unsupportedReason.isEmpty());
}

QTEST_MAIN(ProviderCatalogTest)
#include "ProviderCatalogTest.moc"
