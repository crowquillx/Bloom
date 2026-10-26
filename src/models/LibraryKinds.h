#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

/**
 * @brief The library types Bloom supports: movies and series.
 *
 * Providers name library types differently (Jellyfin `movies` / `tvshows`,
 * Silo `movies` / `series`). Every other type (music, photos, home videos,
 * box sets, mixed folders, books) is left out of the app entirely, so views
 * are filtered here before any consumer sees them.
 */
namespace LibraryKinds {

inline const QString Movies = QStringLiteral("movies");
inline const QString Series = QStringLiteral("series");

/// "movies", "series", or an empty string for an unsupported type.
QString kindForCollectionType(const QString &collectionType);

/// Supported library views in their original order, each tagged with
/// `libraryKind` ("movies" or "series").
QVariantList supportedLibraries(const QVariantList &views);

/// Item IDs of the views whose type is not supported.
QStringList unsupportedLibraryIds(const QVariantList &views);

} // namespace LibraryKinds
