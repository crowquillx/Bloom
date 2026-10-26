#include "CombinedLibraryCursor.h"

#include <QDateTime>

namespace {

QString titleKey(const QVariantMap &item)
{
    const QString sortName = item.value(QStringLiteral("sortName")).toString();
    return (sortName.isEmpty() ? item.value(QStringLiteral("name")).toString() : sortName)
        .toCaseFolded();
}

qint64 dateKey(const QVariantMap &item, const QString &field)
{
    const QString value = item.value(field).toString();
    if (value.isEmpty()) {
        return 0;
    }
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (parsed.isValid()) {
        return parsed.toMSecsSinceEpoch();
    }
    const QDateTime plain = QDateTime::fromString(value, Qt::ISODate);
    return plain.isValid() ? plain.toMSecsSinceEpoch() : 0;
}

/// -1, 0 or 1 comparing @p a and @p b on the primary sort key only.
int compareKey(const QVariantMap &a, const QVariantMap &b, const QString &sortBy)
{
    if (sortBy == QStringLiteral("releaseDate")) {
        const qint64 left = dateKey(a, QStringLiteral("premiereDate"));
        const qint64 right = dateKey(b, QStringLiteral("premiereDate"));
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    if (sortBy == QStringLiteral("dateAdded")) {
        const qint64 left = dateKey(a, QStringLiteral("dateAdded"));
        const qint64 right = dateKey(b, QStringLiteral("dateAdded"));
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    if (sortBy == QStringLiteral("rating")) {
        const double left = a.value(QStringLiteral("communityRating")).toDouble();
        const double right = b.value(QStringLiteral("communityRating")).toDouble();
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    if (sortBy == QStringLiteral("year")) {
        const int left = a.value(QStringLiteral("productionYear")).toInt();
        const int right = b.value(QStringLiteral("productionYear")).toInt();
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    return 0;
}

} // namespace

void CombinedLibraryCursor::reset(const QStringList &libraryIds, const QString &sortBy,
                                  const QString &sortOrder)
{
    m_order.clear();
    m_sources.clear();
    for (const QString &id : libraryIds) {
        if (!id.isEmpty() && !m_order.contains(id)) {
            m_order.append(id);
            m_sources.insert(id, Source{});
        }
    }
    m_sortBy = sortBy;
    m_descending = sortOrder.compare(QStringLiteral("descending"), Qt::CaseInsensitive) == 0;
    m_randomCursor = 0;
}

QStringList CombinedLibraryCursor::librariesNeedingPages()
{
    QStringList needed;
    for (const QString &id : std::as_const(m_order)) {
        Source &source = m_sources[id];
        if (!source.exhausted && source.buffer.isEmpty() && !source.requested) {
            source.requested = true;
            needed.append(id);
        }
    }
    return needed;
}

int CombinedLibraryCursor::nextOffset(const QString &libraryId) const
{
    return m_sources.value(libraryId).nextOffset;
}

void CombinedLibraryCursor::addPage(const QString &libraryId, const QVariantList &items, int total)
{
    if (!m_sources.contains(libraryId)) {
        return;
    }
    Source &source = m_sources[libraryId];
    source.requested = false;
    source.total = qMax(0, total);
    for (const QVariant &value : items) {
        source.buffer.append(value.toMap());
    }
    source.nextOffset += static_cast<int>(items.size());
    if (items.isEmpty() || source.nextOffset >= source.total) {
        source.exhausted = true;
    }
}

void CombinedLibraryCursor::pageFailed(const QString &libraryId)
{
    if (!m_sources.contains(libraryId)) {
        return;
    }
    Source &source = m_sources[libraryId];
    source.requested = false;
    source.exhausted = true;
    if (source.total < 0) {
        source.total = source.nextOffset;
    }
}

bool CombinedLibraryCursor::lessThan(const QVariantMap &a, const QVariantMap &b,
                                     const QString &sortBy, bool descending)
{
    const int primary = compareKey(a, b, sortBy);
    if (primary != 0) {
        return descending ? primary > 0 : primary < 0;
    }
    // Title breaks ties, and is the whole order for title and library order.
    const int title = QString::compare(titleKey(a), titleKey(b));
    return descending && (sortBy.isEmpty() || sortBy == QStringLiteral("title"))
        ? title > 0 : title < 0;
}

QVariantList CombinedLibraryCursor::take(int count)
{
    QVariantList taken;
    const bool random = m_sortBy == QStringLiteral("random");
    while (taken.size() < count) {
        // A library that still has items must have one buffered, or the next
        // item might be one of its unseen ones.
        bool waiting = false;
        QStringList candidates;
        for (const QString &id : std::as_const(m_order)) {
            const Source &source = m_sources.value(id);
            if (!source.buffer.isEmpty()) {
                candidates.append(id);
            } else if (!source.exhausted) {
                waiting = true;
            }
        }
        if (candidates.isEmpty() || (waiting && !random)) {
            break;
        }

        QString chosen;
        if (random) {
            // Shuffled pages interleave; there is no order to preserve.
            chosen = candidates.at(m_randomCursor++ % candidates.size());
        } else {
            chosen = candidates.first();
            for (const QString &id : std::as_const(candidates)) {
                if (lessThan(m_sources.value(id).buffer.first(),
                             m_sources.value(chosen).buffer.first(), m_sortBy, m_descending)) {
                    chosen = id;
                }
            }
        }
        taken.append(m_sources[chosen].buffer.takeFirst());
    }
    return taken;
}

int CombinedLibraryCursor::totalCount() const
{
    int total = 0;
    for (const QString &id : m_order) {
        total += qMax(0, m_sources.value(id).total);
    }
    return total;
}

bool CombinedLibraryCursor::finished() const
{
    for (const QString &id : m_order) {
        const Source &source = m_sources.value(id);
        if (!source.exhausted || !source.buffer.isEmpty()) {
            return false;
        }
    }
    return true;
}
