#include "JellyfinModelMapper.h"
#include "models/MediaExtras.h"
#include <QHash>

#include <QDate>
#include <QDateTime>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QVariantList stringList(const QJsonArray &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QJsonValue &value : values) {
        if (value.isString()) {
            result.append(value.toString());
        } else if (value.isObject()) {
            const QString name = value.toObject().value(QStringLiteral("Name")).toString();
            if (!name.isEmpty()) {
                result.append(name);
            }
        }
    }
    return result;
}

void appendArtwork(QVariantList &artwork,
                   QVariantMap &item,
                   const QString &property,
                   const QString &connectionId,
                   const QString &itemId,
                   Bloom::ArtworkKind kind,
                   const QString &tag,
                   int index = 0)
{
    if (connectionId.isEmpty() || itemId.isEmpty() || tag.isEmpty()) {
        return;
    }
    Bloom::ArtworkRef ref;
    ref.connectionId = connectionId;
    ref.itemId = itemId;
    ref.kind = kind;
    ref.index = index;
    ref.tag = tag;
    const QVariantMap map = ref.toVariantMap();
    artwork.append(map);
    if (!property.isEmpty() && !item.contains(property)) {
        item[property] = map;
    }
}

QVariantList people(const QJsonArray &wirePeople, const QString &connectionId)
{
    QVariantList result;
    result.reserve(wirePeople.size());
    for (const QJsonValue &value : wirePeople) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wirePerson = value.toObject();
        Bloom::Person person;
        person.media.connectionId = connectionId;
        person.media.itemId = wirePerson.value(QStringLiteral("Id")).toString();
        person.name = wirePerson.value(QStringLiteral("Name")).toString();
        person.role = wirePerson.value(QStringLiteral("Role")).toString();
        person.kind = wirePerson.value(QStringLiteral("Type")).toString();
        const QString imageTag = wirePerson.value(QStringLiteral("PrimaryImageTag")).toString();
        if (!person.media.itemId.isEmpty() && !imageTag.isEmpty()) {
            person.artwork.connectionId = connectionId;
            person.artwork.itemId = person.media.itemId;
            person.artwork.kind = Bloom::ArtworkKind::Person;
            person.artwork.tag = imageTag;
        }
        result.append(person.toVariantMap());
    }
    return result;
}

QStringList namedStringList(const QJsonArray &values)
{
    QStringList result;
    for (const QVariant &value : stringList(values)) {
        const QString name = value.toString();
        if (!name.isEmpty()) {
            result.append(name);
        }
    }
    result.removeDuplicates();
    return result;
}

QString wireString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const double rounded = std::round(number);
        const double qint64Minimum =
            static_cast<double>(std::numeric_limits<qint64>::min());
        const double qint64UpperBoundExclusive = -qint64Minimum;
        if (qFuzzyCompare(number, rounded)
            && rounded >= qint64Minimum
            && rounded < qint64UpperBoundExclusive) {
            return QString::number(static_cast<qint64>(rounded));
        }
        return QString::number(number);
    }
    return {};
}

int wireInt(const QJsonValue &value, int defaultValue = 0)
{
    if (value.isDouble()) {
        return value.toInt(defaultValue);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : defaultValue;
    }
    return defaultValue;
}

QString streamType(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (!value.isDouble()) {
        return {};
    }

    switch (value.toInt(-1)) {
    case 0: return QStringLiteral("Audio");
    case 1: return QStringLiteral("Video");
    case 2: return QStringLiteral("Subtitle");
    default: return {};
    }
}

} // namespace

qint64 JellyfinModelMapper::ticksToMilliseconds(qint64 ticks)
{
    return ticks <= 0 ? 0 : ticks / 10000;
}

qint64 JellyfinModelMapper::millisecondsToTicks(qint64 milliseconds)
{
    if (milliseconds <= 0) {
        return 0;
    }
    constexpr qint64 multiplier = 10000;
    if (milliseconds > std::numeric_limits<qint64>::max() / multiplier) {
        return std::numeric_limits<qint64>::max();
    }
    return milliseconds * multiplier;
}

MediaStreamInfo JellyfinModelMapper::mediaStream(const QJsonObject &wireStream)
{
    MediaStreamInfo info;
    info.index = wireStream.value(QStringLiteral("Index")).toInt(-1);
    info.type = streamType(wireStream.value(QStringLiteral("Type")));
    info.codec = wireStream.value(QStringLiteral("Codec")).toString();
    info.language = wireStream.value(QStringLiteral("Language")).toString();
    info.title = wireStream.value(QStringLiteral("Title")).toString();
    info.displayTitle = wireStream.value(QStringLiteral("DisplayTitle")).toString();
    info.isDefault = wireStream.value(QStringLiteral("IsDefault")).toBool();
    info.isForced = wireStream.value(QStringLiteral("IsForced")).toBool();
    info.isExternal = wireStream.value(QStringLiteral("IsExternal")).toBool();
    info.isHearingImpaired = wireStream.value(QStringLiteral("IsHearingImpaired")).toBool();
    info.channels = wireStream.value(QStringLiteral("Channels")).toInt();
    info.channelLayout = wireStream.value(QStringLiteral("ChannelLayout")).toString();
    info.bitRate = wireStream.value(QStringLiteral("BitRate")).toInt();
    info.sampleRate = wireStream.value(QStringLiteral("SampleRate")).toInt();
    info.bitDepth = wireStream.value(QStringLiteral("BitDepth")).toInt();
    info.width = wireStream.value(QStringLiteral("Width")).toInt();
    info.height = wireStream.value(QStringLiteral("Height")).toInt();
    info.averageFrameRate = wireStream.value(QStringLiteral("AverageFrameRate")).toDouble();
    info.realFrameRate = wireStream.value(QStringLiteral("RealFrameRate")).toDouble();
    info.profile = wireStream.value(QStringLiteral("Profile")).toString();
    const QString spatialFormat = wireStream.value(QStringLiteral("AudioSpatialFormat")).toString();
    QVariantMap audioMetadata{{QStringLiteral("codec"), info.codec},
        {QStringLiteral("profile"), info.profile}, {QStringLiteral("title"), info.title},
        {QStringLiteral("audioSpatialFormat"), spatialFormat}};
    const Bloom::AudioSourceMetadata audio = Bloom::AudioSourceMetadata::fromVariantMap(audioMetadata);
    info.atmos = audio.atmos;
    info.dtsX = audio.dtsX;
    info.videoRange = wireString(wireStream.value(QStringLiteral("VideoRange")));
    info.videoRangeType = wireString(wireStream.value(QStringLiteral("VideoRangeType")));
    info.codecTag = wireString(wireStream.value(QStringLiteral("CodecTag")));
    info.codecTagString = wireString(wireStream.value(QStringLiteral("CodecTagString")));
    info.codecId = wireString(wireStream.value(QStringLiteral("CodecId")));
    info.dolbyVisionProfile = wireInt(wireStream.contains(QStringLiteral("DvProfile"))
                                          ? wireStream.value(QStringLiteral("DvProfile"))
                                          : wireStream.value(QStringLiteral("DolbyVisionProfile")));
    info.dolbyVisionLevel = wireInt(wireStream.contains(QStringLiteral("DvLevel"))
                                        ? wireStream.value(QStringLiteral("DvLevel"))
                                        : wireStream.value(QStringLiteral("DolbyVisionLevel")));
    info.dolbyVisionBlSignalCompatibilityId = wireInt(
        wireStream.value(QStringLiteral("DvBlSignalCompatibilityId")));
    info.videoDoViTitle = wireStream.value(QStringLiteral("VideoDoViTitle")).toString();
    // DeliveryUrl belongs to the delivery method the DeviceProfile selected;
    // only an External delivery is a file mpv can load beside the stream.
    if (info.isExternal
        && wireStream.value(QStringLiteral("DeliveryMethod")).toString().compare(
               QStringLiteral("External"), Qt::CaseInsensitive) == 0) {
        const QString deliveryUrl = wireStream.value(QStringLiteral("DeliveryUrl")).toString().trimmed();
        if (wireStream.value(QStringLiteral("IsExternalUrl")).toBool()) {
            // For non-File sources (for example .strm items) Jellyfin hands
            // back the stream's Path, which is often a disk path on the
            // server. Only a web URL is loadable; anything else is left for
            // the provider's GetSubtitle fallback, which reads it server-side.
            const QUrl url(deliveryUrl);
            const QString scheme = url.scheme().toLower();
            if ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
                && !url.host().isEmpty()) {
                info.externalUrl = deliveryUrl;
            }
        } else {
            // Server-relative delivery URLs carry the token of the session
            // that asked for PlaybackInfo. The playback provider adds the
            // current one, so the canonical model never holds a credential.
            info.externalUrl = withoutQueryCredentials(deliveryUrl);
        }
    }
    return info;
}

QString JellyfinModelMapper::withoutQueryCredentials(const QString &url)
{
    const qsizetype queryStart = url.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return url;
    }
    qsizetype queryEnd = url.indexOf(QLatin1Char('#'), queryStart);
    if (queryEnd < 0) {
        queryEnd = url.size();
    }
    static const QLatin1String credentialNames[] = {
        QLatin1String("ApiKey"), QLatin1String("api_key"),
        QLatin1String("X-Emby-Token"), QLatin1String("X-MediaBrowser-Token")};
    QStringList kept;
    const QStringList items = url.mid(queryStart + 1, queryEnd - queryStart - 1)
                                  .split(QLatin1Char('&'));
    for (const QString &item : items) {
        const QString name = QUrl::fromPercentEncoding(item.section(QLatin1Char('='), 0, 0).toUtf8());
        const bool isCredential = std::any_of(
            std::begin(credentialNames), std::end(credentialNames),
            [&name](QLatin1String credential) {
                return name.compare(credential, Qt::CaseInsensitive) == 0;
            });
        if (!isCredential) {
            kept.append(item);
        }
    }
    QString result = url.left(queryStart);
    if (!kept.isEmpty()) {
        result += QLatin1Char('?') + kept.join(QLatin1Char('&'));
    }
    return result + url.mid(queryEnd);
}

MediaSourceInfo JellyfinModelMapper::mediaSource(const QJsonObject &wireSource)
{
    MediaSourceInfo info;
    info.id = wireSource.value(QStringLiteral("Id")).toString();
    info.name = wireSource.value(QStringLiteral("Name")).toString();
    info.path = wireSource.value(QStringLiteral("Path")).toString();
    info.directStreamUrl = wireSource.value(QStringLiteral("DirectStreamUrl")).toString();
    info.transcodingUrl = wireSource.value(QStringLiteral("TranscodingUrl")).toString();
    info.directPlayAvailabilityKnown = wireSource.contains(QStringLiteral("SupportsDirectPlay"));
    info.directPlayAvailable = wireSource.value(QStringLiteral("SupportsDirectPlay")).toBool();
    info.container = wireSource.value(QStringLiteral("Container")).toString();
    info.size = wireSource.value(QStringLiteral("Size")).toVariant().toLongLong();
    info.bitRate = wireSource.value(QStringLiteral("Bitrate")).toInt();
    info.videoType = wireSource.value(QStringLiteral("VideoType")).toString();
    info.durationMs = ticksToMilliseconds(
        wireSource.value(QStringLiteral("RunTimeTicks")).toVariant().toLongLong());
    info.defaultAudioStreamIndex = wireSource.value(
        QStringLiteral("DefaultAudioStreamIndex")).toInt(-1);
    info.defaultSubtitleStreamIndex = wireSource.value(
        QStringLiteral("DefaultSubtitleStreamIndex")).toInt(-1);

    const QJsonArray streams = wireSource.value(QStringLiteral("MediaStreams")).toArray();
    info.mediaStreams.reserve(streams.size());
    for (const QJsonValue &value : streams) {
        if (value.isObject()) {
            info.mediaStreams.append(mediaStream(value.toObject()));
        }
    }
    return info;
}

PlaybackInfoResponse JellyfinModelMapper::playbackInfo(const QJsonObject &wirePlaybackInfo)
{
    PlaybackInfoResponse response;
    response.playSessionId = wirePlaybackInfo.value(QStringLiteral("PlaySessionId")).toString();
    const QJsonArray sources = wirePlaybackInfo.value(QStringLiteral("MediaSources")).toArray();
    response.mediaSources.reserve(sources.size());
    for (const QJsonValue &value : sources) {
        if (value.isObject()) {
            response.mediaSources.append(mediaSource(value.toObject()));
        }
    }
    return response;
}

ParsedItemsResult JellyfinModelMapper::itemsResponse(
    const QByteArray &wireResponse, const QString &parentId)
{
    ParsedItemsResult result;
    result.parentId = parentId;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(wireResponse, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return result;
    }

    const QJsonObject root = document.object();
    result.items = root.value(QStringLiteral("Items")).toArray();
    result.totalRecordCount = root.value(QStringLiteral("TotalRecordCount"))
                                  .toInt(result.items.size());
    result.success = true;
    return result;
}

TrickplayTileInfo JellyfinModelMapper::trickplayTile(const QJsonObject &wireTile)
{
    TrickplayTileInfo info;
    info.width = wireTile.value(QStringLiteral("Width")).toInt();
    info.height = wireTile.value(QStringLiteral("Height")).toInt();
    info.tileWidth = wireTile.value(QStringLiteral("TileWidth")).toInt();
    info.tileHeight = wireTile.value(QStringLiteral("TileHeight")).toInt();
    info.thumbnailCount = wireTile.value(QStringLiteral("ThumbnailCount")).toInt();
    info.interval = wireTile.value(QStringLiteral("Interval")).toInt();
    info.bandwidth = wireTile.value(QStringLiteral("Bandwidth")).toInt();
    return info;
}

TrickplayTileInfoMap JellyfinModelMapper::trickplayInfo(const QJsonObject &wireItem)
{
    const qint64 durationMs = ticksToMilliseconds(
        wireItem.value(QStringLiteral("RunTimeTicks")).toVariant().toLongLong());
    const QJsonObject wireSources = wireItem.value(QStringLiteral("Trickplay")).toObject();
    TrickplayTileInfoMap result;

    for (auto sourceIt = wireSources.constBegin(); sourceIt != wireSources.constEnd(); ++sourceIt) {
        const QJsonObject wireResolutions = sourceIt.value().toObject();
        for (auto resolutionIt = wireResolutions.constBegin();
             resolutionIt != wireResolutions.constEnd(); ++resolutionIt) {
            bool widthValid = false;
            const int width = resolutionIt.key().toInt(&widthValid);
            if (!widthValid || !resolutionIt.value().isObject()) {
                continue;
            }

            TrickplayTileInfo info = trickplayTile(resolutionIt.value().toObject());
            if (info.interval > 0 && durationMs > 0) {
                const int calculatedCount = static_cast<int>(
                    std::ceil(static_cast<double>(durationMs) / info.interval));
                info.thumbnailCount = qMax(info.thumbnailCount, calculatedCount);
            }
            result.insert(width, info);
        }
        if (!result.isEmpty()) {
            break;
        }
    }
    return result;
}

QList<MediaSegmentInfo> JellyfinModelMapper::introSkipperSegments(
    const QString &itemId, const QJsonObject &wireSegments)
{
    static const QMap<QString, QPair<MediaSegmentType, QString>> typeMapping{
        {QStringLiteral("Introduction"), {MediaSegmentType::Intro, QStringLiteral("Intro")}},
        {QStringLiteral("Credits"), {MediaSegmentType::Outro, QStringLiteral("Outro")}},
        {QStringLiteral("Recap"), {MediaSegmentType::Recap, QStringLiteral("Recap")}},
        {QStringLiteral("Preview"), {MediaSegmentType::Preview, QStringLiteral("Preview")}},
        {QStringLiteral("Commercial"), {MediaSegmentType::Commercial, QStringLiteral("Commercial")}}
    };

    QList<MediaSegmentInfo> segments;
    for (auto it = wireSegments.constBegin(); it != wireSegments.constEnd(); ++it) {
        const QJsonObject wireSegment = it.value().toObject();
        if (!wireSegment.value(QStringLiteral("Valid")).toBool()) {
            continue;
        }

        const double startSeconds = wireSegment.value(QStringLiteral("Start")).toDouble();
        const double endSeconds = wireSegment.value(QStringLiteral("End")).toDouble();
        if (startSeconds < 0.0 || endSeconds <= startSeconds) {
            continue;
        }

        MediaSegmentInfo info;
        info.itemId = wireSegment.value(QStringLiteral("EpisodeId")).toString(itemId);
        info.source = QStringLiteral("jellyfin");
        info.startMs = qRound64(startSeconds * 1000.0);
        info.endMs = qRound64(endSeconds * 1000.0);
        const auto mapping = typeMapping.constFind(it.key());
        if (mapping == typeMapping.constEnd()) {
            info.type = MediaSegmentType::Unknown;
            info.typeString = it.key();
        } else {
            info.type = mapping->first;
            info.typeString = mapping->second;
        }
        segments.append(info);
    }
    return segments;
}

QList<MediaSegmentInfo> JellyfinModelMapper::mediaSegments(
    const QString &itemId, const QJsonObject &wireSegments)
{
    static const QMap<QString, QPair<MediaSegmentType, QString>> typeMapping{
        {QStringLiteral("Intro"), {MediaSegmentType::Intro, QStringLiteral("Intro")}},
        {QStringLiteral("Outro"), {MediaSegmentType::Outro, QStringLiteral("Outro")}},
        {QStringLiteral("Recap"), {MediaSegmentType::Recap, QStringLiteral("Recap")}},
        {QStringLiteral("Preview"), {MediaSegmentType::Preview, QStringLiteral("Preview")}},
        {QStringLiteral("Commercial"), {MediaSegmentType::Commercial, QStringLiteral("Commercial")}}
    };

    QList<MediaSegmentInfo> segments;
    const QJsonArray items = wireSegments.value(QStringLiteral("Items")).toArray();
    segments.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject wire = value.toObject();
        const qint64 startMs = ticksToMilliseconds(
            wire.value(QStringLiteral("StartTicks")).toVariant().toLongLong());
        const qint64 endMs = ticksToMilliseconds(
            wire.value(QStringLiteral("EndTicks")).toVariant().toLongLong());
        if (startMs < 0 || endMs <= startMs) {
            continue;
        }

        const QString wireType = wire.value(QStringLiteral("Type")).toString();
        const auto mapping = typeMapping.constFind(wireType);
        MediaSegmentInfo segment;
        segment.id = wire.value(QStringLiteral("Id")).toString();
        segment.itemId = wire.value(QStringLiteral("ItemId")).toString(itemId);
        segment.startMs = startMs;
        segment.endMs = endMs;
        segment.source = QStringLiteral("jellyfin");
        if (mapping == typeMapping.constEnd()) {
            segment.typeString = wireType.isEmpty() ? QStringLiteral("Unknown") : wireType;
        } else {
            segment.type = mapping->first;
            segment.typeString = mapping->second;
        }
        segments.append(segment);
    }
    return segments;
}

QVariantList JellyfinModelMapper::remoteSessions(const QJsonArray &wireSessions,
                                                  const QString &connectionId)
{
    QVariantList sessions;
    sessions.reserve(wireSessions.size());
    for (const QJsonValue &value : wireSessions) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject wireSession = value.toObject();
        const QJsonObject wirePlayState =
            wireSession.value(QStringLiteral("PlayState")).toObject();
        sessions.append(QVariantMap{
            {QStringLiteral("connectionId"), connectionId},
            {QStringLiteral("id"), wireSession.value(QStringLiteral("Id")).toString()},
            {QStringLiteral("deviceId"), wireSession.value(QStringLiteral("DeviceId")).toString()},
            {QStringLiteral("deviceName"), wireSession.value(QStringLiteral("DeviceName")).toString()},
            {QStringLiteral("client"), wireSession.value(QStringLiteral("Client")).toString()},
            {QStringLiteral("clientVersion"), wireSession.value(QStringLiteral("ApplicationVersion")).toString()},
            {QStringLiteral("userId"), wireSession.value(QStringLiteral("UserId")).toString()},
            {QStringLiteral("userName"), wireSession.value(QStringLiteral("UserName")).toString()},
            {QStringLiteral("lastActivityDate"), QDateTime::fromString(
                 wireSession.value(QStringLiteral("LastActivityDate")).toString(), Qt::ISODate)},
            {QStringLiteral("lastPlaybackCheckIn"), QDateTime::fromString(
                 wireSession.value(QStringLiteral("LastPlaybackCheckIn")).toString(), Qt::ISODate)},
            {QStringLiteral("isRemoteSession"), wireSession.value(QStringLiteral("IsRemoteSession")).toBool()},
            {QStringLiteral("supportsRemoteControl"), wireSession.value(QStringLiteral("SupportsRemoteControl")).toBool()},
            {QStringLiteral("playState"), wirePlayState.value(QStringLiteral("PlayMethod")).toString()},
            {QStringLiteral("hasCustomDeviceName"), wireSession.value(QStringLiteral("HasCustomDeviceName")).toBool()}
        });
    }
    return sessions;
}

QString JellyfinModelMapper::libraryIdFromAncestors(const QJsonArray &wireAncestors)
{
    for (const QJsonValue &value : wireAncestors) {
        const QJsonObject ancestor = value.toObject();
        const QString type = ancestor.value(QStringLiteral("Type")).toString();
        const QString collectionType =
            ancestor.value(QStringLiteral("CollectionType")).toString();
        if (type == QStringLiteral("CollectionFolder") || !collectionType.isEmpty()) {
            return ancestor.value(QStringLiteral("Id")).toString();
        }
    }
    return wireAncestors.isEmpty()
        ? QString()
        : wireAncestors.last().toObject().value(QStringLiteral("Id")).toString();
}

QVariantMap JellyfinModelMapper::filterOptions(const QJsonObject &wireFilters)
{
    return {
        {QStringLiteral("genres"), namedStringList(
             wireFilters.value(QStringLiteral("Genres")).toArray())},
        {QStringLiteral("tags"), namedStringList(
             wireFilters.value(QStringLiteral("Tags")).toArray())}
    };
}

QStringList JellyfinModelMapper::namedItems(const QJsonObject &wireItems)
{
    return namedStringList(wireItems.value(QStringLiteral("Items")).toArray());
}

QVariantMap JellyfinModelMapper::mediaItem(const QJsonObject &wireItem,
                                           const QString &connectionId)
{
    const QString itemId = wireItem.value(QStringLiteral("Id")).toString();
    const QJsonObject userData = wireItem.value(QStringLiteral("UserData")).toObject();

    Bloom::UserMediaState state;
    state.watched = userData.value(QStringLiteral("Played")).toBool();
    state.favorite = userData.value(QStringLiteral("IsFavorite")).toBool();
    state.positionMs = ticksToMilliseconds(
        userData.value(QStringLiteral("PlaybackPositionTicks")).toVariant().toLongLong());
    state.unplayedItemCount = userData.value(QStringLiteral("UnplayedItemCount")).toInt();
    state.lastPlayedAt = userData.value(QStringLiteral("LastPlayedDate")).toString();

    QVariantMap item{
        {QStringLiteral("media"), Bloom::MediaRef{connectionId, itemId}.toVariantMap()},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), wireItem.value(QStringLiteral("Name")).toString()},
        {QStringLiteral("sortName"), wireItem.value(QStringLiteral("SortName")).toString()},
        {QStringLiteral("mediaType"), wireItem.value(QStringLiteral("Type")).toString()},
        {QStringLiteral("collectionType"), wireItem.value(QStringLiteral("CollectionType")).toString()},
        {QStringLiteral("parentId"), wireItem.value(QStringLiteral("ParentId")).toString()},
        {QStringLiteral("seriesId"), wireItem.value(QStringLiteral("SeriesId")).toString()},
        {QStringLiteral("seasonId"), wireItem.value(QStringLiteral("SeasonId")).toString()},
        {QStringLiteral("seriesName"), wireItem.value(QStringLiteral("SeriesName")).toString()},
        {QStringLiteral("indexNumber"), wireItem.value(QStringLiteral("IndexNumber")).toInt(-1)},
        {QStringLiteral("parentIndexNumber"), wireItem.value(QStringLiteral("ParentIndexNumber")).toInt(-1)},
        {QStringLiteral("episodeOrder"), wireItem.value(QStringLiteral("DisplayOrder")).toString()},
        {QStringLiteral("overview"), wireItem.value(QStringLiteral("Overview")).toString()},
        {QStringLiteral("productionYear"), wireItem.value(QStringLiteral("ProductionYear")).toInt()},
        {QStringLiteral("premiereDate"), wireItem.value(QStringLiteral("PremiereDate")).toString()},
        {QStringLiteral("dateAdded"), wireItem.value(QStringLiteral("DateCreated")).toString()},
        {QStringLiteral("endDate"), wireItem.value(QStringLiteral("EndDate")).toString()},
        {QStringLiteral("officialRating"), wireItem.value(QStringLiteral("OfficialRating")).toString()},
        {QStringLiteral("communityRating"), wireItem.value(QStringLiteral("CommunityRating")).toDouble()},
        {QStringLiteral("durationMs"), ticksToMilliseconds(
             wireItem.value(QStringLiteral("RunTimeTicks")).toVariant().toLongLong())},
        {QStringLiteral("status"), wireItem.value(QStringLiteral("Status")).toString()},
        {QStringLiteral("recursiveItemCount"), wireItem.value(QStringLiteral("RecursiveItemCount")).toInt()},
        {QStringLiteral("locationType"), wireItem.value(QStringLiteral("LocationType")).toString()},
        {QStringLiteral("path"), wireItem.value(QStringLiteral("Path")).toString()},
        {QStringLiteral("genres"), stringList(wireItem.value(QStringLiteral("Genres")).toArray())},
        {QStringLiteral("studios"), stringList(wireItem.value(QStringLiteral("Studios")).toArray())},
        {QStringLiteral("tags"), stringList(wireItem.value(QStringLiteral("Tags")).toArray())},
        {QStringLiteral("providerIds"), wireItem.value(QStringLiteral("ProviderIds")).toObject().toVariantMap()},
        {QStringLiteral("userState"), state.toVariantMap()},
        {QStringLiteral("watched"), state.watched},
        {QStringLiteral("favorite"), state.favorite},
        {QStringLiteral("positionMs"), state.positionMs},
        {QStringLiteral("unplayedItemCount"), state.unplayedItemCount},
        {QStringLiteral("people"), people(wireItem.value(QStringLiteral("People")).toArray(), connectionId)}
    };

    if (wireItem.contains(QStringLiteral("ChildCount"))) {
        item[QStringLiteral("childCount")] =
            wireItem.value(QStringLiteral("ChildCount")).toInt();
    }
    if (wireItem.value(QStringLiteral("Type")).toString() == QStringLiteral("Episode")) {
        // Jellyfin 12's ItemFields.SpecialEpisodeNumbers selector exposes
        // these as top-level BaseItemDto properties; it is not a nested JSON
        // object. Keep the canonical projection aligned with that wire shape.
        item[QStringLiteral("airsBeforeSeasonNumber")] =
            wireItem.value(QStringLiteral("AirsBeforeSeasonNumber")).toInt(-1);
        item[QStringLiteral("airsAfterSeasonNumber")] =
            wireItem.value(QStringLiteral("AirsAfterSeasonNumber")).toInt(-1);
        item[QStringLiteral("airsBeforeEpisodeNumber")] =
            wireItem.value(QStringLiteral("AirsBeforeEpisodeNumber")).toInt(-1);
    }

    QVariantList artwork;
    const QJsonObject imageTags = wireItem.value(QStringLiteral("ImageTags")).toObject();
    appendArtwork(artwork, item, QStringLiteral("primaryArtwork"), connectionId, itemId,
                  Bloom::ArtworkKind::Primary,
                  imageTags.value(QStringLiteral("Primary")).toString());
    appendArtwork(artwork, item, QStringLiteral("thumbArtwork"), connectionId, itemId,
                  Bloom::ArtworkKind::Thumb,
                  imageTags.value(QStringLiteral("Thumb")).toString());
    appendArtwork(artwork, item, QStringLiteral("logoArtwork"), connectionId, itemId,
                  Bloom::ArtworkKind::Logo,
                  imageTags.value(QStringLiteral("Logo")).toString());
    QString parentPrimaryItemId =
        wireItem.value(QStringLiteral("ParentPrimaryImageItemId")).toString();
    if (parentPrimaryItemId.isEmpty()) {
        parentPrimaryItemId = wireItem.value(QStringLiteral("ParentId")).toString();
    }
    appendArtwork(artwork, item, QStringLiteral("parentPrimaryArtwork"), connectionId,
                  parentPrimaryItemId, Bloom::ArtworkKind::Primary,
                  wireItem.value(QStringLiteral("ParentPrimaryImageTag")).toString());
    appendArtwork(artwork, item, QStringLiteral("seriesPrimaryArtwork"), connectionId,
                  wireItem.value(QStringLiteral("SeriesId")).toString(),
                  Bloom::ArtworkKind::Primary,
                  wireItem.value(QStringLiteral("SeriesPrimaryImageTag")).toString());
    appendArtwork(artwork, item, QStringLiteral("seriesThumbArtwork"), connectionId,
                  wireItem.value(QStringLiteral("SeriesId")).toString(),
                  Bloom::ArtworkKind::Thumb,
                  wireItem.value(QStringLiteral("SeriesThumbImageTag")).toString());
    QString parentThumbItemId =
        wireItem.value(QStringLiteral("ParentThumbItemId")).toString();
    if (parentThumbItemId.isEmpty()) {
        parentThumbItemId = wireItem.value(QStringLiteral("ParentId")).toString();
    }
    appendArtwork(artwork, item, QStringLiteral("parentThumbArtwork"), connectionId,
                  parentThumbItemId, Bloom::ArtworkKind::Thumb,
                  wireItem.value(QStringLiteral("ParentThumbImageTag")).toString());

    const QJsonArray backdropTags = wireItem.value(QStringLiteral("BackdropImageTags")).toArray();
    for (qsizetype index = 0; index < backdropTags.size(); ++index) {
        appendArtwork(artwork, item, QStringLiteral("backdropArtwork"), connectionId, itemId,
                      Bloom::ArtworkKind::Backdrop, backdropTags.at(index).toString(),
                      static_cast<int>(index));
    }
    if (!item.contains(QStringLiteral("backdropArtwork"))) {
        appendArtwork(artwork, item, QStringLiteral("backdropArtwork"), connectionId, itemId,
                      Bloom::ArtworkKind::Backdrop,
                      imageTags.value(QStringLiteral("Backdrop")).toString());
    }

    const QJsonArray parentBackdropTags =
        wireItem.value(QStringLiteral("ParentBackdropImageTags")).toArray();
    QString parentBackdropItemId =
        wireItem.value(QStringLiteral("ParentBackdropItemId")).toString();
    if (parentBackdropItemId.isEmpty()) {
        parentBackdropItemId =
            wireItem.value(QStringLiteral("ParentBackdropImageItemId")).toString();
    }
    if (parentBackdropItemId.isEmpty()) {
        parentBackdropItemId = wireItem.value(QStringLiteral("SeriesId")).toString();
    }
    for (qsizetype index = 0; index < parentBackdropTags.size(); ++index) {
        appendArtwork(artwork, item, QStringLiteral("backdropArtwork"), connectionId,
                      parentBackdropItemId, Bloom::ArtworkKind::Backdrop,
                      parentBackdropTags.at(index).toString(), static_cast<int>(index));
    }
    item[QStringLiteral("artwork")] = artwork;
    return item;
}

QVariantMap JellyfinModelMapper::person(const QJsonObject &wirePerson,
                                        const QString &connectionId)
{
    Bloom::Person person;
    person.media = {connectionId, wirePerson.value(QStringLiteral("Id")).toString()};
    person.name = wirePerson.value(QStringLiteral("Name")).toString();
    if (person.media.itemId.isEmpty() || person.name.isEmpty()) {
        return {};
    }
    person.kind = wirePerson.value(QStringLiteral("Type")).toString();
    person.overview = wirePerson.value(QStringLiteral("Overview")).toString();
    // Jellyfin keeps a person's birth in PremiereDate and death in EndDate,
    // as midnight UTC timestamps.
    const auto calendarDate = [&wirePerson](const QString &key) {
        const QString value = wirePerson.value(key).toString();
        const QDate date = QDate::fromString(value.left(10), Qt::ISODate);
        return date.isValid() ? date.toString(Qt::ISODate) : QString();
    };
    person.birthDate = calendarDate(QStringLiteral("PremiereDate"));
    person.deathDate = calendarDate(QStringLiteral("EndDate"));
    const QJsonArray locations = wirePerson.value(QStringLiteral("ProductionLocations")).toArray();
    if (!locations.isEmpty()) {
        person.birthplace = locations.first().toString();
    }
    person.providerIds =
        wirePerson.value(QStringLiteral("ProviderIds")).toObject().toVariantMap();
    const QString imageTag = wirePerson.value(QStringLiteral("ImageTags")).toObject()
                                 .value(QStringLiteral("Primary")).toString();
    if (!imageTag.isEmpty() && !connectionId.isEmpty()) {
        person.artwork.connectionId = connectionId;
        person.artwork.itemId = person.media.itemId;
        person.artwork.kind = Bloom::ArtworkKind::Person;
        person.artwork.tag = imageTag;
    }
    return person.toVariantMap();
}

QVariantList JellyfinModelMapper::personList(const QJsonArray &wirePeople,
                                             const QString &connectionId)
{
    QVariantList result;
    result.reserve(wirePeople.size());
    for (const QJsonValue &value : wirePeople) {
        const QVariantMap mapped = person(value.toObject(), connectionId);
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

QVariantList JellyfinModelMapper::mediaItems(const QJsonArray &wireItems,
                                             const QString &connectionId)
{
    QVariantList items;
    items.reserve(wireItems.size());
    for (const QJsonValue &value : wireItems) {
        if (value.isObject()) {
            items.append(mediaItem(value.toObject(), connectionId));
        }
    }
    return items;
}

QVariantMap JellyfinModelMapper::extra(const QJsonObject &wireItem,
                                      const QString &connectionId,
                                      const QString &parentId)
{
    namespace Extras = Bloom::MediaExtras;
    static const QHash<QString, QString> kinds{
        {QStringLiteral("Trailer"), Extras::kTrailer},
        {QStringLiteral("Featurette"), Extras::kFeaturette},
        {QStringLiteral("Clip"), Extras::kClip},
        {QStringLiteral("BehindTheScenes"), Extras::kBehindTheScenes},
        {QStringLiteral("DeletedScene"), Extras::kDeletedScene},
        {QStringLiteral("Interview"), Extras::kInterview},
        {QStringLiteral("Scene"), Extras::kScene},
        {QStringLiteral("Short"), Extras::kShort},
        {QStringLiteral("Unknown"), Extras::kOther}};

    const QString itemId = wireItem.value(QStringLiteral("Id")).toString();
    const QJsonValue extraType = wireItem.value(QStringLiteral("ExtraType"));
    const QString wireKind = extraType.isString() ? extraType.toString() : QStringLiteral("Unknown");
    if (itemId.isEmpty() || !kinds.contains(wireKind)
        || wireItem.value(QStringLiteral("LocationType")).toString() == QStringLiteral("Virtual")) {
        return {};
    }

    const QVariantMap item = mediaItem(wireItem, connectionId);
    QVariantMap result{
        {QStringLiteral("media"), item.value(QStringLiteral("media"))},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("parentId"), parentId},
        {QStringLiteral("name"), item.value(QStringLiteral("name"))},
        {QStringLiteral("extraKind"), kinds.value(wireKind)},
        {QStringLiteral("durationMs"), item.value(QStringLiteral("durationMs"))}
    };
    // Extras carry their own 16:9 Primary still when the server has one;
    // a Thumb serves when Primary is missing.
    for (const QString &property : {QStringLiteral("primaryArtwork"), QStringLiteral("thumbArtwork")}) {
        if (item.contains(property)) {
            result.insert(QStringLiteral("primaryArtwork"), item.value(property));
            result.insert(QStringLiteral("artwork"), QVariantList{item.value(property)});
            break;
        }
    }
    return result;
}

QVariantList JellyfinModelMapper::extras(const QJsonArray &wireItems,
                                         const QString &connectionId,
                                         const QString &parentId)
{
    QVariantList result;
    for (const QJsonValue &value : wireItems) {
        const QVariantMap mapped = value.isObject()
            ? extra(value.toObject(), connectionId, parentId) : QVariantMap{};
        if (!mapped.isEmpty()) {
            result.append(mapped);
        }
    }
    return result;
}

Bloom::Chapter JellyfinModelMapper::chapter(const QJsonObject &wireChapter,
                                            const QString &connectionId,
                                            const QString &itemId,
                                            int chapterIndex)
{
    Bloom::Chapter chapter;
    chapter.name = wireChapter.value(QStringLiteral("Name")).toString().trimmed();
    if (chapter.name.isEmpty()) {
        chapter.name = QStringLiteral("Chapter %1").arg(chapterIndex + 1);
    }
    chapter.startMs = ticksToMilliseconds(
        wireChapter.value(QStringLiteral("StartPositionTicks")).toVariant().toLongLong());
    const QString imageTag = wireChapter.value(QStringLiteral("ImageTag")).toString();
    const QString imagePath = wireChapter.value(QStringLiteral("ImagePath")).toString();
    if (!imageTag.isEmpty() || !imagePath.isEmpty()) {
        chapter.artwork.connectionId = connectionId;
        chapter.artwork.itemId = itemId;
        chapter.artwork.kind = Bloom::ArtworkKind::Chapter;
        chapter.artwork.index = qMax(0, chapterIndex);
        chapter.artwork.tag = imageTag;
    }
    return chapter;
}

QVariantList JellyfinModelMapper::chapters(const QJsonArray &wireChapters,
                                           const QString &connectionId,
                                           const QString &itemId)
{
    QVariantList result;
    result.reserve(wireChapters.size());
    for (qsizetype index = 0; index < wireChapters.size(); ++index) {
        const QJsonValue value = wireChapters.at(index);
        if (value.isObject()) {
            result.append(chapter(value.toObject(), connectionId, itemId,
                                  static_cast<int>(index)).toVariantMap());
        }
    }
    return result;
}

QString JellyfinModelMapper::artworkEndpoint(const Bloom::ArtworkRef &artwork)
{
    if (!artwork.isValid()) {
        return {};
    }

    QString imageType;
    switch (artwork.kind) {
    case Bloom::ArtworkKind::Primary:
    case Bloom::ArtworkKind::Person:
        imageType = QStringLiteral("Primary");
        break;
    case Bloom::ArtworkKind::Thumb:
        imageType = QStringLiteral("Thumb");
        break;
    case Bloom::ArtworkKind::Backdrop:
        imageType = QStringLiteral("Backdrop");
        break;
    case Bloom::ArtworkKind::Logo:
        imageType = QStringLiteral("Logo");
        break;
    case Bloom::ArtworkKind::Chapter:
        imageType = QStringLiteral("Chapter");
        break;
    case Bloom::ArtworkKind::Unknown:
        return {};
    }

    QString endpoint;
    if (artwork.kind == Bloom::ArtworkKind::Chapter) {
        endpoint = QStringLiteral("/Items/%1/Images/Chapter/%2")
                       .arg(artwork.itemId, QString::number(artwork.index));
    } else if (artwork.kind == Bloom::ArtworkKind::Backdrop && artwork.index > 0) {
        endpoint = QStringLiteral("/Items/%1/Images/%2/%3")
                       .arg(artwork.itemId, imageType, QString::number(artwork.index));
    } else {
        endpoint = QStringLiteral("/Items/%1/Images/%2")
                       .arg(artwork.itemId, imageType);
    }

    QUrlQuery query;
    if (artwork.requestedWidth > 0) {
        query.addQueryItem(artwork.kind == Bloom::ArtworkKind::Chapter
                               ? QStringLiteral("maxWidth")
                               : QStringLiteral("fillWidth"),
                           QString::number(artwork.requestedWidth));
    }
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("95"));
    if (!artwork.tag.isEmpty()) {
        query.addQueryItem(QStringLiteral("tag"), artwork.tag);
    }
    if (!query.isEmpty()) {
        endpoint += QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
    }
    return endpoint;
}
