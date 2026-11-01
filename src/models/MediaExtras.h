#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Canonical local extras (trailers, featurettes, deleted scenes, ...) of a
// movie or series. Providers map their wire extras into maps with these
// camelCase keys:
//
//   media         MediaRef map of the extra itself
//   connectionId  the connection that owns the extra and its parent
//   itemId        the extra's own playable ID
//   parentId      the movie or series it belongs to
//   name          display title (falls back to the kind label in the UI)
//   extraKind     one of the kinds below
//   durationMs    runtime in milliseconds, 0 when unknown
//   primaryArtwork, primaryArtworkUrl, artwork, artworkThumbhashes
//                 the 16:9 still, when the provider has one
//
// Only local files are extras. Remote (YouTube) trailers are not mapped:
// Bloom's mpv runs without ytdl.
namespace Bloom::MediaExtras {

inline const QString kTrailer = QStringLiteral("trailer");
inline const QString kTeaser = QStringLiteral("teaser");
inline const QString kFeaturette = QStringLiteral("featurette");
inline const QString kClip = QStringLiteral("clip");
inline const QString kBehindTheScenes = QStringLiteral("behindTheScenes");
inline const QString kBloopers = QStringLiteral("bloopers");
inline const QString kDeletedScene = QStringLiteral("deletedScene");
inline const QString kInterview = QStringLiteral("interview");
inline const QString kScene = QStringLiteral("scene");
inline const QString kShort = QStringLiteral("short");
inline const QString kOther = QStringLiteral("other");

/// Every canonical kind.
[[nodiscard]] QStringList kinds();
/// A known kind stays; anything else becomes `other`.
[[nodiscard]] QString normalizedKind(const QString &kind);
/// The extra "Play Trailer" plays: the first trailer, else the first teaser,
/// else an empty map.
[[nodiscard]] QVariantMap preferredTrailer(const QVariantList &extras);

} // namespace Bloom::MediaExtras
