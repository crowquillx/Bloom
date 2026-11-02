#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "models/SubtitleAppearance.h"
#include "network/LibraryService.h"
#include "network/SubtitleAppearanceSync.h"
#include "player/SubtitleAppearancePolicy.h"
#include "providers/jellyfin/JellyfinCatalogProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloSubtitleAppearanceMapper.h"
#include "utils/ConfigManager.h"

using Bloom::SubtitleAppearance;
namespace Policy = Bloom::SubtitleAppearancePolicy;

namespace {

class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
        qputenv("APPDATA", m_previousAppData);
        qputenv("HOME", m_previousHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
};

QString propertyValue(const Policy::MpvProperties &properties, const QString &name)
{
    for (const auto &[property, value] : properties) {
        if (property == name) {
            return value;
        }
    }
    return {};
}

QStringList propertyNames(const Policy::MpvProperties &properties)
{
    QStringList names;
    for (const auto &entry : properties) {
        names.append(entry.first);
    }
    return names;
}

QString effectiveBody(const QJsonObject &document, bool deviceOverride = false)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("subtitle_appearance")},
        {QStringLiteral("profile_id"), QStringLiteral("1")},
        {QStringLiteral("global_value"), QString()},
        {QStringLiteral("effective_value"),
         document.isEmpty() ? QString()
                            : QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact))},
        {QStringLiteral("has_device_override"), deviceOverride},
    }).toJson(QJsonDocument::Compact));
}

// Silo's web player default document.
QJsonObject webDefaultDocument()
{
    return QJsonObject{
        {QStringLiteral("fontSize"), QStringLiteral("large")},
        {QStringLiteral("fontFamily"), QStringLiteral("sans-serif")},
        {QStringLiteral("fontColor"), QStringLiteral("#ffffff")},
        {QStringLiteral("textOpacity"), 100},
        {QStringLiteral("backgroundColor"), QStringLiteral("#000000")},
        {QStringLiteral("backgroundStyle"), QStringLiteral("box")},
        {QStringLiteral("backgroundOpacity"), 75},
        {QStringLiteral("textOutline"), false},
        {QStringLiteral("textOutlineColor"), QStringLiteral("#000000")},
        {QStringLiteral("position"), QStringLiteral("bottom")},
    };
}

// LibraryService stand-in: a settable scope and handlers answered by hand.
class FakeLibraryService final : public LibraryService
{
public:
    FakeLibraryService() : LibraryService(nullptr) {}

    QString subtitleAppearanceScope() const override { return scope; }

    void getSubtitleAppearance(SubtitleAppearanceHandler handler) override
    {
        reads.append(handler);
    }

    void setSubtitleAppearance(const QVariantMap &appearance,
                               const QString &document,
                               SubtitleAppearanceHandler handler) override
    {
        writtenAppearances.append(appearance);
        writtenDocuments.append(document);
        writes.append(handler);
    }

    void resetSubtitleAppearance(SubtitleAppearanceHandler handler) override
    {
        resets.append(handler);
    }

    QString scope;
    QList<SubtitleAppearanceHandler> reads;
    QList<SubtitleAppearanceHandler> writes;
    QList<SubtitleAppearanceHandler> resets;
    QList<QVariantMap> writtenAppearances;
    QStringList writtenDocuments;
};

QVariantMap readResult(const QVariantMap &appearance, const QString &document = QStringLiteral("{}"))
{
    return {{QStringLiteral("appearance"), appearance},
            {QStringLiteral("hasValue"), true},
            {QStringLiteral("hasDeviceOverride"), false},
            {QStringLiteral("document"), document}};
}

}  // namespace

class SubtitleAppearanceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void defaultsLeaveMpvStylingAlone();
    void eachFieldMapsToMpvOptions_data();
    void eachFieldMapsToMpvOptions();
    void boxReplacesOutlineAndShadow();
    void defaultFieldsResolveToProfileThenMpv();
    void changedPropertiesSetOnlyDifferences();
    void normalizesAndValidatesValues();
    void jsonRoundTrip();

    void configPersistsAndMarksUserChanges();
    void configAdoptsOnlyWhileUntouched();
    void configResetForgetsUserChange();

    void siloDocumentMapsSharedFields();
    void siloWriteKeepsUnmappedFields();
    void siloResponsesParse();
    void siloRequestsFollowTheContract();
    void jellyfinHasNoSubtitleAppearance();

    void syncAdoptsOnlyWhenUntouched();
    void syncWritesOnlyOnUserChange();
    void syncKeepsLocalChangeWhenWriteFails();
    void syncDropsWriteForAnotherScope();
    void syncResetRemovesOverrideAndAdoptsAgain();
    void syncStaysLocalWithoutCapability();

private:
    // Every ConfigManager here reads and writes a throwaway config home.
    QTemporaryDir m_configHome;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;
};

void SubtitleAppearanceTest::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    m_isolation = std::make_unique<ScopedConfigIsolation>(m_configHome.path());
}

void SubtitleAppearanceTest::cleanupTestCase()
{
    m_isolation.reset();
}

void SubtitleAppearanceTest::defaultsLeaveMpvStylingAlone()
{
    const SubtitleAppearance appearance;
    QVERIFY(Policy::mpvArguments(appearance).isEmpty());
    const Policy::MpvProperties properties = Policy::mpvProperties(appearance, {});
    QCOMPARE(propertyNames(properties), Policy::managedProperties());
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-scale")), QStringLiteral("1"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-font")), QStringLiteral("sans-serif"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-border-style")),
             QStringLiteral("outline-and-shadow"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-pos")), QStringLiteral("100"));
    // Styled subtitles keep mpv's conservative default.
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-ass-override")), QStringLiteral("scale"));
    QVERIFY(!appearance.applyToStyled);
}

void SubtitleAppearanceTest::eachFieldMapsToMpvOptions_data()
{
    QTest::addColumn<QVariantMap>("values");
    QTest::addColumn<QStringList>("arguments");

    QTest::newRow("size") << QVariantMap{{"sizePercent", 150}}
                          << QStringList{"--sub-scale=1.50"};
    QTest::newRow("smallest size") << QVariantMap{{"sizePercent", 75}}
                                   << QStringList{"--sub-scale=0.75"};
    QTest::newRow("font") << QVariantMap{{"font", "gandhi-sans-bold"}}
                          << QStringList{"--sub-font=Gandhi Sans Bold"};
    QTest::newRow("serif") << QVariantMap{{"font", "serif"}} << QStringList{"--sub-font=serif"};
    QTest::newRow("text colour") << QVariantMap{{"textColor", "#facc15"}}
                                 << QStringList{"--sub-color=#FACC15"};
    QTest::newRow("outline colour") << QVariantMap{{"outlineColor", "#1E3A5F"}}
                                    << QStringList{"--sub-border-color=#1E3A5F"};
    QTest::newRow("outline off") << QVariantMap{{"outline", "off"}}
                                 << QStringList{"--sub-border-size=0"};
    QTest::newRow("outline thick") << QVariantMap{{"outline", "thick"}}
                                   << QStringList{"--sub-border-size=3.6"};
    QTest::newRow("shadow off") << QVariantMap{{"shadow", "off"}}
                                << QStringList{"--sub-shadow-offset=0"};
    QTest::newRow("shadow medium") << QVariantMap{{"shadow", "medium"}}
                                   << QStringList{"--sub-shadow-offset=1.5",
                                                  "--sub-shadow-color=#C0000000"};
    QTest::newRow("position") << QVariantMap{{"positionPercent", 15}}
                              << QStringList{"--sub-pos=85"};
    QTest::newRow("styled") << QVariantMap{{"applyToStyled", true}}
                            << QStringList{"--sub-ass-override=force"};
}

void SubtitleAppearanceTest::eachFieldMapsToMpvOptions()
{
    QFETCH(QVariantMap, values);
    QFETCH(QStringList, arguments);
    QCOMPARE(Policy::mpvArguments(SubtitleAppearance{}.withValues(values)), arguments);
}

void SubtitleAppearanceTest::boxReplacesOutlineAndShadow()
{
    const SubtitleAppearance appearance = SubtitleAppearance{}.withValues({
        {"background", "box"},
        {"backgroundColor", "#1E3A5F"},
        {"backgroundOpacity", 50},
        {"outline", "thick"},
        {"shadow", "strong"},
        {"outlineColor", "#FFFFFF"},
    });
    QCOMPARE(Policy::mpvArguments(appearance),
             (QStringList{"--sub-border-style=opaque-box", "--sub-border-color=#801E3A5F",
                          "--sub-border-size=4", "--sub-shadow-offset=0"}));
}

void SubtitleAppearanceTest::defaultFieldsResolveToProfileThenMpv()
{
    const QStringList profileArgs{
        QStringLiteral("--sub-font=Gandhi Sans Bold"),
        QStringLiteral("--sub-border-size=2.4"),
        QStringLiteral("--sub-outline-color=#FF112233"),
        QStringLiteral("--sub-shadow-offset=0.75"),
        QStringLiteral("--sub-ass-override=no"),
        QStringLiteral("--fullscreen"),
    };
    const Policy::MpvProperties base = Policy::mpvProperties(SubtitleAppearance{}, profileArgs);
    QCOMPARE(propertyValue(base, QStringLiteral("sub-font")), QStringLiteral("Gandhi Sans Bold"));
    QCOMPARE(propertyValue(base, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
    // sub-outline-color is the newer name of sub-border-color.
    QCOMPARE(propertyValue(base, QStringLiteral("sub-border-color")), QStringLiteral("#FF112233"));
    QCOMPARE(propertyValue(base, QStringLiteral("sub-ass-override")), QStringLiteral("no"));
    QCOMPARE(propertyValue(base, QStringLiteral("sub-scale")), QStringLiteral("1"));

    const Policy::MpvProperties own = Policy::mpvProperties(
        SubtitleAppearance{}.withValues({{"font", "verdana"}, {"applyToStyled", true}}), profileArgs);
    QCOMPARE(propertyValue(own, QStringLiteral("sub-font")), QStringLiteral("Verdana"));
    QCOMPARE(propertyValue(own, QStringLiteral("sub-ass-override")), QStringLiteral("force"));
    QCOMPARE(propertyValue(own, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
}

void SubtitleAppearanceTest::changedPropertiesSetOnlyDifferences()
{
    const QStringList profileArgs{QStringLiteral("--sub-border-size=2.4")};
    const SubtitleAppearance large = SubtitleAppearance{}.withValues({{"sizePercent", 125}});
    const auto from = Policy::mpvProperties(SubtitleAppearance{}, profileArgs);
    const auto to = Policy::mpvProperties(large, profileArgs);
    QCOMPARE(Policy::changedProperties(from, to),
             (Policy::MpvProperties{{"sub-scale", "1.25"}}));
    QVERIFY(Policy::changedProperties(to, to).isEmpty());

    // Turning the box off restores the profile's outline, not mpv's.
    const SubtitleAppearance boxed = SubtitleAppearance{}.withValues({{"background", "box"}});
    const auto boxedProperties = Policy::mpvProperties(boxed, profileArgs);
    const auto back = Policy::changedProperties(boxedProperties, from);
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-style")),
             QStringLiteral("outline-and-shadow"));
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-color")), QStringLiteral("#FF000000"));
}

void SubtitleAppearanceTest::normalizesAndValidatesValues()
{
    const SubtitleAppearance defaults;
    QCOMPARE(defaults.withValues({{"sizePercent", 10}}).sizePercent, 75);
    QCOMPARE(defaults.withValues({{"sizePercent", 999}}).sizePercent, 200);
    QCOMPARE(defaults.withValues({{"sizePercent", 112}}).sizePercent, 110);
    QCOMPARE(defaults.withValues({{"sizePercent", 127.6}}).sizePercent, 130);
    QCOMPARE(defaults.withValues({{"sizePercent", "150"}}).sizePercent, 100);
    QCOMPARE(defaults.withValues({{"textColor", "#abcdef"}}).textColor, QStringLiteral("#ABCDEF"));
    QCOMPARE(defaults.withValues({{"textColor", "red"}}).textColor, QStringLiteral("#FFFFFF"));
    QCOMPARE(defaults.withValues({{"textColor", "#12345"}}).textColor, QStringLiteral("#FFFFFF"));
    QCOMPARE(defaults.withValues({{"font", "Comic Sans"}}).font, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"outline", "huge"}}).outline, QStringLiteral("default"));
    QCOMPARE(defaults.withValues({{"background", "BOX"}}).background, QStringLiteral("box"));
    QCOMPARE(defaults.withValues({{"backgroundOpacity", 3}}).backgroundOpacity, 25);
    QCOMPARE(defaults.withValues({{"backgroundOpacity", 101}}).backgroundOpacity, 100);
    QCOMPARE(defaults.withValues({{"positionPercent", -5}}).positionPercent, 0);
    QCOMPARE(defaults.withValues({{"positionPercent", 90}}).positionPercent, 40);
    QCOMPARE(defaults.withValues({{"applyToStyled", 1}}).applyToStyled, false);
    QCOMPARE(defaults.withValues({{"unknown", 5}}), defaults);
}

void SubtitleAppearanceTest::jsonRoundTrip()
{
    const SubtitleAppearance appearance = SubtitleAppearance{}.withValues({
        {"sizePercent", 140}, {"font", "monospace"}, {"textColor", "#22C55E"},
        {"outline", "thin"}, {"shadow", "soft"}, {"positionPercent", 10},
        {"applyToStyled", true}});
    QCOMPARE(SubtitleAppearance::fromJson(appearance.toJson()), appearance);
    QCOMPARE(appearance.toJson().value(QStringLiteral("size_percent")).toInt(), 140);
    QCOMPARE(SubtitleAppearance::fromJson(QJsonObject{{"size_percent", "big"}}), SubtitleAppearance{});
}

void SubtitleAppearanceTest::configPersistsAndMarksUserChanges()
{
    {
        ConfigManager config;
        config.load();
        QCOMPARE(config.getSubtitleAppearance(), SubtitleAppearance{});
        QVERIFY(!config.isSubtitleAppearanceUserModified());

        QSignalSpy changed(&config, &ConfigManager::subtitleAppearanceChanged);
        QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 135);
        config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("#facc15"));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(edited.count(), 2);
        QVERIFY(config.isSubtitleAppearanceUserModified());

        // Invalid values and unchanged ones are no change.
        config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("yellow"));
        config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 135);
        config.setSubtitleAppearanceValue(QStringLiteral("bogus"), 1);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(config.getSubtitleAppearanceMap().value(QStringLiteral("textColor")).toString(),
                 QStringLiteral("#FACC15"));
        config.save();
    }
    ConfigManager reloaded;
    reloaded.load();
    QCOMPARE(reloaded.getSubtitleAppearance().sizePercent, 135);
    QCOMPARE(reloaded.getSubtitleAppearance().textColor, QStringLiteral("#FACC15"));
    QVERIFY(reloaded.isSubtitleAppearanceUserModified());
}

void SubtitleAppearanceTest::configAdoptsOnlyWhileUntouched()
{
    ConfigManager config;
    QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
    const SubtitleAppearance server = SubtitleAppearance{}.withValues({{"sizePercent", 150}});
    QVERIFY(config.adoptSubtitleAppearance(server));
    QCOMPARE(config.getSubtitleAppearance(), server);
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QCOMPARE(edited.count(), 0);

    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    QVERIFY(!config.adoptSubtitleAppearance(SubtitleAppearance{}));
    QCOMPARE(config.getSubtitleAppearance().font, QStringLiteral("serif"));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 150);
}

void SubtitleAppearanceTest::configResetForgetsUserChange()
{
    ConfigManager config;
    config.setSubtitleAppearanceValue(QStringLiteral("background"), QStringLiteral("box"));
    QSignalSpy reset(&config, &ConfigManager::subtitleAppearanceReset);
    QSignalSpy edited(&config, &ConfigManager::subtitleAppearanceEdited);
    config.resetSubtitleAppearance();
    QCOMPARE(reset.count(), 1);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(config.getSubtitleAppearance(), SubtitleAppearance{});
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QVERIFY(config.adoptSubtitleAppearance(SubtitleAppearance{}.withValues({{"sizePercent", 80}})));
}

void SubtitleAppearanceTest::siloDocumentMapsSharedFields()
{
    QVariantMap fields = SiloSubtitleAppearanceMapper::canonicalFields(webDefaultDocument());
    QCOMPARE(fields.value("sizePercent").toInt(), 100);
    QCOMPARE(fields.value("font").toString(), QStringLiteral("default"));
    QCOMPARE(fields.value("textColor").toString(), QStringLiteral("#FFFFFF"));
    QCOMPARE(fields.value("background").toString(), QStringLiteral("box"));
    QCOMPARE(fields.value("backgroundOpacity").toInt(), 75);
    QCOMPARE(fields.value("positionPercent").toInt(), 0);
    QVERIFY(!fields.contains("textOpacity"));
    QVERIFY(!fields.contains("outline"));

    QJsonObject outlined = webDefaultDocument();
    outlined["backgroundStyle"] = "outline";
    outlined["fontSize"] = "xxlarge";
    outlined["fontFamily"] = "serif";
    outlined["textOutlineColor"] = "#374151";
    outlined["position"] = "lower-third";
    fields = SiloSubtitleAppearanceMapper::canonicalFields(outlined);
    QCOMPARE(fields.value("sizePercent").toInt(), 150);
    QCOMPARE(fields.value("font").toString(), QStringLiteral("serif"));
    QCOMPARE(fields.value("background").toString(), QStringLiteral("off"));
    QCOMPARE(fields.value("outline").toString(), QStringLiteral("medium"));
    QCOMPARE(fields.value("outlineColor").toString(), QStringLiteral("#374151"));
    QCOMPARE(fields.value("positionPercent").toInt(), 5);

    QJsonObject plain = webDefaultDocument();
    plain["backgroundStyle"] = "none";
    plain["position"] = "top";
    plain["fontColor"] = "not a colour";
    fields = SiloSubtitleAppearanceMapper::canonicalFields(plain);
    QCOMPARE(fields.value("outline").toString(), QStringLiteral("off"));
    QCOMPARE(fields.value("shadow").toString(), QStringLiteral("off"));
    QVERIFY(!fields.contains("positionPercent"));
    QVERIFY(!fields.contains("textColor"));
}

void SubtitleAppearanceTest::siloWriteKeepsUnmappedFields()
{
    QJsonObject base = webDefaultDocument();
    base["textOpacity"] = 60;
    base["position"] = "top";
    base["futureField"] = "kept";
    const SubtitleAppearance appearance = SubtitleAppearance{}.withValues({
        {"sizePercent", 125}, {"font", "gandhi-sans"}, {"textColor", "#FACC15"},
        {"shadow", "strong"}, {"outline", "thin"}, {"positionPercent", 10}});
    const QJsonObject document = SiloSubtitleAppearanceMapper::document(appearance, base);
    QCOMPARE(document.value("fontSize").toString(), QStringLiteral("xlarge"));
    QCOMPARE(document.value("fontFamily").toString(), QStringLiteral("sans-serif"));
    QCOMPARE(document.value("fontColor").toString(), QStringLiteral("#facc15"));
    QCOMPARE(document.value("backgroundStyle").toString(), QStringLiteral("shadow"));
    QCOMPARE(document.value("textOutline").toBool(), true);
    QCOMPARE(document.value("position").toString(), QStringLiteral("lower-third"));
    QCOMPARE(document.value("textOpacity").toInt(), 60);
    QCOMPARE(document.value("futureField").toString(), QStringLiteral("kept"));

    const QJsonObject body = QJsonDocument::fromJson(SiloSubtitleAppearanceMapper::requestBody(
        SubtitleAppearance{}, QString::fromUtf8(QJsonDocument(base).toJson()))).object();
    QCOMPARE(body.keys(), QStringList{QStringLiteral("value")});
    const QJsonObject value = SiloSubtitleAppearanceMapper::parseDocument(body.value("value").toString());
    QCOMPARE(value.value("backgroundStyle").toString(), QStringLiteral("outline"));
    QCOMPARE(value.value("fontSize").toString(), QStringLiteral("large"));
    QCOMPARE(value.value("futureField").toString(), QStringLiteral("kept"));
}

void SubtitleAppearanceTest::siloResponsesParse()
{
    ProviderCatalogResponse response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, effectiveBody(webDefaultDocument(), true).toUtf8());
    QVERIFY(response.valid);
    QVERIFY(response.capabilityMetadata.value("hasValue").toBool());
    QVERIFY(response.capabilityMetadata.value("hasDeviceOverride").toBool());
    QCOMPARE(response.capabilityMetadata.value("appearance").toMap().value("background").toString(),
             QStringLiteral("box"));
    QVERIFY(!response.capabilityMetadata.value("document").toString().isEmpty());

    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, effectiveBody({}).toUtf8());
    QVERIFY(response.valid);
    QVERIFY(!response.capabilityMetadata.value("hasValue").toBool());

    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SubtitleAppearance, QByteArrayLiteral("{\"key\":1}"));
    QVERIFY(!response.valid);

    // DELETE answers 204 with no body.
    response = SiloSubtitleAppearanceMapper::parseResponse(
        ProviderCatalogOperation::SetSubtitleAppearance, QByteArray());
    QVERIFY(response.valid);

    // A value encoded into a JSON string once more still reads.
    const QString inner = QString::fromUtf8(QJsonDocument(webDefaultDocument()).toJson(QJsonDocument::Compact));
    const QString twice = QString::fromUtf8(QJsonDocument(QJsonArray{inner}).toJson(QJsonDocument::Compact));
    QCOMPARE(SiloSubtitleAppearanceMapper::parseDocument(twice.mid(1, twice.size() - 2)),
             webDefaultDocument());
    QVERIFY(SiloSubtitleAppearanceMapper::parseDocument(QStringLiteral("not json")).isEmpty());
}

void SubtitleAppearanceTest::siloRequestsFollowTheContract()
{
    const SiloCatalogProvider provider;
    ProviderCatalogRequest request =
        provider.createRequest(ProviderCatalogOperation::SubtitleAppearance, {});
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v2/settings/subtitle-appearance/effective"));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    ProviderCatalogQuery query;
    query.stateValue = true;
    query.subtitleAppearance = SubtitleAppearance{}.withValues({{"sizePercent", 150}}).toVariantMap();
    query.subtitleAppearanceDocument = QStringLiteral("{\"textOpacity\":40}");
    request = provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v2/settings/device/subtitle-appearance"));
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);
    const QJsonObject value = SiloSubtitleAppearanceMapper::parseDocument(
        QJsonDocument::fromJson(request.body).object().value("value").toString());
    QCOMPARE(value.value("fontSize").toString(), QStringLiteral("xxlarge"));
    QCOMPARE(value.value("textOpacity").toInt(), 40);

    query.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, query);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QVERIFY(request.body.isEmpty());
    QCOMPARE(request.retrySafety, ProviderCatalogRetrySafety::Idempotent);

    QVERIFY(SiloProviderAdapter().supportsCapability(ProviderCapability::SubtitleAppearance));
}

void SubtitleAppearanceTest::jellyfinHasNoSubtitleAppearance()
{
    const JellyfinCatalogProvider provider;
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SubtitleAppearance, {}).supported);
    QVERIFY(!provider.createRequest(ProviderCatalogOperation::SetSubtitleAppearance, {}).supported);
    QVERIFY(!JellyfinProviderAdapter().supportsCapability(ProviderCapability::SubtitleAppearance));
}

void SubtitleAppearanceTest::syncAdoptsOnlyWhenUntouched()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);

    sync.refresh();
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"sizePercent", 150}, {"background", "box"}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 150);
    QCOMPARE(config.getSubtitleAppearance().background, QStringLiteral("box"));
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    // Adoption is no user change: nothing is written back.
    QVERIFY(!sync.writePending());

    // No value on the server leaves the local appearance.
    sync.refresh();
    library.reads.takeFirst()(true, {{"hasValue", false}});
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 150);

    // After a user change, another profile's appearance is not adopted.
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 90);
    library.scope = QStringLiteral("silo\n2");
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 200}}));
    QCOMPARE(config.getSubtitleAppearance().sizePercent, 90);
}

void SubtitleAppearanceTest::syncWritesOnlyOnUserChange()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    library.reads.takeFirst()(true, readResult({{"sizePercent", 125}},
                                               QStringLiteral("{\"textOpacity\":50}")));
    QVERIFY(library.writes.isEmpty());

    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 130);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 140);
    QVERIFY(sync.writePending());
    QVERIFY(library.writes.isEmpty());
    // Debounced into one write of the latest appearance.
    QTRY_COMPARE_WITH_TIMEOUT(library.writes.size(), 1,
                              SubtitleAppearanceSync::kWriteDebounceMs + 2000);
    QCOMPARE(library.writtenAppearances.first().value("sizePercent").toInt(), 140);
    QCOMPARE(library.writtenDocuments.first(), QStringLiteral("{\"textOpacity\":50}"));

    // A change while the write is out is sent after it.
    config.setSubtitleAppearanceValue(QStringLiteral("font"), QStringLiteral("serif"));
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);
    library.writes.takeFirst()(true, {{"document", QStringLiteral("{\"fontSize\":\"xlarge\"}")}});
    QCOMPARE(library.writes.size(), 1);
    QCOMPARE(library.writtenAppearances.last().value("font").toString(), QStringLiteral("serif"));
    QCOMPARE(library.writtenDocuments.last(), QStringLiteral("{\"fontSize\":\"xlarge\"}"));
}

void SubtitleAppearanceTest::syncKeepsLocalChangeWhenWriteFails()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("textColor"), QStringLiteral("#22C55E"));
    sync.flushPendingWrite();
    QCOMPARE(library.writes.size(), 1);
    library.writes.takeFirst()(false, {});
    QCOMPARE(config.getSubtitleAppearance().textColor, QStringLiteral("#22C55E"));
    QVERIFY(config.isSubtitleAppearanceUserModified());
    QVERIFY(!sync.writePending());
}

void SubtitleAppearanceTest::syncDropsWriteForAnotherScope()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    QVERIFY(sync.writePending());
    library.scope = QStringLiteral("silo\n2");
    sync.flushPendingWrite();
    QVERIFY(library.writes.isEmpty());
}

void SubtitleAppearanceTest::syncResetRemovesOverrideAndAdoptsAgain()
{
    ConfigManager config;
    FakeLibraryService library;
    library.scope = QStringLiteral("silo\n1");
    SubtitleAppearanceSync sync(&config, &library);
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 160);
    QVERIFY(sync.writePending());

    config.resetSubtitleAppearance();
    QVERIFY(!sync.writePending());
    QCOMPARE(library.resets.size(), 1);
    library.resets.takeFirst()(true, {});
    // The profile-wide appearance is read and becomes the starting point.
    QCOMPARE(library.reads.size(), 1);
    library.reads.takeFirst()(true, readResult({{"font", "monospace"}}));
    QCOMPARE(config.getSubtitleAppearance().font, QStringLiteral("monospace"));
    QVERIFY(!config.isSubtitleAppearanceUserModified());
    QVERIFY(library.writes.isEmpty());
}

void SubtitleAppearanceTest::syncStaysLocalWithoutCapability()
{
    ConfigManager config;
    FakeLibraryService library;  // Jellyfin: no scope
    SubtitleAppearanceSync sync(&config, &library);
    sync.refresh();
    config.setSubtitleAppearanceValue(QStringLiteral("sizePercent"), 120);
    config.resetSubtitleAppearance();
    QVERIFY(!sync.writePending());
    QVERIFY(library.reads.isEmpty());
    QVERIFY(library.resets.isEmpty());
    QVERIFY(library.writes.isEmpty());
}

QTEST_GUILESS_MAIN(SubtitleAppearanceTest)
#include "SubtitleAppearanceTest.moc"
