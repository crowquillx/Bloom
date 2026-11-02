#pragma once

#include "models/SubtitleAppearance.h"
#include "providers/ICatalogProvider.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

/**
 * @brief Wire boundary for Silo's subtitle appearance setting.
 *
 * `GET /api/v2/settings/subtitle-appearance/effective` and `PUT
 * /api/v2/settings/device/subtitle-appearance` answer
 * `EffectiveSubtitleAppearance`, whose values are JSON documents in strings.
 * The document is the one Silo's web player writes (`fontSize`,
 * `fontFamily`, `fontColor`, `textOpacity`, `backgroundColor`,
 * `backgroundStyle`, `backgroundOpacity`, `textOutline`,
 * `textOutlineColor`, `position`). Only the fields both sides share map;
 * see docs/provider-compatibility.md for the table. A write keeps every
 * field of the last document Bloom read that it does not map.
 */
class SiloSubtitleAppearanceMapper
{
public:
    /// The canonical fields a Silo document sets, as a partial
    /// Bloom::SubtitleAppearance map. Missing and invalid fields are left
    /// out.
    static QVariantMap canonicalFields(const QJsonObject &document);
    /// `appearance` written over `base` (the last document read), keeping
    /// the fields Bloom does not map.
    static QJsonObject document(const Bloom::SubtitleAppearance &appearance,
                                const QJsonObject &base);
    /// The `SubtitleAppearanceDeviceOverride` body for a write.
    static QByteArray requestBody(const Bloom::SubtitleAppearance &appearance,
                                  const QString &baseDocument);
    /// An EffectiveSubtitleAppearance answer (GET or PUT), or the empty
    /// answer of a DELETE.
    static ProviderCatalogResponse parseResponse(ProviderCatalogOperation operation,
                                                 const QByteArray &body);
    /// A JSON object held in a string; empty when it holds none.
    static QJsonObject parseDocument(const QString &value);
};
