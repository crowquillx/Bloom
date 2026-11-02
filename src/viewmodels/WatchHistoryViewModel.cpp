#include "WatchHistoryViewModel.h"

#include "../core/ServiceLocator.h"
#include "../models/LibraryKinds.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

#include <algorithm>

namespace {

// Distinguishes the requests of every WatchHistoryViewModel: LibraryService
// answers all of them on the same signals.
quint64 nextRequestId()
{
    static quint64 counter = 0;
    return ++counter;
}

bool isHistoryCard(const QVariantMap &item)
{
    const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
    return mediaType == QStringLiteral("Movie") || mediaType == QStringLiteral("Series")
        || mediaType == QStringLiteral("Episode");
}

} // namespace

WatchHistoryViewModel::WatchHistoryViewModel(QObject *parent)
    : WatchHistoryViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

WatchHistoryViewModel::WatchHistoryViewModel(LibraryService *libraryService, QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    connectService();
}

void WatchHistoryViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "WatchHistoryViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalViewsLoadedForRequest,
            this, &WatchHistoryViewModel::onViewsLoaded);
    connect(m_libraryService, &LibraryService::canonicalViewsFailedForRequest,
            this, &WatchHistoryViewModel::onViewsFailed);
    connect(m_libraryService, &LibraryService::canonicalWatchHistoryLoaded,
            this, &WatchHistoryViewModel::onHistoryLoaded);
    connect(m_libraryService, &LibraryService::canonicalWatchHistoryFailed,
            this, &WatchHistoryViewModel::onHistoryFailed);
    connect(m_libraryService, &LibraryService::homeCardWriteFinished,
            this, &WatchHistoryViewModel::onWriteFinished);
}

int WatchHistoryViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant WatchHistoryViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const QVariantMap item = m_items.at(index.row()).toMap();
    switch (role) {
    case ItemDataRole:
        return item;
    case ItemIdRole:
        return item.value(QStringLiteral("itemId"));
    case MediaTypeRole:
        return item.value(QStringLiteral("mediaType"));
    case NameRole:
        return item.value(QStringLiteral("name"));
    default:
        return {};
    }
}

QHash<int, QByteArray> WatchHistoryViewModel::roleNames() const
{
    return {
        {ItemDataRole, "itemData"},
        {ItemIdRole, "itemId"},
        {MediaTypeRole, "mediaType"},
        {NameRole, "name"}
    };
}

QString WatchHistoryViewModel::nextRequestKey(const QString &kind)
{
    return QStringLiteral("history:%1:%2").arg(kind).arg(nextRequestId());
}

void WatchHistoryViewModel::load(const QStringList &hiddenLibraryIds)
{
    m_hiddenLibraryIds = hiddenLibraryIds;
    reload();
}

void WatchHistoryViewModel::reload()
{
    // Every load is new: answers to an older one no longer match.
    ++m_serial;
    m_pageRequestKey.clear();
    m_viewsRequestKey.clear();
    m_libraryRequests.clear();
    m_nextCursor.clear();
    m_emptyPages = 0;
    m_wanted = 0;
    m_libraryCount = 0;
    m_failedLibraries = 0;
    m_cursor.reset({}, QString(), QString());
    if (!m_items.isEmpty()) {
        emitModelReset([this]() { m_items.clear(); });
        emit countChanged();
    }
    m_itemIds.clear();
    // A removal in flight keeps its card out of the new list until the
    // server answers.
    for (const PendingRemoval &removal : std::as_const(m_pendingRemovals)) {
        m_itemIds.insert(removal.item.value(QStringLiteral("itemId")).toString());
    }
    setHasMore(false);
    setLoadingMore(false);
    clearError();

    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    m_canRemove = m_libraryService && !m_connectionId.isEmpty()
        && m_libraryService->supportsWatchHistoryRemoval();
    m_byLibrary = m_libraryService && m_libraryService->watchHistoryByLibrary();
    emit connectionChanged();

    if (!m_libraryService || m_connectionId.isEmpty()) {
        m_firstPagePending = false;
        setLoading(false);
        setError(tr("Sign in to see what you have watched."));
        return;
    }
    m_firstPagePending = true;
    setLoading(true);
    if (m_byLibrary) {
        m_viewsRequestKey = nextRequestKey(QStringLiteral("views"));
        // May answer synchronously.
        m_libraryService->getViewsForRequest(m_viewsRequestKey);
    } else {
        requestStreamPage();
    }
}

void WatchHistoryViewModel::loadMore()
{
    if (!m_libraryService || !m_hasMore || m_firstPagePending || m_loadingMore
        || !m_pageRequestKey.isEmpty() || !m_libraryRequests.isEmpty()) {
        return;
    }
    setLoadingMore(true);
    if (m_byLibrary) {
        m_wanted = PageSize;
        pumpLibraries();
    } else {
        m_emptyPages = 0;
        requestStreamPage();
    }
}

QVariantMap WatchHistoryViewModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).toMap();
}

int WatchHistoryViewModel::indexOfItem(const QString &itemId) const
{
    if (itemId.isEmpty()) {
        return -1;
    }
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).toMap().value(QStringLiteral("itemId")).toString() == itemId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int WatchHistoryViewModel::appendNew(const QVariantList &items)
{
    QVariantList added;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        if (!isHistoryCard(item) || itemId.isEmpty() || m_itemIds.contains(itemId)) {
            continue;
        }
        m_itemIds.insert(itemId);
        added.append(item);
    }
    if (added.isEmpty()) {
        return 0;
    }
    const int first = count();
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(added.size()) - 1);
    m_items.append(added);
    endInsertRows();
    emit countChanged();
    return static_cast<int>(added.size());
}

void WatchHistoryViewModel::requestStreamPage()
{
    m_pageRequestKey = nextRequestKey(QStringLiteral("page"));
    // May answer synchronously.
    m_libraryService->getWatchHistory(QString(), 0, m_nextCursor, PageSize, m_pageRequestKey);
}

void WatchHistoryViewModel::onViewsLoaded(const QString &connectionId,
                                          const QString &requestKey,
                                          const QVariantList &views)
{
    if (requestKey.isEmpty() || requestKey != m_viewsRequestKey
        || connectionId != m_connectionId) {
        return;
    }
    m_viewsRequestKey.clear();
    QStringList libraryIds;
    for (const QVariant &value : LibraryKinds::supportedLibraries(views)) {
        const QString libraryId = value.toMap().value(QStringLiteral("itemId")).toString();
        if (!libraryId.isEmpty() && !m_hiddenLibraryIds.contains(libraryId)
            && !libraryIds.contains(libraryId)) {
            libraryIds.append(libraryId);
        }
    }
    m_cursor.reset(libraryIds, QStringLiteral("datePlayed"), QStringLiteral("descending"));
    m_libraryCount = static_cast<int>(libraryIds.size());
    m_wanted = PageSize;
    pumpLibraries();
}

void WatchHistoryViewModel::onViewsFailed(const QString &connectionId,
                                          const QString &requestKey,
                                          const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_viewsRequestKey
        || connectionId != m_connectionId) {
        return;
    }
    m_viewsRequestKey.clear();
    failFirstPage(error);
}

void WatchHistoryViewModel::pumpLibraries()
{
    // A page that answers synchronously runs this again from inside the
    // loop below; it is picked up by the next round instead.
    if (m_pumping) {
        m_pumpAgain = true;
        return;
    }
    m_pumping = true;
    do {
        m_pumpAgain = false;
        const QVariantList taken = m_cursor.take(m_wanted);
        m_wanted -= static_cast<int>(taken.size());
        appendNew(taken);
        if (m_wanted <= 0 || m_cursor.finished()) {
            finishPage();
            break;
        }
        const QStringList needed = m_cursor.librariesNeedingPages();
        QList<std::pair<QString, QString>> requests;
        for (const QString &libraryId : needed) {
            const QString requestKey = nextRequestKey(QStringLiteral("library"));
            m_libraryRequests.insert(requestKey, libraryId);
            requests.append({requestKey, libraryId});
        }
        const quint64 serial = m_serial;
        for (const auto &[requestKey, libraryId] : std::as_const(requests)) {
            if (serial != m_serial) {
                break;
            }
            m_libraryService->getWatchHistory(libraryId, m_cursor.nextOffset(libraryId),
                                              QString(), PageSize, requestKey);
        }
        if (serial == m_serial && m_libraryRequests.isEmpty() && !m_pumpAgain) {
            // Nothing left to wait for.
            finishPage();
            break;
        }
    } while (m_pumpAgain);
    m_pumping = false;
}

void WatchHistoryViewModel::onHistoryLoaded(const QString &connectionId,
                                            const QString &requestKey,
                                            const QVariantList &items,
                                            int totalRecordCount, bool hasMore,
                                            const QString &nextCursor)
{
    if (requestKey.isEmpty() || connectionId != m_connectionId) {
        return;
    }
    if (m_libraryRequests.contains(requestKey)) {
        const QString libraryId = m_libraryRequests.take(requestKey);
        m_cursor.addPage(libraryId, items, totalRecordCount);
        pumpLibraries();
        return;
    }
    if (requestKey != m_pageRequestKey) {
        return;
    }
    m_pageRequestKey.clear();
    m_nextCursor = nextCursor;
    const int added = appendNew(items);
    // Silo's pages can be short or empty while more remain; the cursor,
    // not the item count, says whether there is more.
    const bool more = hasMore && !nextCursor.isEmpty();
    if (more && added == 0 && ++m_emptyPages < MaxEmptyPages) {
        requestStreamPage();
        return;
    }
    m_emptyPages = 0;
    setHasMore(more);
    finishPage();
}

void WatchHistoryViewModel::onHistoryFailed(const QString &connectionId,
                                            const QString &requestKey,
                                            const QString &error)
{
    if (requestKey.isEmpty() || connectionId != m_connectionId) {
        return;
    }
    if (m_libraryRequests.contains(requestKey)) {
        // The library ends for this load; the others still merge.
        const QString libraryId = m_libraryRequests.take(requestKey);
        qCWarning(lcViewModels) << "WatchHistoryViewModel: library history failed:" << error;
        m_cursor.pageFailed(libraryId);
        ++m_failedLibraries;
        if (m_firstPagePending && m_failedLibraries >= m_libraryCount && m_items.isEmpty()) {
            m_libraryRequests.clear();
            failFirstPage(error);
            return;
        }
        pumpLibraries();
        return;
    }
    if (requestKey != m_pageRequestKey) {
        return;
    }
    m_pageRequestKey.clear();
    m_emptyPages = 0;
    if (m_firstPagePending && m_items.isEmpty()) {
        failFirstPage(error);
        return;
    }
    // Cards already shown stay; the next loadMore() tries again.
    qCWarning(lcViewModels) << "WatchHistoryViewModel: history page failed:" << error;
    finishPage();
}

void WatchHistoryViewModel::finishPage()
{
    if (m_byLibrary) {
        setHasMore(!m_cursor.finished());
    }
    m_firstPagePending = false;
    setLoadingMore(false);
    setLoading(false);
}

void WatchHistoryViewModel::failFirstPage(const QString &error)
{
    m_firstPagePending = false;
    setHasMore(false);
    setLoadingMore(false);
    setError(mapNetworkError(QStringLiteral("getWatchHistory"), error));
    setLoading(false);
}

QVariantList WatchHistoryViewModel::actionsFor(const QVariantMap &item) const
{
    QVariantList actions;
    const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
    const QString seriesName = item.value(QStringLiteral("seriesName")).toString();
    QString name = item.value(QStringLiteral("name")).toString();
    if (mediaType == QStringLiteral("Episode") && !seriesName.isEmpty()) {
        name = tr("%1: %2").arg(seriesName, name);
    }
    if (mediaType == QStringLiteral("Episode")) {
        actions.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("openSeries")},
            {QStringLiteral("text"), tr("Go to series")},
            {QStringLiteral("enabled"),
             !item.value(QStringLiteral("seriesId")).toString().isEmpty()},
            {QStringLiteral("navigation"), true}});
    } else {
        actions.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("openDetails")},
            {QStringLiteral("text"), tr("Go to details")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("navigation"), true}});
    }
    if (!m_canRemove) {
        return actions;
    }
    const QString itemId = item.value(QStringLiteral("itemId")).toString();
    bool pending = false;
    for (const PendingRemoval &removal : m_pendingRemovals) {
        pending = pending || removal.item.value(QStringLiteral("itemId")).toString() == itemId;
    }
    // The server also counts the watches as unwatched and drops their
    // resume points, which outlasts the card: ask first.
    const QString message = mediaType == QStringLiteral("Series")
        ? tr("Every episode of %1 you watched leaves your history. They count as "
             "unwatched again and lose their resume points.").arg(name)
        : tr("%1 leaves your history. It counts as unwatched again and loses its "
             "resume point.").arg(name);
    actions.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("remove")},
        {QStringLiteral("text"), tr("Remove from history")},
        {QStringLiteral("enabled"),
         !pending && !itemId.isEmpty()
             && item.value(QStringLiteral("connectionId")).toString() == m_connectionId},
        {QStringLiteral("navigation"), false},
        {QStringLiteral("confirmTitle"), tr("Remove from history?")},
        {QStringLiteral("confirmMessage"), message},
        {QStringLiteral("confirmText"), tr("Remove")}});
    return actions;
}

bool WatchHistoryViewModel::trigger(const QString &actionId, const QVariantMap &item)
{
    if (actionId != QStringLiteral("remove") || !m_libraryService || !m_canRemove) {
        return false;
    }
    const QString itemId = item.value(QStringLiteral("itemId")).toString();
    const QString name = item.value(QStringLiteral("name")).toString();
    // The page reads the live connection; a card of another one cannot be
    // written there.
    if (item.value(QStringLiteral("connectionId")).toString() != m_connectionId
        || m_libraryService->getActiveConnectionId() != m_connectionId) {
        emit actionFailed(tr("%1 belongs to a source that is not signed in.").arg(name));
        return false;
    }
    const int row = indexOfItem(itemId);
    if (row < 0) {
        return false;
    }
    for (const PendingRemoval &removal : std::as_const(m_pendingRemovals)) {
        if (removal.item.value(QStringLiteral("itemId")).toString() == itemId) {
            return false;
        }
    }
    const QVariantMap shown = m_items.at(row).toMap();
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    const QString requestKey = nextRequestKey(QStringLiteral("remove"));
    m_pendingRemovals.insert(requestKey, PendingRemoval{shown, row, m_serial});
    // May answer synchronously.
    m_libraryService->removeFromWatchHistory(requestKey, itemId);
    return true;
}

void WatchHistoryViewModel::onWriteFinished(const QString &connectionId,
                                            const QString &requestKey,
                                            bool success, const QString &error)
{
    const auto pending = m_pendingRemovals.constFind(requestKey);
    if (pending == m_pendingRemovals.cend()
        || pending->item.value(QStringLiteral("connectionId")).toString() != connectionId) {
        return;
    }
    const PendingRemoval removal = *pending;
    m_pendingRemovals.erase(pending);
    if (success) {
        return;
    }
    const QString itemId = removal.item.value(QStringLiteral("itemId")).toString();
    qCWarning(lcViewModels) << "WatchHistoryViewModel: removal failed:" << error;
    if (removal.serial == m_serial && connectionId == m_connectionId) {
        // Back in its place, or as near as the list now allows.
        insertRow(std::min(removal.row, count()), removal.item);
    } else {
        // A newer load left the card out; the next one shows it again.
        m_itemIds.remove(itemId);
    }
    emit actionFailed(tr("Couldn't remove %1 from your history.")
                          .arg(removal.item.value(QStringLiteral("name")).toString()));
}

void WatchHistoryViewModel::insertRow(int row, const QVariantMap &item)
{
    beginInsertRows(QModelIndex(), row, row);
    m_items.insert(row, item);
    endInsertRows();
    emit countChanged();
}

void WatchHistoryViewModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore) {
        return;
    }
    m_hasMore = hasMore;
    emit hasMoreChanged();
}

void WatchHistoryViewModel::setLoadingMore(bool loadingMore)
{
    if (m_loadingMore == loadingMore) {
        return;
    }
    m_loadingMore = loadingMore;
    emit loadingMoreChanged();
}
