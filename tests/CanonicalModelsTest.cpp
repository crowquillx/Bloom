#include <QtTest/QtTest>

#include "models/MediaModels.h"
#include "providers/jellyfin/JellyfinModelMapper.h"

#include <QJsonArray>
#include <QJsonObject>
#include <limits>

class CanonicalModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinTimeConversionUsesMilliseconds();
    void jellyfinItemMapsToCanonicalCamelCase();
    void jellyfinParentBackdropUsesImageItemId();
    void artworkCacheKeyIsTokenFreeAndRoundTrips();
    void playbackDescriptorExposesProviderNeutralShape();
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
        {QStringLiteral("Type"), QStringLiteral("Movie")},
        {QStringLiteral("RunTimeTicks"), 72'000'000'000.0},
        {QStringLiteral("ImageTags"), QJsonObject{
             {QStringLiteral("Primary"), QStringLiteral("primary-tag")},
             {QStringLiteral("Logo"), QStringLiteral("logo-tag")}
         }},
        {QStringLiteral("BackdropImageTags"), QJsonArray{QStringLiteral("backdrop-tag")}},
        {QStringLiteral("ProviderIds"), QJsonObject{{QStringLiteral("Tmdb"), QStringLiteral("42")}}},
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
    QCOMPARE(item.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Movie"));
    QCOMPARE(item.value(QStringLiteral("durationMs")).toLongLong(), 7'200'000);
    QCOMPARE(item.value(QStringLiteral("positionMs")).toLongLong(), 1500);
    QVERIFY(item.value(QStringLiteral("watched")).toBool());
    QVERIFY(item.value(QStringLiteral("favorite")).toBool());
    QCOMPARE(item.value(QStringLiteral("providerIds")).toMap()
                 .value(QStringLiteral("Tmdb")).toString(), QStringLiteral("42"));

    for (const QString &wireKey : {
             QStringLiteral("Id"),
             QStringLiteral("RunTimeTicks"),
             QStringLiteral("ImageTags"),
             QStringLiteral("BackdropImageTags"),
             QStringLiteral("UserData")}) {
        QVERIFY2(!item.contains(wireKey), qPrintable(wireKey));
    }

    const QVariantMap primary = item.value(QStringLiteral("primaryArtwork")).toMap();
    QCOMPARE(primary.value(QStringLiteral("kind")).toString(), QStringLiteral("primary"));
    QCOMPARE(primary.value(QStringLiteral("tag")).toString(), QStringLiteral("primary-tag"));
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
