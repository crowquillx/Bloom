#include "HomeRecommendations.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <QVersionNumber>

#include <algorithm>

namespace HomeRecommendations {

namespace {

QString itemKey(const QVariantMap &item)
{
    return item.value(QStringLiteral("connectionId")).toString() + QLatin1Char('\n')
        + item.value(QStringLiteral("itemId")).toString();
}

bool isMovie(const QVariantMap &item)
{
    return item.value(QStringLiteral("mediaType")).toString() == QStringLiteral("Movie")
        && !item.value(QStringLiteral("itemId")).toString().isEmpty();
}

QString identityOf(const Category &category)
{
    return category.recommendationType + QLatin1Char(':')
        + category.baselineItemName.trimmed().toCaseFolded();
}

// The category's item IDs, sorted: the same items in any order match.
QString itemListKey(const Category &category)
{
    QStringList ids;
    for (const QVariant &value : category.items) {
        const QString itemId = value.toMap().value(QStringLiteral("itemId")).toString();
        if (!itemId.isEmpty()) {
            ids.append(itemId);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids.join(QLatin1Char('\n'));
}

struct Row {
    QString sectionId;
    QString title;
    QVariantList items;
    QSet<QString> itemKeys;
};

} // namespace

bool isSimilarType(const QString &recommendationType)
{
    return recommendationType == QStringLiteral("SimilarToRecentlyPlayed")
        || recommendationType == QStringLiteral("SimilarToLikedItem");
}

bool similarRowsWork(const QString &serverVersion)
{
    const QVersionNumber version = QVersionNumber::fromString(serverVersion.trimmed());
    if (version.isNull()) {
        return false;
    }
    // jellyfin/jellyfin 97c20e6ac ("Fix movie recommendations", first in
    // v12.0) restored the baseline; v10.10 still passed SimilarTo.
    return version.majorVersion() >= 12
        || (version.majorVersion() == 10 && version.minorVersion() <= 10);
}

bool cacheFresh(qint64 fetchedAtMs, qint64 nowMs)
{
    return fetchedAtMs > 0 && nowMs >= fetchedAtMs && nowMs - fetchedAtMs < CacheLifetimeMs;
}

QStringList itemIds(const QList<QList<Category>> &categoriesByLibrary)
{
    QStringList result;
    QSet<QString> seen;
    for (const QList<Category> &categories : categoriesByLibrary) {
        for (const Category &category : categories) {
            for (const QVariant &value : category.items) {
                const QString itemId = value.toMap().value(QStringLiteral("itemId")).toString();
                if (!itemId.isEmpty() && !seen.contains(itemId)) {
                    seen.insert(itemId);
                    result.append(itemId);
                }
            }
        }
    }
    return result;
}

QString title(const QString &recommendationType, const QString &baselineItemName)
{
    const QString baseline = baselineItemName.trimmed();
    if (baseline.isEmpty()) {
        return {};
    }
    if (recommendationType == QStringLiteral("SimilarToRecentlyPlayed")) {
        return QCoreApplication::translate("HomeRecommendations", "Because you watched %1")
            .arg(baseline);
    }
    if (recommendationType == QStringLiteral("SimilarToLikedItem")) {
        return QCoreApplication::translate("HomeRecommendations", "More like %1").arg(baseline);
    }
    if (recommendationType == QStringLiteral("HasDirectorFromRecentlyPlayed")
        || recommendationType == QStringLiteral("HasLikedDirector")) {
        return QCoreApplication::translate("HomeRecommendations", "Directed by %1").arg(baseline);
    }
    if (recommendationType == QStringLiteral("HasActorFromRecentlyPlayed")
        || recommendationType == QStringLiteral("HasLikedActor")) {
        return QCoreApplication::translate("HomeRecommendations", "Starring %1").arg(baseline);
    }
    return {};
}

QVariantList rows(const QList<QList<Category>> &categoriesByLibrary, const Filter &filter)
{
    // The titles each item list appears under. A server that builds
    // "similar to" categories without their baseline answers the same
    // items for every one of them.
    QHash<QString, QSet<QString>> identitiesByItemList;
    for (const QList<Category> &categories : categoriesByLibrary) {
        for (const Category &category : categories) {
            const QString key = itemListKey(category);
            if (!key.isEmpty()) {
                identitiesByItemList[key].insert(identityOf(category));
            }
        }
    }

    QList<Row> built;
    QHash<QString, qsizetype> rowByIdentity;
    for (const QList<Category> &categories : categoriesByLibrary) {
        for (const Category &category : categories) {
            const QString rowTitle = title(category.recommendationType, category.baselineItemName);
            if (rowTitle.isEmpty()) {
                continue;
            }
            if (isSimilarType(category.recommendationType)
                && (!filter.similarRowsWork
                    || identitiesByItemList.value(itemListKey(category)).size() > 1)) {
                continue;
            }
            QVariantList movies;
            for (const QVariant &value : category.items) {
                const QVariantMap item = value.toMap();
                if (isMovie(item)
                    && (!filter.allowedItemIds
                        || filter.allowedItemIds->contains(
                            item.value(QStringLiteral("itemId")).toString()))) {
                    movies.append(item);
                }
            }
            if (movies.isEmpty()) {
                continue;
            }
            // The same reason and baseline from another library is one row.
            const QString identity = identityOf(category);
            auto existing = rowByIdentity.constFind(identity);
            if (existing == rowByIdentity.cend()) {
                if (built.size() >= MaxRows) {
                    continue;
                }
                built.append(Row{SectionIdPrefix + identity, rowTitle, {}, {}});
                existing = rowByIdentity.insert(identity, built.size() - 1);
            }
            Row &row = built[*existing];
            for (const QVariant &value : movies) {
                if (row.items.size() >= MaxItems) {
                    break;
                }
                const QString key = itemKey(value.toMap());
                if (!row.itemKeys.contains(key)) {
                    row.itemKeys.insert(key);
                    row.items.append(value);
                }
            }
        }
    }

    QVariantList result;
    for (const Row &row : built) {
        result.append(QVariantMap{
            {QStringLiteral("sectionId"), row.sectionId},
            {QStringLiteral("title"), row.title},
            {QStringLiteral("items"), row.items}});
    }
    return result;
}

} // namespace HomeRecommendations
