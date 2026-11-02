#include "CollectionViewModels.h"

#include "../core/ServiceLocator.h"
#include "../models/MediaCollections.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

namespace {

namespace Collections = Bloom::MediaCollections;

// Distinguishes the requests of every collection view model: LibraryService
// answers all of them on the same signals.
QString nextRequestKey(const char *prefix)
{
    static quint64 counter = 0;
    return QStringLiteral("%1:%2").arg(QLatin1String(prefix)).arg(++counter);
}

QVariant collectionRole(const QVariantMap &collection, int role)
{
    switch (role) {
    case LibraryCollectionsViewModel::CollectionDataRole:
        return collection;
    case LibraryCollectionsViewModel::CollectionIdRole:
        return collection.value(QStringLiteral("collectionId"));
    case LibraryCollectionsViewModel::NameRole:
        return collection.value(QStringLiteral("name"));
    default:
        return {};
    }
}

QVariant titleRole(const QVariantMap &item, int role)
{
    switch (role) {
    case CollectionViewModel::ItemDataRole:
        return item;
    case CollectionViewModel::ItemIdRole:
        return item.value(QStringLiteral("itemId"));
    case CollectionViewModel::MediaTypeRole:
        return item.value(QStringLiteral("mediaType"));
    case CollectionViewModel::NameRole:
        return item.value(QStringLiteral("name"));
    default:
        return {};
    }
}

} // namespace

// ============================================================================
// LibraryCollectionsViewModel
// ============================================================================

LibraryCollectionsViewModel::LibraryCollectionsViewModel(QObject *parent)
    : LibraryCollectionsViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

LibraryCollectionsViewModel::LibraryCollectionsViewModel(LibraryService *libraryService,
                                                         QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "LibraryCollectionsViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalLibraryCollectionsLoaded,
            this, &LibraryCollectionsViewModel::onLoaded);
    connect(m_libraryService, &LibraryService::canonicalLibraryCollectionsFailed,
            this, &LibraryCollectionsViewModel::onFailed);
}

int LibraryCollectionsViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant LibraryCollectionsViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_collections.size()) {
        return {};
    }
    return collectionRole(m_collections.at(index.row()).toMap(), role);
}

QHash<int, QByteArray> LibraryCollectionsViewModel::roleNames() const
{
    return {{CollectionDataRole, "collectionData"},
            {CollectionIdRole, "collectionId"},
            {NameRole, "name"}};
}

bool LibraryCollectionsViewModel::available() const
{
    return m_libraryService && m_libraryService->supportsCollections();
}

void LibraryCollectionsViewModel::load(const QString &libraryId)
{
    const QString connectionId =
        m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    if (libraryId == m_libraryId && connectionId == m_connectionId
        && (m_loaded || m_firstPagePending)) {
        return;
    }
    m_libraryId = libraryId;
    emit libraryIdChanged();
    reload();
}

void LibraryCollectionsViewModel::reload()
{
    m_requestKey.clear();
    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    emit libraryIdChanged();
    emit availableChanged();
    if (!m_collections.isEmpty()) {
        emitModelReset([this]() { m_collections.clear(); });
    }
    m_collectionIds.clear();
    m_loaded = false;
    m_hasMore = false;
    m_nextStartIndex = 0;
    emit collectionsChanged();
    setLoadingMore(false);
    clearError();
    m_firstPagePending = false;
    if (!available() || m_libraryId.isEmpty()) {
        setLoading(false);
        return;
    }
    m_firstPagePending = true;
    setLoading(true);
    requestPage();
}

void LibraryCollectionsViewModel::loadMore()
{
    if (!m_libraryService || !m_hasMore || m_firstPagePending || !m_requestKey.isEmpty()) {
        return;
    }
    setLoadingMore(true);
    requestPage();
}

void LibraryCollectionsViewModel::clear()
{
    m_requestKey.clear();
    m_firstPagePending = false;
    m_libraryId.clear();
    m_connectionId.clear();
    emit libraryIdChanged();
    if (!m_collections.isEmpty()) {
        emitModelReset([this]() { m_collections.clear(); });
    }
    m_collectionIds.clear();
    m_loaded = false;
    m_hasMore = false;
    m_nextStartIndex = 0;
    emit collectionsChanged();
    setLoadingMore(false);
    clearError();
    setLoading(false);
}

QVariantMap LibraryCollectionsViewModel::get(int row) const
{
    if (row < 0 || row >= m_collections.size()) {
        return {};
    }
    return m_collections.at(row).toMap();
}

int LibraryCollectionsViewModel::indexOfCollection(const QString &collectionId) const
{
    if (collectionId.isEmpty()) {
        return -1;
    }
    for (qsizetype row = 0; row < m_collections.size(); ++row) {
        if (m_collections.at(row).toMap().value(QStringLiteral("collectionId")).toString()
            == collectionId) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

void LibraryCollectionsViewModel::requestPage()
{
    m_requestKey = nextRequestKey("collections");
    // May answer synchronously.
    m_libraryService->getLibraryCollections(m_libraryId, m_nextStartIndex, PageSize,
                                            m_requestKey);
}

void LibraryCollectionsViewModel::onLoaded(const QString &connectionId,
                                           const QString &libraryId,
                                           const QString &requestKey,
                                           const QVariantList &collections,
                                           int totalRecordCount, bool hasMore)
{
    Q_UNUSED(totalRecordCount)
    if (requestKey.isEmpty() || requestKey != m_requestKey || libraryId != m_libraryId
        || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    m_firstPagePending = false;
    m_loaded = true;
    setLoadingMore(false);
    m_nextStartIndex += static_cast<int>(collections.size());
    // An empty page cannot advance; stop instead of asking again.
    m_hasMore = hasMore && !collections.isEmpty();

    QVariantList added;
    for (const QVariant &value : collections) {
        const QVariantMap collection = value.toMap();
        const QString collectionId = collection.value(QStringLiteral("collectionId")).toString();
        if (!Collections::isCollection(collection) || m_collectionIds.contains(collectionId)) {
            continue;
        }
        m_collectionIds.insert(collectionId);
        added.append(collection);
    }
    if (!added.isEmpty()) {
        const int first = count();
        beginInsertRows(QModelIndex(), first, first + static_cast<int>(added.size()) - 1);
        m_collections.append(added);
        endInsertRows();
    }
    emit collectionsChanged();
    setLoading(false);
}

void LibraryCollectionsViewModel::onFailed(const QString &connectionId,
                                           const QString &libraryId,
                                           const QString &requestKey,
                                           const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey || libraryId != m_libraryId
        || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    if (m_firstPagePending) {
        m_firstPagePending = false;
        setError(mapNetworkError(QStringLiteral("getLibraryCollections"), error));
    } else {
        qCWarning(lcViewModels) << "LibraryCollectionsViewModel: page failed:" << error;
    }
    setLoadingMore(false);
    setLoading(false);
}

void LibraryCollectionsViewModel::setLoadingMore(bool loadingMore)
{
    if (m_loadingMore == loadingMore) {
        return;
    }
    m_loadingMore = loadingMore;
    emit loadingMoreChanged();
}

// ============================================================================
// CollectionViewModel
// ============================================================================

CollectionViewModel::CollectionViewModel(QObject *parent)
    : CollectionViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

CollectionViewModel::CollectionViewModel(LibraryService *libraryService, QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "CollectionViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalCollectionItemsLoaded,
            this, &CollectionViewModel::onLoaded);
    connect(m_libraryService, &LibraryService::canonicalCollectionItemsFailed,
            this, &CollectionViewModel::onFailed);
}

int CollectionViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant CollectionViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    return titleRole(m_items.at(index.row()).toMap(), role);
}

QHash<int, QByteArray> CollectionViewModel::roleNames() const
{
    return {{ItemDataRole, "itemData"},
            {ItemIdRole, "itemId"},
            {MediaTypeRole, "mediaType"},
            {NameRole, "name"}};
}

QStringList CollectionViewModel::sorts() const
{
    return Collections::sorts();
}

void CollectionViewModel::setSortBy(const QString &sortBy)
{
    if (sortBy == m_sortBy || !Collections::sorts().contains(sortBy)) {
        return;
    }
    m_sortBy = sortBy;
    emit sortByChanged();
    if (!m_collectionId.isEmpty()) {
        reload();
    }
}

int CollectionViewModel::totalCount() const
{
    return m_hasMore ? qMax(m_serverTotal, count()) : count();
}

void CollectionViewModel::load(const QVariantMap &collection)
{
    m_collection = collection;
    m_collectionId = collection.value(QStringLiteral("collectionId")).toString();
    m_collectionKind = collection.value(QStringLiteral("collectionKind")).toString();
    reload();
}

void CollectionViewModel::reload()
{
    m_requestKey.clear();
    const QString seedConnectionId = m_collection.value(QStringLiteral("connectionId")).toString();
    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    emit collectionChanged();
    if (!m_items.isEmpty()) {
        emitModelReset([this]() { m_items.clear(); });
    }
    m_itemIds.clear();
    m_hasMore = false;
    m_serverTotal = -1;
    m_nextStartIndex = 0;
    m_nextCursor.clear();
    emit itemsChanged();
    setLoadingMore(false);
    clearError();
    m_firstPagePending = false;

    if (!m_libraryService || !Collections::isCollection(m_collection)) {
        setLoading(false);
        setError(tr("This collection could not be found."));
        return;
    }
    // The page reads the live connection, which must own the collection
    // (merged spaces route to it first).
    if (seedConnectionId != m_connectionId) {
        setLoading(false);
        setError(tr("This collection belongs to a source that is not signed in."));
        return;
    }
    m_firstPagePending = true;
    setLoading(true);
    requestPage();
}

void CollectionViewModel::loadMore()
{
    if (!m_libraryService || !m_hasMore || m_firstPagePending || !m_requestKey.isEmpty()) {
        return;
    }
    setLoadingMore(true);
    requestPage();
}

QVariantMap CollectionViewModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).toMap();
}

int CollectionViewModel::indexOfItem(const QString &itemId) const
{
    if (itemId.isEmpty() || !m_itemIds.contains(itemId)) {
        return -1;
    }
    for (qsizetype row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).toMap().value(QStringLiteral("itemId")).toString() == itemId) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

void CollectionViewModel::requestPage()
{
    m_requestKey = nextRequestKey("collection");
    // May answer synchronously.
    m_libraryService->getCollectionItems(m_collectionId, m_collectionKind, m_sortBy,
                                         m_nextStartIndex, m_nextCursor, PageSize,
                                         m_requestKey);
}

void CollectionViewModel::onLoaded(const QString &connectionId, const QString &collectionId,
                                   const QString &requestKey, const QVariantList &items,
                                   int totalRecordCount, bool hasMore,
                                   const QString &nextCursor)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey
        || collectionId != m_collectionId || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    m_firstPagePending = false;
    setLoadingMore(false);
    // Paging counts what the server sent, before filtering.
    m_nextStartIndex += static_cast<int>(items.size());
    m_nextCursor = nextCursor;
    m_hasMore = hasMore && !items.isEmpty();

    QVariantList added;
    int skipped = 0;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        if (!Collections::isTitle(item) || m_itemIds.contains(itemId)) {
            ++skipped;
            continue;
        }
        m_itemIds.insert(itemId);
        added.append(item);
    }
    if (totalRecordCount > 0) {
        // The server's count includes what Bloom does not show.
        m_serverTotal = qMax(0, totalRecordCount - skipped);
    }
    if (!added.isEmpty()) {
        const int first = count();
        beginInsertRows(QModelIndex(), first, first + static_cast<int>(added.size()) - 1);
        m_items.append(added);
        endInsertRows();
    }
    emit itemsChanged();
    setLoading(false);
}

void CollectionViewModel::onFailed(const QString &connectionId, const QString &collectionId,
                                   const QString &requestKey, const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey
        || collectionId != m_collectionId || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    if (m_firstPagePending) {
        m_firstPagePending = false;
        setError(mapNetworkError(QStringLiteral("getCollectionItems"), error));
    } else {
        // Titles already shown stay; the next loadMore() tries again.
        qCWarning(lcViewModels) << "CollectionViewModel: page failed:" << error;
    }
    setLoadingMore(false);
    setLoading(false);
}

void CollectionViewModel::setLoadingMore(bool loadingMore)
{
    if (m_loadingMore == loadingMore) {
        return;
    }
    m_loadingMore = loadingMore;
    emit loadingMoreChanged();
}

// ============================================================================
// ItemCollectionsViewModel
// ============================================================================

ItemCollectionsViewModel::ItemCollectionsViewModel(QObject *parent)
    : ItemCollectionsViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

ItemCollectionsViewModel::ItemCollectionsViewModel(LibraryService *libraryService,
                                                   QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "ItemCollectionsViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalItemCollectionsLoaded,
            this, &ItemCollectionsViewModel::onLoaded);
    connect(m_libraryService, &LibraryService::canonicalItemCollectionsFailed,
            this, &ItemCollectionsViewModel::onFailed);
}

void ItemCollectionsViewModel::load(const QString &itemId)
{
    const QString connectionId =
        m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    if (itemId != m_itemId || connectionId != m_connectionId) {
        // Another title: its collections are not this one's.
        m_itemId = itemId;
        m_connectionId = connectionId;
        m_collections.clear();
        emit collectionsChanged();
    }
    if (!m_libraryService || m_itemId.isEmpty() || !m_libraryService->supportsCollections()) {
        finish();
        return;
    }
    m_requestKey = nextRequestKey("itemCollections");
    emit loadingChanged();
    // May answer synchronously.
    m_libraryService->getItemCollections(m_itemId, m_requestKey);
}

void ItemCollectionsViewModel::clear()
{
    const bool changed = !m_itemId.isEmpty() || !m_collections.isEmpty();
    m_itemId.clear();
    m_connectionId.clear();
    m_collections.clear();
    finish();
    if (changed) {
        emit collectionsChanged();
    }
}

void ItemCollectionsViewModel::onLoaded(const QString &connectionId, const QString &itemId,
                                        const QString &requestKey,
                                        const QVariantList &collections)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey || itemId != m_itemId
        || connectionId != m_connectionId) {
        return;
    }
    QVariantList kept;
    QSet<QString> seen;
    for (const QVariant &value : collections) {
        const QVariantMap collection = value.toMap();
        const QString collectionId = collection.value(QStringLiteral("collectionId")).toString();
        if (Collections::isCollection(collection) && !seen.contains(collectionId)) {
            seen.insert(collectionId);
            kept.append(collection);
        }
    }
    finish();
    if (kept != m_collections) {
        m_collections = kept;
        emit collectionsChanged();
    }
}

void ItemCollectionsViewModel::onFailed(const QString &connectionId, const QString &itemId,
                                        const QString &requestKey, const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey || itemId != m_itemId
        || connectionId != m_connectionId) {
        return;
    }
    // The shelf is optional: nothing to show, nothing to report.
    qCWarning(lcViewModels) << "ItemCollectionsViewModel: read failed:" << error;
    finish();
    if (!m_collections.isEmpty()) {
        m_collections.clear();
        emit collectionsChanged();
    }
}

void ItemCollectionsViewModel::finish()
{
    if (m_requestKey.isEmpty()) {
        return;
    }
    m_requestKey.clear();
    emit loadingChanged();
}
