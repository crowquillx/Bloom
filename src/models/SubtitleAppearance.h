#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Bloom {

/// How text subtitles look, as the user chose it in Bloom. Every field keeps
/// a valid value: the constructors below normalize what they read. Fields
/// left at their default leave mpv's own styling (the MPV profile's `sub-*`
/// arguments, or mpv's defaults) alone; Bloom::SubtitleAppearancePolicy maps
/// the rest to mpv options.
struct SubtitleAppearance {
    static constexpr int kMinSizePercent = 75;
    static constexpr int kMaxSizePercent = 200;
    static constexpr int kSizeStepPercent = 5;
    static constexpr int kMinBackgroundOpacity = 25;
    static constexpr int kMaxBackgroundOpacity = 100;
    static constexpr int kBackgroundOpacityStep = 5;
    static constexpr int kMaxPositionPercent = 40;
    static constexpr int kPositionStepPercent = 5;
    static constexpr auto kDefaultColor = "default";

    int sizePercent = 100;
    // A fontIds() entry; "default" sets no font.
    QString font = QStringLiteral("default");
    // "#RRGGBB" in upper case, or "default" for the MPV profile's colour
    // (kDefaultColor), so an explicit White or Black still applies under a
    // profile with other colours.
    QString textColor = QString::fromLatin1(kDefaultColor);
    QString outlineColor = QString::fromLatin1(kDefaultColor);
    // An outlineIds() entry.
    QString outline = QStringLiteral("default");
    // A shadowIds() entry.
    QString shadow = QStringLiteral("default");
    // "off" or "box".
    QString background = QStringLiteral("off");
    QString backgroundColor = QStringLiteral("#000000");
    int backgroundOpacity = 75;
    // How far above the default bottom position, in % of the screen height.
    int positionPercent = 0;
    // Also restyle styled (ASS/SSA) subtitles.
    bool applyToStyled = false;

    bool operator==(const SubtitleAppearance &) const = default;

    /// camelCase keys, as QML and canonical provider projections use them.
    QVariantMap toVariantMap() const;
    /// The appearance with the valid keys of `values` applied; unknown keys
    /// are ignored and invalid values keep the current field.
    SubtitleAppearance withValues(const QVariantMap &values) const;
    /// snake_case keys, as `app.json` stores them.
    QJsonObject toJson() const;
    static SubtitleAppearance fromJson(const QJsonObject &object);

    static QStringList fontIds();
    static QStringList outlineIds();
    static QStringList shadowIds();
    static QStringList backgroundIds();
    /// The family mpv (and the Settings preview) uses for a font id; empty
    /// for "default" and unknown ids. Inline so Bloom::PlayerPolicy uses this
    /// header without linking the models library.
    static QString fontFamily(const QString &fontId)
    {
        // Gandhi Sans ships in Bloom's mpv fonts directory (~~/fonts); the
        // others are names fontconfig and DirectWrite resolve everywhere.
        if (fontId == QStringLiteral("gandhi-sans")) return QStringLiteral("Gandhi Sans");
        if (fontId == QStringLiteral("gandhi-sans-bold")) return QStringLiteral("Gandhi Sans Bold");
        if (fontId == QStringLiteral("sans-serif")) return QStringLiteral("sans-serif");
        if (fontId == QStringLiteral("serif")) return QStringLiteral("serif");
        if (fontId == QStringLiteral("monospace")) return QStringLiteral("monospace");
        if (fontId == QStringLiteral("arial")) return QStringLiteral("Arial");
        if (fontId == QStringLiteral("verdana")) return QStringLiteral("Verdana");
        return {};
    }
    /// Snaps a size into the stepper's range and steps.
    static int normalizedSizePercent(int sizePercent);
    /// "#RRGGBB" in upper case, or empty when `color` is no 6-digit hex colour.
    static QString normalizedColor(const QString &color);
    /// normalizedColor, but also accepting kDefaultColor (text and outline).
    static QString normalizedColorOrDefault(const QString &color);
};

}  // namespace Bloom
