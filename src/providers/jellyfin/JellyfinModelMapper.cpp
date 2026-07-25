#include "JellyfinModelMapper.h"

#include <QDateTime>
#include <QJsonValue>
#include <QUrlQuery>
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
    info.width = wireStream.value(QStringLiteral("Width")).toInt();
    info.height = wireStream.value(QStringLiteral("Height")).toInt();
    info.averageFrameRate = wireStream.value(QStringLiteral("AverageFrameRate")).toDouble();
    info.realFrameRate = wireStream.value(QStringLiteral("RealFrameRate")).toDouble();
    info.profile = wireStream.value(QStringLiteral("Profile")).toString();
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
    return info;
}

MediaSourceInfo JellyfinModelMapper::mediaSource(const QJsonObject &wireSource)
{
    MediaSourceInfo info;
    info.id = wireSource.value(QStringLiteral("Id")).toString();
    info.name = wireSource.value(QStringLiteral("Name")).toString();
    info.path = wireSource.value(QStringLiteral("Path")).toString();
    info.directStreamUrl = wireSource.value(QStringLiteral("DirectStreamUrl")).toString();
    info.transcodingUrl = wireSource.value(QStringLiteral("TranscodingUrl")).toString();
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
        {QStringLiteral("overview"), wireItem.value(QStringLiteral("Overview")).toString()},
        {QStringLiteral("productionYear"), wireItem.value(QStringLiteral("ProductionYear")).toInt()},
        {QStringLiteral("premiereDate"), wireItem.value(QStringLiteral("PremiereDate")).toString()},
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
