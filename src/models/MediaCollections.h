#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Canonical collections: a browse view over a library's movies and series,
// not a library kind. Providers map their wire collections (Jellyfin box
// sets, Silo library and personal collections) into maps with these
// camelCase keys:
//
//   media           MediaRef map of the collection
//   connectionId    the connection that owns the collection
//   itemId          the collection's ID (also collectionId)
//   collectionId    the collection's ID
//   collectionKind  how the provider reads its titles: `boxSet` (Jellyfin),
//                   `library` (a Silo curated collection) or `user` (a Silo
//                   personal collection)
//   mediaType       always `Collection`
//   name            display title
//   overview        description, empty when none
//   itemCount       how many titles the server counts, -1 when unknown
//   primaryArtwork, primaryArtworkUrl, backdropArtwork, backdropArtworkUrl,
//   artwork         the poster and backdrop, when the provider has them
//
// A collection's titles are ordinary canonical movie and series maps; every
// other type is dropped (LibraryKinds).
namespace Bloom::MediaCollections {

inline const QString kBoxSet = QStringLiteral("boxSet");
inline const QString kLibrary = QStringLiteral("library");
inline const QString kUser = QStringLiteral("user");
inline const QString kMediaType = QStringLiteral("Collection");

/// Canonical title sorts a collection offers, in menu order. The empty
/// sort is the collection's own order.
inline const QString kSortCollectionOrder;
inline const QString kSortReleaseDate = QStringLiteral("releaseDate");
inline const QString kSortTitle = QStringLiteral("title");

[[nodiscard]] inline QStringList sorts()
{
    return {kSortCollectionOrder, kSortReleaseDate, kSortTitle};
}

/// Whether a canonical item is a title a collection shows: a movie or a
/// series with an ID.
[[nodiscard]] inline bool isTitle(const QVariantMap &item)
{
    const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
    return (mediaType == QStringLiteral("Movie") || mediaType == QStringLiteral("Series"))
        && !item.value(QStringLiteral("itemId")).toString().isEmpty();
}

/// Whether a map is a usable canonical collection.
[[nodiscard]] inline bool isCollection(const QVariantMap &collection)
{
    return !collection.value(QStringLiteral("collectionId")).toString().isEmpty()
        && !collection.value(QStringLiteral("connectionId")).toString().isEmpty()
        && !collection.value(QStringLiteral("collectionKind")).toString().isEmpty();
}

} // namespace Bloom::MediaCollections
