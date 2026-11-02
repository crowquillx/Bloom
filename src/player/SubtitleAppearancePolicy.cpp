#include "player/SubtitleAppearancePolicy.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace Bloom::SubtitleAppearancePolicy {
namespace {

constexpr auto kSubScale = "sub-scale";
constexpr auto kSubFont = "sub-font";
constexpr auto kSubColor = "sub-color";
constexpr auto kSubBorderStyle = "sub-border-style";
constexpr auto kSubBorderColor = "sub-border-color";
constexpr auto kSubBorderSize = "sub-border-size";
constexpr auto kSubShadowOffset = "sub-shadow-offset";
constexpr auto kSubShadowColor = "sub-shadow-color";
constexpr auto kSubPos = "sub-pos";
constexpr auto kSubAssOverride = "sub-ass-override";

// Margin of the opaque box around each line, in mpv's scaled pixels.
constexpr auto kBoxPadding = "4";
constexpr auto kShadowColor = "#C0000000";

// "#RRGGBB" with an opacity in percent as mpv's "#AARRGGBB".
QString colorWithAlpha(const QString &color, int opacityPercent)
{
    const int alpha = static_cast<int>(std::lround(std::clamp(opacityPercent, 0, 100) * 255 / 100.0));
    return QStringLiteral("#%1%2").arg(alpha, 2, 16, QLatin1Char('0')).arg(color.mid(1)).toUpper();
}

QString outlineSize(const QString &outline)
{
    if (outline == QStringLiteral("off")) return QStringLiteral("0");
    if (outline == QStringLiteral("thin")) return QStringLiteral("1.2");
    if (outline == QStringLiteral("medium")) return QStringLiteral("2.4");
    if (outline == QStringLiteral("thick")) return QStringLiteral("3.6");
    return {};
}

QString shadowOffset(const QString &shadow)
{
    if (shadow == QStringLiteral("off")) return QStringLiteral("0");
    if (shadow == QStringLiteral("soft")) return QStringLiteral("0.75");
    if (shadow == QStringLiteral("medium")) return QStringLiteral("1.5");
    if (shadow == QStringLiteral("strong")) return QStringLiteral("2.5");
    return {};
}

// mpv 0.41's defaults for the managed properties.
QString mpvDefault(const QString &property)
{
    static const QHash<QString, QString> defaults{
        {QString::fromLatin1(kSubScale), QStringLiteral("1")},
        {QString::fromLatin1(kSubFont), QStringLiteral("sans-serif")},
        {QString::fromLatin1(kSubColor), QStringLiteral("#FFFFFFFF")},
        {QString::fromLatin1(kSubBorderStyle), QStringLiteral("outline-and-shadow")},
        {QString::fromLatin1(kSubBorderColor), QStringLiteral("#FF000000")},
        {QString::fromLatin1(kSubBorderSize), QStringLiteral("1.65")},
        {QString::fromLatin1(kSubShadowOffset), QStringLiteral("0")},
        {QString::fromLatin1(kSubShadowColor), QStringLiteral("#AF000000")},
        {QString::fromLatin1(kSubPos), QStringLiteral("100")},
        {QString::fromLatin1(kSubAssOverride), QStringLiteral("scale")},
    };
    return defaults.value(property);
}

// Newer mpv names for the managed aliases.
QString managedName(const QString &name)
{
    if (name == QStringLiteral("sub-outline-color")) return QString::fromLatin1(kSubBorderColor);
    if (name == QStringLiteral("sub-outline-size")) return QString::fromLatin1(kSubBorderSize);
    if (name == QStringLiteral("sub-back-color")) return QString::fromLatin1(kSubShadowColor);
    return name;
}

// The properties the appearance sets itself.
MpvProperties ownProperties(const SubtitleAppearance &appearance)
{
    MpvProperties properties;
    const auto add = [&properties](const char *name, const QString &value) {
        properties.append({QString::fromLatin1(name), value});
    };
    const SubtitleAppearance defaults;
    if (appearance.sizePercent != defaults.sizePercent) {
        add(kSubScale, QString::number(appearance.sizePercent / 100.0, 'f', 2));
    }
    const QString family = SubtitleAppearance::fontFamily(appearance.font);
    if (!family.isEmpty()) {
        add(kSubFont, family);
    }
    if (appearance.textColor != defaults.textColor) {
        add(kSubColor, appearance.textColor);
    }
    if (appearance.background == QStringLiteral("box")) {
        // mpv draws an opaque box per line in the outline colour, with the
        // outline size as its margin. The shadow would add a second box.
        add(kSubBorderStyle, QStringLiteral("opaque-box"));
        add(kSubBorderColor, colorWithAlpha(appearance.backgroundColor, appearance.backgroundOpacity));
        add(kSubBorderSize, QString::fromLatin1(kBoxPadding));
        add(kSubShadowOffset, QStringLiteral("0"));
    } else {
        if (appearance.outlineColor != defaults.outlineColor) {
            add(kSubBorderColor, appearance.outlineColor);
        }
        if (const QString size = outlineSize(appearance.outline); !size.isEmpty()) {
            add(kSubBorderSize, size);
        }
        if (const QString offset = shadowOffset(appearance.shadow); !offset.isEmpty()) {
            add(kSubShadowOffset, offset);
            if (appearance.shadow != QStringLiteral("off")) {
                add(kSubShadowColor, QString::fromLatin1(kShadowColor));
            }
        }
    }
    if (appearance.positionPercent != defaults.positionPercent) {
        add(kSubPos, QString::number(100 - appearance.positionPercent));
    }
    if (appearance.applyToStyled) {
        // Only `force` applies font, colours and borders to styled
        // subtitles; it also breaks typesetting, so it stays opt-in.
        add(kSubAssOverride, QStringLiteral("force"));
    }
    return properties;
}

}  // namespace

QStringList managedProperties()
{
    return {QString::fromLatin1(kSubScale), QString::fromLatin1(kSubFont),
            QString::fromLatin1(kSubColor), QString::fromLatin1(kSubBorderStyle),
            QString::fromLatin1(kSubBorderColor), QString::fromLatin1(kSubBorderSize),
            QString::fromLatin1(kSubShadowOffset), QString::fromLatin1(kSubShadowColor),
            QString::fromLatin1(kSubPos), QString::fromLatin1(kSubAssOverride)};
}

QStringList mpvArguments(const SubtitleAppearance &appearance)
{
    QStringList arguments;
    for (const auto &[name, value] : ownProperties(appearance)) {
        arguments.append(QStringLiteral("--%1=%2").arg(name, value));
    }
    return arguments;
}

MpvProperties mpvProperties(const SubtitleAppearance &appearance, const QStringList &baseArgs)
{
    const QStringList names = managedProperties();
    QHash<QString, QString> base;
    for (const QString &arg : baseArgs) {
        const qsizetype separator = arg.indexOf(QLatin1Char('='));
        if (!arg.startsWith(QStringLiteral("--")) || separator < 0) {
            continue;
        }
        const QString name = managedName(arg.mid(2, separator - 2));
        if (names.contains(name)) {
            base.insert(name, arg.mid(separator + 1));
        }
    }
    QHash<QString, QString> own;
    for (const auto &[name, value] : ownProperties(appearance)) {
        own.insert(name, value);
    }
    MpvProperties properties;
    for (const QString &name : names) {
        properties.append({name, own.value(name, base.value(name, mpvDefault(name)))});
    }
    return properties;
}

MpvProperties changedProperties(const MpvProperties &from, const MpvProperties &to)
{
    MpvProperties changed;
    for (const auto &entry : to) {
        const auto previous = std::find_if(from.cbegin(), from.cend(), [&entry](const auto &candidate) {
            return candidate.first == entry.first;
        });
        if (previous == from.cend() || previous->second != entry.second) {
            changed.append(entry);
        }
    }
    return changed;
}

}  // namespace Bloom::SubtitleAppearancePolicy
