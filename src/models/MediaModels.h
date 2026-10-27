#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include "AudioTypes.h"

namespace Bloom {

enum class ArtworkKind {
    Primary,
    Thumb,
    Backdrop,
    Logo,
    Chapter,
    Person,
    Unknown
};

QString artworkKindName(ArtworkKind kind);
ArtworkKind artworkKindFromName(const QString &name);

enum class ArtworkOwnerKind {
    MediaItem,
    Library,
    Person,
    Chapter
};

QString artworkOwnerKindName(ArtworkOwnerKind kind);
ArtworkOwnerKind artworkOwnerKindFromName(const QString &name);

struct MediaRef {
    QString connectionId;
    QString itemId;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

struct ArtworkRef {
    QString connectionId;
    QString itemId;
    ArtworkKind kind = ArtworkKind::Unknown;
    ArtworkOwnerKind ownerKind = ArtworkOwnerKind::MediaItem;
    int index = 0;
    QString tag;
    int requestedWidth = 0;
    // Opaque, transient fetch location. It is retained only in memory and is
    // excluded from equality, cache identity, and serialized model metadata.
    QString sourceUrl;

    bool isValid() const;
    bool operator==(const ArtworkRef &other) const;
    QString cacheKey() const;
    QVariantMap toVariantMap() const;

    // Resolves the in-process source associated with a token-free cache key.
    // The value is intentionally unavailable after process restart.
    static QString transientSourceUrlForCacheKey(const QString &key);
    // Discards opaque sources when the authenticated account context resets.
    static void clearTransientSourceUrls();
    static ArtworkRef fromCacheKey(const QString &key);
    static ArtworkRef fromVariantMap(const QVariantMap &map);
};

struct UserMediaState {
    bool watched = false;
    bool favorite = false;
    // On the profile's watchlist (providers with a watchlist only).
    bool inWatchlist = false;
    qint64 positionMs = 0;
    int unplayedItemCount = 0;
    QString lastPlayedAt;

    QVariantMap toVariantMap() const;
};

struct Person {
    MediaRef media;
    QString name;
    QString role;
    QString kind;
    ArtworkRef artwork;

    QVariantMap toVariantMap() const;
};

struct Chapter {
    QString name;
    qint64 startMs = 0;
    ArtworkRef artwork;
    // Optional canonical metadata preserved by providers that expose richer
    // chapter envelopes (for example, file-scoped native chapters).
    int index = -1;
    QString fileId;
    qint64 endMs = 0;
    QString source;
    QString thumbnailUrl;
    QString thumbnailThumbhash;

    QVariantMap toVariantMap() const;
};

enum class PlaybackMethod {
    Unknown,
    DirectPlay,
    DirectStream,
    Transcode
};

QString playbackMethodName(PlaybackMethod method);

struct StreamRequest {
    QUrl url;
    QVariantMap headers;
    PlaybackMethod method = PlaybackMethod::Unknown;
    bool pinsAudioTrack = false;
    bool pinsSubtitleTrack = false;
    QString pinnedAudioTrackId;
    QString pinnedSubtitleTrackId;
    // Added to a player position to get the source position. Non-zero when a
    // server stream starts its own clock at a resume point.
    qint64 timelineOffsetMs = 0;
    // The player may seek only inside what the server has produced; other
    // seeks ask the provider for a stream anchored at the new position.
    bool seekRequiresReanchor = false;
    // False when the server packages a stream that carries only the tracks
    // listed below, in mpv order, instead of every source stream.
    bool carriesSourceTracks = true;
    QList<int> deliveredAudioTracks;
    QList<int> deliveredSubtitleTracks;
    // The pinned subtitle is rendered into the video; any other subtitle
    // choice needs a new stream.
    bool subtitleBurnedIn = false;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

struct PlaybackTrack {
    QString trackId;
    QString kind;
    QString language;
    QString codec;
    QString displayTitle;
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;
    bool isHearingImpaired = false;
    AudioSourceMetadata audio;
    // Transient executable location for external subtitle tracks. This is
    // retained in-process only and is not part of persistent model identity.
    QUrl externalUrl;
    // Provider source stream index this track delivers, when the source
    // metadata already lists it; -1 for tracks that exist only in the plan.
    int sourceIndex = -1;
    // The current stream cannot select this track; choosing it asks the
    // provider for a new stream that delivers it.
    bool requiresReplan = false;

    QVariantMap toVariantMap() const;
};

struct PlaybackReportingCapabilities {
    bool start = false;
    bool progress = false;
    bool pause = false;
    bool stop = false;

    QVariantMap toVariantMap() const;
};

struct PlaybackDescriptor {
    MediaRef media;
    StreamRequest stream;
    QString mediaVersionId;
    QString playbackSessionId;
    qint64 durationMs = 0;
    qint64 startPositionMs = 0;
    QList<PlaybackTrack> audioTracks;
    QList<PlaybackTrack> subtitleTracks;
    QString selectedAudioTrackId;
    QString selectedSubtitleTrackId;
    AudioOutputPlan audioOutputPlan;
    QList<Chapter> chapters;
    PlaybackReportingCapabilities reporting;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

} // namespace Bloom

Q_DECLARE_METATYPE(Bloom::MediaRef)
Q_DECLARE_METATYPE(Bloom::ArtworkRef)
Q_DECLARE_METATYPE(Bloom::PlaybackDescriptor)
