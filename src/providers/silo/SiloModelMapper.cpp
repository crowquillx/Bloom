#include "SiloModelMapper.h"

#include <algorithm>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace {
QString identityString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (!value.isDouble()) {
        return {};
    }
    const double number = value.toDouble();
    constexpr double maximumExactJsonInteger = 9007199254740991.0;
    if (!std::isfinite(number) || std::trunc(number) != number
        || std::abs(number) > maximumExactJsonInteger) {
        return {};
    }
    return QString::number(static_cast<qint64>(number));
}

QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray values = value.toArray();
    result.reserve(values.size());
    for (const QJsonValue &entry : values) {
        if (entry.isString()) {
            result.append(entry.toString());
        }
    }
    return result;
}

qint64 jsonInteger(const QJsonValue &value, qint64 fallback = 0)
{
    if (!value.isDouble()) {
        return fallback;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<qint64>::min())
        || number >= static_cast<double>(std::numeric_limits<qint64>::max())) {
        return fallback;
    }
    return static_cast<qint64>(number);
}

qint64 isoDateMilliseconds(const QJsonValue &value)
{
    if (!value.isString() || value.toString().isEmpty()) {
        return -1;
    }
    QDateTime date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(value.toString(), Qt::ISODate);
    }
    return date.isValid() ? date.toMSecsSinceEpoch() : -1;
}

bool secondsToMillisecondsRepresentable(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return false;
    }
    const long double milliseconds = static_cast<long double>(seconds) * 1000.0L;
    return milliseconds <= static_cast<long double>(std::numeric_limits<qint64>::max());
}

QString canonicalMediaType(const QString &wireType)
{
    const QString type = wireType.trimmed().toLower();
    if (type == QStringLiteral("movie")) return QStringLiteral("Movie");
    if (type == QStringLiteral("series") || type == QStringLiteral("show")) {
        return QStringLiteral("Series");
    }
    if (type == QStringLiteral("season")) return QStringLiteral("Season");
    if (type == QStringLiteral("episode")) return QStringLiteral("Episode");
    if (type == QStringLiteral("person")) return QStringLiteral("Person");
    if (type == QStringLiteral("audiobook")) return QStringLiteral("AudioBook");
    if (type == QStringLiteral("ebook")) return QStringLiteral("Book");
    if (type == QStringLiteral("music") || type == QStringLiteral("audio")) {
        return QStringLiteral("Audio");
    }
    return wireType;
}

QVariantMap providerIds(const QJsonObject &wire)
{
    QVariantMap result;
    const QJsonObject nested = wire.value(QStringLiteral("provider_ids")).toObject();
    const auto add = [&result, &wire, &nested](const QString &canonical,
                                               const QString &snakeCase,
                                               const QString &shortName) {
        QString value = identityString(wire.value(snakeCase));
        if (value.isEmpty()) {
            value = identityString(wire.value(shortName));
        }
        if (value.isEmpty()) {
            value = identityString(nested.value(snakeCase));
        }
        if (value.isEmpty()) {
            value = identityString(nested.value(canonical));
        }
        if (value.isEmpty()) {
            value = identityString(nested.value(shortName));
        }
        if (!value.isEmpty()) {
            result.insert(canonical, value);
        }
    };
    add(QStringLiteral("Imdb"), QStringLiteral("imdb_id"), QStringLiteral("imdb"));
    add(QStringLiteral("Tmdb"), QStringLiteral("tmdb_id"), QStringLiteral("tmdb"));
    add(QStringLiteral("Tvdb"), QStringLiteral("tvdb_id"), QStringLiteral("tvdb"));
    add(QStringLiteral("Plex"), QStringLiteral("plex_guid"), QStringLiteral("plex"));
    return result;
}

void appendArtwork(QVariantList &all,
                   QVariantMap &owner,
                   const QString &property,
                   const QString &urlProperty,
                   const QString &connectionId,
                   const QString &itemId,
                   Bloom::ArtworkKind kind,
                   const QString &url,
                   int index = 0,
                   Bloom::ArtworkOwnerKind ownerKind = Bloom::ArtworkOwnerKind::MediaItem)
{
    if (url.isEmpty() || connectionId.isEmpty() || itemId.isEmpty()) {
        return;
    }
    Bloom::ArtworkRef artwork;
    artwork.connectionId = connectionId;
    artwork.itemId = itemId;
    artwork.kind = kind;
    artwork.ownerKind = ownerKind;
    artwork.index = qMax(0, index);
    artwork.sourceUrl = url;
    const QVariantMap ref = artwork.toVariantMap();
    if (!owner.contains(property)) {
        owner.insert(property, ref);
        owner.insert(urlProperty, url);
    }
    all.append(ref);
}

QVariantList people(const QJsonObject &wireItem, const QString &connectionId)
{
    QVariantList result;
    const auto append = [&result, &connectionId](const QJsonArray &credits,
                                                  const QString &kind,
                                                  const QString &roleKey) {
        for (const QJsonValue &value : credits) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject wire = value.toObject();
            const QString name = wire.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            const QString personId = identityString(wire.value(QStringLiteral("person_id")));
            Bloom::Person person;
            person.media = {connectionId, personId};
            person.name = name;
            person.role = wire.value(roleKey).toString();
            person.kind = kind;
            const QString photoUrl = wire.value(QStringLiteral("photo_url")).toString();
            if (!personId.isEmpty() && !photoUrl.isEmpty()) {
                person.artwork.connectionId = connectionId;
                person.artwork.itemId = personId;
                person.artwork.kind = Bloom::ArtworkKind::Person;
                person.artwork.ownerKind = Bloom::ArtworkOwnerKind::Person;
                person.artwork.sourceUrl = photoUrl;
            }
            QVariantMap mapped = person.toVariantMap();
            mapped[QStringLiteral("providerIds")] = providerIds(wire);
            if (!photoUrl.isEmpty()) {
                mapped[QStringLiteral("artworkUrl")] = photoUrl;
            }
            const QString thumbhash = wire.value(QStringLiteral("photo_thumbhash")).toString();
            if (!thumbhash.isEmpty()) {
                mapped[QStringLiteral("artworkThumbhash")] = thumbhash;
            }
            result.append(mapped);
        }
    };
    append(wireItem.value(QStringLiteral("cast")).toArray(),
           QStringLiteral("Actor"), QStringLiteral("character"));
    append(wireItem.value(QStringLiteral("crew")).toArray(),
           QStringLiteral("Crew"), QStringLiteral("job"));
    return result;
}

double frameRate(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char('/'));
    bool numeratorOk = false;
    const double numerator = parts.value(0).toDouble(&numeratorOk);
    if (!numeratorOk) {
        return 0.0;
    }
    if (parts.size() == 1) {
        return numerator;
    }
    bool denominatorOk = false;
    const double denominator = parts.value(1).toDouble(&denominatorOk);
    return denominatorOk && denominator != 0.0 ? numerator / denominator : 0.0;
}

int trackIndex(const QJsonObject &wireTrack, int fallback)
{
    if (!wireTrack.contains(QStringLiteral("index"))) {
        return fallback;
    }
    return wireTrack.value(QStringLiteral("index")).toInt(fallback);
}

QVariantList tracks(const QJsonArray &wireTracks,
                    const QString &kind,
                    int initialIndex,
                    QVariantList *mediaStreams)
{
    QVariantList result;
    result.reserve(wireTracks.size());
    for (qsizetype offset = 0; offset < wireTracks.size(); ++offset) {
        if (!wireTracks.at(offset).isObject()) {
            continue;
        }
        const QJsonObject wire = wireTracks.at(offset).toObject();
        const int index = trackIndex(
            wire, initialIndex + static_cast<int>(offset));
        MediaStreamInfo stream = SiloModelMapper::mediaStream(wire, kind, index);
        QVariantMap mapped = stream.toVariantMap();
        if (kind.compare(QStringLiteral("Video"), Qt::CaseInsensitive) == 0) {
            mapped[QStringLiteral("dolbyVision")] = wire.value(QStringLiteral("dolby_vision")).toString();
            mapped[QStringLiteral("hdr10Plus")] = wire.value(QStringLiteral("hdr10_plus")).toBool();
            mapped[QStringLiteral("bitDepth")] = wire.value(QStringLiteral("bit_depth")).toInt();
            mapped[QStringLiteral("pixelFormat")] = wire.value(QStringLiteral("pixel_format")).toString();
            mapped[QStringLiteral("aspectRatio")] = wire.value(QStringLiteral("aspect_ratio")).toString();
        } else if (kind.compare(QStringLiteral("Audio"), Qt::CaseInsensitive) == 0) {
            mapped[QStringLiteral("sampleRate")] = wire.value(QStringLiteral("sample_rate")).toInt();
            mapped[QStringLiteral("bitDepth")] = wire.value(QStringLiteral("bit_depth")).toInt();
            mapped[QStringLiteral("resolution")] = wire.value(QStringLiteral("resolution")).toString();
        } else if (kind.compare(QStringLiteral("Subtitle"), Qt::CaseInsensitive) == 0) {
            mapped[QStringLiteral("fileName")] = wire.value(QStringLiteral("file_name")).toString();
            const QString url = wire.value(QStringLiteral("url")).toString();
            const QString downloadedUrl = wire.value(QStringLiteral("downloaded_url")).toString();
            const QString externalUrl = wire.value(QStringLiteral("external_url")).toString();
            mapped[QStringLiteral("externalUrl")] = !url.isEmpty()
                ? url
                : (!downloadedUrl.isEmpty() ? downloadedUrl : externalUrl);
            mapped[QStringLiteral("downloaded")] = wire.value(QStringLiteral("downloaded")).toBool();
            mapped[QStringLiteral("source")] = wire.value(QStringLiteral("source")).toString();
            mapped[QStringLiteral("path")] = wire.value(QStringLiteral("path")).toString();
        }
        result.append(mapped);
        if (mediaStreams) {
            mediaStreams->append(stream.toVariantMap());
        }
    }
    return result;
}

QVariantMap markerVariant(const QString &name, const QJsonObject &wire)
{
    const QJsonValue start = wire.contains(QStringLiteral("start_seconds"))
        ? wire.value(QStringLiteral("start_seconds"))
        : wire.value(QStringLiteral("start"));
    const QJsonValue end = wire.contains(QStringLiteral("end_seconds"))
        ? wire.value(QStringLiteral("end_seconds"))
        : wire.value(QStringLiteral("end"));
    if (!start.isDouble() || !end.isDouble()
        || !std::isfinite(start.toDouble()) || !std::isfinite(end.toDouble())
        || start.toDouble() < 0.0 || end.toDouble() <= start.toDouble()) {
        return {};
    }
    return {
        {QStringLiteral("type"), name},
        {QStringLiteral("startMs"), SiloModelMapper::secondsToMilliseconds(start.toDouble())},
        {QStringLiteral("endMs"), SiloModelMapper::secondsToMilliseconds(end.toDouble())},
        {QStringLiteral("source"), wire.value(QStringLiteral("source")).toString()},
        {QStringLiteral("provider"), wire.value(QStringLiteral("provider")).toString()},
        {QStringLiteral("confidence"), wire.value(QStringLiteral("confidence")).toDouble()},
        {QStringLiteral("algorithm"), wire.value(QStringLiteral("algorithm")).toString()},
        {QStringLiteral("detectedAt"), wire.value(QStringLiteral("detected_at")).toString()}
    };
}

QVariantList markerVariants(const QJsonObject &wire)
{
    QVariantList result;
    for (const QString &name : {QStringLiteral("intro"), QStringLiteral("credits"),
                                QStringLiteral("recap"), QStringLiteral("preview")}) {
        if (!wire.value(name).isObject()) {
            continue;
        }
        const QVariantMap marker = markerVariant(name, wire.value(name).toObject());
        if (!marker.isEmpty()) {
            result.append(marker);
        }
    }
    return result;
}

QVariantList subtitles(const QJsonArray &wireSubtitles)
{
    return tracks(wireSubtitles, QStringLiteral("Subtitle"), 0, nullptr);
}

struct OrderedPlaybackPart
{
    int index = 0;
    QJsonObject wire;
};

int playbackPartIndex(const QJsonObject &part, int fallback)
{
    const QStringList keys{
        QStringLiteral("part_index"),
        QStringLiteral("presentation_part_index")
    };
    for (const QString &key : keys) {
        if (part.contains(key)) {
            const int index = part.value(key).toInt(fallback);
            return index >= 0 ? index : fallback;
        }
    }
    return fallback;
}

QList<OrderedPlaybackPart> orderedPlaybackParts(const QJsonObject &variant)
{
    QList<OrderedPlaybackPart> parts;
    const QJsonArray wireParts = variant.value(QStringLiteral("parts")).toArray();
    parts.reserve(wireParts.size());
    for (qsizetype i = 0; i < wireParts.size(); ++i) {
        if (!wireParts.at(i).isObject()) {
            continue;
        }
        parts.append(OrderedPlaybackPart{
            playbackPartIndex(wireParts.at(i).toObject(),
                              static_cast<int>(parts.size()) + 1),
            wireParts.at(i).toObject()
        });
    }
    std::stable_sort(parts.begin(), parts.end(),
                     [](const OrderedPlaybackPart &left, const OrderedPlaybackPart &right) {
                         return left.index < right.index;
                     });
    return parts;
}

int playbackPartTotal(const QJsonObject &variant, int partCount)
{
    const int explicitTotal = variant.value(QStringLiteral("part_count")).toInt(0);
    return explicitTotal > 0 ? explicitTotal : partCount;
}

QVariantList mappedPlaybackPartVersions(const QJsonArray &wireVersions,
                                        int partIndex,
                                        int partTotal,
                                        const QString &connectionId,
                                        const QString &itemId)
{
    QVariantList result;
    result.reserve(wireVersions.size());
    for (const QJsonValue &value : wireVersions) {
        if (!value.isObject()) {
            continue;
        }
        const QVariantMap mapped = SiloModelMapper::mediaVersion(
            value.toObject(), connectionId, itemId);
        if (mapped.isEmpty()) {
            continue;
        }
        QVariantMap annotated = mapped;
        if (!annotated.contains(QStringLiteral("presentationPartIndex"))
            && partIndex >= 0) {
            annotated[QStringLiteral("presentationPartIndex")] = partIndex;
        }
        if (!annotated.contains(QStringLiteral("presentationPartTotal"))
            && partTotal > 0) {
            annotated[QStringLiteral("presentationPartTotal")] = partTotal;
        }
        result.append(annotated);
    }
    return result;
}

QJsonArray flattenedPlaybackVersions(const QJsonArray &wireVariants)
{
    QJsonArray result;
    for (const QJsonValue &value : wireVariants) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject variant = value.toObject();
        const QList<OrderedPlaybackPart> parts = orderedPlaybackParts(variant);
        const int partTotal = playbackPartTotal(variant, parts.size());
        for (const OrderedPlaybackPart &part : parts) {
            for (const QJsonValue &versionValue :
                 part.wire.value(QStringLiteral("versions")).toArray()) {
                if (!versionValue.isObject()) {
                    continue;
                }
                QJsonObject version = versionValue.toObject();
                version.insert(QStringLiteral("playback_variant_id"),
                               variant.value(QStringLiteral("variant_id")).toString());
                if (!version.contains(QStringLiteral("presentation_part_index"))) {
                    version.insert(QStringLiteral("presentation_part_index"), part.index);
                }
                if (partTotal > 0
                    && !version.contains(QStringLiteral("presentation_part_total"))) {
                    version.insert(QStringLiteral("presentation_part_total"), partTotal);
                }
                result.append(version);
            }
        }
    }
    return result;
}

QVariantList playbackVariants(const QJsonArray &wireVariants,
                              const QString &connectionId,
                              const QString &itemId)
{
    QVariantList result;
    result.reserve(wireVariants.size());
    for (const QJsonValue &value : wireVariants) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wire = value.toObject();
        const QList<OrderedPlaybackPart> orderedParts = orderedPlaybackParts(wire);
        const int partTotal = playbackPartTotal(wire, orderedParts.size());
        QVariantList parts;
        parts.reserve(orderedParts.size());
        for (const OrderedPlaybackPart &orderedPart : orderedParts) {
            const QJsonObject part = orderedPart.wire;
            parts.append(QVariantMap{
                {QStringLiteral("partIndex"), orderedPart.index},
                {QStringLiteral("defaultFileId"), identityString(part.value(QStringLiteral("default_file_id")))},
                {QStringLiteral("totalDurationMs"), SiloModelMapper::secondsToMilliseconds(
                     part.value(QStringLiteral("total_duration")).toDouble())},
                {QStringLiteral("versions"), mappedPlaybackPartVersions(
                     part.value(QStringLiteral("versions")).toArray(),
                     orderedPart.index, partTotal, connectionId, itemId)}
            });
        }
        result.append(QVariantMap{
            {QStringLiteral("variantId"), wire.value(QStringLiteral("variant_id")).toString()},
            {QStringLiteral("edition"), wire.value(QStringLiteral("edition_raw")).toString()},
            {QStringLiteral("editionKey"), wire.value(QStringLiteral("edition_key")).toString()},
            {QStringLiteral("presentationKind"), wire.value(QStringLiteral("presentation_kind")).toString()},
            {QStringLiteral("presentationGroupKey"), wire.value(QStringLiteral("presentation_group_key")).toString()},
            {QStringLiteral("partCount"), partTotal},
            {QStringLiteral("totalDurationMs"), SiloModelMapper::secondsToMilliseconds(
                 wire.value(QStringLiteral("total_duration")).toDouble())},
            {QStringLiteral("defaultFileId"), identityString(wire.value(QStringLiteral("default_file_id")))},
            {QStringLiteral("parts"), parts}
        });
    }
    return result;
}
} // namespace

qint64 SiloModelMapper::secondsToMilliseconds(double seconds)
{
    if (!secondsToMillisecondsRepresentable(seconds) || seconds <= 0.0) {
        return 0;
    }
    return static_cast<qint64>(std::llround(seconds * 1000.0));
}


ParsedItemsResult SiloModelMapper::itemsResponse(const QByteArray &wireResponse,
                                                 const QString &parentId)
{
    ParsedItemsResult result;
    result.parentId = parentId;
    result.queryKey = parentId;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(wireResponse, &error);
    if (error.error != QJsonParseError::NoError) {
        return result;
    }
    if (document.isArray()) {
        result.items = document.array();
        result.totalRecordCount = static_cast<int>(result.items.size());
        result.success = true;
        return result;
    }
    if (!document.isObject()) {
        return result;
    }
    const QJsonObject object = document.object();

    result.items = object.value(QStringLiteral("items")).toArray();
    result.totalRecordCount = object.value(QStringLiteral("total")).isDouble()
        ? qMax(0, object.value(QStringLiteral("total")).toInt())
        : static_cast<int>(result.items.size());
    result.success = true;
    return result;
}

ProviderCatalogResponse SiloModelMapper::catalogResponse(
    ProviderCatalogOperation operation,
    const QByteArray &wireResponse,
    const QHash<QByteArray, QByteArray> &responseHeaders)
{
    ProviderCatalogResponse result;
    const QByteArray trimmed = wireResponse.trimmed();
    if (trimmed.isEmpty()) {
        if (operation == ProviderCatalogOperation::SetWatched
            || operation == ProviderCatalogOperation::SetFavorite) {
            result.valid = true;
        } else {
            result.error = QStringLiteral("Silo returned an empty catalog response");
        }
        return result;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
    if (error.error != QJsonParseError::NoError) {
        result.error = error.errorString();
        return result;
    }

    const auto responseHeader = [&responseHeaders](const QByteArray &name) {
        for (auto it = responseHeaders.constBegin(); it != responseHeaders.constEnd(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
                return it.value();
            }
        }
        return QByteArray();
    };
    const QByteArray etag = responseHeader(QByteArrayLiteral("ETag"));
    const QByteArray lastModified = responseHeader(QByteArrayLiteral("Last-Modified"));
    if (!etag.isEmpty()) {
        result.snapshot.insert(QStringLiteral("etag"), QString::fromUtf8(etag));
    }
    if (!lastModified.isEmpty()) {
        result.snapshot.insert(QStringLiteral("lastModified"),
                               QString::fromUtf8(lastModified));
    }
    if (document.isArray()) {
        switch (operation) {
        case ProviderCatalogOperation::Views:
        case ProviderCatalogOperation::Items:
        case ProviderCatalogOperation::NextUp:
        case ProviderCatalogOperation::LatestMedia:
        case ProviderCatalogOperation::SimilarItems:
        case ProviderCatalogOperation::Search:
            result.rawItems = document.array();
            result.total = static_cast<int>(result.rawItems.size());
            result.valid = true;
            return result;
        case ProviderCatalogOperation::Chapters:
        case ProviderCatalogOperation::Versions:
            result.rawItems = document.array();
            result.rawItem.insert(QStringLiteral("versions"), result.rawItems);
            result.valid = true;
            return result;
        default:
            result.error = QStringLiteral("Unexpected Silo array response");
            return result;
        }
    }
    if (!document.isObject()) {
        result.error = QStringLiteral("Unexpected Silo catalog response type");
        return result;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("error")).isString()) {
        result.error = object.value(QStringLiteral("message")).toString(
            object.value(QStringLiteral("error")).toString());
        return result;
    }

    if (object.value(QStringLiteral("total")).isDouble()) {
        const qint64 total = jsonInteger(object.value(QStringLiteral("total")), -1);
        if (total >= 0 && total <= std::numeric_limits<int>::max()) {
            result.total = static_cast<int>(total);
            result.capabilityMetadata.insert(QStringLiteral("totalPresent"), true);
        }
    }
    if (object.value(QStringLiteral("total_exact")).isBool()) {
        result.capabilityMetadata.insert(
            QStringLiteral("totalExact"),
            object.value(QStringLiteral("total_exact")).toBool());
    }
    if (object.value(QStringLiteral("has_more")).isBool()) {
        result.hasMore = object.value(QStringLiteral("has_more")).toBool();
        result.capabilityMetadata.insert(QStringLiteral("hasMorePresent"), true);
    } else if (object.value(QStringLiteral("hasMore")).isBool()) {
        result.hasMore = object.value(QStringLiteral("hasMore")).toBool();
        result.capabilityMetadata.insert(QStringLiteral("hasMorePresent"), true);
    }
    if (object.value(QStringLiteral("snapshot")).isString()) {
        result.snapshot.insert(QStringLiteral("snapshot"),
                               object.value(QStringLiteral("snapshot")).toString());
    } else if (object.value(QStringLiteral("snapshot")).isObject()) {
        result.snapshot.insert(QStringLiteral("snapshot"),
                               object.value(QStringLiteral("snapshot")).toObject().toVariantMap());
    }
    for (const QString &key : {QStringLiteral("cursor"), QStringLiteral("next_cursor"),
                               QStringLiteral("nextCursor")}) {
        if (object.value(key).isString()) {
            result.snapshot.insert(key, object.value(key).toString());
        }
    }
    for (const QString &key : {QStringLiteral("offset"), QStringLiteral("limit")}) {
        if (object.value(key).isDouble()) {
            result.capabilityMetadata.insert(key, jsonInteger(object.value(key)));
        }
    }
    if (object.value(QStringLiteral("search_diagnostics")).isObject()) {
        result.capabilityMetadata.insert(
            QStringLiteral("searchDiagnostics"),
            object.value(QStringLiteral("search_diagnostics")).toObject().toVariantMap());
    }

    if ((operation == ProviderCatalogOperation::NextUp
         || operation == ProviderCatalogOperation::Items)
        && object.value(QStringLiteral("section")).isObject()) {
        result.rawItem = object.value(QStringLiteral("section")).toObject();
        if (!result.rawItem.value(QStringLiteral("items")).isArray()) {
            result.error = QStringLiteral("Silo section response has no items array");
            return result;
        }
        result.rawItems = result.rawItem.value(QStringLiteral("items")).toArray();
        result.capabilityMetadata.insert(
            QStringLiteral("section"),
            result.rawItem.toVariantMap());
        if (result.rawItem.value(QStringLiteral("total_count")).isDouble()) {
            const qint64 sectionTotal = jsonInteger(
                result.rawItem.value(QStringLiteral("total_count")), -1);
            if (sectionTotal >= 0
                && sectionTotal <= std::numeric_limits<int>::max()) {
                result.total = static_cast<int>(sectionTotal);
                result.capabilityMetadata.insert(
                    QStringLiteral("totalPresent"), true);
            }
        }
        if (!result.capabilityMetadata.value(QStringLiteral("totalPresent")).toBool()) {
            result.total = static_cast<int>(result.rawItems.size());
        }
        result.valid = true;
        return result;
    }

    if (operation == ProviderCatalogOperation::FilterOptions) {
        result.rawItem = object;
        result.filterMetadata = filterOptions(object);
        if (object.value(QStringLiteral("matches")).isArray()
            || object.value(QStringLiteral("items")).isArray()) {
            result.filterMetadata.insert(QStringLiteral("namedItems"),
                                         namedItems(object));
        }
        result.valid = true;
        return result;
    }
    if (operation == ProviderCatalogOperation::Chapters
        || operation == ProviderCatalogOperation::Versions) {
        result.rawItem = object;
        if (object.value(QStringLiteral("versions")).isArray()) {
            result.rawItems = object.value(QStringLiteral("versions")).toArray();
        }
        result.valid = true;
        return result;
    }
    if (operation == ProviderCatalogOperation::Item
        || operation == ProviderCatalogOperation::SetWatched
        || operation == ProviderCatalogOperation::SetFavorite) {
        result.rawItem = object;
        result.valid = true;
        return result;
    }

    for (const QString &key : {QStringLiteral("items"), QStringLiteral("sections"),
                               QStringLiteral("seasons"), QStringLiteral("episodes"),
                               QStringLiteral("versions")}) {
        if (object.value(key).isArray()) {
            result.rawItems = object.value(key).toArray();
            if (key == QStringLiteral("seasons")
                || key == QStringLiteral("episodes")
                || (operation == ProviderCatalogOperation::SimilarItems
                    && key == QStringLiteral("items"))) {
                QJsonArray normalizedItems;
                for (const QJsonValue &value : result.rawItems) {
                    if (!value.isObject()) {
                        continue;
                    }
                    QJsonObject item = value.toObject();
                    if (key == QStringLiteral("seasons")
                        && !item.value(QStringLiteral("type")).isString()) {
                        item.insert(QStringLiteral("type"), QStringLiteral("season"));
                    } else if (key == QStringLiteral("episodes")
                               && !item.value(QStringLiteral("type")).isString()) {
                        item.insert(QStringLiteral("type"), QStringLiteral("episode"));
                    }
                    if (operation == ProviderCatalogOperation::SimilarItems
                        && !item.value(QStringLiteral("content_id")).isString()) {
                        item.insert(QStringLiteral("content_id"),
                                    item.value(QStringLiteral("media_item_id")));
                    }
                    normalizedItems.append(item);
                }
                result.rawItems = normalizedItems;
            }
            if (!result.capabilityMetadata.value(QStringLiteral("totalPresent")).toBool()) {
                result.total = static_cast<int>(result.rawItems.size());
            }
            result.capabilityMetadata.insert(QStringLiteral("envelope"), key);
            result.valid = true;
            return result;
        }
    }

    result.error = QStringLiteral("Silo catalog response did not contain the expected resource");
    return result;
}

QVariantMap SiloModelMapper::library(const QJsonObject &wireLibrary,
                                     const QString &connectionId)
{
    const QString libraryId = identityString(wireLibrary.value(QStringLiteral("id")));
    const QString name = wireLibrary.value(QStringLiteral("name")).toString();
    if (libraryId.isEmpty() || name.isEmpty()) {
        return {};
    }

    QVariantMap result{
        {QStringLiteral("media"), Bloom::MediaRef{connectionId, libraryId}.toVariantMap()},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), libraryId},
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("name"), name},
        {QStringLiteral("sortName"), name},
        {QStringLiteral("mediaType"), QStringLiteral("CollectionFolder")},
        {QStringLiteral("collectionType"), wireLibrary.value(QStringLiteral("type")).toString()},
        {QStringLiteral("sortOrder"), wireLibrary.value(QStringLiteral("sort_order")).toInt()}
    };
    QVariantList artwork;
    appendArtwork(artwork, result, QStringLiteral("primaryArtwork"),
                  QStringLiteral("primaryArtworkUrl"), connectionId, libraryId,
                  Bloom::ArtworkKind::Primary,
                  wireLibrary.value(QStringLiteral("poster_url")).toString(),
                  0, Bloom::ArtworkOwnerKind::Library);
    result[QStringLiteral("artwork")] = artwork;
    return result;
}

QVariantList SiloModelMapper::libraries(const QJsonArray &wireLibraries,
                                        const QString &connectionId)
{
    QVariantList result;
    result.reserve(wireLibraries.size());
    for (const QJsonValue &value : wireLibraries) {
        if (!value.isObject()) {
            continue;
        }
        const QVariantMap mapped = library(value.toObject(), connectionId);
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

MediaStreamInfo SiloModelMapper::mediaStream(const QJsonObject &wireTrack,
                                             const QString &kind,
                                             int index)
{
    MediaStreamInfo result;
    result.index = index;
    result.type = kind;
    result.codec = wireTrack.value(QStringLiteral("codec")).toString();
    result.language = wireTrack.value(QStringLiteral("language")).toString();
    result.title = wireTrack.value(QStringLiteral("title")).toString();
    if (result.title.isEmpty()) {
        result.title = wireTrack.value(QStringLiteral("embedded_title")).toString();
    }
    result.displayTitle = result.title;
    result.isDefault = wireTrack.value(QStringLiteral("default")).toBool();
    result.isForced = wireTrack.value(QStringLiteral("forced")).toBool();
    result.isExternal = wireTrack.value(QStringLiteral("external")).toBool()
        || wireTrack.value(QStringLiteral("downloaded")).toBool()
        || !wireTrack.value(QStringLiteral("url")).toString().isEmpty()
        || !wireTrack.value(QStringLiteral("downloaded_url")).toString().isEmpty()
        || !wireTrack.value(QStringLiteral("external_url")).toString().isEmpty()
        || !wireTrack.value(QStringLiteral("path")).toString().isEmpty();
    result.isHearingImpaired = wireTrack.value(QStringLiteral("hearing_impaired")).toBool();
    result.channels = wireTrack.value(QStringLiteral("channels")).toInt();
    result.channelLayout = wireTrack.value(QStringLiteral("layout")).toString();
    result.bitRate = wireTrack.value(QStringLiteral("bitrate")).toInt();
    result.width = wireTrack.value(QStringLiteral("width")).toInt();
    result.height = wireTrack.value(QStringLiteral("height")).toInt();
    result.averageFrameRate = frameRate(wireTrack.value(QStringLiteral("frame_rate")).toString());
    result.realFrameRate = result.averageFrameRate;
    result.profile = wireTrack.value(QStringLiteral("profile")).toString();
    result.videoRange = wireTrack.value(QStringLiteral("video_range")).toString();
    result.videoRangeType = wireTrack.value(QStringLiteral("video_range_type")).toString();
    result.dolbyVisionProfile = wireTrack.value(QStringLiteral("dv_profile")).toInt();
    result.dolbyVisionBlSignalCompatibilityId =
        wireTrack.value(QStringLiteral("dv_bl_compat_id")).toInt();
    return result;
}

PlaybackInfoResponse SiloModelMapper::playbackInfoFromVersions(const QJsonArray &wireVersions)
{
    PlaybackInfoResponse response;
    response.mediaSources.reserve(wireVersions.size());
    for (const QJsonValue &value : wireVersions) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wire = value.toObject();
        const QString id = identityString(wire.value(QStringLiteral("file_id")));
        if (id.isEmpty()) {
            continue;
        }
        MediaSourceInfo source;
        source.id = id;
        source.name = wire.value(QStringLiteral("file_name")).toString();
        source.path = wire.value(QStringLiteral("file_path")).toString();
        source.container = wire.value(QStringLiteral("container")).toString();
        source.size = jsonInteger(wire.value(QStringLiteral("file_size")));
        source.bitRate = wire.value(QStringLiteral("bitrate")).toInt();
        source.videoType = wire.value(QStringLiteral("codec_video")).toString();
        source.durationMs = secondsToMilliseconds(wire.value(QStringLiteral("duration")).toDouble());
        source.playbackVariantId =
            wire.value(QStringLiteral("playback_variant_id")).toString();
        source.presentationPartIndex =
            wire.value(QStringLiteral("presentation_part_index")).toInt();
        source.presentationPartTotal =
            wire.value(QStringLiteral("presentation_part_total")).toInt();

        const QJsonArray video = wire.value(QStringLiteral("video_tracks")).toArray();
        const QJsonArray audio = wire.value(QStringLiteral("audio_tracks")).toArray();
        QJsonArray subtitles = wire.value(QStringLiteral("subtitle_tracks")).toArray();
        if (subtitles.isEmpty()) {
            subtitles = wire.value(QStringLiteral("subtitles")).toArray();
        }
        for (qsizetype i = 0; i < video.size(); ++i) {
            if (video.at(i).isObject()) {
                source.mediaStreams.append(mediaStream(
                    video.at(i).toObject(), QStringLiteral("Video"),
                    trackIndex(video.at(i).toObject(), static_cast<int>(i))));
            }
        }
        for (qsizetype i = 0; i < audio.size(); ++i) {
            if (audio.at(i).isObject()) {
                const MediaStreamInfo stream = mediaStream(
                    audio.at(i).toObject(), QStringLiteral("Audio"),
                    trackIndex(audio.at(i).toObject(), static_cast<int>(i)));
                if (source.defaultAudioStreamIndex < 0 && stream.isDefault) {
                    source.defaultAudioStreamIndex = stream.index;
                }
                source.mediaStreams.append(stream);
            }
        }
        for (qsizetype i = 0; i < subtitles.size(); ++i) {
            if (subtitles.at(i).isObject()) {
                const QJsonObject subtitle = subtitles.at(i).toObject();
                const MediaStreamInfo stream = mediaStream(
                    subtitle, QStringLiteral("Subtitle"),
                    trackIndex(subtitle, static_cast<int>(i)));
                if (source.defaultSubtitleStreamIndex < 0 && stream.isDefault) {
                    source.defaultSubtitleStreamIndex = stream.index;
                }
                source.mediaStreams.append(stream);
            }
        }
        if (source.defaultAudioStreamIndex < 0 && !audio.isEmpty()) {
            source.defaultAudioStreamIndex = 0;
        }
        if (source.defaultSubtitleStreamIndex < 0 && !subtitles.isEmpty()) {
            source.defaultSubtitleStreamIndex = 0;
        }
        const QString streamUrl = wire.value(QStringLiteral("stream_url")).toString();
        source.directStreamUrl = streamUrl.isEmpty()
            ? wire.value(QStringLiteral("download_url")).toString()
            : streamUrl;
        source.transcodingUrl = wire.value(QStringLiteral("hls_url")).toString();
        response.mediaSources.append(source);
    }
    return response;
}

PlaybackInfoResponse SiloModelMapper::playbackInfo(const QJsonObject &wireItem)
{
    const QJsonObject nested = wireItem.value(QStringLiteral("playback_info")).isObject()
        ? wireItem.value(QStringLiteral("playback_info")).toObject()
        : QJsonObject{};
    const auto mediaSourcesFrom = [](const QJsonObject &object) {
        const QJsonArray mediaSources = object.value(QStringLiteral("media_sources")).toArray();
        return mediaSources.isEmpty() ? QJsonArray{} : mediaSources;
    };
    QJsonArray versions = mediaSourcesFrom(wireItem);
    if (!versions.isEmpty()) {
        return playbackInfoFromVersions(versions);
    }
    versions = mediaSourcesFrom(nested);
    if (!versions.isEmpty()) {
        return playbackInfoFromVersions(versions);
    }

    const auto variantsFrom = [](const QJsonObject &object) {
        return flattenedPlaybackVersions(
            object.value(QStringLiteral("playback_variants")).toArray());
    };
    versions = variantsFrom(wireItem);
    if (versions.isEmpty()) {
        versions = variantsFrom(nested);
    }
    if (!versions.isEmpty()) {
        return playbackInfoFromVersions(versions);
    }

    const auto versionsOrFilesFrom = [](const QJsonObject &object) {
        QJsonArray result = object.value(QStringLiteral("versions")).toArray();
        if (result.isEmpty()) {
            result = object.value(QStringLiteral("files")).toArray();
        }
        return result;
    };
    versions = versionsOrFilesFrom(wireItem);
    if (versions.isEmpty()) {
        versions = versionsOrFilesFrom(nested);
    }
    return playbackInfoFromVersions(versions);
}

QVariantMap SiloModelMapper::mediaVersion(const QJsonObject &wireVersion,
                                          const QString &connectionId,
                                          const QString &itemId)
{
    const QString fileId = identityString(wireVersion.value(QStringLiteral("file_id")));
    if (fileId.isEmpty()) {
        return {};
    }

    QVariantList mediaStreams;
    const QVariantList videoTracks = tracks(wireVersion.value(QStringLiteral("video_tracks")).toArray(),
                                            QStringLiteral("Video"), 0, &mediaStreams);
    const QVariantList audioTracks = tracks(wireVersion.value(QStringLiteral("audio_tracks")).toArray(),
                                            QStringLiteral("Audio"), 0, &mediaStreams);
    QJsonArray subtitleWireTracks =
        wireVersion.value(QStringLiteral("subtitle_tracks")).toArray();
    if (subtitleWireTracks.isEmpty()) {
        subtitleWireTracks = wireVersion.value(QStringLiteral("subtitles")).toArray();
    }
    const QVariantList subtitleTracks = tracks(
        subtitleWireTracks, QStringLiteral("Subtitle"), 0, &mediaStreams);

    QVariantMap result{
        {QStringLiteral("id"), fileId},
        {QStringLiteral("fileId"), fileId},
        {QStringLiteral("mediaVersionId"), fileId},
        {QStringLiteral("fileName"), wireVersion.value(QStringLiteral("file_name")).toString()},
        {QStringLiteral("resolution"), wireVersion.value(QStringLiteral("resolution")).toString()},
        {QStringLiteral("videoCodec"), wireVersion.value(QStringLiteral("codec_video")).toString()},
        {QStringLiteral("audioCodec"), wireVersion.value(QStringLiteral("codec_audio")).toString()},
        {QStringLiteral("hdr"), wireVersion.value(QStringLiteral("hdr")).toBool()},
        {QStringLiteral("durationMs"), secondsToMilliseconds(
             wireVersion.value(QStringLiteral("duration")).toDouble())},
        {QStringLiteral("hdrType"), wireVersion.value(QStringLiteral("hdr_type")).toString(
             wireVersion.value(QStringLiteral("video_range_type")).toString())},
        {QStringLiteral("dolbyVision"), wireVersion.value(QStringLiteral("dolby_vision")).toString()},
        {QStringLiteral("dolbyVisionProfile"), wireVersion.value(QStringLiteral("dv_profile")).toInt()},
        {QStringLiteral("hdr10Plus"), wireVersion.value(QStringLiteral("hdr10_plus")).toBool()},
        {QStringLiteral("width"), wireVersion.value(QStringLiteral("width")).toInt()},
        {QStringLiteral("height"), wireVersion.value(QStringLiteral("height")).toInt()},
        {QStringLiteral("multiEpisodeStart"), wireVersion.value(QStringLiteral("multi_episode_start")).toInt()},
        {QStringLiteral("multiEpisodeEnd"), wireVersion.value(QStringLiteral("multi_episode_end")).toInt()},
        {QStringLiteral("effectiveAudioTrackIndex"), wireVersion.value(QStringLiteral("effective_audio_track_index")).toInt(-1)},
        {QStringLiteral("effectiveAudioLanguage"), wireVersion.value(QStringLiteral("effective_audio_language")).toString()},
        {QStringLiteral("videoTracks"), videoTracks},
        {QStringLiteral("audioTracks"), audioTracks},
        {QStringLiteral("subtitleTracks"), subtitleTracks},
        {QStringLiteral("mediaStreams"), mediaStreams},
        {QStringLiteral("chapters"), chapters(
             wireVersion.value(QStringLiteral("chapters")).toArray(), connectionId, itemId, fileId)},
        {QStringLiteral("markers"), markerVariants(wireVersion)}
    };
    if (wireVersion.contains(QStringLiteral("presentation_part_index"))) {
        result[QStringLiteral("presentationPartIndex")] =
            wireVersion.value(QStringLiteral("presentation_part_index")).toInt();
    }
    if (wireVersion.contains(QStringLiteral("presentation_part_total"))) {
        result[QStringLiteral("presentationPartTotal")] =
            wireVersion.value(QStringLiteral("presentation_part_total")).toInt();
    }
    if (wireVersion.value(QStringLiteral("file_path")).isString()) {
        result[QStringLiteral("filePath")] = wireVersion.value(QStringLiteral("file_path")).toString();
    }
    return result;
}

QVariantList SiloModelMapper::mediaVersions(const QJsonArray &wireVersions,
                                            const QString &connectionId,
                                            const QString &itemId)
{
    QVariantList result;
    result.reserve(wireVersions.size());
    for (const QJsonValue &value : wireVersions) {
        if (!value.isObject()) {
            continue;
        }
        const QVariantMap mapped = mediaVersion(value.toObject(), connectionId, itemId);
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

QVariantMap SiloModelMapper::mediaItem(const QJsonObject &wireItem,
                                       const QString &connectionId)
{
    QString itemId = identityString(wireItem.value(QStringLiteral("content_id")));
    if (itemId.isEmpty()) {
        itemId = identityString(wireItem.value(QStringLiteral("media_item_id")));
    }
    if (itemId.isEmpty()
        && wireItem.contains(QStringLiteral("id"))
        && wireItem.value(QStringLiteral("section_type")).isString()) {
        const QString sectionId =
            identityString(wireItem.value(QStringLiteral("id")));
        const QString title = wireItem.value(QStringLiteral("title")).toString(
            wireItem.value(QStringLiteral("name")).toString());
        if (sectionId.isEmpty() || title.isEmpty()) {
            return {};
        }
        return {
            {QStringLiteral("media"),
             Bloom::MediaRef{connectionId, sectionId}.toVariantMap()},
            {QStringLiteral("connectionId"), connectionId},
            {QStringLiteral("itemId"), sectionId},
            {QStringLiteral("sectionId"), sectionId},
            {QStringLiteral("name"), title},
            {QStringLiteral("sortName"), title},
            {QStringLiteral("mediaType"), QStringLiteral("CollectionFolder")},
            {QStringLiteral("collectionType"),
             wireItem.value(QStringLiteral("section_type")).toString()},
            {QStringLiteral("featured"),
             wireItem.value(QStringLiteral("featured")).toBool()},
            {QStringLiteral("itemLimit"),
             wireItem.value(QStringLiteral("item_limit")).toInt()},
            {QStringLiteral("isCustom"),
             wireItem.value(QStringLiteral("is_custom")).toBool()},
            {QStringLiteral("customized"),
             wireItem.value(QStringLiteral("customized")).toBool()}
        };
    }
    if (itemId.isEmpty() && wireItem.contains(QStringLiteral("id"))
        && wireItem.contains(QStringLiteral("name"))) {
        return library(wireItem, connectionId);
    }
    if (itemId.isEmpty()) {
        return {};
    }

    const QJsonObject userData = wireItem.value(QStringLiteral("user_data")).isObject()
        ? wireItem.value(QStringLiteral("user_data")).toObject()
        : (wireItem.value(QStringLiteral("user_state")).isObject()
               ? wireItem.value(QStringLiteral("user_state")).toObject()
               : (wireItem.value(QStringLiteral("state")).isObject()
                      ? wireItem.value(QStringLiteral("state")).toObject()
                      : QJsonObject{}));
    const auto stateValue = [&userData, &wireItem](const QString &key) {
        return userData.contains(key) ? userData.value(key) : wireItem.value(key);
    };
    Bloom::UserMediaState state;
    state.watched = stateValue(QStringLiteral("played")).toBool();
    state.favorite = stateValue(QStringLiteral("is_favorite")).isBool()
        ? stateValue(QStringLiteral("is_favorite")).toBool()
        : stateValue(QStringLiteral("favorite")).toBool();
    state.positionMs = secondsToMilliseconds(
        stateValue(QStringLiteral("position_seconds")).toDouble());
    state.unplayedItemCount = stateValue(QStringLiteral("unplayed_count")).toInt();
    state.lastPlayedAt = stateValue(QStringLiteral("last_played_at")).toString(
        stateValue(QStringLiteral("progress_updated_at")).toString());

    QJsonObject nestedPlaybackInfo;
    if (wireItem.value(QStringLiteral("playback_info")).isObject()) {
        nestedPlaybackInfo = wireItem.value(QStringLiteral("playback_info")).toObject();
    }
    QJsonArray wirePlaybackVariants =
        wireItem.value(QStringLiteral("playback_variants")).toArray();
    if (wirePlaybackVariants.isEmpty()) {
        wirePlaybackVariants = nestedPlaybackInfo.value(
            QStringLiteral("playback_variants")).toArray();
    }
    const auto versionsFrom = [](const QJsonObject &object) {
        QJsonArray versions = object.value(QStringLiteral("versions")).toArray();
        if (versions.isEmpty()) {
            versions = object.value(QStringLiteral("files")).toArray();
        }
        if (versions.isEmpty()) {
            versions = flattenedPlaybackVersions(
                object.value(QStringLiteral("playback_variants")).toArray());
        }
        return versions;
    };
    QJsonArray wireVersions = versionsFrom(wireItem);
    if (wireVersions.isEmpty() && !nestedPlaybackInfo.isEmpty()) {
        wireVersions = versionsFrom(nestedPlaybackInfo);
    }
    const QVariantList versions = mediaVersions(wireVersions, connectionId, itemId);
    const QVariantList mappedPlaybackVariants = playbackVariants(
        wirePlaybackVariants, connectionId, itemId);
    const qint64 playbackVariantDurationMs = mappedPlaybackVariants.isEmpty()
        ? 0
        : mappedPlaybackVariants.first().toMap()
              .value(QStringLiteral("totalDurationMs")).toLongLong();
    qint64 durationMs = 0;
    if (wireItem.value(QStringLiteral("duration_seconds")).isDouble()) {
        durationMs = secondsToMilliseconds(
            wireItem.value(QStringLiteral("duration_seconds")).toDouble());
    } else if (userData.value(QStringLiteral("duration_seconds")).isDouble()) {
        durationMs = secondsToMilliseconds(
            userData.value(QStringLiteral("duration_seconds")).toDouble());
    } else if (playbackVariantDurationMs > 0) {
        durationMs = playbackVariantDurationMs;
    } else if (!versions.isEmpty()) {
        durationMs = versions.first().toMap().value(QStringLiteral("durationMs")).toLongLong();
    } else if (wireItem.value(QStringLiteral("runtime")).isDouble()) {
        durationMs = secondsToMilliseconds(
            wireItem.value(QStringLiteral("runtime")).toDouble() * 60.0);
    }
    QString defaultFileId = identityString(wireItem.value(QStringLiteral("default_file_id")));
    if (defaultFileId.isEmpty()) {
        defaultFileId = identityString(wireItem.value(QStringLiteral("file_id")));
    }
    if (defaultFileId.isEmpty() && !mappedPlaybackVariants.isEmpty()) {
        defaultFileId = mappedPlaybackVariants.first().toMap()
                            .value(QStringLiteral("defaultFileId")).toString();
    }
    if (defaultFileId.isEmpty() && !versions.isEmpty()) {
        defaultFileId = versions.first().toMap().value(QStringLiteral("fileId")).toString();
    }

    double communityRating = 0.0;
    if (wireItem.value(QStringLiteral("rating_imdb")).isDouble()) {
        communityRating = wireItem.value(QStringLiteral("rating_imdb")).toDouble();
    } else if (wireItem.value(QStringLiteral("rating_tmdb")).isDouble()) {
        communityRating = wireItem.value(QStringLiteral("rating_tmdb")).toDouble();
    }

    const QString seriesId = identityString(wireItem.value(QStringLiteral("series_id")));
    QVariantMap result{
        {QStringLiteral("media"), Bloom::MediaRef{connectionId, itemId}.toVariantMap()},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("defaultFileId"), defaultFileId},
        {QStringLiteral("name"), wireItem.value(QStringLiteral("title")).toString(
             wireItem.value(QStringLiteral("name")).toString())},
        {QStringLiteral("sortName"), wireItem.value(QStringLiteral("sort_title")).toString(
             wireItem.value(QStringLiteral("title")).toString(
                 wireItem.value(QStringLiteral("name")).toString()))},
        {QStringLiteral("originalTitle"), wireItem.value(QStringLiteral("original_title")).toString()},
        {QStringLiteral("mediaType"), canonicalMediaType(
             wireItem.value(QStringLiteral("type")).toString())},
        {QStringLiteral("parentId"), identityString(wireItem.value(QStringLiteral("parent_id")))},
        {QStringLiteral("seriesId"), seriesId},
        {QStringLiteral("seasonId"), identityString(wireItem.value(QStringLiteral("season_id")))},
        {QStringLiteral("seriesName"), wireItem.value(QStringLiteral("series_title")).toString()},
        {QStringLiteral("indexNumber"), wireItem.value(QStringLiteral("episode_number")).toInt(-1)},
        {QStringLiteral("parentIndexNumber"), wireItem.value(QStringLiteral("season_number")).toInt(-1)},
        {QStringLiteral("overview"), wireItem.value(QStringLiteral("overview")).toString()},
        {QStringLiteral("tagline"), wireItem.value(QStringLiteral("tagline")).toString()},
        {QStringLiteral("productionYear"), wireItem.value(QStringLiteral("year")).toInt()},
        {QStringLiteral("premiereDate"),
         wireItem.value(QStringLiteral("release_date")).toString(
             wireItem.value(QStringLiteral("air_date")).toString())},
        {QStringLiteral("endDate"), wireItem.value(QStringLiteral("last_air_date")).toString()},
        {QStringLiteral("officialRating"), wireItem.value(QStringLiteral("content_rating")).toString()},
        {QStringLiteral("communityRating"), communityRating},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("runtimeMinutes"), wireItem.value(QStringLiteral("runtime")).toInt()},
        {QStringLiteral("status"), wireItem.value(QStringLiteral("show_status")).toString(
             wireItem.value(QStringLiteral("status")).toString())},
        {QStringLiteral("genres"), stringList(wireItem.value(QStringLiteral("genres")))},
        {QStringLiteral("studios"), stringList(wireItem.value(QStringLiteral("studios")))},
        {QStringLiteral("networks"), stringList(wireItem.value(QStringLiteral("networks")))},
        {QStringLiteral("tags"), stringList(wireItem.value(QStringLiteral("keywords")))},
        {QStringLiteral("providerIds"), providerIds(wireItem)},
        {QStringLiteral("userState"), state.toVariantMap()},
        {QStringLiteral("watched"), state.watched},
        {QStringLiteral("favorite"), state.favorite},
        {QStringLiteral("positionMs"), state.positionMs},
        {QStringLiteral("unplayedItemCount"), state.unplayedItemCount},
        {QStringLiteral("isInProgress"),
         state.positionMs > 0
             || stateValue(QStringLiteral("is_in_progress")).toBool()},
        {QStringLiteral("people"), people(wireItem, connectionId)},
        {QStringLiteral("versions"), versions},
        {QStringLiteral("playbackVariants"), mappedPlaybackVariants},
        {QStringLiteral("subtitles"), subtitles(wireItem.value(QStringLiteral("subtitles")).toArray())},
        {QStringLiteral("markers"), markerVariants(wireItem)}
    };
    if (wireItem.value(QStringLiteral("reason")).isString()) {
        result[QStringLiteral("recommendationReason")] =
            wireItem.value(QStringLiteral("reason")).toString();
    }
    if (wireItem.value(QStringLiteral("reason_detail")).isString()) {
        result[QStringLiteral("recommendationReasonDetail")] =
            wireItem.value(QStringLiteral("reason_detail")).toString();
    }
    if (result.value(QStringLiteral("parentId")).toString().isEmpty()) {
        const QString seasonId = result.value(QStringLiteral("seasonId")).toString();
        result[QStringLiteral("parentId")] = seasonId.isEmpty() ? seriesId : seasonId;
    }

    if (wireItem.contains(QStringLiteral("season_count"))) {
        result[QStringLiteral("seasonCount")] = wireItem.value(QStringLiteral("season_count")).toInt();
        result[QStringLiteral("childCount")] = wireItem.value(QStringLiteral("season_count")).toInt();
    }
    if (wireItem.contains(QStringLiteral("episode_count"))) {
        result[QStringLiteral("episodeCount")] = wireItem.value(QStringLiteral("episode_count")).toInt();
        result[QStringLiteral("childCount")] = wireItem.value(QStringLiteral("episode_count")).toInt();
    }
    result[QStringLiteral("ratings")] = QVariantMap{
        {QStringLiteral("imdb"), wireItem.value(QStringLiteral("rating_imdb")).toDouble()},
        {QStringLiteral("tmdb"), wireItem.value(QStringLiteral("rating_tmdb")).toDouble()},
        {QStringLiteral("rottenTomatoesCritic"), wireItem.value(QStringLiteral("rating_rt_critic")).toInt()},
        {QStringLiteral("rottenTomatoesAudience"), wireItem.value(QStringLiteral("rating_rt_audience")).toInt()}
    };
    if (wireItem.value(QStringLiteral("restrictions")).isObject()) {
        result[QStringLiteral("restrictions")] =
            wireItem.value(QStringLiteral("restrictions")).toObject().toVariantMap();
    } else if (wireItem.value(QStringLiteral("access_restrictions")).isObject()) {
        result[QStringLiteral("restrictions")] =
            wireItem.value(QStringLiteral("access_restrictions")).toObject().toVariantMap();
    } else if (wireItem.value(QStringLiteral("restrictions")).isArray()) {
        result[QStringLiteral("restrictions")] =
            wireItem.value(QStringLiteral("restrictions")).toArray().toVariantList();
    }
    if (wireItem.value(QStringLiteral("restricted")).isBool()) {
        result[QStringLiteral("restricted")] =
            wireItem.value(QStringLiteral("restricted")).toBool();
    }

    const QString posterUrl = wireItem.value(QStringLiteral("poster_url")).toString();
    const QString stillUrl = wireItem.value(QStringLiteral("still_url")).toString();
    const QString primaryArtworkUrl = posterUrl.isEmpty() ? stillUrl : posterUrl;
    const QString posterThumbhash =
        wireItem.value(QStringLiteral("poster_thumbhash")).toString();
    const QString stillThumbhash =
        wireItem.value(QStringLiteral("still_thumbhash")).toString();
    const QString primaryThumbhash =
        posterThumbhash.isEmpty() ? stillThumbhash : posterThumbhash;
    QVariantList artwork;
    appendArtwork(artwork, result, QStringLiteral("primaryArtwork"),
                  QStringLiteral("primaryArtworkUrl"), connectionId, itemId,
                  Bloom::ArtworkKind::Primary,
                  primaryArtworkUrl);
    appendArtwork(artwork, result, QStringLiteral("backdropArtwork"),
                  QStringLiteral("backdropArtworkUrl"), connectionId, itemId,
                  Bloom::ArtworkKind::Backdrop,
                  wireItem.value(QStringLiteral("backdrop_url")).toString());
    appendArtwork(artwork, result, QStringLiteral("logoArtwork"),
                  QStringLiteral("logoArtworkUrl"), connectionId, itemId,
                  Bloom::ArtworkKind::Logo,
                  wireItem.value(QStringLiteral("logo_url")).toString());
    result[QStringLiteral("artwork")] = artwork;
    result[QStringLiteral("artworkThumbhashes")] = QVariantMap{
        {QStringLiteral("poster"), primaryThumbhash},
        {QStringLiteral("backdrop"), wireItem.value(QStringLiteral("backdrop_thumbhash")).toString()}
    };
    QVariantMap artworkExpiresAt;
    for (const auto &[kind, field] : {
             std::pair{QStringLiteral("primary"),
                       !posterUrl.isEmpty()
                           ? QStringLiteral("poster_url_expires_at")
                           : QStringLiteral("still_url_expires_at")},
             std::pair{QStringLiteral("backdrop"), QStringLiteral("backdrop_url_expires_at")},
             std::pair{QStringLiteral("logo"), QStringLiteral("logo_url_expires_at")}}) {
        if (wireItem.value(field).isString()) {
            artworkExpiresAt.insert(kind, wireItem.value(field).toString());
        }
    }
    if (!artworkExpiresAt.isEmpty()) {
        result[QStringLiteral("artworkExpiresAt")] = artworkExpiresAt;
    }
    return result;
}

QVariantList SiloModelMapper::mediaItems(const QJsonArray &wireItems,
                                         const QString &connectionId)
{
    QVariantList result;
    result.reserve(wireItems.size());
    for (const QJsonValue &value : wireItems) {
        if (!value.isObject()) {
            continue;
        }
        const QVariantMap mapped = mediaItem(value.toObject(), connectionId);
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

QVariantMap SiloModelMapper::chapter(const QJsonObject &wireChapter,
                                     const QString &connectionId,
                                     const QString &itemId,
                                     const QString &fileId,
                                     int fallbackIndex)
{
    const QJsonValue startValue = wireChapter.value(QStringLiteral("start_seconds"));
    const QJsonValue endValue = wireChapter.value(QStringLiteral("end_seconds"));
    if (!startValue.isDouble() || !secondsToMillisecondsRepresentable(startValue.toDouble())) {
        return {};
    }
    const bool hasEnd = endValue.isDouble() && std::isfinite(endValue.toDouble());
    if (hasEnd && (!secondsToMillisecondsRepresentable(endValue.toDouble())
                   || endValue.toDouble() < startValue.toDouble())) {
        return {};
    }

    const int index = wireChapter.contains(QStringLiteral("index"))
        ? wireChapter.value(QStringLiteral("index")).toInt(fallbackIndex)
        : fallbackIndex;
    QVariantMap result{
        {QStringLiteral("name"), wireChapter.value(QStringLiteral("title")).toString()},
        {QStringLiteral("index"), index},
        {QStringLiteral("fileId"), fileId},
        {QStringLiteral("startMs"), secondsToMilliseconds(
             wireChapter.value(QStringLiteral("start_seconds")).toDouble())},
        {QStringLiteral("endMs"), hasEnd
             ? secondsToMilliseconds(wireChapter.value(QStringLiteral("end_seconds")).toDouble())
             : -1},
        {QStringLiteral("source"), wireChapter.value(QStringLiteral("source")).toString()},
        {QStringLiteral("thumbnailUrl"), wireChapter.value(QStringLiteral("thumbnail_url")).toString()},
        {QStringLiteral("thumbnailThumbhash"), wireChapter.value(QStringLiteral("thumbnail_thumbhash")).toString()}
    };
    if (!result.value(QStringLiteral("thumbnailUrl")).toString().isEmpty()
        && !connectionId.isEmpty() && !itemId.isEmpty()) {
        Bloom::ArtworkRef artwork;
        artwork.connectionId = connectionId;
        artwork.itemId = itemId;
        artwork.kind = Bloom::ArtworkKind::Chapter;
        artwork.tag = fileId;
        artwork.ownerKind = Bloom::ArtworkOwnerKind::Chapter;
        artwork.index = qMax(0, index);
        artwork.sourceUrl = result.value(QStringLiteral("thumbnailUrl")).toString();
        result[QStringLiteral("artwork")] = artwork.toVariantMap();
    }
    return result;
}

QVariantList SiloModelMapper::chapters(const QJsonArray &wireChapters,
                                       const QString &connectionId,
                                       const QString &itemId,
                                       const QString &fileId)
{
    QVariantList result;
    result.reserve(wireChapters.size());
    for (qsizetype index = 0; index < wireChapters.size(); ++index) {
        if (!wireChapters.at(index).isObject()) {
            continue;
        }
        const QVariantMap mapped = chapter(wireChapters.at(index).toObject(), connectionId,
                                           itemId, fileId, static_cast<int>(index));
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

QVariantList SiloModelMapper::chaptersFromItem(const QJsonObject &wireItem,
                                               const QString &connectionId,
                                               const QString &itemId)
{
    QVariantList result;
    QSet<QString> seen;
    const auto appendChapters = [&result, &seen, &connectionId, &itemId](
                                    const QJsonObject &version) {
        const QString fileId = identityString(version.value(QStringLiteral("file_id")));
        const QVariantList mapped = chapters(version.value(QStringLiteral("chapters")).toArray(),
                                              connectionId, itemId, fileId);
        for (const QVariant &entry : mapped) {
            const QVariantMap chapterMap = entry.toMap();
            const QString key = fileId + QLatin1Char(':')
                + QString::number(chapterMap.value(QStringLiteral("index")).toInt())
                + QLatin1Char(':')
                + QString::number(chapterMap.value(QStringLiteral("startMs")).toLongLong());
            if (!seen.contains(key)) {
                seen.insert(key);
                result.append(entry);
            }
        }
    };

    if (wireItem.value(QStringLiteral("chapters")).isArray()) {
        QJsonObject direct;
        direct.insert(QStringLiteral("file_id"), wireItem.value(QStringLiteral("file_id")));
        direct.insert(QStringLiteral("chapters"), wireItem.value(QStringLiteral("chapters")));
        appendChapters(direct);
    }
    for (const QJsonValue &value : wireItem.value(QStringLiteral("versions")).toArray()) {
        if (value.isObject()) {
            appendChapters(value.toObject());
        }
    }
    for (const QJsonValue &value : wireItem.value(QStringLiteral("files")).toArray()) {
        if (value.isObject()) {
            appendChapters(value.toObject());
        }
    }
    for (const QJsonValue &variantValue :
         wireItem.value(QStringLiteral("playback_variants")).toArray()) {
        if (!variantValue.isObject()) {
            continue;
        }
        for (const QJsonValue &partValue :
             variantValue.toObject().value(QStringLiteral("parts")).toArray()) {
            if (!partValue.isObject()) {
                continue;
            }
            for (const QJsonValue &versionValue :
                 partValue.toObject().value(QStringLiteral("versions")).toArray()) {
                if (versionValue.isObject()) {
                    appendChapters(versionValue.toObject());
                }
            }
        }
    }
    return result;
}

QVariantMap SiloModelMapper::filterOptions(const QJsonObject &wireFilters)
{
    return {
        {QStringLiteral("genres"), stringList(wireFilters.value(QStringLiteral("genres")))},
        {QStringLiteral("studios"), stringList(wireFilters.value(QStringLiteral("studios")))},
        {QStringLiteral("networks"), stringList(wireFilters.value(QStringLiteral("networks")))},
        {QStringLiteral("countries"), stringList(wireFilters.value(QStringLiteral("countries")))},
        {QStringLiteral("originalLanguages"), stringList(wireFilters.value(QStringLiteral("original_languages")))},
        {QStringLiteral("contentRatings"), stringList(wireFilters.value(QStringLiteral("content_ratings")))},
        {QStringLiteral("authors"), stringList(wireFilters.value(QStringLiteral("authors")))},
        {QStringLiteral("narrators"), stringList(wireFilters.value(QStringLiteral("narrators")))},
        {QStringLiteral("series"), stringList(wireFilters.value(QStringLiteral("series")))},
        {QStringLiteral("resolutions"), stringList(wireFilters.value(QStringLiteral("resolutions")))},
        {QStringLiteral("audioLanguages"), stringList(wireFilters.value(QStringLiteral("audio_languages")))},
        {QStringLiteral("subtitleLanguages"), stringList(wireFilters.value(QStringLiteral("subtitle_languages")))}
    };
}

QStringList SiloModelMapper::namedItems(const QJsonObject &wireItems)
{
    QStringList result;
    const QJsonArray items = wireItems.value(QStringLiteral("items")).isArray()
        ? wireItems.value(QStringLiteral("items")).toArray()
        : wireItems.value(QStringLiteral("matches")).toArray();
    result.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isString()) {
            result.append(value.toString());
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value(QStringLiteral("name")).toString(
                object.value(QStringLiteral("title")).toString());
            if (!name.isEmpty()) {
                result.append(name);
            }
        }
    }
    return result;
}

QList<ProviderProfile> SiloModelMapper::profiles(const QJsonArray &wireProfiles)
{
    QList<ProviderProfile> result;
    result.reserve(wireProfiles.size());
    for (const QJsonValue &value : wireProfiles) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wire = value.toObject();
        ProviderProfile profile;
        profile.id = wire.value(QStringLiteral("id")).toString();
        profile.name = wire.value(QStringLiteral("name")).toString();
        profile.avatarUrl = wire.value(QStringLiteral("avatar_url")).toString();
        profile.hasPin = wire.value(QStringLiteral("has_pin")).toBool();
        profile.isChild = wire.value(QStringLiteral("is_child")).toBool();
        profile.isPrimary = wire.value(QStringLiteral("is_primary")).toBool();
        if (profile.isValid() && !profile.name.isEmpty()) {
            result.append(profile);
        }
    }
    return result;
}

QList<ProviderAuthSession> SiloModelMapper::authSessions(const QJsonArray &wireSessions)
{
    QList<ProviderAuthSession> result;
    result.reserve(wireSessions.size());
    for (const QJsonValue &value : wireSessions) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wire = value.toObject();
        ProviderAuthSession session;
        session.id = wire.value(QStringLiteral("id")).toString();
        session.deviceName = wire.value(QStringLiteral("device_name")).toString();
        session.ipAddress = wire.value(QStringLiteral("ip_address")).toString();
        session.createdAt = isoDateMilliseconds(wire.value(QStringLiteral("created_at")));
        session.expiresAt = isoDateMilliseconds(wire.value(QStringLiteral("expires_at")));
        session.revokedAt = isoDateMilliseconds(wire.value(QStringLiteral("revoked_at")));
        session.isCurrent = wire.value(QStringLiteral("is_current")).toBool();
        if (session.isValid()) {
            result.append(session);
        }
    }
    return result;
}

QVariantMap SiloModelMapper::nativeState(const QJsonObject &wireState,
                                         const QString &connectionId)
{
    QJsonObject state = wireState.value(QStringLiteral("user_data")).isObject()
        ? wireState.value(QStringLiteral("user_data")).toObject()
        : (wireState.value(QStringLiteral("user_state")).isObject()
               ? wireState.value(QStringLiteral("user_state")).toObject()
               : (wireState.value(QStringLiteral("state")).isObject()
                      ? wireState.value(QStringLiteral("state")).toObject()
                      : wireState));
    if (state.value(QStringLiteral("user_data")).isObject()) {
        state = state.value(QStringLiteral("user_data")).toObject();
    } else if (state.value(QStringLiteral("user_state")).isObject()) {
        state = state.value(QStringLiteral("user_state")).toObject();
    }
    const auto value = [&state, &wireState](const QString &key) {
        return state.contains(key) ? state.value(key) : wireState.value(key);
    };
    const bool recognized = value(QStringLiteral("played")).isBool()
        || value(QStringLiteral("is_favorite")).isBool()
        || value(QStringLiteral("favorite")).isBool()
        || value(QStringLiteral("position_seconds")).isDouble()
        || value(QStringLiteral("is_in_progress")).isBool();
    if (!recognized) {
        return {};
    }

    Bloom::UserMediaState userState;
    userState.watched = value(QStringLiteral("played")).toBool();
    userState.favorite = value(QStringLiteral("is_favorite")).isBool()
        ? value(QStringLiteral("is_favorite")).toBool()
        : value(QStringLiteral("favorite")).toBool();
    userState.positionMs = secondsToMilliseconds(
        value(QStringLiteral("position_seconds")).toDouble());
    userState.unplayedItemCount = value(QStringLiteral("unplayed_count")).toInt();
    userState.lastPlayedAt = value(QStringLiteral("last_played_at")).toString(
        value(QStringLiteral("progress_updated_at")).toString());
    QString contentId = identityString(wireState.value(QStringLiteral("content_id")));
    if (contentId.isEmpty()) {
        contentId = identityString(value(QStringLiteral("content_id")));
    }
    if (contentId.isEmpty()) {
        contentId = identityString(wireState.value(QStringLiteral("media_item_id")));
    }
    if (contentId.isEmpty()) {
        contentId = identityString(value(QStringLiteral("media_item_id")));
    }
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), contentId},
        {QStringLiteral("userState"), userState.toVariantMap()},
        {QStringLiteral("watched"), userState.watched},
        {QStringLiteral("favorite"), userState.favorite},
        {QStringLiteral("positionMs"), userState.positionMs},
        {QStringLiteral("durationMs"), secondsToMilliseconds(
             value(QStringLiteral("duration_seconds")).toDouble())},
        {QStringLiteral("watchedCount"), value(QStringLiteral("watched_count")).toInt()},
        {QStringLiteral("isInProgress"),
         userState.positionMs > 0
             || value(QStringLiteral("is_in_progress")).toBool()},
        {QStringLiteral("lastFileId"), identityString(value(QStringLiteral("last_file_id")))}
    };
}

QList<MediaSegmentInfo> SiloModelMapper::mediaSegments(const QString &itemId,
                                                       const QJsonObject &wireSegments)
{
    QList<MediaSegmentInfo> result;
    const QString fileId = identityString(wireSegments.value(QStringLiteral("file_id")));
    for (const QString &name : {QStringLiteral("intro"), QStringLiteral("credits"),
                                QStringLiteral("recap"), QStringLiteral("preview")}) {
        if (!wireSegments.value(name).isObject()) {
            continue;
        }
        const QJsonObject wire = wireSegments.value(name).toObject();
        const QJsonValue start = wire.contains(QStringLiteral("start_seconds"))
            ? wire.value(QStringLiteral("start_seconds")) : wire.value(QStringLiteral("start"));
        const QJsonValue end = wire.contains(QStringLiteral("end_seconds"))
            ? wire.value(QStringLiteral("end_seconds")) : wire.value(QStringLiteral("end"));
        if (!start.isDouble() || !end.isDouble()
            || !secondsToMillisecondsRepresentable(start.toDouble())
            || !secondsToMillisecondsRepresentable(end.toDouble())
            || end.toDouble() <= start.toDouble()) {
            continue;
        }

        MediaSegmentInfo segment;
        segment.id = fileId.isEmpty() ? QString() : fileId + QLatin1Char(':') + name;
        segment.itemId = itemId;
        segment.startMs = secondsToMilliseconds(start.toDouble());
        segment.endMs = secondsToMilliseconds(end.toDouble());
        segment.source = wire.value(QStringLiteral("source")).toString();
        segment.confidence = wire.value(QStringLiteral("confidence")).toDouble();
        if (name == QStringLiteral("intro")) {
            segment.type = MediaSegmentType::Intro;
            segment.typeString = QStringLiteral("Intro");
        } else if (name == QStringLiteral("credits")) {
            segment.type = MediaSegmentType::Outro;
            segment.typeString = QStringLiteral("Outro");
        } else if (name == QStringLiteral("recap")) {
            segment.type = MediaSegmentType::Recap;
            segment.typeString = QStringLiteral("Recap");
        } else {
            segment.type = MediaSegmentType::Preview;
            segment.typeString = QStringLiteral("Preview");
        }
        result.append(segment);
    }
    return result;
}
