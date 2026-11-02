#include "models/SubtitleAppearance.h"

#include <QJsonValue>
#include <QList>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace Bloom {
namespace {

int clampToSteps(int value, int minimum, int maximum, int step)
{
    const int clamped = std::clamp(value, minimum, maximum);
    return minimum + static_cast<int>(std::lround((clamped - minimum) / double(step))) * step;
}

std::optional<int> intValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        break;
    default:
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number)) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(std::clamp(number, -1.0e6, 1.0e6)));
}

QString choice(const QVariant &value, const QStringList &allowed)
{
    if (value.typeId() != QMetaType::QString) {
        return {};
    }
    const QString text = value.toString().trimmed().toLower();
    return allowed.contains(text) ? text : QString();
}

}  // namespace

QVariantMap SubtitleAppearance::toVariantMap() const
{
    return {
        {QStringLiteral("sizePercent"), sizePercent},
        {QStringLiteral("font"), font},
        {QStringLiteral("textColor"), textColor},
        {QStringLiteral("outlineColor"), outlineColor},
        {QStringLiteral("outline"), outline},
        {QStringLiteral("shadow"), shadow},
        {QStringLiteral("background"), background},
        {QStringLiteral("backgroundColor"), backgroundColor},
        {QStringLiteral("backgroundOpacity"), backgroundOpacity},
        {QStringLiteral("positionPercent"), positionPercent},
        {QStringLiteral("applyToStyled"), applyToStyled},
    };
}

SubtitleAppearance SubtitleAppearance::withValues(const QVariantMap &values) const
{
    SubtitleAppearance result = *this;
    const auto number = [&values](const char *key) {
        const auto it = values.constFind(QString::fromLatin1(key));
        return it == values.cend() ? std::nullopt : intValue(*it);
    };
    const auto text = [&values](const char *key, const QStringList &allowed) {
        const auto it = values.constFind(QString::fromLatin1(key));
        return it == values.cend() ? QString() : choice(*it, allowed);
    };
    const auto color = [&values](const char *key) {
        const auto it = values.constFind(QString::fromLatin1(key));
        return it == values.cend() || it->typeId() != QMetaType::QString
            ? QString() : normalizedColor(it->toString());
    };
    if (const auto value = number("sizePercent")) {
        result.sizePercent = normalizedSizePercent(*value);
    }
    if (const QString value = text("font", fontIds()); !value.isEmpty()) {
        result.font = value;
    }
    if (const QString value = color("textColor"); !value.isEmpty()) {
        result.textColor = value;
    }
    if (const QString value = color("outlineColor"); !value.isEmpty()) {
        result.outlineColor = value;
    }
    if (const QString value = text("outline", outlineIds()); !value.isEmpty()) {
        result.outline = value;
    }
    if (const QString value = text("shadow", shadowIds()); !value.isEmpty()) {
        result.shadow = value;
    }
    if (const QString value = text("background", backgroundIds()); !value.isEmpty()) {
        result.background = value;
    }
    if (const QString value = color("backgroundColor"); !value.isEmpty()) {
        result.backgroundColor = value;
    }
    if (const auto value = number("backgroundOpacity")) {
        result.backgroundOpacity = clampToSteps(*value, kMinBackgroundOpacity,
                                                kMaxBackgroundOpacity, kBackgroundOpacityStep);
    }
    if (const auto value = number("positionPercent")) {
        result.positionPercent = clampToSteps(*value, 0, kMaxPositionPercent, kPositionStepPercent);
    }
    const auto styled = values.constFind(QStringLiteral("applyToStyled"));
    if (styled != values.cend() && styled->typeId() == QMetaType::Bool) {
        result.applyToStyled = styled->toBool();
    }
    return result;
}

namespace {
// app.json key -> canonical key.
const QList<std::pair<const char *, const char *>> &storedKeys()
{
    static const QList<std::pair<const char *, const char *>> keys{
        {"size_percent", "sizePercent"},
        {"font", "font"},
        {"text_color", "textColor"},
        {"outline_color", "outlineColor"},
        {"outline", "outline"},
        {"shadow", "shadow"},
        {"background", "background"},
        {"background_color", "backgroundColor"},
        {"background_opacity", "backgroundOpacity"},
        {"position_percent", "positionPercent"},
        {"apply_to_styled", "applyToStyled"},
    };
    return keys;
}
}  // namespace

QJsonObject SubtitleAppearance::toJson() const
{
    const QVariantMap values = toVariantMap();
    QJsonObject object;
    for (const auto &[stored, canonical] : storedKeys()) {
        object.insert(QString::fromLatin1(stored),
                      QJsonValue::fromVariant(values.value(QString::fromLatin1(canonical))));
    }
    return object;
}

SubtitleAppearance SubtitleAppearance::fromJson(const QJsonObject &object)
{
    QVariantMap values;
    for (const auto &[stored, canonical] : storedKeys()) {
        const QJsonValue value = object.value(QString::fromLatin1(stored));
        if (!value.isUndefined() && !value.isNull()) {
            values.insert(QString::fromLatin1(canonical), value.toVariant());
        }
    }
    return SubtitleAppearance{}.withValues(values);
}

QStringList SubtitleAppearance::fontIds()
{
    return {QStringLiteral("default"), QStringLiteral("gandhi-sans"),
            QStringLiteral("gandhi-sans-bold"), QStringLiteral("sans-serif"),
            QStringLiteral("serif"), QStringLiteral("monospace"),
            QStringLiteral("arial"), QStringLiteral("verdana")};
}

QStringList SubtitleAppearance::outlineIds()
{
    return {QStringLiteral("default"), QStringLiteral("off"), QStringLiteral("thin"),
            QStringLiteral("medium"), QStringLiteral("thick")};
}

QStringList SubtitleAppearance::shadowIds()
{
    return {QStringLiteral("default"), QStringLiteral("off"), QStringLiteral("soft"),
            QStringLiteral("medium"), QStringLiteral("strong")};
}

QStringList SubtitleAppearance::backgroundIds()
{
    return {QStringLiteral("off"), QStringLiteral("box")};
}

int SubtitleAppearance::normalizedSizePercent(int sizePercent)
{
    return clampToSteps(sizePercent, kMinSizePercent, kMaxSizePercent, kSizeStepPercent);
}

QString SubtitleAppearance::normalizedColor(const QString &color)
{
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    const QString trimmed = color.trimmed();
    return pattern.match(trimmed).hasMatch() ? trimmed.toUpper() : QString();
}

}  // namespace Bloom
