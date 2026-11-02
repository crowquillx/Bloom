#include <QtTest/QtTest>

#include <functional>

#include "models/SubtitleAppearance.h"
#include "player/SubtitleAppearancePolicy.h"

// Links only Bloom::PlayerPolicy (Qt Core): the appearances here are built
// field by field, since the SubtitleAppearance helpers that parse and
// validate live in Bloom::Models.

using Bloom::SubtitleAppearance;
namespace Policy = Bloom::SubtitleAppearancePolicy;

namespace {

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

SubtitleAppearance with(const std::function<void(SubtitleAppearance &)> &change)
{
    SubtitleAppearance appearance;
    change(appearance);
    return appearance;
}

}  // namespace

class SubtitleAppearancePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsLeaveMpvStylingAlone();
    void eachFieldMapsToMpvOptions();
    void explicitColoursApplyUnderAnyProfile();
    void boxReplacesOutlineAndShadow();
    void defaultFieldsResolveToProfileThenMpv();
    void changedPropertiesSetOnlyDifferences();
    void previewFollowsTheProfile();
};

void SubtitleAppearancePolicyTest::defaultsLeaveMpvStylingAlone()
{
    const SubtitleAppearance appearance;
    QVERIFY(Policy::mpvArguments(appearance).isEmpty());
    const Policy::MpvProperties properties = Policy::mpvProperties(appearance, {});
    QCOMPARE(propertyNames(properties), Policy::managedProperties());
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-scale")), QStringLiteral("1"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-font")), QStringLiteral("sans-serif"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-color")), QStringLiteral("#FFFFFFFF"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-border-style")),
             QStringLiteral("outline-and-shadow"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-pos")), QStringLiteral("100"));
    // Styled subtitles keep mpv's conservative default.
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-ass-override")), QStringLiteral("scale"));
    QVERIFY(!appearance.applyToStyled);
}

void SubtitleAppearancePolicyTest::eachFieldMapsToMpvOptions()
{
    const QList<std::pair<SubtitleAppearance, QStringList>> cases{
        {with([](auto &a) { a.sizePercent = 150; }), {"--sub-scale=1.50"}},
        {with([](auto &a) { a.sizePercent = 75; }), {"--sub-scale=0.75"}},
        {with([](auto &a) { a.font = "gandhi-sans-bold"; }), {"--sub-font=Gandhi Sans Bold"}},
        {with([](auto &a) { a.font = "serif"; }), {"--sub-font=serif"}},
        {with([](auto &a) { a.textColor = "#FACC15"; }), {"--sub-color=#FACC15"}},
        {with([](auto &a) { a.outlineColor = "#1E3A5F"; }), {"--sub-border-color=#1E3A5F"}},
        {with([](auto &a) { a.outline = "off"; }), {"--sub-border-size=0"}},
        {with([](auto &a) { a.outline = "thick"; }), {"--sub-border-size=3.6"}},
        {with([](auto &a) { a.shadow = "off"; }), {"--sub-shadow-offset=0"}},
        {with([](auto &a) { a.shadow = "medium"; }),
         {"--sub-shadow-offset=1.5", "--sub-shadow-color=#C0000000"}},
        {with([](auto &a) { a.positionPercent = 15; }), {"--sub-pos=85"}},
        {with([](auto &a) { a.applyToStyled = true; }), {"--sub-ass-override=force"}},
    };
    for (const auto &[appearance, arguments] : cases) {
        QCOMPARE(Policy::mpvArguments(appearance), arguments);
    }
}

void SubtitleAppearancePolicyTest::explicitColoursApplyUnderAnyProfile()
{
    // White and Black are values of their own, not "Default": they win
    // over a profile with other colours.
    const QStringList profileArgs{QStringLiteral("--sub-color=#FFFACC15"),
                                  QStringLiteral("--sub-border-color=#FF1E3A5F")};
    QCOMPARE(SubtitleAppearance{}.textColor, QStringLiteral("default"));
    const SubtitleAppearance explicitColours = with([](auto &a) {
        a.textColor = "#FFFFFF";
        a.outlineColor = "#000000";
    });
    QCOMPARE(Policy::mpvArguments(explicitColours),
             (QStringList{"--sub-color=#FFFFFF", "--sub-border-color=#000000"}));
    const auto properties = Policy::mpvProperties(explicitColours, profileArgs);
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-color")), QStringLiteral("#FFFFFF"));
    QCOMPARE(propertyValue(properties, QStringLiteral("sub-border-color")), QStringLiteral("#000000"));
    const auto defaults = Policy::mpvProperties(SubtitleAppearance{}, profileArgs);
    QCOMPARE(propertyValue(defaults, QStringLiteral("sub-color")), QStringLiteral("#FFFACC15"));
}

void SubtitleAppearancePolicyTest::boxReplacesOutlineAndShadow()
{
    const SubtitleAppearance appearance = with([](auto &a) {
        a.background = "box";
        a.backgroundColor = "#1E3A5F";
        a.backgroundOpacity = 50;
        a.outline = "thick";
        a.shadow = "strong";
        a.outlineColor = "#FFFFFF";
    });
    QCOMPARE(Policy::mpvArguments(appearance),
             (QStringList{"--sub-border-style=opaque-box", "--sub-border-color=#801E3A5F",
                          "--sub-border-size=4", "--sub-shadow-offset=0"}));
}

void SubtitleAppearancePolicyTest::defaultFieldsResolveToProfileThenMpv()
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

    const Policy::MpvProperties own = Policy::mpvProperties(with([](auto &a) {
        a.font = "verdana";
        a.applyToStyled = true;
    }), profileArgs);
    QCOMPARE(propertyValue(own, QStringLiteral("sub-font")), QStringLiteral("Verdana"));
    QCOMPARE(propertyValue(own, QStringLiteral("sub-ass-override")), QStringLiteral("force"));
    QCOMPARE(propertyValue(own, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
}

void SubtitleAppearancePolicyTest::changedPropertiesSetOnlyDifferences()
{
    const QStringList profileArgs{QStringLiteral("--sub-border-size=2.4")};
    const SubtitleAppearance large = with([](auto &a) { a.sizePercent = 125; });
    const auto from = Policy::mpvProperties(SubtitleAppearance{}, profileArgs);
    const auto to = Policy::mpvProperties(large, profileArgs);
    QCOMPARE(Policy::changedProperties(from, to),
             (Policy::MpvProperties{{"sub-scale", "1.25"}}));
    QVERIFY(Policy::changedProperties(to, to).isEmpty());

    // A size change while styled subtitles are restyled sends the size only,
    // so a sub-ass-override mpv's own keys changed is not sent back.
    const SubtitleAppearance styled = with([](auto &a) { a.applyToStyled = true; });
    SubtitleAppearance styledLarge = styled;
    styledLarge.sizePercent = 125;
    QCOMPARE(Policy::changedProperties(Policy::mpvProperties(styled, profileArgs),
                                       Policy::mpvProperties(styledLarge, profileArgs)),
             (Policy::MpvProperties{{"sub-scale", "1.25"}}));

    // Turning the box off restores the profile's outline, not mpv's.
    const SubtitleAppearance boxed = with([](auto &a) { a.background = "box"; });
    const auto boxedProperties = Policy::mpvProperties(boxed, profileArgs);
    const auto back = Policy::changedProperties(boxedProperties, from);
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-style")),
             QStringLiteral("outline-and-shadow"));
    QCOMPARE(propertyValue(back, QStringLiteral("sub-border-color")), QStringLiteral("#FF000000"));
}

void SubtitleAppearancePolicyTest::previewFollowsTheProfile()
{
    const QStringList profileArgs{QStringLiteral("--sub-font-size=48"),
                                  QStringLiteral("--sub-border-size=2.4")};
    auto preview = Policy::previewProperties(SubtitleAppearance{}, profileArgs);
    QCOMPARE(propertyValue(preview, QStringLiteral("sub-font-size")), QStringLiteral("48"));
    QCOMPARE(propertyValue(preview, QStringLiteral("sub-border-size")), QStringLiteral("2.4"));
    preview = Policy::previewProperties(with([](auto &a) { a.outline = "thin"; }), {});
    QCOMPARE(propertyValue(preview, QStringLiteral("sub-font-size")), QStringLiteral("38"));
    QCOMPARE(propertyValue(preview, QStringLiteral("sub-border-size")), QStringLiteral("1.2"));
}

QTEST_GUILESS_MAIN(SubtitleAppearancePolicyTest)
#include "SubtitleAppearancePolicyTest.moc"
