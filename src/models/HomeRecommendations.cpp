#include "HomeRecommendations.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QVariantMap>

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

struct Row {
    QString sectionId;
    QString title;
    QVariantList items;
    QSet<QString> itemKeys;
};

} // namespace

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

QVariantList rows(const QList<QList<Category>> &categoriesByLibrary)
{
    QList<Row> built;
    QHash<QString, qsizetype> rowByIdentity;
    for (const QList<Category> &categories : categoriesByLibrary) {
        for (const Category &category : categories) {
            const QString rowTitle = title(category.recommendationType, category.baselineItemName);
            if (rowTitle.isEmpty()) {
                continue;
            }
            QVariantList movies;
            for (const QVariant &value : category.items) {
                const QVariantMap item = value.toMap();
                if (isMovie(item)) {
                    movies.append(item);
                }
            }
            if (movies.isEmpty()) {
                continue;
            }
            // The same reason and baseline from another library is one row.
            const QString identity = category.recommendationType + QLatin1Char(':')
                + category.baselineItemName.trimmed().toCaseFolded();
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
