#include "LibraryKinds.h"

#include <QVariantMap>

namespace LibraryKinds {

QString kindForCollectionType(const QString &collectionType)
{
    const QString type = collectionType.trimmed().toLower();
    if (type == QStringLiteral("movies")) {
        return Movies;
    }
    if (type == QStringLiteral("tvshows") || type == QStringLiteral("series")) {
        return Series;
    }
    return {};
}

QVariantList supportedLibraries(const QVariantList &views)
{
    QVariantList supported;
    for (const QVariant &value : views) {
        QVariantMap view = value.toMap();
        const QString kind =
            kindForCollectionType(view.value(QStringLiteral("collectionType")).toString());
        if (kind.isEmpty()) {
            continue;
        }
        view.insert(QStringLiteral("libraryKind"), kind);
        supported.append(view);
    }
    return supported;
}

QStringList unsupportedLibraryIds(const QVariantList &views)
{
    QStringList ids;
    for (const QVariant &value : views) {
        const QVariantMap view = value.toMap();
        if (kindForCollectionType(view.value(QStringLiteral("collectionType")).toString()).isEmpty()) {
            const QString id = view.value(QStringLiteral("itemId")).toString();
            if (!id.isEmpty()) {
                ids.append(id);
            }
        }
    }
    return ids;
}

} // namespace LibraryKinds
