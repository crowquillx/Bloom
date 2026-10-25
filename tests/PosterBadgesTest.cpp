#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "models/PosterBadges.h"
#include "providers/jellyfin/JellyfinCatalogProvider.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloModelMapper.h"

using Bloom::PosterBadgeCorner;
using Bloom::PosterBadgeLayout;
using Bloom::PosterBadgeSlot;

namespace {

QJsonObject effectiveSettings(const QJsonValue &overlays, const QJsonValue &enabled = QJsonValue::Null)
{
    return QJsonObject{
        {QStringLiteral("revision"), 15},
        {QStringLiteral("items"),
         QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("ui.card_overlays")},
                                {QStringLiteral("value"), overlays},
                                {QStringLiteral("source"), QStringLiteral("profile")}},
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("ui.card_overlays_enabled")},
                                {QStringLiteral("value"), enabled},
                                {QStringLiteral("source"), QStringLiteral("default")}}}}};
}

QJsonObject overlayItem(bool enabled, const QString &position = {})
{
    QJsonObject item{{QStringLiteral("enabled"), enabled}};
    if (!position.isEmpty()) {
        item.insert(QStringLiteral("position"), position);
    }
    return item;
}

// "kind@corner" (with "+icon" when the icon shows), in render order.
QStringList describe(const PosterBadgeLayout &layout)
{
    QStringList result;
    for (const PosterBadgeSlot &slot : layout.badges) {
        result.append(slot.kind + QLatin1Char('@') + Bloom::posterBadgeCornerName(slot.corner)
                      + (slot.showIcon ? QStringLiteral("+icon") : QString()));
    }
    return result;
}

QStringList texts(const QVariantList &badges)
{
    QStringList result;
    for (const QVariant &value : badges) {
        const QVariantMap badge = value.toMap();
        result.append(badge.value(QStringLiteral("corner")).toString() + QLatin1Char(':')
                      + badge.value(QStringLiteral("text")).toString());
    }
    return result;
}

PosterBadgeLayout layoutOf(std::initializer_list<PosterBadgeSlot> entries)
{
    PosterBadgeLayout layout;
    layout.enabled = true;
    layout.badges = QList<PosterBadgeSlot>(entries);
    return layout;
}

} // namespace

class PosterBadgesTest : public QObject
{
    Q_OBJECT

private slots:
    void siloItemCarriesBadgeInputs();
    void siloDefaultsMatchTheWebRegistry();
    void siloEnabledFollowsProfileThenServer();
    void siloVersionTwoPreferences();
    void siloLegacyAndAdminDefaultDocuments();
    void resolvesAndFormatsBadges();
    void skipsMissingValuesBeforeCornerLimit();
    void siloCatalogRequestsPreferences();
};

void PosterBadgesTest::siloItemCarriesBadgeInputs()
{
    const QVariantMap item = SiloModelMapper::mediaItem(
        QJsonObject{{QStringLiteral("content_id"), QStringLiteral("c-1")},
                    {QStringLiteral("type"), QStringLiteral("series")},
                    {QStringLiteral("title"), QStringLiteral("Show")},
                    {QStringLiteral("advisory_age"), 13},
                    {QStringLiteral("original_language"), QStringLiteral("ja")},
                    {QStringLiteral("show_status"), QStringLiteral("Returning Series")},
                    {QStringLiteral("overlay_summary"),
                     QJsonObject{{QStringLiteral("resolution"), QStringLiteral("2160p")},
                                 {QStringLiteral("hdr"), QStringLiteral("DV")},
                                 {QStringLiteral("audio_channels"), QStringLiteral("7.1")},
                                 {QStringLiteral("release_type"), QStringLiteral("REMUX")},
                                 {QStringLiteral("edition"), QStringLiteral("")},
                                 {QStringLiteral("multi_audio"), true},
                                 {QStringLiteral("multi_sub"), false}}}},
        QStringLiteral("silo-1"));

    const QVariantMap summary = item.value(QStringLiteral("mediaSummary")).toMap();
    QCOMPARE(summary.value(QStringLiteral("resolution")).toString(), QStringLiteral("2160p"));
    QCOMPARE(summary.value(QStringLiteral("hdr")).toString(), QStringLiteral("DV"));
    QCOMPARE(summary.value(QStringLiteral("audioChannels")).toString(), QStringLiteral("7.1"));
    QCOMPARE(summary.value(QStringLiteral("releaseType")).toString(), QStringLiteral("REMUX"));
    QVERIFY(summary.value(QStringLiteral("multiAudio")).toBool());
    // Empty and false values are left out rather than shown.
    QVERIFY(!summary.contains(QStringLiteral("edition")));
    QVERIFY(!summary.contains(QStringLiteral("multiSub")));
    QCOMPARE(item.value(QStringLiteral("advisoryAge")).toInt(), 13);
    QCOMPARE(item.value(QStringLiteral("originalLanguage")).toString(), QStringLiteral("ja"));
    QCOMPARE(item.value(QStringLiteral("showStatus")).toString(), QStringLiteral("Returning Series"));

    const QVariantMap bare = SiloModelMapper::mediaItem(
        QJsonObject{{QStringLiteral("content_id"), QStringLiteral("c-2")},
                    {QStringLiteral("type"), QStringLiteral("movie")}},
        QStringLiteral("silo-1"));
    QVERIFY(!bare.contains(QStringLiteral("mediaSummary")));
    QVERIFY(!bare.contains(QStringLiteral("showStatus")));
}

void PosterBadgesTest::siloDefaultsMatchTheWebRegistry()
{
    // No profile choice and no admin defaults: the shipped classic layout.
    const PosterBadgeLayout layout = SiloModelMapper::posterBadgeLayout(
        effectiveSettings(QJsonValue::Null), QJsonObject{});
    QVERIFY(layout.enabled);
    QCOMPARE(describe(layout),
             QStringList({QStringLiteral("resolution@topLeft"), QStringLiteral("hdr@topLeft"),
                          QStringLiteral("audio@topLeft"), QStringLiteral("releaseType@bottomLeft")}));

    // A missing settings entry reads the same as a null one.
    QCOMPARE(describe(SiloModelMapper::posterBadgeLayout(QJsonObject{}, QJsonObject{})),
             describe(layout));
}

void PosterBadgesTest::siloEnabledFollowsProfileThenServer()
{
    const QJsonObject serverOff{{QStringLiteral("enabled"), false}};
    QVERIFY(!SiloModelMapper::posterBadgeLayout(effectiveSettings(QJsonValue::Null), serverOff).enabled);
    // An explicit profile choice wins in either direction.
    QVERIFY(SiloModelMapper::posterBadgeLayout(
                effectiveSettings(QJsonValue::Null, true), serverOff).enabled);
    QVERIFY(!SiloModelMapper::posterBadgeLayout(
                 effectiveSettings(QJsonValue::Null, false),
                 QJsonObject{{QStringLiteral("enabled"), true}}).enabled);
}

void PosterBadgesTest::siloVersionTwoPreferences()
{
    const QJsonObject document{
        {QStringLiteral("version"), 2},
        {QStringLiteral("preset"), QStringLiteral("pill")},
        {QStringLiteral("order"), QJsonArray{QStringLiteral("audio"), QStringLiteral("rating_imdb"),
                                             QStringLiteral("not_an_overlay")}},
        {QStringLiteral("items"),
         QJsonObject{
             {QStringLiteral("resolution_hdr"), overlayItem(true)},
             {QStringLiteral("audio"), overlayItem(true, QStringLiteral("bottom-right"))},
             {QStringLiteral("release_type"), overlayItem(false, QStringLiteral("bottom-left"))},
             {QStringLiteral("rating_imdb"),
              QJsonObject{{QStringLiteral("enabled"), true},
                          {QStringLiteral("position"), QStringLiteral("top-right")},
                          {QStringLiteral("accentColor"), QStringLiteral("#f5c518")}}},
             {QStringLiteral("year"),
              QJsonObject{{QStringLiteral("enabled"), true},
                          {QStringLiteral("position"), QStringLiteral("sideways")},
                          {QStringLiteral("showIcon"), false}}},
             {QStringLiteral("imdb_top_250"), overlayItem(true, QStringLiteral("top-right"))},
         }}};
    const PosterBadgeLayout layout =
        SiloModelMapper::posterBadgeLayout(effectiveSettings(document), QJsonObject{});

    // Ranked ids lead; the combined badge hides the standalone resolution and
    // HDR badges; an invalid position keeps the default corner; ids without
    // card data are dropped; the pill preset prefers icons.
    QCOMPARE(describe(layout),
             QStringList({QStringLiteral("audio@bottomRight+icon"),
                          QStringLiteral("ratingImdb@topRight+icon"),
                          QStringLiteral("resolutionHdr@topLeft+icon"),
                          QStringLiteral("year@bottomLeft")}));
}

void PosterBadgesTest::siloLegacyAndAdminDefaultDocuments()
{
    // A version 1 document is a flat id map on the classic preset, possibly
    // stored as a JSON string.
    const QString legacy = QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("hdr"), overlayItem(false)},
                                  {QStringLiteral("network"), overlayItem(true, QStringLiteral("top-right"))}})
            .toJson(QJsonDocument::Compact));
    QCOMPARE(describe(SiloModelMapper::posterBadgeLayout(effectiveSettings(legacy), QJsonObject{})),
             QStringList({QStringLiteral("resolution@topLeft"), QStringLiteral("audio@topLeft"),
                          QStringLiteral("releaseType@bottomLeft"), QStringLiteral("network@topRight")}));

    // Without a profile choice the administrator's defaults document applies.
    const QJsonObject adminConfig{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("defaults"),
         QString::fromUtf8(QJsonDocument(QJsonObject{
             {QStringLiteral("version"), 2},
             {QStringLiteral("preset"), QStringLiteral("minimal")},
             {QStringLiteral("order"), QJsonArray{}},
             {QStringLiteral("items"),
              QJsonObject{{QStringLiteral("resolution"), overlayItem(true)},
                          {QStringLiteral("hdr"), overlayItem(false)},
                          {QStringLiteral("audio"), overlayItem(false)},
                          {QStringLiteral("release_type"), overlayItem(false)}}}})
                               .toJson(QJsonDocument::Compact))}};
    QCOMPARE(describe(SiloModelMapper::posterBadgeLayout(effectiveSettings(QJsonValue::Null), adminConfig)),
             QStringList({QStringLiteral("resolution@topLeft")}));
    // A stored profile value wins over the admin defaults.
    QCOMPARE(describe(SiloModelMapper::posterBadgeLayout(effectiveSettings(legacy), adminConfig)).size(), 4);

    // Garbage reads as no preference.
    QCOMPARE(describe(SiloModelMapper::posterBadgeLayout(
                 effectiveSettings(QStringLiteral("{not json")), QJsonObject{})).size(), 4);
}

void PosterBadgesTest::resolvesAndFormatsBadges()
{
    const PosterBadgeLayout layout = layoutOf({
        {QStringLiteral("resolution"), PosterBadgeCorner::TopLeft, true},
        {QStringLiteral("resolutionHdr"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("ratingImdb"), PosterBadgeCorner::TopRight, false},
        {QStringLiteral("ratingRtCritic"), PosterBadgeCorner::TopRight, false},
        {QStringLiteral("runtime"), PosterBadgeCorner::BottomLeft, true},
        {QStringLiteral("originalLanguage"), PosterBadgeCorner::BottomLeft, false},
        {QStringLiteral("advisoryAge"), PosterBadgeCorner::BottomLeft, false},
        {QStringLiteral("showStatus"), PosterBadgeCorner::BottomRight, false},
        {QStringLiteral("multiSub"), PosterBadgeCorner::BottomRight, false},
        {QStringLiteral("studio"), PosterBadgeCorner::BottomRight, false},
    });
    const QVariantMap item{
        {QStringLiteral("connectionId"), QStringLiteral("silo-1")},
        {QStringLiteral("mediaSummary"),
         QVariantMap{{QStringLiteral("resolution"), QStringLiteral("2160p")},
                     {QStringLiteral("hdr"), QStringLiteral("DV Profile 8")},
                     {QStringLiteral("multiSub"), true}}},
        {QStringLiteral("ratings"),
         QVariantMap{{QStringLiteral("imdb"), 8.46}, {QStringLiteral("rottenTomatoesCritic"), 93}}},
        {QStringLiteral("runtimeMinutes"), 125},
        {QStringLiteral("originalLanguage"), QStringLiteral("ja")},
        {QStringLiteral("advisoryAge"), 13},
        {QStringLiteral("showStatus"), QStringLiteral("in_production")},
        {QStringLiteral("studios"), QVariantList{QStringLiteral(" "), QStringLiteral("Studio Ghibli")}},
    };

    const QVariantList badges = Bloom::resolvePosterBadges(layout, item);
    QCOMPARE(texts(badges),
             QStringList({QStringLiteral("topLeft:4K"), QStringLiteral("topLeft:4K DV"),
                          QStringLiteral("topRight:IMDb 8.5"), QStringLiteral("topRight:RT 93%"),
                          QStringLiteral("bottomLeft:2h 5m"), QStringLiteral("bottomLeft:Japanese"),
                          QStringLiteral("bottomLeft:13+"), QStringLiteral("bottomRight:Returning"),
                          QStringLiteral("bottomRight:CC"), QStringLiteral("bottomRight:Studio Ghibli")}));

    const QVariantMap resolution = badges.at(0).toMap();
    QCOMPARE(resolution.value(QStringLiteral("kind")).toString(), QStringLiteral("resolution"));
    QCOMPARE(resolution.value(QStringLiteral("icon")).toString(), QStringLiteral("monitor"));
    // Icons show only where the layout asks for them.
    QVERIFY(badges.at(1).toMap().value(QStringLiteral("icon")).toString().isEmpty());
    const QVariantMap imdb = badges.at(2).toMap();
    QCOMPARE(imdb.value(QStringLiteral("ratingSource")).toString(), QStringLiteral("imdb"));
    QCOMPARE(imdb.value(QStringLiteral("score")).toString(), QStringLiteral("8.5"));
    QCOMPARE(badges.at(3).toMap().value(QStringLiteral("ratingSource")).toString(),
             QStringLiteral("tomatoes"));

    PosterBadgeLayout disabled = layout;
    disabled.enabled = false;
    QVERIFY(Bloom::resolvePosterBadges(disabled, item).isEmpty());
}

void PosterBadgesTest::skipsMissingValuesBeforeCornerLimit()
{
    const PosterBadgeLayout layout = layoutOf({
        {QStringLiteral("resolution"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("edition"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("hdr"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("audio"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("videoCodec"), PosterBadgeCorner::TopLeft, false},
        {QStringLiteral("ratingTmdb"), PosterBadgeCorner::TopRight, false},
        {QStringLiteral("unknownKind"), PosterBadgeCorner::TopRight, false},
    });
    const QVariantMap item{
        {QStringLiteral("mediaSummary"),
         QVariantMap{{QStringLiteral("resolution"), QStringLiteral("1080P")},
                     {QStringLiteral("hdr"), QStringLiteral("HDR10")},
                     {QStringLiteral("audio"), QStringLiteral("TrueHD")},
                     {QStringLiteral("videoCodec"), QStringLiteral("H.265")}}},
        // A zero rating is no rating.
        {QStringLiteral("ratings"), QVariantMap{{QStringLiteral("tmdb"), 0.0}}},
    };
    // The missing edition does not use up a slot; the fourth value is cut.
    QCOMPARE(texts(Bloom::resolvePosterBadges(layout, item)),
             QStringList({QStringLiteral("topLeft:1080p"), QStringLiteral("topLeft:HDR10"),
                          QStringLiteral("topLeft:TrueHD")}));
    QVERIFY(!Bloom::isKnownPosterBadgeKind(QStringLiteral("unknownKind")));
}

void PosterBadgesTest::siloCatalogRequestsPreferences()
{
    const SiloCatalogProvider silo;
    const ProviderCatalogRequest settings =
        silo.createRequest(ProviderCatalogOperation::PosterBadgeSettings, {});
    QVERIFY(settings.supported);
    QVERIFY(settings.method == ProviderHttpMethod::Get);
    QVERIFY(settings.retrySafety == ProviderCatalogRetrySafety::Idempotent);
    const QUrl settingsUrl(settings.relativeEndpoint);
    QCOMPARE(settingsUrl.path(), QStringLiteral("/api/v2/settings/values/effective"));
    QCOMPARE(QUrlQuery(settingsUrl).allQueryItemValues(QStringLiteral("keys")),
             QStringList({QStringLiteral("ui.card_overlays"), QStringLiteral("ui.card_overlays_enabled")}));
    QCOMPARE(silo.createRequest(ProviderCatalogOperation::PosterBadgeDefaults, {}).relativeEndpoint,
             QStringLiteral("/api/v2/settings/overlay-config"));

    QVERIFY(silo.parseResponse(ProviderCatalogOperation::PosterBadgeSettings,
                               QByteArrayLiteral(R"({"revision":15,"items":[]})")).valid);
    QVERIFY(!silo.parseResponse(ProviderCatalogOperation::PosterBadgeSettings,
                                QByteArrayLiteral(R"({"revision":15})")).valid);
    const ProviderCatalogResponse config = silo.parseResponse(
        ProviderCatalogOperation::PosterBadgeDefaults,
        QByteArrayLiteral(R"({"enabled":false,"quick_actions_enabled":false,"quick_actions_default":"both"})"));
    QVERIFY(config.valid);
    QCOMPARE(config.rawItem.value(QStringLiteral("enabled")).toBool(true), false);

    const JellyfinCatalogProvider jellyfin;
    QVERIFY(!jellyfin.createRequest(ProviderCatalogOperation::PosterBadgeSettings, {}).supported);
    QVERIFY(!jellyfin.createRequest(ProviderCatalogOperation::PosterBadgeDefaults, {}).supported);
}

QTEST_GUILESS_MAIN(PosterBadgesTest)
#include "PosterBadgesTest.moc"
