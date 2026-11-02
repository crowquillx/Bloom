#include "providers/silo/SiloSubtitleAppearanceMapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>

namespace {

// Silo's font sizes against Bloom's percent: "large" is the web player's
// 100 % (32 px at 720p); "small" (20 px) is below Bloom's smallest size.
struct FontSize {
    const char *wire;
    int percent;
};
constexpr FontSize kFontSizes[] = {
    {"small", 75}, {"medium", 80}, {"large", 100}, {"xlarge", 125}, {"xxlarge", 150},
};

QString wireFontSize(int percent)
{
    if (percent <= 77) return QStringLiteral("small");
    if (percent <= 90) return QStringLiteral("medium");
    if (percent <= 112) return QStringLiteral("large");
    if (percent <= 137) return QStringLiteral("xlarge");
    return QStringLiteral("xxlarge");
}

QString color(const QJsonObject &document, const char *key)
{
    const QJsonValue value = document.value(QLatin1String(key));
    return value.isString() ? Bloom::SubtitleAppearance::normalizedColor(value.toString()) : QString();
}

bool hasOutline(const QString &outline)
{
    return outline == QStringLiteral("thin") || outline == QStringLiteral("medium")
        || outline == QStringLiteral("thick");
}

bool hasShadow(const QString &shadow)
{
    return shadow == QStringLiteral("soft") || shadow == QStringLiteral("medium")
        || shadow == QStringLiteral("strong");
}

}  // namespace

QVariantMap SiloSubtitleAppearanceMapper::canonicalFields(const QJsonObject &document)
{
    QVariantMap fields;
    const QString fontSize = document.value(QLatin1String("fontSize")).toString();
    for (const FontSize &size : kFontSizes) {
        if (fontSize == QLatin1String(size.wire)) {
            fields.insert(QStringLiteral("sizePercent"), size.percent);
        }
    }
    const QString family = document.value(QLatin1String("fontFamily")).toString();
    if (family == QStringLiteral("serif") || family == QStringLiteral("monospace")) {
        fields.insert(QStringLiteral("font"), family);
    } else if (family == QStringLiteral("sans-serif")) {
        // Bloom's default font is a sans-serif one already (the profile's).
        fields.insert(QStringLiteral("font"), QStringLiteral("default"));
    }
    if (const QString text = color(document, "fontColor"); !text.isEmpty()) {
        fields.insert(QStringLiteral("textColor"), text);
    }
    if (const QString outline = color(document, "textOutlineColor"); !outline.isEmpty()) {
        fields.insert(QStringLiteral("outlineColor"), outline);
    }
    if (const QString background = color(document, "backgroundColor"); !background.isEmpty()) {
        fields.insert(QStringLiteral("backgroundColor"), background);
    }
    const QJsonValue opacity = document.value(QLatin1String("backgroundOpacity"));
    if (opacity.isDouble() && std::isfinite(opacity.toDouble())
        && opacity.toDouble() >= 0 && opacity.toDouble() <= 100) {
        fields.insert(QStringLiteral("backgroundOpacity"),
                      static_cast<int>(std::lround(opacity.toDouble())));
    }
    const QJsonValue textOutline = document.value(QLatin1String("textOutline"));
    const bool outlined = textOutline.isBool() && textOutline.toBool();
    const QString style = document.value(QLatin1String("backgroundStyle")).toString();
    if (style == QStringLiteral("box")) {
        fields.insert(QStringLiteral("background"), QStringLiteral("box"));
    } else if (style == QStringLiteral("shadow") || style == QStringLiteral("outline")
               || style == QStringLiteral("none")) {
        fields.insert(QStringLiteral("background"), QStringLiteral("off"));
        fields.insert(QStringLiteral("shadow"),
                      style == QStringLiteral("shadow") ? QStringLiteral("medium")
                      : style == QStringLiteral("none") ? QStringLiteral("off")
                                                         : QStringLiteral("default"));
        if (style == QStringLiteral("outline") || outlined) {
            fields.insert(QStringLiteral("outline"), QStringLiteral("medium"));
        } else if (style == QStringLiteral("none")) {
            fields.insert(QStringLiteral("outline"), QStringLiteral("off"));
        }
    }
    const QString position = document.value(QLatin1String("position")).toString();
    if (position == QStringLiteral("bottom")) {
        fields.insert(QStringLiteral("positionPercent"), 0);
    } else if (position == QStringLiteral("lower-third")) {
        fields.insert(QStringLiteral("positionPercent"), 5);
    }
    // "top", textOpacity and anything newer have no Bloom field.
    return fields;
}

QJsonObject SiloSubtitleAppearanceMapper::document(const Bloom::SubtitleAppearance &appearance,
                                                   const QJsonObject &base)
{
    QJsonObject document = base;
    document.insert(QStringLiteral("fontSize"), wireFontSize(appearance.sizePercent));
    document.insert(QStringLiteral("fontFamily"),
                    appearance.font == QStringLiteral("serif")
                            || appearance.font == QStringLiteral("monospace")
                        ? appearance.font
                        : QStringLiteral("sans-serif"));
    document.insert(QStringLiteral("fontColor"), appearance.textColor.toLower());
    document.insert(QStringLiteral("textOutlineColor"), appearance.outlineColor.toLower());
    document.insert(QStringLiteral("backgroundColor"), appearance.backgroundColor.toLower());
    document.insert(QStringLiteral("backgroundOpacity"), appearance.backgroundOpacity);
    QString style;
    if (appearance.background == QStringLiteral("box")) {
        style = QStringLiteral("box");
    } else if (hasShadow(appearance.shadow)) {
        style = QStringLiteral("shadow");
    } else if (appearance.outline == QStringLiteral("off")) {
        style = QStringLiteral("none");
    } else {
        // mpv outlines text unless the outline is off.
        style = QStringLiteral("outline");
    }
    document.insert(QStringLiteral("backgroundStyle"), style);
    document.insert(QStringLiteral("textOutline"),
                    style != QStringLiteral("outline") && style != QStringLiteral("none")
                        && hasOutline(appearance.outline));
    document.insert(QStringLiteral("position"),
                    appearance.positionPercent >= 5 ? QStringLiteral("lower-third")
                                                    : QStringLiteral("bottom"));
    return document;
}

QByteArray SiloSubtitleAppearanceMapper::requestBody(const Bloom::SubtitleAppearance &appearance,
                                                     const QString &baseDocument)
{
    const QJsonObject value = document(appearance, parseDocument(baseDocument));
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("value"),
                              QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))},
                         })
        .toJson(QJsonDocument::Compact);
}

QJsonObject SiloSubtitleAppearanceMapper::parseDocument(const QString &value)
{
    // The value is a JSON document; older writers encoded the object into a
    // JSON string once more.
    QJsonValue parsed = QJsonValue(value);
    for (int depth = 0; depth < 2 && parsed.isString(); ++depth) {
        const QString text = parsed.toString().trimmed();
        if (text.isEmpty()) {
            return {};
        }
        const QJsonDocument wrapped = QJsonDocument::fromJson(
            QByteArrayLiteral("[") + text.toUtf8() + QByteArrayLiteral("]"));
        if (!wrapped.isArray() || wrapped.array().size() != 1) {
            return {};
        }
        parsed = wrapped.array().first();
    }
    return parsed.isObject() ? parsed.toObject() : QJsonObject();
}

ProviderCatalogResponse SiloSubtitleAppearanceMapper::parseResponse(
    ProviderCatalogOperation operation, const QByteArray &body)
{
    ProviderCatalogResponse response;
    if (operation == ProviderCatalogOperation::SetSubtitleAppearance && body.trimmed().isEmpty()) {
        // deleteSubtitleAppearanceDeviceOverride answers 204.
        response.valid = true;
        return response;
    }
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonValue effective = object.value(QLatin1String("effective_value"));
    const QJsonValue hasOverride = object.value(QLatin1String("has_device_override"));
    if (!effective.isString() || !hasOverride.isBool()) {
        response.error = QStringLiteral("Invalid Silo subtitle appearance response");
        return response;
    }
    const QJsonObject document = parseDocument(effective.toString());
    response.valid = true;
    response.capabilityMetadata = {
        {QStringLiteral("appearance"), canonicalFields(document)},
        {QStringLiteral("hasValue"), !document.isEmpty()},
        {QStringLiteral("hasDeviceOverride"), hasOverride.toBool()},
        {QStringLiteral("document"),
         document.isEmpty() ? QString()
                            : QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact))},
    };
    return response;
}
