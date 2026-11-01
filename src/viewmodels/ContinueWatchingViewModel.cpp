#include "ContinueWatchingViewModel.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"

#include <QAtomicInteger>
#include <QTimer>
#include <algorithm>

namespace {

const QString kContinueWatching = QStringLiteral("continue_watching");
const QString kNextUp = QStringLiteral("next_up");

QString itemString(const QVariantMap &item, const QString &key)
{
    return item.value(key).toString();
}

} // namespace

ContinueWatchingViewModel::ContinueWatchingViewModel(QObject *parent)
    : ContinueWatchingViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

ContinueWatchingViewModel::ContinueWatchingViewModel(LibraryService *libraryService,
                                                     QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
{
    connectService();
}

void ContinueWatchingViewModel::connectService()
{
    if (!m_libraryService) {
        return;
    }
    connect(m_libraryService, &LibraryService::homeCardWriteFinished,
            this, &ContinueWatchingViewModel::onHomeCardWriteFinished);
    connect(m_libraryService, &LibraryService::itemPlayedStatusChanged,
            this, [this](const QString &itemId, bool played) {
        onPlayedChanged(itemId, played, true, QString());
    });
    connect(m_libraryService, &LibraryService::itemPlayedStatusFailed,
            this, [this](const QString &itemId, bool played, const QString &error) {
        onPlayedChanged(itemId, played, false, error);
    });
    connect(m_libraryService, &LibraryService::favoriteStatusChanged,
            this, [this](const QString &itemId, bool favorite) {
        onFavoriteChanged(itemId, favorite, true, QString());
    });
    connect(m_libraryService, &LibraryService::favoriteStatusFailed,
            this, [this](const QString &itemId, bool favorite, const QString &error) {
        onFavoriteChanged(itemId, favorite, false, error);
    });
    connect(m_libraryService, &LibraryService::watchlistStatusChanged,
            this, [this](const QString &itemId, bool inWatchlist) {
        onWatchlistChanged(itemId, inWatchlist, true, QString());
    });
    connect(m_libraryService, &LibraryService::watchlistStatusFailed,
            this, [this](const QString &itemId, bool inWatchlist, const QString &error) {
        onWatchlistChanged(itemId, inWatchlist, false, error);
    });
}

void ContinueWatchingViewModel::setMergeRows(bool mergeRows)
{
    if (m_mergeRows == mergeRows) {
        return;
    }
    m_mergeRows = mergeRows;
    emit mergeRowsChanged();
    rebuild();
}

QString ContinueWatchingViewModel::undoConnectionId() const
{
    return m_undo ? m_undo->connectionId : QString();
}

QString ContinueWatchingViewModel::surfaceOf(const QVariantMap &item)
{
    return item.value(QStringLiteral("positionMs")).toLongLong() > 0 ? kContinueWatching
                                                                     : kNextUp;
}

void ContinueWatchingViewModel::setItems(const QVariantList &items)
{
    m_items = items;
    // The list was read after the confirmed writes (Home reads again when
    // one is confirmed), so it is the server's word on those cards.
    for (auto it = m_hides.begin(); it != m_hides.end();) {
        if (it->confirmed) {
            it = m_hides.erase(it);
        } else {
            ++it;
        }
    }
    rebuild();
}

void ContinueWatchingViewModel::clear()
{
    const bool hadUndo = m_undo.has_value();
    m_items.clear();
    m_hides.clear();
    m_pending.clear();
    m_undo.reset();
    rebuild();
    if (hadUndo) {
        emit undoChanged();
    }
}

QVariantList ContinueWatchingViewModel::actionsFor(const QVariantMap &item) const
{
    QVariantList actions;
    const auto add = [&actions](const QString &id, const QString &text, bool enabled,
                                bool navigation = false) {
        actions.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("text"), text},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("navigation"), navigation}
        });
    };

    const HomeCardCapabilities capabilities = m_libraryService
        ? m_libraryService->homeCardCapabilities(itemString(item, QStringLiteral("connectionId")))
        : HomeCardCapabilities{};
    const QString mediaType = itemString(item, QStringLiteral("mediaType"));
    const bool isMovie = mediaType == QStringLiteral("Movie");
    const bool isEpisode = mediaType == QStringLiteral("Episode");
    const QString seriesId = itemString(item, QStringLiteral("seriesId"));

    if (isEpisode) {
        add(QStringLiteral("openSeries"), tr("Go to series"), !seriesId.isEmpty(), true);
    } else {
        add(QStringLiteral("openDetails"), tr("Go to details"), true, true);
    }

    if (surfaceOf(item) == kContinueWatching) {
        if (capabilities.dismissals) {
            add(QStringLiteral("remove"), tr("Remove from Continue Watching"),
                !itemString(item, QStringLiteral("progressUpdatedAt")).isEmpty());
        } else if (capabilities.resumeReset) {
            add(QStringLiteral("remove"), tr("Remove from Continue Watching"), true);
        }
    } else if (capabilities.dismissals) {
        add(QStringLiteral("hide"), tr("Hide from Up Next"), !seriesId.isEmpty());
    }

    if (item.value(QStringLiteral("watched")).toBool()) {
        add(QStringLiteral("markUnwatched"), tr("Mark as unwatched"), true);
    } else {
        add(QStringLiteral("markWatched"), tr("Mark as watched"), true);
    }

    if (isMovie) {
        if (item.value(QStringLiteral("favorite")).toBool()) {
            add(QStringLiteral("unfavorite"), tr("Remove from favorites"), true);
        } else {
            add(QStringLiteral("favorite"), tr("Add to favorites"), true);
        }
        if (capabilities.watchlist) {
            if (item.value(QStringLiteral("inWatchlist")).toBool()) {
                add(QStringLiteral("watchlistRemove"), tr("Remove from watchlist"), true);
            } else {
                add(QStringLiteral("watchlistAdd"), tr("Add to watchlist"), true);
            }
        }
    }
    return actions;
}

bool ContinueWatchingViewModel::trigger(const QString &actionId, const QVariantMap &item)
{
    if (!m_libraryService) {
        return false;
    }
    bool offered = false;
    for (const QVariant &value : actionsFor(item)) {
        const QVariantMap action = value.toMap();
        if (action.value(QStringLiteral("id")).toString() == actionId) {
            offered = action.value(QStringLiteral("enabled")).toBool()
                && !action.value(QStringLiteral("navigation")).toBool();
            break;
        }
    }
    const QString connectionId = itemString(item, QStringLiteral("connectionId"));
    const QString itemId = itemString(item, QStringLiteral("itemId"));
    if (!offered || itemId.isEmpty()
        || !pendingKeyFor(connectionId, itemId).isEmpty()) {
        return false;
    }
    // Writes run on the live connection; merged cards are routed there
    // before they get here.
    if (!connectionId.isEmpty() && connectionId != liveConnectionId()) {
        emit actionFailed(tr("Couldn't reach the server for %1.").arg(titleOf(item)));
        return false;
    }

    Pending pending;
    pending.actionId = actionId;
    pending.connectionId = connectionId;
    pending.itemId = itemId;
    pending.title = titleOf(item);

    if (actionId == QStringLiteral("remove") || actionId == QStringLiteral("hide")
        || actionId == QStringLiteral("markWatched")) {
        const HomeCardCapabilities capabilities =
            m_libraryService->homeCardCapabilities(connectionId);
        const QString seriesId = itemString(item, QStringLiteral("seriesId"));
        Hide hide;
        hide.connectionId = connectionId;
        hide.itemId = itemId;
        if (actionId != QStringLiteral("markWatched") && capabilities.dismissals) {
            pending.wireSurface = actionId == QStringLiteral("remove") ? kContinueWatching
                                                                       : kNextUp;
            // Silo drops the whole show from both rows for an episode.
            pending.dropsSeries = itemString(item, QStringLiteral("mediaType"))
                    == QStringLiteral("Episode")
                && !seriesId.isEmpty();
            if (pending.dropsSeries) {
                hide.seriesId = seriesId;
            }
            pending.undoable = true;
        } else if (actionId == QStringLiteral("remove")) {
            // A cleared resume position may bring the item back to Up Next.
            hide.surface = kContinueWatching;
        }
        pending.hides = true;
        pending.snapshots = snapshotsFor(hide);
        const QString key = startPending(pending);
        m_hides.insert(key, hide);
        rebuild();
        if (actionId == QStringLiteral("markWatched")) {
            m_libraryService->markItemPlayed(itemId);
        } else if (pending.wireSurface.isEmpty()) {
            m_libraryService->clearResumePosition(key, itemId);
        } else {
            m_libraryService->setHomeItemDismissed(
                key, pending.wireSurface, itemId, seriesId,
                itemString(item, QStringLiteral("progressUpdatedAt")), true);
        }
        return true;
    }

    QVariant value;
    if (actionId == QStringLiteral("markUnwatched")) {
        pending.field = QStringLiteral("watched");
        value = false;
    } else if (actionId == QStringLiteral("favorite")
               || actionId == QStringLiteral("unfavorite")) {
        pending.field = QStringLiteral("favorite");
        value = actionId == QStringLiteral("favorite");
    } else if (actionId == QStringLiteral("watchlistAdd")
               || actionId == QStringLiteral("watchlistRemove")) {
        pending.field = QStringLiteral("inWatchlist");
        value = actionId == QStringLiteral("watchlistAdd");
    } else {
        return false;
    }
    pending.previousValue = item.value(pending.field, false);
    startPending(pending);
    setField(connectionId, itemId, pending.field, value);
    if (actionId == QStringLiteral("markUnwatched")) {
        m_libraryService->markItemUnplayed(itemId);
    } else if (pending.field == QStringLiteral("favorite")) {
        if (value.toBool()) {
            m_libraryService->markItemFavorite(itemId);
        } else {
            m_libraryService->markItemUnfavorite(itemId);
        }
    } else {
        m_libraryService->setItemWatchlisted(itemId, value.toBool());
    }
    return true;
}

bool ContinueWatchingViewModel::undo()
{
    if (!m_undo || !m_libraryService) {
        return false;
    }
    if (m_undo->connectionId != liveConnectionId()) {
        emit actionFailed(tr("Couldn't reach the server for %1.").arg(m_undo->title));
        return false;
    }
    Undo undo = *m_undo;
    m_undo.reset();
    emit undoChanged();

    // Show the cards again, also when a newer list already left them out.
    m_hides.remove(undo.hideKey);
    for (const Snapshot &snapshot : undo.snapshots) {
        const QString itemId = itemString(snapshot.item, QStringLiteral("itemId"));
        const QString connectionId = itemString(snapshot.item, QStringLiteral("connectionId"));
        const bool listed = std::any_of(m_items.cbegin(), m_items.cend(),
                                        [&](const QVariant &value) {
            const QVariantMap item = value.toMap();
            return itemString(item, QStringLiteral("itemId")) == itemId
                && itemString(item, QStringLiteral("connectionId")) == connectionId;
        });
        if (!listed) {
            m_items.insert(std::clamp<qsizetype>(snapshot.index, 0, m_items.size()),
                           snapshot.item);
        }
    }
    rebuild();

    Pending pending;
    pending.actionId = QStringLiteral("undo");
    pending.connectionId = undo.connectionId;
    pending.itemId = undo.itemId;
    pending.title = undo.title;
    pending.isUndo = true;
    pending.undo = undo;
    const QString key = startPending(pending);
    m_libraryService->setHomeItemDismissed(key, undo.surface, undo.itemId, QString(),
                                           QString(), false);
    return true;
}

void ContinueWatchingViewModel::dismissUndo()
{
    if (m_undo) {
        m_undo.reset();
        emit undoChanged();
    }
}

void ContinueWatchingViewModel::rebuild()
{
    m_visible.clear();
    m_continueWatching.clear();
    m_nextUp.clear();
    for (const QVariant &value : std::as_const(m_items)) {
        const QVariantMap item = value.toMap();
        if (isHidden(item)) {
            continue;
        }
        m_visible.append(value);
        if (surfaceOf(item) == kContinueWatching) {
            m_continueWatching.append(value);
        } else if (!m_mergeRows) {
            m_nextUp.append(value);
        }
    }
    if (m_mergeRows) {
        m_nextUp = m_visible;
    }
    emit itemsChanged();
}

bool ContinueWatchingViewModel::matches(const Hide &hide, const QVariantMap &item)
{
    if (itemString(item, QStringLiteral("connectionId")) != hide.connectionId) {
        return false;
    }
    if (!hide.surface.isEmpty() && surfaceOf(item) != hide.surface) {
        return false;
    }
    return itemString(item, QStringLiteral("itemId")) == hide.itemId
        || (!hide.seriesId.isEmpty()
            && itemString(item, QStringLiteral("seriesId")) == hide.seriesId);
}

bool ContinueWatchingViewModel::isHidden(const QVariantMap &item) const
{
    return std::any_of(m_hides.cbegin(), m_hides.cend(), [&item](const Hide &hide) {
        return matches(hide, item);
    });
}

QList<ContinueWatchingViewModel::Snapshot> ContinueWatchingViewModel::snapshotsFor(
    const Hide &hide) const
{
    QList<Snapshot> snapshots;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        const QVariantMap item = m_items.at(i).toMap();
        if (matches(hide, item)) {
            snapshots.append({static_cast<int>(i), item});
        }
    }
    return snapshots;
}

void ContinueWatchingViewModel::setField(const QString &connectionId, const QString &itemId,
                                         const QString &field, const QVariant &value)
{
    for (QVariant &entry : m_items) {
        QVariantMap item = entry.toMap();
        if (itemString(item, QStringLiteral("connectionId")) != connectionId
            || itemString(item, QStringLiteral("itemId")) != itemId) {
            continue;
        }
        item.insert(field, value);
        QVariantMap userState = item.value(QStringLiteral("userState")).toMap();
        if (userState.contains(field)) {
            userState.insert(field, value);
            item.insert(QStringLiteral("userState"), userState);
        }
        entry = item;
    }
    rebuild();
}

QString ContinueWatchingViewModel::pendingKeyFor(const QString &connectionId,
                                                 const QString &itemId) const
{
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->connectionId == connectionId && it->itemId == itemId) {
            return it.key();
        }
    }
    return {};
}

QString ContinueWatchingViewModel::startPending(Pending pending)
{
    const QString key = newRequestKey();
    m_pending.insert(key, std::move(pending));
    QTimer::singleShot(m_pendingTimeoutMs, this, [this, key]() { expire(key); });
    return key;
}

void ContinueWatchingViewModel::finish(const QString &key, bool succeeded, const QString &error)
{
    const auto it = m_pending.find(key);
    if (it == m_pending.end()) {
        return;
    }
    const Pending pending = it.value();
    m_pending.erase(it);

    if (pending.isUndo) {
        if (succeeded) {
            emit refreshRequested();
            return;
        }
        // The server still hides the cards.
        Hide hide = pending.undo->hide;
        hide.confirmed = true;
        m_hides.insert(key, hide);
        rebuild();
        emit actionFailed(tr("Couldn't undo removing %1. %2").arg(pending.title, error));
        return;
    }

    if (pending.hides) {
        if (!succeeded) {
            m_hides.remove(key);
            rebuild();
            emit actionFailed(pending.actionId == QStringLiteral("markWatched")
                                  ? tr("Couldn't mark %1 as watched. %2").arg(pending.title, error)
                                  : tr("Couldn't remove %1. %2").arg(pending.title, error));
            return;
        }
        auto hide = m_hides.find(key);
        if (hide != m_hides.end()) {
            hide->confirmed = true;
        }
        if (pending.undoable && hide != m_hides.end()) {
            Undo undo;
            undo.hideKey = key;
            undo.connectionId = pending.connectionId;
            undo.itemId = pending.itemId;
            undo.surface = pending.wireSurface;
            undo.title = pending.title;
            undo.hide = hide.value();
            undo.snapshots = pending.snapshots;
            m_undo = undo;
            emit undoChanged();
            emit undoOffered(pending.dropsSeries
                                 ? tr("Removed %1 from Continue Watching and Up Next")
                                       .arg(pending.title)
                                 : tr("Removed %1 from Continue Watching").arg(pending.title));
        }
        emit refreshRequested();
        return;
    }

    if (!succeeded) {
        setField(pending.connectionId, pending.itemId, pending.field, pending.previousValue);
        emit actionFailed(tr("Couldn't update %1. %2").arg(pending.title, error));
        return;
    }
    if (pending.actionId == QStringLiteral("markUnwatched")) {
        emit refreshRequested();
    }
}

void ContinueWatchingViewModel::expire(const QString &key)
{
    const auto it = m_pending.find(key);
    if (it == m_pending.end()) {
        return;
    }
    const bool hides = it->hides;
    m_pending.erase(it);
    // No answer is coming; the next list shows the server's state.
    if (hides && m_hides.remove(key) > 0) {
        rebuild();
    }
    emit refreshRequested();
}

QString ContinueWatchingViewModel::findPending(const QString &actionId,
                                               const QString &itemId) const
{
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->actionId == actionId && it->itemId == itemId) {
            return it.key();
        }
    }
    return {};
}

QString ContinueWatchingViewModel::liveConnectionId() const
{
    return m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
}

QString ContinueWatchingViewModel::titleOf(const QVariantMap &item)
{
    const QString seriesName = itemString(item, QStringLiteral("seriesName"));
    return seriesName.isEmpty() ? itemString(item, QStringLiteral("name")) : seriesName;
}

QString ContinueWatchingViewModel::newRequestKey()
{
    // Unique across every instance: two Homes may briefly coexist.
    static QAtomicInteger<quint64> counter;
    return QStringLiteral("home-card-%1").arg(counter.fetchAndAddRelaxed(1) + 1);
}

void ContinueWatchingViewModel::onHomeCardWriteFinished(const QString &,
                                                        const QString &requestKey,
                                                        bool succeeded,
                                                        const QString &error)
{
    finish(requestKey, succeeded, error);
}

void ContinueWatchingViewModel::onPlayedChanged(const QString &itemId, bool played,
                                                bool succeeded, const QString &error)
{
    const QString key = findPending(played ? QStringLiteral("markWatched")
                                           : QStringLiteral("markUnwatched"),
                                    itemId);
    if (!key.isEmpty()) {
        finish(key, succeeded, error);
    }
}

void ContinueWatchingViewModel::onFavoriteChanged(const QString &itemId, bool favorite,
                                                  bool succeeded, const QString &error)
{
    const QString key = findPending(favorite ? QStringLiteral("favorite")
                                             : QStringLiteral("unfavorite"),
                                    itemId);
    if (!key.isEmpty()) {
        finish(key, succeeded, error);
    }
}

void ContinueWatchingViewModel::onWatchlistChanged(const QString &itemId, bool inWatchlist,
                                                   bool succeeded, const QString &error)
{
    const QString key = findPending(inWatchlist ? QStringLiteral("watchlistAdd")
                                                : QStringLiteral("watchlistRemove"),
                                    itemId);
    if (!key.isEmpty()) {
        finish(key, succeeded, error);
    }
}
