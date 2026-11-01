#include "ContinueWatchingViewModel.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"

#include <QAtomicInteger>
#include <QSet>
#include <algorithm>

namespace {

const QString kContinueWatching = QStringLiteral("continue_watching");
const QString kNextUp = QStringLiteral("next_up");

QString itemString(const QVariantMap &item, const QString &key)
{
    return item.value(key).toString();
}

bool sameCard(const QVariantMap &item, const QString &connectionId, const QString &itemId)
{
    return itemString(item, QStringLiteral("connectionId")) == connectionId
        && itemString(item, QStringLiteral("itemId")) == itemId;
}

} // namespace

HomeCardListModel::HomeCardListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HomeCardListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant HomeCardListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()
        || role != ModelDataRole) {
        return {};
    }
    return m_items.at(index.row());
}

QHash<int, QByteArray> HomeCardListModel::roleNames() const
{
    return {{ModelDataRole, "modelData"}};
}

QVariantMap HomeCardListModel::get(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index).toMap() : QVariantMap();
}

QString HomeCardListModel::keyOf(const QVariant &item)
{
    const QVariantMap map = item.toMap();
    const QString itemId = itemString(map, QStringLiteral("itemId"));
    return itemId.isEmpty()
        ? QString()
        : itemString(map, QStringLiteral("connectionId")) + QLatin1Char('\n') + itemId;
}

void HomeCardListModel::setItems(const QVariantList &items)
{
    const int oldCount = count();
    // Rows are matched by key; a list without unique keys is reset.
    QStringList keys;
    keys.reserve(items.size());
    QSet<QString> newKeys;
    bool unique = true;
    for (const QVariant &item : items) {
        const QString key = keyOf(item);
        unique = unique && !key.isEmpty() && !newKeys.contains(key);
        newKeys.insert(key);
        keys.append(key);
    }
    QSet<QString> oldKeys;
    for (const QVariant &item : std::as_const(m_items)) {
        const QString key = keyOf(item);
        unique = unique && !key.isEmpty() && !oldKeys.contains(key);
        oldKeys.insert(key);
    }
    if (!unique) {
        beginResetModel();
        m_items = items;
        endResetModel();
        if (count() != oldCount) {
            emit countChanged();
        }
        return;
    }

    for (qsizetype i = 0; i < items.size(); ++i) {
        while (i < m_items.size() && !newKeys.contains(keyOf(m_items.at(i)))) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            m_items.removeAt(i);
            endRemoveRows();
        }
        // Rows before i already match the new list.
        qsizetype from = -1;
        for (qsizetype j = i; j < m_items.size(); ++j) {
            if (keyOf(m_items.at(j)) == keys.at(i)) {
                from = j;
                break;
            }
        }
        if (from < 0) {
            beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
            m_items.insert(i, items.at(i));
            endInsertRows();
            continue;
        }
        if (from != i) {
            beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {},
                          static_cast<int>(i));
            m_items.move(from, i);
            endMoveRows();
        }
        if (m_items.at(i) != items.at(i)) {
            m_items[i] = items.at(i);
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed, {ModelDataRole});
        }
    }
    if (m_items.size() > items.size()) {
        beginRemoveRows({}, static_cast<int>(items.size()), static_cast<int>(m_items.size() - 1));
        m_items.erase(m_items.begin() + items.size(), m_items.end());
        endRemoveRows();
    }
    if (count() != oldCount) {
        emit countChanged();
    }
}

ContinueWatchingViewModel::ContinueWatchingViewModel(QObject *parent)
    : ContinueWatchingViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

ContinueWatchingViewModel::ContinueWatchingViewModel(LibraryService *libraryService,
                                                     QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
    , m_continueWatchingModel(new HomeCardListModel(this))
    , m_nextUpModel(new HomeCardListModel(this))
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
    // A list read before a write in flight landed still has the old state:
    // keep the value sent, and keep an undo's cards shown.
    for (const Pending &pending : std::as_const(m_pending)) {
        if (!pending.field.isEmpty()) {
            applyField(pending.connectionId, pending.itemId, pending.field, pending.value);
        }
        if (pending.isUndo && pending.undo) {
            restoreSnapshots(pending.undo->snapshots);
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
    const QString title = titleOf(item);

    if (isEpisode) {
        add(QStringLiteral("openSeries"), tr("Go to series"), !seriesId.isEmpty(), true);
    } else {
        add(QStringLiteral("openDetails"), tr("Go to details"), true, true);
    }

    // Silo drops the whole show from both rows, and on the profile's watch
    // providers, for an episode on either surface.
    const bool dropsShow = capabilities.dismissals && isEpisode;
    const auto addDismissal = [&](const QString &id, const QString &text, bool enabled) {
        add(id, dropsShow ? tr("Stop watching %1").arg(title) : text, enabled);
        if (!dropsShow) {
            return;
        }
        QVariantMap action = actions.takeLast().toMap();
        action.insert(QStringLiteral("confirmTitle"), tr("Stop watching %1?").arg(title));
        action.insert(QStringLiteral("confirmMessage"),
                      tr("%1 leaves Continue Watching and Up Next until you play it again. "
                         "The server also marks the show as dropped on the watch services "
                         "connected to this profile, such as Trakt.")
                          .arg(title));
        action.insert(QStringLiteral("confirmText"), tr("Stop watching"));
        actions.append(action);
    };
    if (surfaceOf(item) == kContinueWatching) {
        if (capabilities.dismissals) {
            addDismissal(QStringLiteral("remove"), tr("Remove from Continue Watching"),
                         !itemString(item, QStringLiteral("progressUpdatedAt")).isEmpty());
        } else if (capabilities.resumeReset) {
            add(QStringLiteral("remove"), tr("Remove from Continue Watching"), true);
        }
    } else if (capabilities.dismissals) {
        addDismissal(QStringLiteral("hide"), tr("Hide from Up Next"), !seriesId.isEmpty());
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
        if (capabilities.notInterested) {
            if (item.value(QStringLiteral("notInterested")).toBool()) {
                add(QStringLiteral("interested"), tr("Interested again"), true);
            } else {
                add(QStringLiteral("notInterested"), tr("Not interested"), true);
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
            m_libraryService->setHomeItemState(key, QStringLiteral("watched"), itemId, true);
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
    } else if (actionId == QStringLiteral("notInterested")
               || actionId == QStringLiteral("interested")) {
        pending.field = QStringLiteral("notInterested");
        value = actionId == QStringLiteral("notInterested");
    } else {
        return false;
    }
    pending.value = value;
    pending.previousValue = item.value(pending.field, false);
    const QString key = startPending(pending);
    setField(connectionId, itemId, pending.field, value);
    m_libraryService->setHomeItemState(key, pending.field, itemId, value.toBool());
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
    restoreSnapshots(undo.snapshots);
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
    QVariantList continueWatching;
    QVariantList nextUp;
    for (const QVariant &value : std::as_const(m_items)) {
        const QVariantMap item = value.toMap();
        if (isHidden(item)) {
            continue;
        }
        m_visible.append(value);
        if (surfaceOf(item) == kContinueWatching) {
            continueWatching.append(value);
        } else if (!m_mergeRows) {
            nextUp.append(value);
        }
    }
    if (m_mergeRows) {
        nextUp = m_visible;
    }
    m_continueWatchingModel->setItems(continueWatching);
    m_nextUpModel->setItems(nextUp);
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

void ContinueWatchingViewModel::restoreSnapshots(const QList<Snapshot> &snapshots)
{
    for (const Snapshot &snapshot : snapshots) {
        const QString itemId = itemString(snapshot.item, QStringLiteral("itemId"));
        const QString connectionId = itemString(snapshot.item, QStringLiteral("connectionId"));
        const bool listed = std::any_of(m_items.cbegin(), m_items.cend(),
                                        [&](const QVariant &value) {
            return sameCard(value.toMap(), connectionId, itemId);
        });
        if (!listed) {
            m_items.insert(std::clamp<qsizetype>(snapshot.index, 0, m_items.size()),
                           snapshot.item);
        }
    }
}

void ContinueWatchingViewModel::applyField(const QString &connectionId, const QString &itemId,
                                           const QString &field, const QVariant &value)
{
    for (QVariant &entry : m_items) {
        QVariantMap item = entry.toMap();
        if (!sameCard(item, connectionId, itemId)) {
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
}

void ContinueWatchingViewModel::setField(const QString &connectionId, const QString &itemId,
                                         const QString &field, const QVariant &value)
{
    applyField(connectionId, itemId, field, value);
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
            QString message;
            if (pending.dropsSeries) {
                message = tr("Stopped watching %1. It is also marked as dropped on "
                             "connected watch services.").arg(pending.title);
            } else if (pending.wireSurface == kNextUp) {
                message = tr("Hid %1 from Up Next").arg(pending.title);
            } else {
                message = tr("Removed %1 from Continue Watching").arg(pending.title);
            }
            emit undoOffered(message);
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

void ContinueWatchingViewModel::onHomeCardWriteFinished(const QString &connectionId,
                                                        const QString &requestKey,
                                                        bool succeeded,
                                                        const QString &error)
{
    // The key is ours alone; the connection must match too, so a reply for
    // another account never settles this card.
    const auto it = m_pending.constFind(requestKey);
    if (it == m_pending.cend()
        || (!it->connectionId.isEmpty() && it->connectionId != connectionId)) {
        return;
    }
    finish(requestKey, succeeded, error);
}
