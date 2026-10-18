#include <QtTest/QtTest>

#include "models/MediaModels.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "providers/silo/SiloModelMapper.h"
#include "player/AudioPolicy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <limits>

class CanonicalModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinTimeConversionUsesMilliseconds();
    void jellyfinPlaybackInfoMapsToMilliseconds();
    void audioSourceMetadataUsesStructuredProviderEvidence();
    void jellyfinAndSiloAudioMetadataShareAudioPolicyPlan();
    void jellyfinTrickplayAndSegmentsMapAtProviderBoundary();
    void jellyfinRemoteSessionsMapAtProviderBoundary();
    void jellyfinItemMapsToCanonicalCamelCase();
    void jellyfinParentBackdropUsesImageItemId();
    void jellyfinSeriesFieldsAndChaptersMapCanonically();
    void artworkCacheKeyIsTokenFreeAndRoundTrips();
    void jellyfinArtworkEndpointContainsNoCredential();
    void playbackDescriptorExposesProviderNeutralShape();
    void jellyfinPlaybackProviderFinalizesStreamAtBoundary();
    void jellyfinDirectPlayAvailabilityControlsStaticStreamSelection();
    void jellyfinPlaybackProviderFinalizesTrickplayUrl();
    void jellyfinPlaybackInfoRequestUsesBodyUserId();
    void jellyfinPlaybackReportsSerializeAtProviderBoundary();
};

void CanonicalModelsTest::audioSourceMetadataUsesStructuredProviderEvidence()
{
    const Bloom::AudioSourceMetadata masterAudio = Bloom::AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("dts")},
        {QStringLiteral("profile"), QStringLiteral("DTS-HD MA")},
        {QStringLiteral("title"), QStringLiteral("DTS:X")}
    });
    QCOMPARE(masterAudio.fidelity, QStringLiteral("lossless"));
    QVERIFY(masterAudio.dtsX);

    const Bloom::AudioSourceMetadata highResolution = Bloom::AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("dts-hd")},
        {QStringLiteral("profile"), QStringLiteral("DTS-HD HRA")}
    });
    QCOMPARE(highResolution.fidelity, QStringLiteral("lossy"));

    const Bloom::AudioSourceMetadata filenameOnly = Bloom::AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("truehd")},
        {QStringLiteral("path"), QStringLiteral("/library/Dolby.Atmos/title.mkv")}
    });
    QVERIFY(!filenameOnly.atmos);
    QCOMPARE(filenameOnly.fidelity, QStringLiteral("lossless"));
}

void CanonicalModelsTest::jellyfinAndSiloAudioMetadataShareAudioPolicyPlan()
{
    const PlaybackInfoResponse jellyfin = JellyfinModelMapper::playbackInfo({
        {QStringLiteral("MediaSources"), QJsonArray{QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("jf-source")},
            {QStringLiteral("MediaStreams"), QJsonArray{QJsonObject{
                {QStringLiteral("Index"), 0},
                {QStringLiteral("Type"), QStringLiteral("Audio")},
                {QStringLiteral("Codec"), QStringLiteral("truehd")},
                {QStringLiteral("Profile"), QStringLiteral("Dolby Atmos")},
                {QStringLiteral("AudioSpatialFormat"), QStringLiteral("DolbyAtmos")},
                {QStringLiteral("Channels"), 8},
                {QStringLiteral("ChannelLayout"), QStringLiteral("7.1")},
                {QStringLiteral("SampleRate"), 48000},
                {QStringLiteral("BitDepth"), 24},
                {QStringLiteral("BitRate"), 5000000}
            }}}
        }}}
    });
    const PlaybackInfoResponse silo = SiloModelMapper::playbackInfo({
        {QStringLiteral("media_sources"), QJsonArray{QJsonObject{
            {QStringLiteral("file_id"), QStringLiteral("silo-source")},
            {QStringLiteral("audio_tracks"), QJsonArray{QJsonObject{
                {QStringLiteral("index"), 0},
                {QStringLiteral("codec"), QStringLiteral("truehd")},
                {QStringLiteral("profile"), QStringLiteral("Dolby Atmos")},
                {QStringLiteral("title"), QStringLiteral("Dolby Atmos")},
                {QStringLiteral("channels"), 8},
                {QStringLiteral("layout"), QStringLiteral("7.1")},
                {QStringLiteral("sample_rate"), 48000},
                {QStringLiteral("bit_depth"), 24},
                {QStringLiteral("bitrate"), 5000000}
            }}}
        }}}
    });

    const auto sourceMetadata = [](const PlaybackInfoResponse &response) {
        const MediaStreamInfo stream = response.mediaSources.first().getAudioStreams().first();
        return Bloom::AudioSourceMetadata::fromVariantMap(stream.toVariantMap());
    };
    const Bloom::AudioSourceMetadata jfAudio = sourceMetadata(jellyfin);
    const Bloom::AudioSourceMetadata siloAudio = sourceMetadata(silo);
    QCOMPARE(jfAudio.toVariantMap(), siloAudio.toVariantMap());
    QVERIFY(jfAudio.atmos);

    const Bloom::AudioOutputCapabilities output{
        QStringLiteral("hdmi"), QStringLiteral("endpoint-1"),
        {QStringLiteral("truehd")}, {QStringLiteral("7.1")},
        QStringLiteral("test"), true
    };
    const Bloom::AudioPreferences preferences;
    const Bloom::AudioOutputPlan jfPlan = Bloom::AudioPolicy::plan(jfAudio, output, preferences);
    const Bloom::AudioOutputPlan siloPlan = Bloom::AudioPolicy::plan(siloAudio, output, preferences);
    QCOMPARE(jfPlan.toVariantMap(), siloPlan.toVariantMap());
}

void CanonicalModelsTest::jellyfinTimeConversionUsesMilliseconds()
{
    QCOMPARE(JellyfinModelMapper::ticksToMilliseconds(10'000'000), 1000);
    QCOMPARE(JellyfinModelMapper::ticksToMilliseconds(-1), 0);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(1000), 10'000'000);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(-1), 0);
    QCOMPARE(JellyfinModelMapper::millisecondsToTicks(std::numeric_limits<qint64>::max()),
             std::numeric_limits<qint64>::max());
}

void CanonicalModelsTest::jellyfinPlaybackInfoMapsToMilliseconds()
{
    const PlaybackInfoResponse playbackInfo = JellyfinModelMapper::playbackInfo(
        QJsonObject{
            {QStringLiteral("PlaySessionId"), QStringLiteral("session-1")},
            {QStringLiteral("MediaSources"), QJsonArray{
                 QJsonObject{
                     {QStringLiteral("Id"), QStringLiteral("source-1")},
                     {QStringLiteral("RunTimeTicks"), 20'000'000},
                     {QStringLiteral("MediaStreams"), QJsonArray{
                          QJsonObject{
                              {QStringLiteral("Index"), 3},
                              {QStringLiteral("Type"), QStringLiteral("Audio")},
                              {QStringLiteral("Codec"), QStringLiteral("truehd")},
                              {QStringLiteral("Profile"), QStringLiteral("Dolby Atmos")},
                              {QStringLiteral("AudioSpatialFormat"), QStringLiteral("DolbyAtmos")},
                              {QStringLiteral("SampleRate"), 48000},
                              {QStringLiteral("BitDepth"), 24},
                              {QStringLiteral("Channels"), 8},
                              {QStringLiteral("ChannelLayout"), QStringLiteral("7.1")},
                              {QStringLiteral("BitRate"), 5000000},
                              {QStringLiteral("DisplayTitle"), QStringLiteral("English")},
                              {QStringLiteral("CodecTag"), 4'294'967'295.0}
                          }
                      }}
                 }
             }}
        });

    QCOMPARE(playbackInfo.playSessionId, QStringLiteral("session-1"));
    QCOMPARE(playbackInfo.mediaSources.size(), 1);
    QCOMPARE(playbackInfo.mediaSources.first().durationMs, 2000);
    QCOMPARE(playbackInfo.mediaSources.first().mediaStreams.first().index, 3);
    QCOMPARE(playbackInfo.mediaSources.first().mediaStreams.first().codecTag,
             QStringLiteral("4294967295"));
    const MediaStreamInfo audio = playbackInfo.mediaSources.first().mediaStreams.first();
    QCOMPARE(audio.sampleRate, 48000);
    QCOMPARE(audio.bitDepth, 24);
    QVERIFY(audio.atmos);
    const QVariantMap source = playbackInfo.getMediaSourcesVariant().first().toMap();
    QCOMPARE(source.value(QStringLiteral("durationMs")).toLongLong(), 2000);
    QVERIFY(!source.contains(QStringLiteral("runTimeTicks")));

    const ParsedItemsResult items = JellyfinModelMapper::itemsResponse(
        QJsonDocument(QJsonObject{
            {QStringLiteral("Items"), QJsonArray{
                 QJsonObject{{QStringLiteral("Id"), QStringLiteral("movie-1")}}
             }},
            {QStringLiteral("TotalRecordCount"), 3}
        }).toJson(),
        QStringLiteral("library-1"));
    QVERIFY(items.success);
    QCOMPARE(items.parentId, QStringLiteral("library-1"));
    QCOMPARE(items.items.size(), 1);
    QCOMPARE(items.totalRecordCount, 3);
}

void CanonicalModelsTest::jellyfinTrickplayAndSegmentsMapAtProviderBoundary()
{
    const TrickplayTileInfoMap trickplay = JellyfinModelMapper::trickplayInfo(
        QJsonObject{
            {QStringLiteral("RunTimeTicks"), 50'000'000},
            {QStringLiteral("Trickplay"), QJsonObject{
                 {QStringLiteral("source-1"), QJsonObject{
                      {QStringLiteral("320"), QJsonObject{
                           {QStringLiteral("Width"), 320},
                           {QStringLiteral("Height"), 180},
                           {QStringLiteral("TileWidth"), 10},
                           {QStringLiteral("TileHeight"), 10},
                           {QStringLiteral("ThumbnailCount"), 2},
                           {QStringLiteral("Interval"), 1000},
                           {QStringLiteral("Bandwidth"), 8000}
                       }}
                  }}
             }}
        });

    QCOMPARE(trickplay.size(), 1);
    QCOMPARE(trickplay.value(320).height, 180);
    QCOMPARE(trickplay.value(320).thumbnailCount, 5);
    QCOMPARE(trickplay.value(320).interval, 1000);

    const QList<MediaSegmentInfo> segments = JellyfinModelMapper::introSkipperSegments(
        QStringLiteral("episode-1"),
        QJsonObject{
            {QStringLiteral("Introduction"), QJsonObject{
                 {QStringLiteral("Valid"), true},
                 {QStringLiteral("EpisodeId"), QStringLiteral("episode-1")},
                 {QStringLiteral("Start"), 1.25},
                 {QStringLiteral("End"), 8.5}
             }},
            {QStringLiteral("Credits"), QJsonObject{
                 {QStringLiteral("Valid"), false},
                 {QStringLiteral("Start"), 10.0},
                 {QStringLiteral("End"), 20.0}
             }}
        });

    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().itemId, QStringLiteral("episode-1"));
    QCOMPARE(segments.first().type, MediaSegmentType::Intro);
    QCOMPARE(segments.first().typeString, QStringLiteral("Intro"));
    QCOMPARE(segments.first().startMs, 1250);
    QCOMPARE(segments.first().endMs, 8500);
    QCOMPARE(segments.first().source, QStringLiteral("jellyfin"));

    const QList<MediaSegmentInfo> coreSegments = JellyfinModelMapper::mediaSegments(
        QStringLiteral("episode-1"),
        QJsonObject{{QStringLiteral("Items"), QJsonArray{
            QJsonObject{{QStringLiteral("Id"), QStringLiteral("segment-1")},
                        {QStringLiteral("ItemId"), QStringLiteral("episode-1")},
                        {QStringLiteral("Type"), QStringLiteral("Intro")},
                        {QStringLiteral("StartTicks"), 12'500'000},
                        {QStringLiteral("EndTicks"), 85'000'000}},
            QJsonObject{{QStringLiteral("Type"), QStringLiteral("Outro")},
                        {QStringLiteral("StartTicks"), 90'000'000},
                        {QStringLiteral("EndTicks"), 80'000'000}}
        }}});
    QCOMPARE(coreSegments.size(), 1);
    QCOMPARE(coreSegments.first().id, QStringLiteral("segment-1"));
    QCOMPARE(coreSegments.first().itemId, QStringLiteral("episode-1"));
    QCOMPARE(coreSegments.first().type, MediaSegmentType::Intro);
    QCOMPARE(coreSegments.first().startMs, 1250);
    QCOMPARE(coreSegments.first().endMs, 8500);
}

void CanonicalModelsTest::jellyfinRemoteSessionsMapAtProviderBoundary()
{
    const QVariantList sessions = JellyfinModelMapper::remoteSessions(
        QJsonArray{
            QJsonObject{
                {QStringLiteral("Id"), QStringLiteral("session-1")},
                {QStringLiteral("DeviceId"), QStringLiteral("device-1")},
                {QStringLiteral("DeviceName"), QStringLiteral("Living Room")},
                {QStringLiteral("Client"), QStringLiteral("Bloom")},
                {QStringLiteral("ApplicationVersion"), QStringLiteral("1.2.3")},
                {QStringLiteral("UserId"), QStringLiteral("user-1")},
                {QStringLiteral("UserName"), QStringLiteral("Ada")},
                {QStringLiteral("LastActivityDate"), QStringLiteral("2026-01-02T03:04:05Z")},
                {QStringLiteral("IsRemoteSession"), true},
                {QStringLiteral("SupportsRemoteControl"), true},
                {QStringLiteral("PlayState"), QJsonObject{
                     {QStringLiteral("PlayMethod"), QStringLiteral("DirectPlay")}
                 }}
            }
        },
        QStringLiteral("connection-1"));

    QCOMPARE(sessions.size(), 1);
    const QVariantMap session = sessions.first().toMap();
    QCOMPARE(session.value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("connection-1"));
    QCOMPARE(session.value(QStringLiteral("id")).toString(), QStringLiteral("session-1"));
    QCOMPARE(session.value(QStringLiteral("deviceId")).toString(), QStringLiteral("device-1"));
    QCOMPARE(session.value(QStringLiteral("clientVersion")).toString(),
             QStringLiteral("1.2.3"));
    QCOMPARE(session.value(QStringLiteral("playState")).toString(),
             QStringLiteral("DirectPlay"));
    QVERIFY(session.value(QStringLiteral("lastActivityDate")).toDateTime().isValid());
    QVERIFY(!session.contains(QStringLiteral("DeviceId")));

    QCOMPARE(JellyfinModelMapper::libraryIdFromAncestors(
                 QJsonArray{
                     QJsonObject{
                         {QStringLiteral("Id"), QStringLiteral("series-1")},
                         {QStringLiteral("Type"), QStringLiteral("Series")}
                     },
                     QJsonObject{
                         {QStringLiteral("Id"), QStringLiteral("library-1")},
                         {QStringLiteral("Type"), QStringLiteral("CollectionFolder")},
                         {QStringLiteral("CollectionType"), QStringLiteral("tvshows")}
                     }
                 }),
             QStringLiteral("library-1"));

    const QVariantMap filters = JellyfinModelMapper::filterOptions(QJsonObject{
        {QStringLiteral("Genres"), QJsonArray{QStringLiteral("Drama")}},
        {QStringLiteral("Tags"), QJsonArray{QStringLiteral("Favorite")}}
    });
    QCOMPARE(filters.value(QStringLiteral("genres")).toStringList(),
             QStringList{QStringLiteral("Drama")});
    QCOMPARE(filters.value(QStringLiteral("tags")).toStringList(),
             QStringList{QStringLiteral("Favorite")});
    QCOMPARE(JellyfinModelMapper::namedItems(QJsonObject{
                 {QStringLiteral("Items"), QJsonArray{
                      QJsonObject{{QStringLiteral("Name"), QStringLiteral("Studio A")}}
                  }}
             }),
             QStringList{QStringLiteral("Studio A")});
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
        {QStringLiteral("SeriesThumbImageTag"), QStringLiteral("series-thumb-tag")},
        {QStringLiteral("ParentThumbItemId"), QStringLiteral("parent-thumb-owner")},
        {QStringLiteral("ParentThumbImageTag"), QStringLiteral("parent-thumb-tag")},
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
    const QVariantMap seriesThumb =
        item.value(QStringLiteral("seriesThumbArtwork")).toMap();
    QCOMPARE(seriesThumb.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("series-1"));
    QCOMPARE(seriesThumb.value(QStringLiteral("kind")).toString(),
             QStringLiteral("thumb"));
    QCOMPARE(seriesThumb.value(QStringLiteral("tag")).toString(),
             QStringLiteral("series-thumb-tag"));
    const QVariantMap parentThumb =
        item.value(QStringLiteral("parentThumbArtwork")).toMap();
    QCOMPARE(parentThumb.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("parent-thumb-owner"));
    QCOMPARE(parentThumb.value(QStringLiteral("kind")).toString(),
             QStringLiteral("thumb"));
    QCOMPARE(parentThumb.value(QStringLiteral("tag")).toString(),
             QStringLiteral("parent-thumb-tag"));
    const QVariantMap fallbackParentThumb = JellyfinModelMapper::mediaItem(
        QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("episode-thumb-fallback")},
            {QStringLiteral("ParentId"), QStringLiteral("season-thumb-owner")},
            {QStringLiteral("ParentThumbImageTag"), QStringLiteral("fallback-thumb-tag")}
        },
        QStringLiteral("connection-1"))
        .value(QStringLiteral("parentThumbArtwork")).toMap();
    QCOMPARE(fallbackParentThumb.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("season-thumb-owner"));
    QCOMPARE(fallbackParentThumb.value(QStringLiteral("tag")).toString(),
             QStringLiteral("fallback-thumb-tag"));
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
        {QStringLiteral("directStreamUrl"),
         QStringLiteral("/Videos/movie-1/stream?api_key=legacy-token&deviceProfileId=obsolete")},
        {QStringLiteral("durationMs"), 2000},
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
    QCOMPARE(query.queryItemValue(QStringLiteral("ApiKey")), QStringLiteral("secret-token"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("api_key")));
    QVERIFY(!query.hasQueryItem(QStringLiteral("deviceProfileId")));
    QCOMPARE(query.queryItemValue(QStringLiteral("MediaSourceId")), QStringLiteral("source-1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("AudioStreamIndex")), QStringLiteral("2"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SubtitleStreamIndex")), QStringLiteral("4"));

    QVariantMap fallbackSource = source;
    fallbackSource.remove(QStringLiteral("directStreamUrl"));
    fallbackSource[QStringLiteral("directPlayAvailable")] = true;
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

void CanonicalModelsTest::jellyfinDirectPlayAvailabilityControlsStaticStreamSelection()
{
    const auto parsedSource = [](bool supportsDirectPlay) {
        const PlaybackInfoResponse parsed = JellyfinModelMapper::playbackInfo({
            {QStringLiteral("MediaSources"), QJsonArray{QJsonObject{
                {QStringLiteral("Id"), QStringLiteral("source-1")},
                {QStringLiteral("SupportsDirectPlay"), supportsDirectPlay},
                {QStringLiteral("TranscodingUrl"), QStringLiteral("/Videos/movie-1/master.m3u8")}
            }}}
        });
        return parsed;
    };
    const PlaybackInfoResponse directPlayResponse = parsedSource(true);
    const MediaSourceInfo directPlaySource = directPlayResponse.mediaSources.first();
    QVERIFY(directPlaySource.directPlayAvailable);
    const QVariantMap canonical = directPlayResponse.getMediaSourcesVariant().first().toMap();
    QVERIFY(canonical.value(QStringLiteral("directPlayAvailable")).toBool());

    const JellyfinPlaybackProvider provider;
    const PlaybackProviderContext context{QUrl(QStringLiteral("https://media.example.test/")), {}};
    const Bloom::MediaRef media{QStringLiteral("connection-1"), QStringLiteral("movie-1")};
    const Bloom::PlaybackDescriptor directPlay = provider.createDescriptor(
        context, media, canonical, -1, -1, 0);
    QCOMPARE(directPlay.stream.method, Bloom::PlaybackMethod::DirectPlay);
    QCOMPARE(directPlay.stream.url.path(), QStringLiteral("/Videos/movie-1/stream"));
    QCOMPARE(QUrlQuery(directPlay.stream.url).queryItemValue(QStringLiteral("Static")),
             QStringLiteral("true"));

    const QVariantMap unsupported = parsedSource(false).getMediaSourcesVariant().first().toMap();
    QVERIFY(!unsupported.value(QStringLiteral("directPlayAvailable")).toBool());
    const Bloom::PlaybackDescriptor transcode = provider.createDescriptor(
        context, media, unsupported, -1, -1, 0);
    QCOMPARE(transcode.stream.method, Bloom::PlaybackMethod::Transcode);
    QCOMPARE(transcode.stream.url.path(), QStringLiteral("/Videos/movie-1/master.m3u8"));

    const PlaybackInfoResponse unknownResponse = JellyfinModelMapper::playbackInfo({
        {QStringLiteral("MediaSources"), QJsonArray{QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("source-1")},
            {QStringLiteral("TranscodingUrl"), QStringLiteral("/Videos/movie-1/master.m3u8")}
        }}}
    });
    QVERIFY(!unknownResponse.mediaSources.first().directPlayAvailabilityKnown);
    const QVariantMap unknown = unknownResponse.getMediaSourcesVariant().first().toMap();
    QVERIFY(!unknown.contains(QStringLiteral("directPlayAvailable")));
    const Bloom::PlaybackDescriptor legacyStatic = provider.createDescriptor(
        context, media, unknown, -1, -1, 0);
    QCOMPARE(legacyStatic.stream.method, Bloom::PlaybackMethod::Transcode);
    QCOMPARE(legacyStatic.stream.url.path(), QStringLiteral("/Videos/movie-1/master.m3u8"));

    QVariantMap unknownWithoutUrls = unknown;
    unknownWithoutUrls.remove(QStringLiteral("transcodingUrl"));
    const Bloom::PlaybackDescriptor legacyStaticFallback = provider.createDescriptor(
        context, media, unknownWithoutUrls, -1, -1, 0);
    QCOMPARE(legacyStaticFallback.stream.method, Bloom::PlaybackMethod::DirectPlay);
    QCOMPARE(QUrlQuery(legacyStaticFallback.stream.url).queryItemValue(QStringLiteral("Static")),
             QStringLiteral("true"));
}

void CanonicalModelsTest::jellyfinPlaybackProviderFinalizesTrickplayUrl()
{
    const JellyfinPlaybackProvider provider;
    const QUrl url = provider.createTrickplayTileUrl(
        PlaybackProviderContext{
            QUrl(QStringLiteral("https://media.example.test/base/")),
            QStringLiteral("secret-token")
        },
        QStringLiteral("movie-1"),
        320,
        4);

    QCOMPARE(url.path(), QStringLiteral("/base/Videos/movie-1/Trickplay/320/4.jpg"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("ApiKey")),
             QStringLiteral("secret-token"));
    QVERIFY(provider.createTrickplayTileUrl(
        PlaybackProviderContext{}, QStringLiteral("movie-1"), 320, 4).isEmpty());
}

void CanonicalModelsTest::jellyfinPlaybackInfoRequestUsesBodyUserId()
{
    JellyfinPlaybackProvider provider;
    PlaybackProviderContext context;
    context.profileId = QStringLiteral("user-1");
    const PlaybackInfoRequest request = provider.createPlaybackInfoRequest(
        context,
        Bloom::MediaRef{QStringLiteral("connection-1"), QStringLiteral("item/1")},
        {});
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.endpoint, QStringLiteral("/Items/item%2F1/PlaybackInfo"));
    QCOMPARE(request.body.value(QStringLiteral("UserId")).toString(),
             QStringLiteral("user-1"));
    const QJsonObject deviceProfile = request.body.value(QStringLiteral("DeviceProfile")).toObject();
    const QJsonArray directPlayProfiles = deviceProfile.value(QStringLiteral("DirectPlayProfiles")).toArray();
    QCOMPARE(directPlayProfiles.size(), 1);
    QVERIFY(!directPlayProfiles.first().toObject().contains(QStringLiteral("Container")));
    QVERIFY(!directPlayProfiles.first().toObject().contains(QStringLiteral("VideoCodec")));
    const QJsonObject audioProfile = directPlayProfiles.first().toObject();
    QCOMPARE(audioProfile.value(QStringLiteral("Type")).toString(), QStringLiteral("Video"));
    QCOMPARE(audioProfile.value(QStringLiteral("AudioCodec")).toString(),
             Bloom::AudioPolicy::localDecodeCodecs().join(QLatin1Char(',')));
    QVERIFY(!request.endpoint.contains(QStringLiteral("UserId"), Qt::CaseInsensitive));

    const PlaybackInfoParseResult parsed = provider.parsePlaybackInfoResponse(
        context, {},
        QJsonObject{{QStringLiteral("MediaSources"), QJsonArray{
            QJsonObject{{QStringLiteral("Id"), QStringLiteral("source-1")}}
        }}});
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.response.mediaSources.size(), 1);
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

void CanonicalModelsTest::jellyfinPlaybackReportsSerializeAtProviderBoundary()
{
    const JellyfinPlaybackProvider provider;
    PlaybackReport report;
    report.event = PlaybackReportEvent::Progress;
    report.media = {QStringLiteral("connection-1"), QStringLiteral("movie-1")};
    report.positionMs = 1234;
    report.mediaVersionId = QStringLiteral("source-1");
    report.audioTrackId = QStringLiteral("2");
    report.subtitleTrackId = QStringLiteral("4");
    report.playbackSessionId = QStringLiteral("session-1");
    report.canSeek = true;
    report.isPaused = false;
    report.isMuted = true;
    report.playbackMethod = QStringLiteral("directStream");

    const PlaybackReportRequest progress = provider.createReportRequest(report);
    QCOMPARE(progress.endpoint, QStringLiteral("/Sessions/Playing/Progress"));
    QVERIFY(progress.deferSessionExpiry);
    QCOMPARE(progress.body.value(QStringLiteral("ItemId")).toString(),
             QStringLiteral("movie-1"));
    QCOMPARE(progress.body.value(QStringLiteral("PositionTicks")).toVariant().toLongLong(),
             12'340'000LL);
    QCOMPARE(progress.body.value(QStringLiteral("MediaSourceId")).toString(),
             QStringLiteral("source-1"));
    QCOMPARE(progress.body.value(QStringLiteral("AudioStreamIndex")).toInt(), 2);
    QCOMPARE(progress.body.value(QStringLiteral("SubtitleStreamIndex")).toInt(), 4);
    QCOMPARE(progress.body.value(QStringLiteral("PlaySessionId")).toString(),
             QStringLiteral("session-1"));
    QCOMPARE(progress.body.value(QStringLiteral("PlayMethod")).toString(),
             QStringLiteral("DirectStream"));
    QVERIFY(!progress.body.contains(QStringLiteral("EventName")));

    report.event = PlaybackReportEvent::Start;
    report.positionMs = -1;
    const PlaybackReportRequest start = provider.createReportRequest(report);
    QCOMPARE(start.endpoint, QStringLiteral("/Sessions/Playing"));
    QVERIFY(!start.body.contains(QStringLiteral("PositionTicks")));
    QVERIFY(!start.body.contains(QStringLiteral("EventName")));

    report.event = PlaybackReportEvent::Stop;
    report.positionMs = 4321;
    const PlaybackReportRequest stop = provider.createReportRequest(report);
    QCOMPARE(stop.endpoint, QStringLiteral("/Sessions/Playing/Stopped"));
    QVERIFY(!stop.deferSessionExpiry);
    QCOMPARE(stop.body.value(QStringLiteral("PositionTicks")).toVariant().toLongLong(),
             43'210'000LL);
    QVERIFY(!stop.body.contains(QStringLiteral("EventName")));
    QVERIFY(!stop.body.contains(QStringLiteral("CanSeek")));
    QVERIFY(!stop.body.contains(QStringLiteral("IsPaused")));
    QVERIFY(!stop.body.contains(QStringLiteral("PlayMethod")));
    QVERIFY(!stop.body.contains(QStringLiteral("AudioStreamIndex")));
}

QTEST_MAIN(CanonicalModelsTest)
#include "CanonicalModelsTest.moc"
