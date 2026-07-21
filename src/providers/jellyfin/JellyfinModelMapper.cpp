#include "JellyfinModelMapper.h"

#include <QDateTime>
#include <QJsonValue>
#include <QUrlQuery>
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
        {QStringLiteral("childCount"), wireItem.value(QStringLiteral("ChildCount")).toInt()},
        {QStringLiteral("status"), wireItem.value(QStringLiteral("Status")).toString()},
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
    chapter.name = wireChapter.value(QStringLiteral("Name")).toString();
    chapter.startMs = ticksToMilliseconds(
        wireChapter.value(QStringLiteral("StartPositionTicks")).toVariant().toLongLong());
    const QString imageTag = wireChapter.value(QStringLiteral("ImageTag")).toString();
    if (!imageTag.isEmpty()) {
        chapter.artwork.connectionId = connectionId;
        chapter.artwork.itemId = itemId;
        chapter.artwork.kind = Bloom::ArtworkKind::Chapter;
        chapter.artwork.index = qMax(0, chapterIndex);
        chapter.artwork.tag = imageTag;
    }
    return chapter;
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
