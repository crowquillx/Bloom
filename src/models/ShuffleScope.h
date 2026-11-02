#pragma once

#include <QString>
#include <QVariantMap>

/**
 * @brief What a shuffle plays from, in canonical names.
 *
 * A scope is `{kind, id}`: a movie or series library (`library`, the library
 * ID), a series (`series`), one season (`season`), or a collection
 * (`collection` with a `collectionKind` of `boxSet` for a Jellyfin box set,
 * `library` for a Silo curated collection, `user` for a Silo personal one).
 * Providers map it to their wire scope; QML and the player use only these
 * names. A shuffle plays the scope's movies and episodes, never extras.
 */
namespace Bloom::ShuffleScope {

inline const QString kLibrary = QStringLiteral("library");
inline const QString kSeries = QStringLiteral("series");
inline const QString kSeason = QStringLiteral("season");
inline const QString kCollection = QStringLiteral("collection");

inline const QString kBoxSet = QStringLiteral("boxSet");
inline const QString kLibraryCollection = QStringLiteral("library");
inline const QString kUserCollection = QStringLiteral("user");

struct Scope {
    QString kind;
    QString id;
    // Collections only.
    QString collectionKind;
    // Display name of the scope ("Breaking Bad", "Season 2", "Movies").
    QString title;
    // Connection that owns the scope; empty means the active one.
    QString connectionId;

    bool isValid() const
    {
        if (id.trimmed().isEmpty()) {
            return false;
        }
        if (kind == kCollection) {
            return collectionKind == kBoxSet || collectionKind == kLibraryCollection
                || collectionKind == kUserCollection;
        }
        return kind == kLibrary || kind == kSeries || kind == kSeason;
    }
};

/// The scope QML passes: `{kind, id, collectionKind, title, connectionId}`.
inline Scope fromVariantMap(const QVariantMap &map)
{
    Scope scope;
    scope.kind = map.value(QStringLiteral("kind")).toString().trimmed();
    scope.id = map.value(QStringLiteral("id")).toString().trimmed();
    if (scope.kind == kCollection) {
        scope.collectionKind = map.value(QStringLiteral("collectionKind")).toString().trimmed();
    }
    scope.title = map.value(QStringLiteral("title")).toString();
    scope.connectionId = map.value(QStringLiteral("connectionId")).toString();
    return scope;
}

/// `kind`, or `collection:<collectionKind>` for collections: the key a
/// provider's capability lists scope kinds by.
inline QString capabilityKey(const QString &kind, const QString &collectionKind)
{
    return kind == kCollection ? kind + QLatin1Char(':') + collectionKind : kind;
}

} // namespace Bloom::ShuffleScope
