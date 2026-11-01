#include "ContinueWatching.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <algorithm>

namespace {

struct InProgressItem {
    QVariant item;
    qint64 lastPlayedMs = -1;
};

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

namespace ContinueWatching {

QVariantList order(const QVariantList &items)
{
    QList<InProgressItem> inProgress;
    QVariantList upNext;
    QSet<QString> seen;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString().isEmpty()) {
            continue;
        }
        const QString key = mediaRefKey(item);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        if (item.value(QStringLiteral("positionMs")).toLongLong() > 0) {
            inProgress.append({value, lastPlayedMs(item)});
        } else {
            upNext.append(value);
        }
    }
    std::stable_sort(inProgress.begin(), inProgress.end(),
                     [](const InProgressItem &a, const InProgressItem &b) {
        return a.lastPlayedMs > b.lastPlayedMs;
    });

    QVariantList ordered;
    ordered.reserve(inProgress.size() + upNext.size());
    for (const InProgressItem &entry : std::as_const(inProgress)) {
        ordered.append(entry.item);
    }
    ordered.append(upNext);
    return ordered;
}

QVariantList combine(const QVariantList &resume, const QVariantList &nextUp)
{
    return order(resume + nextUp);
}

} // namespace ContinueWatching
