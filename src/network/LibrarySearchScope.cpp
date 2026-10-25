#include "LibrarySearchScope.h"

#include <QSet>
#include <QVariantMap>

namespace LibrarySearchScope {

QStringList searchableLibraryIds(const QVariantList &views, const QStringList &excludedIds)
{
    QStringList ids;
    for (const QVariant &value : views) {
        const QString id = value.toMap().value(QStringLiteral("itemId")).toString();
        if (!id.isEmpty() && !excludedIds.contains(id) && !ids.contains(id)) {
            ids.append(id);
        }
    }
    return ids;
}

QVariantList interleave(const QList<QVariantList> &perLibraryResults, int limit)
{
    QVariantList merged;
    QSet<QString> seen;
    qsizetype longest = 0;
    for (const QVariantList &results : perLibraryResults) {
        longest = qMax(longest, results.size());
    }
    for (qsizetype rank = 0; rank < longest; ++rank) {
        for (const QVariantList &results : perLibraryResults) {
            if (rank >= results.size()) {
                continue;
            }
            const QVariant &value = results.at(rank);
            const QString itemId = value.toMap().value(QStringLiteral("itemId")).toString();
            if (!itemId.isEmpty()) {
                if (seen.contains(itemId)) {
                    continue;
                }
                seen.insert(itemId);
            }
            merged.append(value);
            if (limit > 0 && merged.size() >= limit) {
                return merged;
            }
        }
    }
    return merged;
}

} // namespace LibrarySearchScope
