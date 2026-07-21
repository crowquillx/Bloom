#include <QtTest/QtTest>

#include "models/MediaModels.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <limits>

class CanonicalModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinTimeConversionUsesMilliseconds();
    void jellyfinItemMapsToCanonicalCamelCase();
    void jellyfinParentBackdropUsesImageItemId();
    void jellyfinSeriesFieldsAndChaptersMapCanonically();
    void artworkCacheKeyIsTokenFreeAndRoundTrips();
    void jellyfinArtworkEndpointContainsNoCredential();
    void playbackDescriptorExposesProviderNeutralShape();
    void jellyfinPlaybackProviderFinalizesStreamAtBoundary();
};

void CanonicalModelsTest::jellyfinTimeConversionUsesMilliseconds()
{
    QCOMPARE(JellyfinModelMapper::ticksToMilliseconds(10'000'000), 1000);
    QCOMPARE(JellyfinModelMapper::ticksToMilliseconds(-1), 0);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(1000), 10'000'000);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(-1), 0);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(std::numeric_limits<qint64>::max()),
             std::numeric_limits<qint64>::max());
}

void CanonicalModelsTest::jellyfinItemMapsToCanonicalCamelCase()
{
    const QJsonObject wire{
        {QStringLiteral("Id"), QStringLiteral("movie-1")},
        {QStringLiteral("Name"), QStringLiteral("Example")},
        {QStringLiteral("SortName"), QStringLiteral("Example, The")},
        {QStringLiteral("Type"), QStringLiteral("Movie")},
        {QStringLiteral("ParentId"), QStringLiteral("parent-1")},
        {QStringLiteral("ParentPrimaryImageItemId"), QStringLiteral("parent-image-owner")},
        {QStringLiteral("SeriesId"), QStringLiteral("series-1")},
        {QStringLiteral("ParentPrimaryImageTag"), QStringLiteral("parent-primary-tag")},
        {QStringLiteral("SeriesPrimaryImageTag"), QStringLiteral("series-primary-tag")},
        {QStringLiteral("Overview"), QStringLiteral("A sample overview")},
        {QStringLiteral("ProductionYear"), 2024},
        {QStringLiteral("PremiereDate"), QStringLiteral("2024-01-15T00:00:00.000Z")},
        {QStringLiteral("OfficialRating"), QStringLiteral("PG-13")},
        {QStringLiteral("CommunityRating"), 8.5},
        {QStringLiteral("RunTimeTicks"), 72'000'000'000.0},
        {QStringLiteral("Genres"), QJsonArray{QStringLiteral("Action"), QStringLiteral("Sci-Fi")}},
        {QStringLiteral("ImageTags"), QJsonObject{
             {QStringLiteral("Primary"), QStringLiteral("primary-tag")},
             {QStringLiteral("Logo"), QStringLiteral("logo-tag")}
         }},
        {QStringLiteral("BackdropImageTags"), QJsonArray{QStringLiteral("backdrop-tag")}},
        {QStringLiteral("ProviderIds"), QJsonObject{
             {QStringLiteral("Imdb"), QStringLiteral("tt123")},
             {QStringLiteral("Tmdb"), QStringLiteral("42")}
         }},
        {QStringLiteral("People"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("Id"), QStringLiteral("person-1")},
                 {QStringLiteral("Name"), QStringLiteral("Ada")},
                 {QStringLiteral("Role"), QStringLiteral("Director")},
                 {QStringLiteral("Type"), QStringLiteral("Director")},
                 {QStringLiteral("PrimaryImageTag"), QStringLiteral("person-tag")}
             }
         }},
        {QStringLiteral("UserData"), QJsonObject{
             {QStringLiteral("Played"), true},
             {QStringLiteral("IsFavorite"), true},
             {QStringLiteral("PlaybackPositionTicks"), 15'000'000.0}
         }}
    };

    const QVariantMap item = JellyfinModelMapper::mediaItem(
        wire, QStringLiteral("connection-1"));

    QCOMPARE(item.value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("connection-1"));
    QCOMPARE(item.value(QStringLiteral("itemId")).toString(), QStringLiteral("movie-1"));
    QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("Example"));
    QCOMPARE(item.value(QStringLiteral("sortName")).toString(), QStringLiteral("Example, The"));
    QCOMPARE(item.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(item.value(QStringLiteral("overview")).toString(), QStringLiteral("A sample overview"));
    QCOMPARE(item.value(QStringLiteral("productionYear")).toInt(), 2024);
    QCOMPARE(item.value(QStringLiteral("premiereDate")).toString(),
             QStringLiteral("2024-01-15T00:00:00.000Z"));
    QCOMPARE(item.value(QStringLiteral("officialRating")).toString(), QStringLiteral("PG-13"));
    QCOMPARE(item.value(QStringLiteral("communityRating")).toDouble(), 8.5);
    QCOMPARE(item.value(QStringLiteral("durationMs")).toLongLong(), 7'200'000);
    QCOMPARE(item.value(QStringLiteral("positionMs")).toLongLong(), 1500);
    QVERIFY(item.value(QStringLiteral("watched")).toBool());
    QVERIFY(item.value(QStringLiteral("favorite")).toBool());
    QCOMPARE(item.value(QStringLiteral("genres")).toList().size(), 2);
    QCOMPARE(item.value(QStringLiteral("genres")).toList().at(0).toString(),
             QStringLiteral("Action"));
    QCOMPARE(item.value(QStringLiteral("providerIds")).toMap()
                 .value(QStringLiteral("Imdb")).toString(), QStringLiteral("tt123"));
    QCOMPARE(item.value(QStringLiteral("providerIds")).toMap()
                 .value(QStringLiteral("Tmdb")).toString(), QStringLiteral("42"));

    for (const QString &wireKey : {
             QStringLiteral("Id"),
             QStringLiteral("RunTimeTicks"),
             QStringLiteral("ImageTags"),
             QStringLiteral("BackdropImageTags"),
             QStringLiteral("UserData"),
             QStringLiteral("ProviderIds")}) {
        QVERIFY2(!item.contains(wireKey), qPrintable(wireKey));
    }

    const QVariantMap primary = item.value(QStringLiteral("primaryArtwork")).toMap();
    QCOMPARE(primary.value(QStringLiteral("kind")).toString(), QStringLiteral("primary"));
    QCOMPARE(primary.value(QStringLiteral("tag")).toString(), QStringLiteral("primary-tag"));
    QCOMPARE(item.value(QStringLiteral("logoArtwork")).toMap()
                 .value(QStringLiteral("tag")).toString(), QStringLiteral("logo-tag"));
    const QVariantMap parentPrimary =
        item.value(QStringLiteral("parentPrimaryArtwork")).toMap();
    QCOMPARE(parentPrimary.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("parent-image-owner"));
    QCOMPARE(parentPrimary.value(QStringLiteral("tag")).toString(),
             QStringLiteral("parent-primary-tag"));
    const QVariantMap seriesPrimary =
        item.value(QStringLiteral("seriesPrimaryArtwork")).toMap();
    QCOMPARE(seriesPrimary.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("series-1"));
    QCOMPARE(seriesPrimary.value(QStringLiteral("tag")).toString(),
             QStringLiteral("series-primary-tag"));
    const QVariantMap fallbackParentPrimary = JellyfinModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("episode-2")},
            {QStringLiteral("ParentId"), QStringLiteral("season-2")},
            {QStringLiteral("ParentPrimaryImageTag"), QStringLiteral("fallback-tag")}
        },
        QStringLiteral("connection-1"))
        .value(QStringLiteral("parentPrimaryArtwork")).toMap();
    QCOMPARE(fallbackParentPrimary.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("season-2"));
    QCOMPARE(fallbackParentPrimary.value(QStringLiteral("tag")).toString(),
             QStringLiteral("fallback-tag"));
    QCOMPARE(item.value(QStringLiteral("backdropArtwork")).toMap()
                 .value(QStringLiteral("tag")).toString(), QStringLiteral("backdrop-tag"));

    const QVariantList people = item.value(QStringLiteral("people")).toList();
    QCOMPARE(people.size(), 1);
    const QVariantMap person = people.first().toMap();
    QCOMPARE(person.value(QStringLiteral("name")).toString(), QStringLiteral("Ada"));
    QCOMPARE(person.value(QStringLiteral("role")).toString(), QStringLiteral("Director"));
    QCOMPARE(person.value(QStringLiteral("personId")).toString(), QStringLiteral("person-1"));
    QCOMPARE(person.value(QStringLiteral("artwork")).toMap()
                 .value(QStringLiteral("kind")).toString(), QStringLiteral("person"));

    const QVariantList similar = JellyfinModelMapper::mediaItems(
        QJsonArray{wire}, QStringLiteral("connection-1"));
    QCOMPARE(similar.size(), 1);
    QCOMPARE(similar.first().toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("movie-1"));
}

void CanonicalModelsTest::jellyfinParentBackdropUsesImageItemId()
{
    const QVariantMap item = JellyfinModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("episode-1")},
            {QStringLiteral("ParentBackdropItemId"), QString()},
            {QStringLiteral("ParentBackdropImageItemId"), QStringLiteral("series-1")},
            {QStringLiteral("ParentBackdropImageTags"),
             QJsonArray{QStringLiteral("series-backdrop-tag")}}
        },
        QStringLiteral("connection-1"));

    const QVariantMap artwork = item.value(QStringLiteral("backdropArtwork")).toMap();
    QCOMPARE(artwork.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("series-1"));
    QCOMPARE(artwork.value(QStringLiteral("tag")).toString(),
             QStringLiteral("series-backdrop-tag"));
}

void CanonicalModelsTest::jellyfinSeriesFieldsAndChaptersMapCanonically()
{
    const QVariantMap episode = JellyfinModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("episode-1")},
            {QStringLiteral("Type"), QStringLiteral("Episode")},
            {QStringLiteral("RecursiveItemCount"), 48},
            {QStringLiteral("Status"), QStringLiteral("Continuing")},
            {QStringLiteral("AirsBeforeSeasonNumber"), 2},
            {QStringLiteral("AirsAfterSeasonNumber"), 1},
            {QStringLiteral("AirsBeforeEpisodeNumber"), 4}
        },
        QStringLiteral("connection-1"));

    QCOMPARE(episode.value(QStringLiteral("recursiveItemCount")).toInt(), 48);
    QCOMPARE(episode.value(QStringLiteral("status")).toString(), QStringLiteral("Continuing"));
    QCOMPARE(episode.value(QStringLiteral("airsBeforeSeasonNumber")).toInt(), 2);
    QCOMPARE(episode.value(QStringLiteral("airsAfterSeasonNumber")).toInt(), 1);
    QCOMPARE(episode.value(QStringLiteral("airsBeforeEpisodeNumber")).toInt(), 4);

    const QVariantList chapters = JellyfinModelMapper::chapters(
        QJsonArray{
            QJsonObject{
                {QStringLiteral("Name"), QStringLiteral("Cold Open")},
                {QStringLiteral("StartPositionTicks"), 125'000'000},
                {QStringLiteral("ImagePath"), QStringLiteral("/Items/episode-1/Images/Chapter/0")}
            }
        },
        QStringLiteral("connection-1"),
        QStringLiteral("episode-1"));

    QCOMPARE(chapters.size(), 1);
    const QVariantMap chapter = chapters.first().toMap();
    QCOMPARE(chapter.value(QStringLiteral("name")).toString(), QStringLiteral("Cold Open"));
    QCOMPARE(chapter.value(QStringLiteral("startMs")).toLongLong(), 12'500);
    const QVariantMap artwork = chapter.value(QStringLiteral("artwork")).toMap();
    QCOMPARE(artwork.value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("connection-1"));
    QCOMPARE(artwork.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("episode-1"));
    QCOMPARE(artwork.value(QStringLiteral("kind")).toString(), QStringLiteral("chapter"));
    QCOMPARE(artwork.value(QStringLiteral("index")).toInt(), 0);
    QVERIFY(!chapter.contains(QStringLiteral("StartPositionTicks")));
    QVERIFY(!chapter.contains(QStringLiteral("ImagePath")));

    const Bloom::Chapter unnamed = JellyfinModelMapper::chapter(
        QJsonObject{}, QStringLiteral("connection-1"), QStringLiteral("episode-1"), 2);
    QCOMPARE(unnamed.name, QStringLiteral("Chapter 3"));
}

void CanonicalModelsTest::artworkCacheKeyIsTokenFreeAndRoundTrips()
{
    Bloom::ArtworkRef ref;
    ref.connectionId = QStringLiteral("connection-1");
    ref.itemId = QStringLiteral("movie-1");
    ref.kind = Bloom::ArtworkKind::Backdrop;
    ref.index = 2;
    ref.tag = QStringLiteral("tag-1");
    ref.requestedWidth = 1920;

    const QString key = ref.cacheKey();
    QVERIFY(key.startsWith(QStringLiteral("artwork:")));
    QVERIFY(!key.contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
    QVERIFY(!key.contains(QStringLiteral("secret-token")));

    const Bloom::ArtworkRef decoded = Bloom::ArtworkRef::fromCacheKey(key);
    QCOMPARE(decoded.connectionId, ref.connectionId);
    QCOMPARE(decoded.itemId, ref.itemId);
    QCOMPARE(static_cast<int>(decoded.kind), static_cast<int>(ref.kind));
    QCOMPARE(decoded.index, ref.index);
    QCOMPARE(decoded.tag, ref.tag);
    QCOMPARE(decoded.requestedWidth, ref.requestedWidth);
}

void CanonicalModelsTest::jellyfinArtworkEndpointContainsNoCredential()
{
    Bloom::ArtworkRef artwork;
    artwork.connectionId = QStringLiteral("connection-1");
    artwork.itemId = QStringLiteral("episode-1");
    artwork.kind = Bloom::ArtworkKind::Chapter;
    artwork.index = 3;
    artwork.tag = QStringLiteral("chapter-tag");
    artwork.requestedWidth = 480;

    const QString endpoint = JellyfinModelMapper::artworkEndpoint(artwork);
    QVERIFY(endpoint.startsWith(QStringLiteral("/Items/episode-1/Images/Chapter/3?")));
    QVERIFY(endpoint.contains(QStringLiteral("maxWidth=480")));
    QVERIFY(endpoint.contains(QStringLiteral("tag=chapter-tag")));
    QVERIFY(!endpoint.contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
    QVERIFY(!endpoint.contains(QStringLiteral("token"), Qt::CaseInsensitive));
}

void CanonicalModelsTest::jellyfinPlaybackProviderFinalizesStreamAtBoundary()
{
    const PlaybackProviderContext context{
        QUrl(QStringLiteral("https://media.example.test/base/")),
        QStringLiteral("secret-token")
    };
    const Bloom::MediaRef media{
        QStringLiteral("connection-1"),
        QStringLiteral("movie-1")
    };
    const QVariantMap source{
        {QStringLiteral("id"), QStringLiteral("source-1")},
        {QStringLiteral("directStreamUrl"), QStringLiteral("/Videos/movie-1/stream")},
        {QStringLiteral("runTimeTicks"), 20'000'000},
        {QStringLiteral("mediaStreams"), QVariantList{
             QVariantMap{
                 {QStringLiteral("type"), QStringLiteral("Audio")},
                 {QStringLiteral("index"), 2},
                 {QStringLiteral("language"), QStringLiteral("eng")}
             },
             QVariantMap{
                 {QStringLiteral("type"), QStringLiteral("Subtitle")},
                 {QStringLiteral("index"), 4},
                 {QStringLiteral("isExternal"), true}
             }
         }}
    };

    const JellyfinPlaybackProvider provider;
    const Bloom::PlaybackDescriptor descriptor = provider.createDescriptor(
        context, media, source, 2, 4, 500, QStringLiteral("session-1"));

    QVERIFY(descriptor.isValid());
    QCOMPARE(descriptor.durationMs, 2000);
    QCOMPARE(descriptor.startPositionMs, 500);
    QCOMPARE(descriptor.playbackSessionId, QStringLiteral("session-1"));
    QCOMPARE(descriptor.audioTracks.size(), 1);
    QCOMPARE(descriptor.subtitleTracks.size(), 1);
    QCOMPARE(descriptor.stream.method, Bloom::PlaybackMethod::DirectStream);
    QVERIFY(descriptor.stream.pinsAudioTrack);
    QVERIFY(descriptor.stream.pinsSubtitleTrack);
    QCOMPARE(descriptor.stream.pinnedAudioTrackId, QStringLiteral("2"));
    QCOMPARE(descriptor.stream.pinnedSubtitleTrackId, QStringLiteral("4"));

    const QUrlQuery query(descriptor.stream.url);
    QCOMPARE(descriptor.stream.url.host(), QStringLiteral("media.example.test"));
    QCOMPARE(descriptor.stream.url.path(), QStringLiteral("/base/Videos/movie-1/stream"));
    QCOMPARE(query.queryItemValue(QStringLiteral("api_key")), QStringLiteral("secret-token"));
    QCOMPARE(query.queryItemValue(QStringLiteral("MediaSourceId")), QStringLiteral("source-1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("AudioStreamIndex")), QStringLiteral("2"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SubtitleStreamIndex")), QStringLiteral("4"));

    QVariantMap fallbackSource = source;
    fallbackSource.remove(QStringLiteral("directStreamUrl"));
    const Bloom::PlaybackDescriptor fallback = provider.createDescriptor(
        context, media, fallbackSource, -1, -1, 0);
    QCOMPARE(fallback.stream.method, Bloom::PlaybackMethod::DirectPlay);
    QCOMPARE(fallback.stream.url.path(), QStringLiteral("/base/Videos/movie-1/stream"));

    QVariantMap prefixedSource = source;
    prefixedSource[QStringLiteral("directStreamUrl")] =
        QStringLiteral("/base/Videos/movie-1/stream");
    const Bloom::PlaybackDescriptor prefixed = provider.createDescriptor(
        context, media, prefixedSource, -1, -1, 0);
    QCOMPARE(prefixed.stream.url.path(), QStringLiteral("/base/Videos/movie-1/stream"));
}

void CanonicalModelsTest::playbackDescriptorExposesProviderNeutralShape()
{
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = {QStringLiteral("connection-1"), QStringLiteral("movie-1")};
    descriptor.stream.url = QUrl(QStringLiteral("https://media.example.test/stream"));
    descriptor.stream.headers = {
        {QStringLiteral("Authorization"), QStringLiteral("opaque")}
    };
    QVERIFY(!descriptor.isValid());
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.durationMs = 7'200'000;
    descriptor.startPositionMs = 30'000;
    descriptor.reporting = {true, true, true, true};

    QVERIFY(descriptor.isValid());
    const QVariantMap map = descriptor.toVariantMap();
    QCOMPARE(map.value(QStringLiteral("durationMs")).toLongLong(), 7'200'000);
    QCOMPARE(map.value(QStringLiteral("startPositionMs")).toLongLong(), 30'000);
    QCOMPARE(map.value(QStringLiteral("stream")).toMap()
                 .value(QStringLiteral("method")).toString(), QStringLiteral("directPlay"));
    QVERIFY(!map.contains(QStringLiteral("RunTimeTicks")));
    QVERIFY(!map.contains(QStringLiteral("MediaSourceId")));
}

QTEST_MAIN(CanonicalModelsTest)
#include "CanonicalModelsTest.moc"
