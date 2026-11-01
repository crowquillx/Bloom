#pragma once

#include "models/MediaExtras.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The extras shelf of one movie or series detail page. A canonical detail
// that carries `extras` (Silo) supplies them directly; otherwise the view
// model asks LibraryService::getItemExtras (Jellyfin) and hands the answer
// back here. Only answers for the current connection and item apply. A small
// in-memory cache shows a return visit's shelf at once while a background
// request refreshes it; partial answers are never cached.
class ItemExtrasState
{
public:
    struct LoadResult {
        bool changed = false;
        // Ask LibraryService::getItemExtras(itemId).
        bool request = false;
    };

    LoadResult load(const QString &connectionId, const QString &itemId, const QVariantMap &item)
    {
        LoadResult result;
        if (itemId.isEmpty()) {
            result.changed = clear();
            return result;
        }
        const bool sameItem = connectionId == m_connectionId && itemId == m_itemId;
        m_connectionId = connectionId;
        m_itemId = itemId;
        if (!sameItem) {
            m_pending = false;
        }
        if (item.contains(QStringLiteral("extras"))) {
            m_pending = false;
            result.changed = apply(item.value(QStringLiteral("extras")).toList());
            remember();
            return result;
        }
        // A cached shelf shows at once and is refreshed in the background,
        // so extras added or removed on the server since show up.
        const auto cached = m_cache.constFind(cacheKey());
        if (cached != m_cache.constEnd()) {
            result.changed = apply(*cached);
        } else if (!sameItem) {
            result.changed = apply({});
        }
        if (!m_pending) {
            m_pending = true;
            result.request = true;
        }
        return result;
    }

    // A getItemExtras answer; true when the shelf changed. A partial answer
    // (one provider request failed) shows but is not cached, so a later load
    // asks again for the full list.
    bool applyLoaded(const QString &connectionId,
                     const QString &itemId,
                     const QVariantList &extras,
                     bool complete = true)
    {
        if (connectionId != m_connectionId || itemId != m_itemId) {
            return false;
        }
        m_pending = false;
        const bool changed = apply(extras);
        if (complete) {
            remember();
        }
        return changed;
    }

    // A failed getItemExtras keeps whatever the shelf shows and allows a
    // later load to ask again.
    void applyFailed(const QString &connectionId, const QString &itemId)
    {
        if (connectionId == m_connectionId && itemId == m_itemId) {
            m_pending = false;
        }
    }

    bool clear()
    {
        m_connectionId.clear();
        m_itemId.clear();
        m_pending = false;
        return apply({});
    }

    QVariantList extras() const { return m_extras; }
    // A getItemExtras answer for the current item is still expected.
    bool loading() const { return m_pending; }
    QVariantMap trailer() const { return Bloom::MediaExtras::preferredTrailer(m_extras); }

private:
    static constexpr int kCacheLimit = 32;

    QString cacheKey() const { return m_connectionId + QLatin1Char('\n') + m_itemId; }

    bool apply(const QVariantList &extras)
    {
        if (extras == m_extras) {
            return false;
        }
        m_extras = extras;
        return true;
    }

    void remember()
    {
        const QString key = cacheKey();
        m_cacheOrder.removeAll(key);
        m_cacheOrder.append(key);
        m_cache.insert(key, m_extras);
        while (m_cacheOrder.size() > kCacheLimit) {
            m_cache.remove(m_cacheOrder.takeFirst());
        }
    }

    QString m_connectionId;
    QString m_itemId;
    QVariantList m_extras;
    bool m_pending = false;
    QHash<QString, QVariantList> m_cache;
    QStringList m_cacheOrder;
};
