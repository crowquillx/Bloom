#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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
    static ArtworkRef fromCacheKey(const QString &key);
    static ArtworkRef fromVariantMap(const QVariantMap &map);
};

struct UserMediaState {
    bool watched = false;
    bool favorite = false;
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
    QList<Chapter> chapters;
    PlaybackReportingCapabilities reporting;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

} // namespace Bloom

Q_DECLARE_METATYPE(Bloom::MediaRef)
Q_DECLARE_METATYPE(Bloom::ArtworkRef)
Q_DECLARE_METATYPE(Bloom::PlaybackDescriptor)
