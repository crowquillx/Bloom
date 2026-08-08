#include "MediaModels.h"

#include <QCache>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <string>

namespace {

QMutex transientArtworkSourceMutex;
QCache<QString, QString> transientArtworkSources(512);

void rememberTransientArtworkSource(const QString &cacheKey, const QString &sourceUrl)
{
    if (cacheKey.isEmpty() || sourceUrl.isEmpty()) {
        return;
    }
    QMutexLocker locker(&transientArtworkSourceMutex);
    transientArtworkSources.insert(cacheKey, new QString(sourceUrl));
}

QString transientArtworkSource(const QString &cacheKey)
{
    QMutexLocker locker(&transientArtworkSourceMutex);
    const QString *source = transientArtworkSources.object(cacheKey);
    return source ? *source : QString();
}

} // namespace

namespace Bloom {

QString artworkKindName(ArtworkKind kind)
{
    switch (kind) {
    case ArtworkKind::Primary: return QStringLiteral("primary");
    case ArtworkKind::Thumb: return QStringLiteral("thumb");
    case ArtworkKind::Backdrop: return QStringLiteral("backdrop");
    case ArtworkKind::Logo: return QStringLiteral("logo");
    case ArtworkKind::Chapter: return QStringLiteral("chapter");
    case ArtworkKind::Person: return QStringLiteral("person");
    case ArtworkKind::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

ArtworkKind artworkKindFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("primary")) return ArtworkKind::Primary;
    if (normalized == QStringLiteral("thumb")) return ArtworkKind::Thumb;
    if (normalized == QStringLiteral("backdrop")) return ArtworkKind::Backdrop;
    if (normalized == QStringLiteral("logo")) return ArtworkKind::Logo;
    if (normalized == QStringLiteral("chapter")) return ArtworkKind::Chapter;
    if (normalized == QStringLiteral("person")) return ArtworkKind::Person;
    return ArtworkKind::Unknown;
}

QString artworkOwnerKindName(ArtworkOwnerKind kind)
{
    switch (kind) {
    case ArtworkOwnerKind::MediaItem: return QStringLiteral("mediaItem");
    case ArtworkOwnerKind::Library: return QStringLiteral("library");
    case ArtworkOwnerKind::Person: return QStringLiteral("person");
    case ArtworkOwnerKind::Chapter: return QStringLiteral("chapter");
    }
    return QStringLiteral("mediaItem");
}

ArtworkOwnerKind artworkOwnerKindFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("mediaitem")) return ArtworkOwnerKind::MediaItem;
    if (normalized == QStringLiteral("library")) return ArtworkOwnerKind::Library;
    if (normalized == QStringLiteral("person")) return ArtworkOwnerKind::Person;
    if (normalized == QStringLiteral("chapter")) return ArtworkOwnerKind::Chapter;
    return ArtworkOwnerKind::MediaItem;
}

QString ArtworkRef::transientSourceUrlForCacheKey(const QString &key)
{
    return transientArtworkSource(key);
}

bool MediaRef::isValid() const
{
    return !connectionId.trimmed().isEmpty() && !itemId.trimmed().isEmpty();
}

QVariantMap MediaRef::toVariantMap() const
{
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId}
    };
}

bool ArtworkRef::isValid() const
{
    return !connectionId.trimmed().isEmpty()
        && !itemId.trimmed().isEmpty()
        && kind != ArtworkKind::Unknown
        && index >= 0
        && requestedWidth >= 0;
}

bool ArtworkRef::operator==(const ArtworkRef &other) const
{
    return connectionId == other.connectionId
        && itemId == other.itemId
        && kind == other.kind
        && ownerKind == other.ownerKind
        && index == other.index
        && tag == other.tag
        && requestedWidth == other.requestedWidth;
}

QString ArtworkRef::cacheKey() const
{
    const QJsonObject object{
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("kind"), artworkKindName(kind)},
        {QStringLiteral("ownerKind"), artworkOwnerKindName(ownerKind)},
        {QStringLiteral("index"), index},
        {QStringLiteral("tag"), tag},
        {QStringLiteral("requestedWidth"), requestedWidth}
    };
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact)
                                   .toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals);
    const QString key = QStringLiteral("artwork:") + QString::fromLatin1(payload);
    rememberTransientArtworkSource(key, sourceUrl);
    return key;
}

QVariantMap ArtworkRef::toVariantMap() const
{
    const QString key = cacheKey();
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("kind"), artworkKindName(kind)},
        {QStringLiteral("ownerKind"), artworkOwnerKindName(ownerKind)},
        {QStringLiteral("index"), index},
        {QStringLiteral("tag"), tag},
        {QStringLiteral("requestedWidth"), requestedWidth},
        {QStringLiteral("cacheKey"), key}
    };
}

ArtworkRef ArtworkRef::fromCacheKey(const QString &key)
{
    constexpr auto prefix = "artwork:";
    if (!key.startsWith(QString::fromLatin1(prefix))) {
        return {};
    }
    const QByteArray encoded = key.mid(static_cast<qsizetype>(std::char_traits<char>::length(prefix)))
                                   .toLatin1();
    const QByteArray payload = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {};
    }
    return fromVariantMap(document.object().toVariantMap());
}

// Cache keys never restore sourceUrl. Persistent model metadata does not
// contain it; in-memory callers may provide it through a QVariantMap.

ArtworkRef ArtworkRef::fromVariantMap(const QVariantMap &map)
{
    ArtworkRef ref;
    ref.connectionId = map.value(QStringLiteral("connectionId")).toString();
    ref.itemId = map.value(QStringLiteral("itemId")).toString();
    ref.kind = artworkKindFromName(map.value(QStringLiteral("kind")).toString());
    ref.ownerKind = artworkOwnerKindFromName(
        map.value(QStringLiteral("ownerKind")).toString());
    ref.index = map.value(QStringLiteral("index"), 0).toInt();
    ref.tag = map.value(QStringLiteral("tag")).toString();
    ref.requestedWidth = map.value(QStringLiteral("requestedWidth"), 0).toInt();
    ref.sourceUrl = map.value(QStringLiteral("sourceUrl")).toString();
    return ref;
}

QVariantMap UserMediaState::toVariantMap() const
{
    return {
        {QStringLiteral("watched"), watched},
        {QStringLiteral("favorite"), favorite},
        {QStringLiteral("positionMs"), positionMs},
        {QStringLiteral("unplayedItemCount"), unplayedItemCount},
        {QStringLiteral("lastPlayedAt"), lastPlayedAt}
    };
}

QVariantMap Person::toVariantMap() const
{
    QVariantMap map{
        {QStringLiteral("media"), media.toVariantMap()},
        {QStringLiteral("personId"), media.itemId},
        {QStringLiteral("name"), name},
        {QStringLiteral("role"), role},
        {QStringLiteral("kind"), kind}
    };
    if (artwork.isValid()) {
        map[QStringLiteral("artwork")] = artwork.toVariantMap();
    }
    return map;
}

QVariantMap Chapter::toVariantMap() const
{
    QVariantMap map{
        {QStringLiteral("name"), name},
        {QStringLiteral("startMs"), startMs}
    };
    if (index >= 0) {
        map[QStringLiteral("index")] = index;
    }
    if (!fileId.isEmpty()) {
        map[QStringLiteral("fileId")] = fileId;
    }
    if (endMs > 0) {
        map[QStringLiteral("endMs")] = endMs;
    }
    if (!source.isEmpty()) {
        map[QStringLiteral("source")] = source;
    }
    if (!thumbnailUrl.isEmpty()) {
        map[QStringLiteral("thumbnailUrl")] = thumbnailUrl;
    }
    if (!thumbnailThumbhash.isEmpty()) {
        map[QStringLiteral("thumbnailThumbhash")] = thumbnailThumbhash;
    }
    if (artwork.isValid()) {
        map[QStringLiteral("artwork")] = artwork.toVariantMap();
    }
    return map;
}

QString playbackMethodName(PlaybackMethod method)
{
    switch (method) {
    case PlaybackMethod::DirectPlay: return QStringLiteral("directPlay");
    case PlaybackMethod::DirectStream: return QStringLiteral("directStream");
    case PlaybackMethod::Transcode: return QStringLiteral("transcode");
    case PlaybackMethod::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

bool StreamRequest::isValid() const
{
    return url.isValid()
        && !url.isEmpty()
        && method != PlaybackMethod::Unknown;
}

QVariantMap StreamRequest::toVariantMap() const
{
    return {
        {QStringLiteral("url"), url},
        {QStringLiteral("headers"), headers},
        {QStringLiteral("method"), playbackMethodName(method)},
        {QStringLiteral("pinsAudioTrack"), pinsAudioTrack},
        {QStringLiteral("pinsSubtitleTrack"), pinsSubtitleTrack},
        {QStringLiteral("pinnedAudioTrackId"), pinnedAudioTrackId},
        {QStringLiteral("pinnedSubtitleTrackId"), pinnedSubtitleTrackId}
    };
}

QVariantMap PlaybackTrack::toVariantMap() const
{
    return {
        {QStringLiteral("trackId"), trackId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("language"), language},
        {QStringLiteral("codec"), codec},
        {QStringLiteral("displayTitle"), displayTitle},
        {QStringLiteral("isDefault"), isDefault},
        {QStringLiteral("isForced"), isForced},
        {QStringLiteral("isExternal"), isExternal},
        {QStringLiteral("isHearingImpaired"), isHearingImpaired}
    };
}

QVariantMap PlaybackReportingCapabilities::toVariantMap() const
{
    return {
        {QStringLiteral("start"), start},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("pause"), pause},
        {QStringLiteral("stop"), stop}
    };
}

bool PlaybackDescriptor::isValid() const
{
    return media.isValid() && stream.isValid();
}

QVariantMap PlaybackDescriptor::toVariantMap() const
{
    QVariantList audio;
    for (const PlaybackTrack &track : audioTracks) {
        audio.append(track.toVariantMap());
    }
    QVariantList subtitles;
    for (const PlaybackTrack &track : subtitleTracks) {
        subtitles.append(track.toVariantMap());
    }
    QVariantList chapterMaps;
    for (const Chapter &chapter : chapters) {
        chapterMaps.append(chapter.toVariantMap());
    }

    return {
        {QStringLiteral("media"), media.toVariantMap()},
        {QStringLiteral("stream"), stream.toVariantMap()},
        {QStringLiteral("mediaVersionId"), mediaVersionId},
        {QStringLiteral("playbackSessionId"), playbackSessionId},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("startPositionMs"), startPositionMs},
        {QStringLiteral("audioTracks"), audio},
        {QStringLiteral("subtitleTracks"), subtitles},
        {QStringLiteral("selectedAudioTrackId"), selectedAudioTrackId},
        {QStringLiteral("selectedSubtitleTrackId"), selectedSubtitleTrackId},
        {QStringLiteral("chapters"), chapterMaps},
        {QStringLiteral("reporting"), reporting.toVariantMap()}
    };
}

} // namespace Bloom
