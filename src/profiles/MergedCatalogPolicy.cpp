#include "MergedCatalogPolicy.h"

#include <QDateTime>
#include <algorithm>

namespace {

struct RankedItem {
    QVariantMap item;
    int priority = 0;
    int rank = 0;
    qint64 lastPlayedMs = -1;
};

QList<MergedSourceResult> healthySourcesByPriority(const QList<MergedSourceResult> &sources)
{
    QList<MergedSourceResult> healthy;
    for (const MergedSourceResult &source : sources) {
        if (source.succeeded) {
            healthy.append(source);
        }
    }
    std::stable_sort(healthy.begin(), healthy.end(),
                     [](const MergedSourceResult &a, const MergedSourceResult &b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.connectionId < b.connectionId;
    });
    return healthy;
}

qint64 lastPlayedMs(const QVariantMap &item)
{
    const QString value = item.value(QStringLiteral("userState")).toMap()
                              .value(QStringLiteral("lastPlayedAt")).toString();
    if (value.isEmpty()) {
        return -1;
    }
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    return parsed.isValid() ? parsed.toMSecsSinceEpoch() : -1;
}

QString mediaRefKey(const QVariantMap &item)
{
    return item.value(QStringLiteral("connectionId")).toString() + QLatin1Char('\n')
        + item.value(QStringLiteral("itemId")).toString();
}

} // namespace

QVariantMap DefaultMergedCatalogPolicy::withSourceLabel(QVariantMap item, const QString &label)
{
    item.insert(QStringLiteral("sourceLabel"), label);
    return item;
}

QVariantList DefaultMergedCatalogPolicy::merge(MergedCatalogOperation operation,
                                               const QList<MergedSourceResult> &sources) const
{
    const QList<MergedSourceResult> healthy = healthySourcesByPriority(sources);
    QVariantList merged;

    switch (operation) {
    case MergedCatalogOperation::Views:
        for (const MergedSourceResult &source : healthy) {
            for (const QVariant &value : source.items) {
                merged.append(withSourceLabel(value.toMap(), source.label));
            }
        }
        return merged;

    case MergedCatalogOperation::Search: {
        qsizetype longest = 0;
        for (const MergedSourceResult &source : healthy) {
            longest = std::max(longest, source.items.size());
        }
        for (qsizetype rank = 0; rank < longest; ++rank) {
            for (const MergedSourceResult &source : healthy) {
                if (rank < source.items.size()) {
                    merged.append(withSourceLabel(source.items.at(rank).toMap(), source.label));
                }
            }
        }
        return merged;
    }

    case MergedCatalogOperation::NextUp: {
        QList<RankedItem> ranked;
        for (const MergedSourceResult &source : healthy) {
            int rank = 0;
            for (const QVariant &value : source.items) {
                RankedItem entry;
                entry.item = withSourceLabel(value.toMap(), source.label);
                entry.priority = source.priority;
                entry.rank = rank++;
                entry.lastPlayedMs = lastPlayedMs(entry.item);
                ranked.append(entry);
            }
        }
        // Requested sort key (recency), then member priority, then the
        // provider's own order, then MediaRef for a total order.
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const RankedItem &a, const RankedItem &b) {
            if (a.lastPlayedMs != b.lastPlayedMs) {
                return a.lastPlayedMs > b.lastPlayedMs;
            }
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            if (a.rank != b.rank) {
                return a.rank < b.rank;
            }
            return mediaRefKey(a.item) < mediaRefKey(b.item);
        });
        for (const RankedItem &entry : ranked) {
            merged.append(entry.item);
        }
        return merged;
    }
    }
    return merged;
}
