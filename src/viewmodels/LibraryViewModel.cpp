#include "LibraryViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
#include "../utils/DetailViewCache.h"
#include "../utils/LibraryCacheStore.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <algorithm>
#include "../utils/BloomLogging.h"

// Static cache initialization
QHash<QString, LibraryCacheEntry> LibraryViewModel::s_libraryCache;
QList<QString> LibraryViewModel::s_libraryCacheLru;
qint64 LibraryViewModel::s_libraryCacheBytes = 0;

LibraryViewModel::LibraryViewModel(QObject *parent)
    : BaseViewModel(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_configManager = ServiceLocator::tryGet<ConfigManager>();
    reopenCacheStore();
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged,
                this, &LibraryViewModel::reopenCacheStore);
    }

    if (m_libraryService) {
        connect(m_libraryService, &LibraryService::canonicalViewsLoadedForRequest,
                this, &LibraryViewModel::onViewsLoaded);
        connect(m_libraryService, &LibraryService::canonicalViewsFailedForRequest,
                this, &LibraryViewModel::onViewsFailed);
        connect(m_libraryService, &LibraryService::canonicalItemsLoadedForConnection,
                this, &LibraryViewModel::onItemsLoaded);
        connect(m_libraryService, &LibraryService::canonicalItemsNotModifiedForConnection,
                this, &LibraryViewModel::onItemsNotModified);
        connect(m_libraryService, &LibraryService::canonicalItemsFailedForConnection,
                this, &LibraryViewModel::onItemsFailed);
        connect(m_libraryService, &LibraryService::filterOptionsLoadedForRequest,
                this, &LibraryViewModel::onFilterOptionsLoaded);
        connect(m_libraryService, &LibraryService::filterOptionsFailedForRequest,
                this, &LibraryViewModel::onFilterOptionsFailed);
    } else {
        qCWarning(lcViewModels) << "LibraryViewModel: LibraryService not available in ServiceLocator";
    }
}

int LibraryViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant LibraryViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const QJsonObject &item = m_items.at(index.row());

    switch (role) {
    case NameRole:
        return item.value(QStringLiteral("name")).toString();
    case ImageUrlRole:
        return getImageUrl(item);
    case IdRole:
        return item.value(QStringLiteral("itemId")).toString();
    case TypeRole:
        return item.value(QStringLiteral("mediaType")).toString();
    case ModelDataRole:
        return item.toVariantMap();
    case ProductionYearRole:
        return item.value(QStringLiteral("productionYear")).toInt();
    case IndexNumberRole:
        return item.value(QStringLiteral("indexNumber")).toInt();
    case ParentIndexNumberRole:
        return item.value(QStringLiteral("parentIndexNumber")).toInt();
    case OverviewRole:
        return item.value(QStringLiteral("overview")).toString();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LibraryViewModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ImageUrlRole, "imageUrl"},
        {IdRole, "itemId"},
        {TypeRole, "itemType"},
        {ModelDataRole, "modelData"},
        {ProductionYearRole, "productionYear"},
        {IndexNumberRole, "indexNumber"},
        {ParentIndexNumberRole, "parentIndexNumber"},
        {OverviewRole, "overview"}
    };
}

void LibraryViewModel::loadLibrary(const QString &parentId, int startIndex, int limit)
{
    loadLibrary(parentId, QString(), startIndex, limit);
}

void LibraryViewModel::loadLibrary(const QString &parentId, const QString &collectionType, int startIndex, int limit)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    beginGeneration();
    const bool parentChanged = m_currentParentId != parentId;
    m_currentParentId = parentId;
    m_currentCollectionType = collectionType;
    m_pageLimit = limit;
    m_loadingViews = false;
    m_activeConnectionId = requestConnectionId();
    if (parentChanged) {
        emit currentParentIdChanged();
    }
    
    // SWR Pattern: Check for any cached data (even stale) for initial loads
    const bool includeHeavyFields = limit == 0;
    LibraryItemQuery query =
        buildCurrentQuery(startIndex, limit, includeHeavyFields);
    m_activeDatasetKey = query.datasetKey();

    if (startIndex == 0 && hasAnyCachedData(m_activeDatasetKey)) {
        LibraryCacheEntry cached = getCachedData(m_activeDatasetKey);
        bool isStale = !cached.isValid(kCacheTtlMs);
        
        qCDebug(lcViewModels) << "LibraryViewModel::loadLibrary SWR" << (isStale ? "STALE" : "FRESH") 
                 << "cache for" << m_activeDatasetKey
                 << "items:" << cached.items.size() << "total:" << cached.totalRecordCount;
        
        // Always serve cached data immediately (instant UI)
        setTotalRecordCount(cached.totalRecordCount);
        setItems(cached.items);
        emit loadComplete();
        emit canLoadMoreChanged();
        
        // If cache is still fresh, we're done
        if (!isStale) {
            return;
        }
        
        // SWR: Cache is stale - trigger background refresh
        // This won't show loading spinner, data is already displayed
        qCDebug(lcViewModels) << "LibraryViewModel: SWR background refresh for" << m_activeDatasetKey;
        m_isBackgroundRefresh = true;
        m_loadTimer.restart();
        clearError();
        query.requestKey = registerRequest(
            LoadKind::BackgroundRefresh,
            parentId,
            m_activeDatasetKey,
            startIndex,
            limit,
            includeHeavyFields);
        m_activeBackgroundRequestKey = query.requestKey;
        m_libraryService->getItems(query);
        return;
    }
    
    // No cached data - do a normal blocking load with spinner
    setLoading(true);
    emit canLoadMoreChanged();
    m_loadTimer.restart();
    clearError();
    query.requestKey = registerRequest(
        LoadKind::Initial,
        parentId,
        m_activeDatasetKey,
        startIndex,
        limit,
        includeHeavyFields);
    m_activeInitialRequestKey = query.requestKey;

    qCDebug(lcViewModels) << "LibraryViewModel::loadLibrary" << parentId
                          << "datasetKey:" << m_activeDatasetKey
                          << "requestKey:" << query.requestKey
                          << "startIndex:" << startIndex << "limit:" << limit
                          << "heavyFields:" << includeHeavyFields;
    m_libraryService->getItems(query);
}

void LibraryViewModel::loadViews()
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    beginGeneration();
    const bool parentChanged = !m_currentParentId.isEmpty();
    m_currentParentId.clear();
    m_currentCollectionType.clear();
    m_activeDatasetKey.clear();
    m_activeConnectionId = requestConnectionId();
    m_pageLimit = 0;
    m_loadingViews = true;
    
    if (parentChanged) {
        emit currentParentIdChanged();
    }
    setLoading(true);
    emit canLoadMoreChanged();
    clearError();

    qCDebug(lcViewModels) << "LibraryViewModel::loadViews";
    m_activeViewsRequestKey = registerRequest(
        LoadKind::Views, QString(), QString(), 0, 0, false);
    m_libraryService->getViewsForRequest(m_activeViewsRequestKey);
}

void LibraryViewModel::refresh()
{
    if (m_loadingViews || m_currentParentId.isEmpty()) {
        loadViews();
    } else {
        loadLibrary(
            m_currentParentId, m_currentCollectionType, 0, m_pageLimit);
    }
}

void LibraryViewModel::reload()
{
    refresh();
}

void LibraryViewModel::clear()
{
    beginGeneration();
    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

    setTotalRecordCount(0);
    clearError();
    emit canLoadMoreChanged();
}

void LibraryViewModel::loadMore(int limit)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }
    
    // Don't load more if already loading or no more items
    if (isLoading() || m_isLoadingMore || m_items.size() >= m_totalRecordCount) {
        return;
    }
    
    // Can't load more for views (top-level libraries)
    if (m_loadingViews || m_currentParentId.isEmpty()) {
        return;
    }
    
    setIsLoadingMore(true);
    m_loadMoreTimer.restart();
    clearError();
    
    int startIndex = m_items.size();
    qCDebug(lcViewModels) << "LibraryViewModel::loadMore from index" << startIndex << "limit:" << limit;
    
    LibraryItemQuery query = buildCurrentQuery(startIndex, limit, false);
    const QString datasetKey = query.datasetKey();
    if (datasetKey != m_activeDatasetKey) {
        qCDebug(lcViewModels)
            << "LibraryViewModel: query changed before pagination; starting a new dataset";
        loadLibrary(
            m_currentParentId, m_currentCollectionType, 0,
            m_pageLimit > 0 ? m_pageLimit : limit);
        return;
    }
    query.requestKey = registerRequest(
        LoadKind::LoadMore,
        m_currentParentId,
        datasetKey,
        startIndex,
        limit,
        false);
    m_activeLoadMoreRequestKey = query.requestKey;
    m_libraryService->getItems(query);
}

QVariantMap LibraryViewModel::getItem(int index) const
{
    if (index < 0 || index >= m_items.size())
        return QVariantMap();
    
    return m_items.at(index).toVariantMap();
}

QString LibraryViewModel::buildImageUrl(const QVariantMap &item) const
{
    if (!m_libraryService)
        return QString();
    
    return getImageUrl(QJsonObject::fromVariantMap(item));
}

void LibraryViewModel::updateViews(const QVariantList &views)
{
    QVariantList viewsList;
    for (const QVariant &view : views) {
        const QVariantMap viewMap = view.toMap();
        if (viewMap.value(QStringLiteral("collectionType")).toString() == QStringLiteral("boxsets")) {
            qCDebug(lcViewModels) << "LibraryViewModel: Filtering out Collections library from views";
            continue;
        }
        viewsList.append(viewMap);
    }
    if (m_views != viewsList) {
        m_views = viewsList;
        emit viewsChanged();
    }
}

void LibraryViewModel::onViewsLoaded(const QString &connectionId,
                                     const QString &requestKey,
                                     const QVariantList &views)
{
    if (requestKey.isEmpty()) {
        if (connectionId.isEmpty()
            || connectionId == requestConnectionId()) {
            updateViews(views);
        }
        return;
    }

    const auto request =
        takeCurrentRequest(connectionId, QString(), requestKey);
    if (!request || request->kind != LoadKind::Views
        || requestKey != m_activeViewsRequestKey) {
        return;
    }

    updateViews(views);

    m_activeViewsRequestKey.clear();
    m_loadingViews = false;
    setLoading(false);
    setTotalRecordCount(m_views.size());
    setItems(QJsonArray::fromVariantList(m_views));

    emit loadComplete();
}

void LibraryViewModel::onViewsFailed(const QString &connectionId,
                                     const QString &requestKey,
                                     const QString &error)
{
    const auto request =
        takeCurrentRequest(connectionId, QString(), requestKey);
    if (!request || request->kind != LoadKind::Views
        || requestKey != m_activeViewsRequestKey) {
        return;
    }

    m_activeViewsRequestKey.clear();
    m_loadingViews = false;
    setLoading(false);
    setError(mapNetworkError(QStringLiteral("getViews"), error));
    emit loadError(error);
}

void LibraryViewModel::onItemsLoaded(const QString &connectionId,
                                     const QString &parentId,
                                     const QString &requestKey,
                                     const QVariantList &canonicalItems,
                                     int totalRecordCount)
{
    const auto request =
        takeCurrentRequest(connectionId, parentId, requestKey);
    if (!request) {
        return;
    }
    const QJsonArray items = QJsonArray::fromVariantList(canonicalItems);

    qCDebug(lcViewModels) << "LibraryViewModel::onItemsLoaded" << parentId
             << items.size()
             << "items, total:" << totalRecordCount 
             << "loadKind:" << static_cast<int>(request->kind)
             << "datasetKey:" << request->datasetKey
             << "requestKey:" << requestKey;
    
    if (request->kind == LoadKind::LoadMore
        && requestKey == m_activeLoadMoreRequestKey) {
        m_activeLoadMoreRequestKey.clear();
        setIsLoadingMore(false);
        setTotalRecordCount(totalRecordCount);
        appendItems(items);
        qCDebug(lcViewModels) << "LibraryViewModel: loadMore completed in" << m_loadMoreTimer.elapsed() << "ms";
        updateCachePage(
            request->datasetKey, items, totalRecordCount, request->offset);
        
        emit loadMoreComplete();
        emit canLoadMoreChanged();
    } else if (request->kind == LoadKind::BackgroundRefresh
               && requestKey == m_activeBackgroundRequestKey) {
        m_activeBackgroundRequestKey.clear();
        // SWR: Background refresh completed
        m_isBackgroundRefresh = false;
        qCDebug(lcViewModels) << "LibraryViewModel: background refresh completed in" << m_loadTimer.elapsed() << "ms";

        const LibraryCacheEntry cached = getCachedData(request->datasetKey);
        // A successful first-page revalidation cannot prove that cached later
        // pages still occupy the same positions. Insertions, deletions, or
        // reordering can shift the page boundary, so retain the old full
        // snapshot only for a provider-level not-modified response.
        const QJsonArray refreshed = items;

        // Check if data actually changed
        if (hasDataChanged(refreshed, totalRecordCount, cached)) {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR detected changes, updating model";
            setTotalRecordCount(totalRecordCount);
            updateItemsFromBackground(refreshed);
            emit canLoadMoreChanged();
        } else {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR no changes detected, updating timestamp only";
        }
        
        // Always update cache with fresh data and timestamp
        updateCache(request->datasetKey, refreshed, totalRecordCount);
    } else if (request->kind == LoadKind::Initial
               && requestKey == m_activeInitialRequestKey) {
        m_activeInitialRequestKey.clear();
        setLoading(false);
        setTotalRecordCount(totalRecordCount);
        setItems(items);
        qCDebug(lcViewModels) << "LibraryViewModel: initial load completed in" << m_loadTimer.elapsed() << "ms";
        
        // Cache the data for faster back navigation (only for initial loads)
        if (request->offset == 0) {
            updateCache(request->datasetKey, items, totalRecordCount);
        } else {
            updateCachePage(
                request->datasetKey, items, totalRecordCount, request->offset);
        }
        
        emit loadComplete();
        emit canLoadMoreChanged();
    } else {
        rejectStaleRequest(
            requestKey, QStringLiteral("active load kind changed"));
    }
}

void LibraryViewModel::onItemsNotModified(const QString &connectionId,
                                          const QString &parentId,
                                          const QString &requestKey)
{
    const auto request =
        takeCurrentRequest(connectionId, parentId, requestKey);
    if (!request) {
        return;
    }

    if (request->kind == LoadKind::BackgroundRefresh
        && requestKey == m_activeBackgroundRequestKey) {
        m_activeBackgroundRequestKey.clear();
        m_isBackgroundRefresh = false;
        const LibraryCacheEntry cached = getCachedData(request->datasetKey);
        if (cached.hasSnapshot()) {
            updateCache(
                request->datasetKey, cached.items, cached.totalRecordCount);
        }
    } else if (request->kind == LoadKind::Initial
               && requestKey == m_activeInitialRequestKey) {
        m_activeInitialRequestKey.clear();
        setLoading(false);
        emit loadComplete();
    } else if (request->kind == LoadKind::LoadMore
               && requestKey == m_activeLoadMoreRequestKey) {
        m_activeLoadMoreRequestKey.clear();
        setIsLoadingMore(false);
        emit loadMoreComplete();
    } else {
        rejectStaleRequest(
            requestKey,
            QStringLiteral("not-modified no longer owns active state"));
    }
}

void LibraryViewModel::onItemsFailed(const QString &connectionId,
                                     const QString &parentId,
                                     const QString &requestKey,
                                     const QString &error)
{
    const auto request =
        takeCurrentRequest(connectionId, parentId, requestKey);
    if (!request) {
        return;
    }

    qCWarning(lcViewModels) << "LibraryViewModel request failed"
                            << requestKey << error;
    if (request->kind == LoadKind::BackgroundRefresh
        && requestKey == m_activeBackgroundRequestKey) {
        m_activeBackgroundRequestKey.clear();
        m_isBackgroundRefresh = false;
        qCDebug(lcViewModels)
            << "LibraryViewModel: preserving cached content after SWR failure";
        return;
    }
    if (request->kind == LoadKind::LoadMore
        && requestKey == m_activeLoadMoreRequestKey) {
        m_activeLoadMoreRequestKey.clear();
        setIsLoadingMore(false);
    } else if (request->kind == LoadKind::Initial
               && requestKey == m_activeInitialRequestKey) {
        m_activeInitialRequestKey.clear();
        setLoading(false);
    } else {
        rejectStaleRequest(
            requestKey, QStringLiteral("failure no longer owns active state"));
        return;
    }

    setError(mapNetworkError(QStringLiteral("getItems"), error));
    emit loadError(error);
    emit canLoadMoreChanged();
}

void LibraryViewModel::onFilterOptionsLoaded(const QString &connectionId,
                                             const QString &parentId,
                                             const QString &requestKey,
                                             const QStringList &genres,
                                             const QStringList &tags,
                                             const QStringList &studios)
{
    const auto request =
        takeCurrentRequest(connectionId, parentId, requestKey);
    if (!request || request->kind != LoadKind::FilterOptions
        || requestKey != m_activeFilterRequestKey) {
        return;
    }

    m_activeFilterRequestKey.clear();
    m_availableGenres = genres;
    m_availableTags = tags;
    m_availableStudios = studios;
    setFilterOptionsLoading(false);
    emit filterOptionsChanged();
}

void LibraryViewModel::onFilterOptionsFailed(const QString &connectionId,
                                             const QString &parentId,
                                             const QString &requestKey,
                                             const QString &error)
{
    const auto request =
        takeCurrentRequest(connectionId, parentId, requestKey);
    if (!request || request->kind != LoadKind::FilterOptions
        || requestKey != m_activeFilterRequestKey) {
        return;
    }

    m_activeFilterRequestKey.clear();
    setFilterOptionsLoading(false);
    qCWarning(lcViewModels) << "LibraryViewModel: filter options failed"
                            << error;
}

int LibraryViewModel::activeFilterCount() const
{
    int count = 0;
    count += m_selectedGenres.size();
    count += m_selectedTags.size();
    count += m_selectedStudios.size();
    if (m_watchedFilter != "any")
        ++count;
    if (m_favoriteFilter != "any")
        ++count;
    if (m_addedSinceFilter != "any")
        ++count;
    if (m_minYear > 0 || m_maxYear > 0)
        ++count;
    if (m_minCommunityRating > 0.0)
        ++count;
    return count;
}

void LibraryViewModel::setSearchTerm(const QString &term)
{
    const QString normalized = term.trimmed();
    if (m_searchTerm == normalized)
        return;
    m_searchTerm = normalized;
    emit searchTermChanged();
}

void LibraryViewModel::setSortBy(const QString &sortBy)
{
    static const QStringList allowed = {
        QString(),
        QStringLiteral("SortName"),
        QStringLiteral("PremiereDate"),
        QStringLiteral("DateCreated"),
        QStringLiteral("CommunityRating"),
        QStringLiteral("ProductionYear"),
        QStringLiteral("Random"),
    };
    const QString normalized = allowed.contains(sortBy) ? sortBy : QString();
    if (m_sortBy == normalized)
        return;
    m_sortBy = normalized;
    emit sortByChanged();
}

void LibraryViewModel::setSortOrder(const QString &sortOrder)
{
    static const QStringList allowed = {
        QString(),
        QStringLiteral("Ascending"),
        QStringLiteral("Descending"),
    };
    const QString normalized = allowed.contains(sortOrder) ? sortOrder : QString();
    if (m_sortOrder == normalized)
        return;
    m_sortOrder = normalized;
    emit sortOrderChanged();
}

void LibraryViewModel::setWatchedFilter(const QString &filter)
{
    const QString normalized = (filter == "played" || filter == "unplayed") ? filter : QStringLiteral("any");
    if (m_watchedFilter == normalized)
        return;
    m_watchedFilter = normalized;
    emit watchedFilterChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::setFavoriteFilter(const QString &filter)
{
    const QString normalized = (filter == "favorite" || filter == "notFavorite") ? filter : QStringLiteral("any");
    if (m_favoriteFilter == normalized)
        return;
    m_favoriteFilter = normalized;
    emit favoriteFilterChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::setAddedSinceFilter(const QString &filter)
{
    static const QStringList allowed = {"any", "7d", "30d", "90d", "1y"};
    const QString normalized = allowed.contains(filter) ? filter : QStringLiteral("any");
    if (m_addedSinceFilter == normalized)
        return;
    m_addedSinceFilter = normalized;
    emit addedSinceFilterChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::setMinYear(int year)
{
    const int normalized = qMax(0, year);
    if (m_minYear == normalized)
        return;
    m_minYear = normalized;
    emit yearRangeChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::setMaxYear(int year)
{
    const int normalized = qMax(0, year);
    if (m_maxYear == normalized)
        return;
    m_maxYear = normalized;
    emit yearRangeChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::setMinCommunityRating(double rating)
{
    const double normalized = qBound(0.0, rating, 10.0);
    if (qFuzzyCompare(m_minCommunityRating + 1.0, normalized + 1.0))
        return;
    m_minCommunityRating = normalized;
    emit minCommunityRatingChanged();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::toggleGenre(const QString &genre)
{
    toggleString(m_selectedGenres, genre, &LibraryViewModel::selectedGenresChanged);
}

void LibraryViewModel::toggleTag(const QString &tag)
{
    toggleString(m_selectedTags, tag, &LibraryViewModel::selectedTagsChanged);
}

void LibraryViewModel::toggleStudio(const QString &studio)
{
    toggleString(m_selectedStudios, studio, &LibraryViewModel::selectedStudiosChanged);
}

void LibraryViewModel::clearQuery()
{
    if (!m_searchTerm.isEmpty()) {
        m_searchTerm.clear();
        emit searchTermChanged();
    }
    if (!m_sortBy.isEmpty()) {
        m_sortBy.clear();
        emit sortByChanged();
    }
    if (!m_sortOrder.isEmpty()) {
        m_sortOrder.clear();
        emit sortOrderChanged();
    }
    clearFilters();
}

void LibraryViewModel::clearFilters()
{
    const bool hadFilters = activeFilterCount() > 0;
    if (!m_selectedGenres.isEmpty()) {
        m_selectedGenres.clear();
        emit selectedGenresChanged();
    }
    if (!m_selectedTags.isEmpty()) {
        m_selectedTags.clear();
        emit selectedTagsChanged();
    }
    if (!m_selectedStudios.isEmpty()) {
        m_selectedStudios.clear();
        emit selectedStudiosChanged();
    }
    if (m_watchedFilter != "any") {
        m_watchedFilter = "any";
        emit watchedFilterChanged();
    }
    if (m_favoriteFilter != "any") {
        m_favoriteFilter = "any";
        emit favoriteFilterChanged();
    }
    if (m_addedSinceFilter != "any") {
        m_addedSinceFilter = "any";
        emit addedSinceFilterChanged();
    }
    if (m_minYear != 0 || m_maxYear != 0) {
        m_minYear = 0;
        m_maxYear = 0;
        emit yearRangeChanged();
    }
    if (m_minCommunityRating > 0.0) {
        m_minCommunityRating = 0.0;
        emit minCommunityRatingChanged();
    }
    if (hadFilters) {
        emitActiveFilterCountChanged();
    }
}

void LibraryViewModel::reloadWithCurrentQuery()
{
    if (!m_currentParentId.isEmpty()) {
        loadLibrary(m_currentParentId, m_currentCollectionType, 0, m_pageLimit);
    }
}

void LibraryViewModel::loadFilterOptions(const QString &parentId, const QString &collectionType)
{
    if (!m_libraryService || parentId.isEmpty()
        || parentId != m_currentParentId) {
        return;
    }
    if (!m_activeFilterRequestKey.isEmpty()) {
        m_pendingRequests.remove(m_activeFilterRequestKey);
    }
    setFilterOptionsLoading(true);
    m_activeFilterRequestKey = registerRequest(
        LoadKind::FilterOptions,
        parentId,
        QString(),
        0,
        0,
        false);
    m_libraryService->getFilterOptionsForRequest(
        parentId,
        includeItemTypesForCollection(collectionType),
        true,
        m_activeFilterRequestKey);
}

LibraryItemQuery LibraryViewModel::buildCurrentQuery(int startIndex, int limit, bool includeHeavyFields) const
{
    LibraryItemQuery query;
    query.parentId = m_currentParentId;
    query.startIndex = startIndex;
    query.limit = limit;
    query.searchTerm = m_searchTerm;
    query.genres = m_selectedGenres;
    query.tags = m_selectedTags;
    query.studios = m_selectedStudios;
    query.minDateLastSaved = addedSinceDate();
    query.watched = triStateFromFilter(m_watchedFilter);
    query.favorite = triStateFromFilter(m_favoriteFilter);
    query.minCommunityRating = m_minCommunityRating;
    query.sortBy = m_sortBy;
    query.sortOrder = m_sortOrder;
    query.includeHeavyFields = includeHeavyFields;
    query.includeItemTypes = includeItemTypesForCollection(m_currentCollectionType);

    const bool hasLibraryQuery = !m_searchTerm.isEmpty()
        || !m_selectedGenres.isEmpty()
        || !m_selectedTags.isEmpty()
        || !m_selectedStudios.isEmpty()
        || m_watchedFilter != "any"
        || m_favoriteFilter != "any"
        || m_addedSinceFilter != "any"
        || m_minYear > 0
        || m_maxYear > 0
        || m_minCommunityRating > 0.0
        || !m_sortBy.isEmpty()
        || !m_sortOrder.isEmpty();
    query.recursive = hasLibraryQuery && !query.includeItemTypes.isEmpty();

    if (m_minYear > 0) {
        query.minPremiereDate = QDate(m_minYear, 1, 1);
    }
    if (m_maxYear > 0) {
        query.maxPremiereDate = QDate(m_maxYear, 12, 31);
    }
    return query;
}

QStringList LibraryViewModel::includeItemTypesForCollection(const QString &collectionType) const
{
    if (collectionType == "movies") {
        return {"Movie"};
    }
    if (collectionType == "tvshows" || collectionType == "anime") {
        return {"Series"};
    }
    if (collectionType == "mixed" || collectionType.isEmpty()) {
        return {"Movie", "Series"};
    }
    return QStringList();
}

LibraryItemQuery::TriState LibraryViewModel::triStateFromFilter(const QString &filter) const
{
    if (filter == "played" || filter == "favorite")
        return LibraryItemQuery::TriState::Yes;
    if (filter == "unplayed" || filter == "notFavorite")
        return LibraryItemQuery::TriState::No;
    return LibraryItemQuery::TriState::Any;
}

QDate LibraryViewModel::addedSinceDate() const
{
    const QDate today = QDate::currentDate();
    if (m_addedSinceFilter == "7d")
        return today.addDays(-7);
    if (m_addedSinceFilter == "30d")
        return today.addDays(-30);
    if (m_addedSinceFilter == "90d")
        return today.addDays(-90);
    if (m_addedSinceFilter == "1y")
        return today.addYears(-1);
    return QDate();
}

void LibraryViewModel::emitActiveFilterCountChanged()
{
    emit activeFilterCountChanged();
}

void LibraryViewModel::setFilterOptionsLoading(bool loading)
{
    if (m_filterOptionsLoading == loading)
        return;
    m_filterOptionsLoading = loading;
    emit filterOptionsLoadingChanged();
}

void LibraryViewModel::toggleString(QStringList &list, const QString &value, void (LibraryViewModel::*signal)())
{
    const QString normalized = value.trimmed();
    if (normalized.isEmpty())
        return;
    if (list.contains(normalized)) {
        list.removeAll(normalized);
    } else {
        list.append(normalized);
        std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
    }
    emit (this->*signal)();
    emitActiveFilterCountChanged();
}

void LibraryViewModel::beginGeneration()
{
    ++m_requestGeneration;
    m_pendingRequests.clear();
    m_activeViewsRequestKey.clear();
    m_activeInitialRequestKey.clear();
    m_activeLoadMoreRequestKey.clear();
    m_activeBackgroundRequestKey.clear();
    m_activeFilterRequestKey.clear();
    m_isBackgroundRefresh = false;
    m_loadingViews = false;
    setLoading(false);
    setIsLoadingMore(false);
    setFilterOptionsLoading(false);
}

QString LibraryViewModel::registerRequest(LoadKind kind,
                                          const QString &parentId,
                                          const QString &datasetKey,
                                          int offset,
                                          int limit,
                                          bool includeHeavyFields)
{
    const QString requestKey =
        QStringLiteral("library:%1:%2:%3")
            .arg(m_requestGeneration)
            .arg(++m_requestSerial)
            .arg(static_cast<int>(kind));
    RequestIdentity request;
    request.generation = m_requestGeneration;
    request.kind = kind;
    request.connectionId = requestConnectionId();
    request.parentId = parentId;
    request.datasetKey = datasetKey;
    request.offset = offset;
    request.limit = limit;
    request.includeHeavyFields = includeHeavyFields;
    m_pendingRequests.insert(requestKey, request);
    return requestKey;
}

std::optional<LibraryViewModel::RequestIdentity>
LibraryViewModel::takeCurrentRequest(const QString &connectionId,
                                     const QString &parentId,
                                     const QString &requestKey)
{
    const auto it = m_pendingRequests.find(requestKey);
    if (it == m_pendingRequests.end()) {
        rejectStaleRequest(requestKey, QStringLiteral("request is no longer pending"));
        return std::nullopt;
    }

    const RequestIdentity request = it.value();
    if (request.generation != m_requestGeneration) {
        rejectStaleRequest(requestKey, QStringLiteral("generation changed"));
        return std::nullopt;
    }
    if (!connectionId.isEmpty() && connectionId != request.connectionId) {
        rejectStaleRequest(requestKey, QStringLiteral("connection changed"));
        return std::nullopt;
    }
    if (request.connectionId != m_activeConnectionId) {
        rejectStaleRequest(
            requestKey, QStringLiteral("active connection changed"));
        return std::nullopt;
    }
    if (request.parentId != parentId
        || request.parentId != m_currentParentId) {
        rejectStaleRequest(requestKey, QStringLiteral("parent changed"));
        return std::nullopt;
    }
    if (!request.datasetKey.isEmpty()
        && request.datasetKey != m_activeDatasetKey) {
        rejectStaleRequest(requestKey, QStringLiteral("dataset changed"));
        return std::nullopt;
    }
    m_pendingRequests.erase(it);
    return request;
}

QString LibraryViewModel::requestConnectionId() const
{
    const QString cacheScope = connectionScopeId();
    if (cacheScope != QStringLiteral("_local")) {
        return cacheScope;
    }
    if (m_libraryService) {
        const QString serviceConnectionId =
            m_libraryService->getActiveConnectionId();
        if (!serviceConnectionId.isEmpty()) {
            return serviceConnectionId;
        }
    }
    return cacheScope;
}

void LibraryViewModel::rejectStaleRequest(const QString &requestKey,
                                          const QString &reason) const
{
    ++m_staleRequestRejections;
    qCDebug(lcViewModels) << "LibraryViewModel: rejected stale request"
                          << requestKey << reason
                          << "count:" << m_staleRequestRejections;
}

QJsonArray LibraryViewModel::mergePage(const LibraryCacheEntry &cached,
                                       const QJsonArray &page,
                                       int offset,
                                       int totalRecordCount) const
{
    QJsonArray merged = cached.hasSnapshot() ? cached.items : QJsonArray();
    const int normalizedOffset = qMax(0, offset);
    for (int i = 0; i < page.size(); ++i) {
        const int position = normalizedOffset + i;
        if (position < merged.size()) {
            merged.replace(position, page.at(i));
        } else {
            merged.append(page.at(i));
        }
    }
    while (totalRecordCount >= 0 && merged.size() > totalRecordCount) {
        merged.removeLast();
    }
    return merged;
}

void LibraryViewModel::updateCachePage(const QString &datasetKey,
                                       const QJsonArray &page,
                                       int totalRecordCount,
                                       int offset)
{
    if (!page.isEmpty() && !isCanonicalCachePayload(page)) {
        qCWarning(lcViewModels)
            << "LibraryViewModel: refusing non-canonical cache page for"
            << datasetKey;
        return;
    }

    const LibraryCacheEntry cached = getCachedData(datasetKey);
    const int normalizedOffset = qMax(0, offset);
    if (normalizedOffset > cached.items.size()) {
        qCWarning(lcViewModels)
            << "LibraryViewModel: refusing non-contiguous cache page for"
            << datasetKey << "at offset" << normalizedOffset
            << "with cached prefix size" << cached.items.size();
        return;
    }

    LibraryCacheEntry entry;
    entry.items = mergePage(cached, page, normalizedOffset, totalRecordCount);
    entry.totalRecordCount = totalRecordCount;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    storeMemoryCache(scopedCacheKey(datasetKey), entry);

    if (m_cacheStore && m_cacheStore->isOpen()
        && !m_cacheStore->upsertItems(
            datasetKey, page, totalRecordCount, false, normalizedOffset)) {
        qCWarning(lcViewModels)
            << "LibraryViewModel: failed to upsert paginated cache for"
            << datasetKey;
    }
}

void LibraryViewModel::setIsLoadingMore(bool loading)
{
    if (m_isLoadingMore == loading)
        return;
    m_isLoadingMore = loading;
    emit isLoadingMoreChanged();
    emit canLoadMoreChanged();
}

void LibraryViewModel::setTotalRecordCount(int count)
{
    if (m_totalRecordCount == count)
        return;
    m_totalRecordCount = count;
    emit totalRecordCountChanged();
}

bool LibraryViewModel::isEmptyFolder(const QJsonObject &item) const
{
    const QString type = item.value(QStringLiteral("mediaType")).toString();
    
    // Types that are containers and should be filtered if empty
    static const QStringList containerTypes = {
        "Folder",
        "BoxSet",
        "Series",
        "Season",
        "MusicAlbum",
        "MusicArtist",
        "PhotoAlbum",
        "Playlist"
    };
    
    if (containerTypes.contains(type)) {
        // Check if ChildCount exists and is 0
        if (item.contains(QStringLiteral("childCount"))) {
            int childCount = item.value(QStringLiteral("childCount")).toInt();
            if (childCount == 0) {
                qCDebug(lcViewModels) << "Filtering out empty" << type << ":"
                                      << item.value(QStringLiteral("name")).toString();
                return true;
            }
        }
    }
    
    return false;
}

void LibraryViewModel::setItems(const QJsonArray &items)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (const QJsonValue &val : items) {
        QJsonObject item = val.toObject();
        if (!isEmptyFolder(item)) {
            m_items.append(item);
        }
    }
    endResetModel();
}

void LibraryViewModel::appendItems(const QJsonArray &items)
{
    if (items.isEmpty())
        return;
    
    // Filter out empty folders first
    QList<QJsonObject> filteredItems;
    filteredItems.reserve(items.size());
    for (const QJsonValue &val : items) {
        QJsonObject item = val.toObject();
        if (!isEmptyFolder(item)) {
            filteredItems.append(item);
        }
    }
    
    if (filteredItems.isEmpty())
        return;
    
    int firstNew = m_items.size();
    int lastNew = firstNew + filteredItems.size() - 1;
    
    beginInsertRows(QModelIndex(), firstNew, lastNew);
    m_items.reserve(m_items.size() + filteredItems.size());
    for (const QJsonObject &item : filteredItems) {
        m_items.append(item);
    }
    endInsertRows();
}

QString LibraryViewModel::getImageUrl(const QJsonObject &item) const
{
    if (!m_libraryService)
        return QString();

    const QString type = item.value(QStringLiteral("mediaType")).toString();
    const QStringList candidates = type == QStringLiteral("Episode")
        ? QStringList{QStringLiteral("thumbArtwork"),
                      QStringLiteral("primaryArtwork"),
                      QStringLiteral("parentPrimaryArtwork"),
                      QStringLiteral("seriesPrimaryArtwork")}
        : QStringList{QStringLiteral("primaryArtwork"),
                      QStringLiteral("parentPrimaryArtwork"),
                      QStringLiteral("seriesPrimaryArtwork")};

    for (const QString &key : candidates) {
        const QJsonObject artwork = item.value(key).toObject();
        const QString connectionId = artwork.value(QStringLiteral("connectionId")).toString();
        const QString itemId = artwork.value(QStringLiteral("itemId")).toString();
        const QString kind = artwork.value(QStringLiteral("kind")).toString();
        const QString tag = artwork.value(QStringLiteral("tag")).toString();
        if (!connectionId.isEmpty() && !itemId.isEmpty() && !kind.isEmpty() && !tag.isEmpty()) {
            return m_libraryService->getCachedArtworkUrlFromRef(
                artwork.toVariantMap(), 640);
        }
    }

    return QString();
}

// ============================================================================
// Cache Management
// ============================================================================

bool LibraryViewModel::hasCachedData(const QString &parentId) const
{
    const QString memoryKey = scopedCacheKey(parentId);
    if (s_libraryCache.contains(memoryKey)) {
        const LibraryCacheEntry entry = s_libraryCache.value(memoryKey);
        if (!isCanonicalCachePayload(entry.items)) {
            removeMemoryCache(memoryKey);
        } else if (entry.isValid(kCacheTtlMs)) {
            touchMemoryCache(memoryKey);
            return true;
        }
    }

    if (m_cacheStore && m_cacheStore->isOpen()) {
        auto slice = m_cacheStore->read(parentId);
        if (slice.hasSnapshot()
            && (!slice.hasData() || !isCanonicalCachePayload(slice.items))) {
            m_cacheStore->clearParent(parentId);
        } else if (slice.hasData() && slice.isFresh(kDiskCacheTtlMs)) {
            LibraryCacheEntry entry;
            entry.items = slice.items;
            entry.totalRecordCount = slice.totalCount;
            entry.timestamp = slice.updatedAtMs;
            storeMemoryCache(memoryKey, entry);
            return true;
        }
    }

    return false;
}

bool LibraryViewModel::hasAnyCachedData(const QString &parentId) const
{
    const QString memoryKey = scopedCacheKey(parentId);
    if (s_libraryCache.contains(memoryKey)) {
        const LibraryCacheEntry entry = s_libraryCache.value(memoryKey);
        if (!isCanonicalCachePayload(entry.items)) {
            removeMemoryCache(memoryKey);
        } else if (entry.hasSnapshot()) {
            touchMemoryCache(memoryKey);
            return true;
        }
    }

    if (m_cacheStore && m_cacheStore->isOpen()) {
        auto slice = m_cacheStore->read(parentId);
        if (slice.hasSnapshot()
            && (!slice.hasData() || !isCanonicalCachePayload(slice.items))) {
            m_cacheStore->clearParent(parentId);
        } else if (slice.hasData()) {
            LibraryCacheEntry entry;
            entry.items = slice.items;
            entry.totalRecordCount = slice.totalCount;
            entry.timestamp = slice.updatedAtMs;
            storeMemoryCache(memoryKey, entry);
            return true;
        }
    }

    return false;
}

LibraryCacheEntry LibraryViewModel::getCachedData(const QString &parentId) const
{
    const QString memoryKey = scopedCacheKey(parentId);
    if (s_libraryCache.contains(memoryKey)) {
        touchMemoryCache(memoryKey);
        return s_libraryCache.value(memoryKey);
    }
    if (m_cacheStore && m_cacheStore->isOpen()) {
        const auto slice = m_cacheStore->read(parentId);
        if (slice.hasData() && isCanonicalCachePayload(slice.items)) {
            LibraryCacheEntry entry;
            entry.items = slice.items;
            entry.totalRecordCount = slice.totalCount;
            entry.timestamp = slice.updatedAtMs;
            storeMemoryCache(memoryKey, entry);
            return entry;
        }
    }
    return {};
}

void LibraryViewModel::storeMemoryCache(
    const QString &memoryKey,
    const LibraryCacheEntry &entry) const
{
    removeMemoryCache(memoryKey);
    LibraryCacheEntry stored = entry;
    stored.byteSize = cacheEntrySize(memoryKey, stored);
    s_libraryCache.insert(memoryKey, stored);
    s_libraryCacheLru.append(memoryKey);
    s_libraryCacheBytes += stored.byteSize;
    evictMemoryCache();
}

void LibraryViewModel::removeMemoryCache(const QString &memoryKey) const
{
    const auto it = s_libraryCache.find(memoryKey);
    if (it != s_libraryCache.end()) {
        s_libraryCacheBytes -= it.value().byteSize;
        s_libraryCache.erase(it);
    }
    s_libraryCacheLru.removeAll(memoryKey);
    s_libraryCacheBytes = qMax<qint64>(0, s_libraryCacheBytes);
}

void LibraryViewModel::touchMemoryCache(const QString &memoryKey) const
{
    s_libraryCacheLru.removeAll(memoryKey);
    if (s_libraryCache.contains(memoryKey)) {
        s_libraryCacheLru.append(memoryKey);
    }
}

void LibraryViewModel::evictMemoryCache() const
{
    while ((!s_libraryCacheLru.isEmpty())
           && (s_libraryCache.size() > kMemoryCacheMaxEntries
               || s_libraryCacheBytes > kMemoryCacheMaxBytes)) {
        const QString oldestKey = s_libraryCacheLru.takeFirst();
        const auto it = s_libraryCache.find(oldestKey);
        if (it == s_libraryCache.end()) {
            continue;
        }
        s_libraryCacheBytes -= it.value().byteSize;
        s_libraryCache.erase(it);
    }
    s_libraryCacheBytes = qMax<qint64>(0, s_libraryCacheBytes);
}

qint64 LibraryViewModel::cacheEntrySize(
    const QString &memoryKey,
    const LibraryCacheEntry &entry) const
{
    return memoryKey.toUtf8().size()
        + QJsonDocument(entry.items).toJson(QJsonDocument::Compact).size()
        + static_cast<qint64>(sizeof(LibraryCacheEntry));
}

void LibraryViewModel::updateCache(const QString &parentId, const QJsonArray &items, int totalRecordCount)
{
    if (!items.isEmpty() && !isCanonicalCachePayload(items)) {
        qCWarning(lcViewModels) << "LibraryViewModel: refusing to cache non-canonical items for"
                                << parentId;
        return;
    }

    LibraryCacheEntry entry;
    entry.items = items;
    entry.totalRecordCount = totalRecordCount;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();

    storeMemoryCache(scopedCacheKey(parentId), entry);

    if (m_cacheStore && m_cacheStore->isOpen()) {
        if (!m_cacheStore->replaceAll(parentId, items, totalRecordCount)) {
            qCWarning(lcViewModels) << "LibraryViewModel: failed to persist library cache for" << parentId;
        }
    }
}

void LibraryViewModel::clearCacheEntry(const QString &parentId)
{
    removeMemoryCache(scopedCacheKey(parentId));
    if (m_cacheStore && m_cacheStore->isOpen()) {
        m_cacheStore->clearParent(parentId);
    }
}

void LibraryViewModel::clearAllCache()
{
    s_libraryCache.clear();
    s_libraryCacheLru.clear();
    s_libraryCacheBytes = 0;
    if (auto *vm = ServiceLocator::tryGet<LibraryViewModel>()) {
        if (vm->m_cacheStore && vm->m_cacheStore->isOpen()) {
            vm->m_cacheStore->clearAll();
        }
    }
    qCDebug(lcViewModels) << "LibraryViewModel: Cleared all cache";
}

void LibraryViewModel::invalidateCache(const QString &parentId)
{
    const QString memoryKey = scopedCacheKey(parentId);
    if (s_libraryCache.contains(memoryKey)) {
        removeMemoryCache(memoryKey);
        qCDebug(lcViewModels) << "LibraryViewModel: Invalidated cache for" << parentId;
    }
    if (m_cacheStore && m_cacheStore->isOpen()) {
        m_cacheStore->clearParent(parentId);
    }
}

QString LibraryViewModel::connectionScopeId() const
{
    if (m_configManager) {
        const auto connection = m_configManager->getActiveConnection();
        if (connection.has_value() && !connection->connectionId.isEmpty()) {
            return connection->connectionId;
        }
    }
    return QStringLiteral("_local");
}

QString LibraryViewModel::scopedCacheKey(const QString &remoteKey) const
{
    return connectionScopeId() + QLatin1Char('\n') + remoteKey;
}

void LibraryViewModel::reopenCacheStore()
{
    const QString scopeId = connectionScopeId();
    if (m_cacheStore && m_cacheScopeId == scopeId) {
        return;
    }

    beginGeneration();
    m_activeConnectionId = requestConnectionId();

    m_cacheScopeId = scopeId;
    const QString dbPath = cacheDbPath();
    m_cacheStore = std::make_unique<LibraryCacheStore>(dbPath, kDiskCacheTtlMs);
    if (!m_cacheStore->open()) {
        qCWarning(lcViewModels) << "LibraryViewModel: failed to open library cache store at" << dbPath;
    }

    setItems(QJsonArray());
    setTotalRecordCount(0);
    m_views.clear();
    emit viewsChanged();
    if (!m_currentParentId.isEmpty()) {
        m_currentParentId.clear();
        emit currentParentIdChanged();
    }
    m_activeDatasetKey.clear();
}

QString LibraryViewModel::cacheDir() const
{
    QString baseDir;
    if (m_configManager) {
        baseDir = m_configManager->getConfigDir();
    } else {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/Bloom";
    }
    const QString scope = DetailViewCache::connectionScopeCacheKey(connectionScopeId());
    return baseDir + QStringLiteral("/cache/connections/") + scope + QStringLiteral("/library");
}

QString LibraryViewModel::cacheDbPath() const
{
    QString dir = cacheDir();
    QDir d(dir);
    if (!d.exists()) {
        d.mkpath(".");
    }
    return d.filePath("library_cache.db");
}

bool LibraryViewModel::hasDataChanged(const QJsonArray &newItems, int newTotal, const LibraryCacheEntry &cached) const
{
    // Quick checks first
    if (newTotal != cached.totalRecordCount) {
        qCDebug(lcViewModels) << "LibraryViewModel: SWR total changed" << cached.totalRecordCount << "->" << newTotal;
        return true;
    }
    
    if (newItems.size() != cached.items.size()) {
        qCDebug(lcViewModels) << "LibraryViewModel: SWR item count changed" << cached.items.size() << "->" << newItems.size();
        return true;
    }
    
    // Compare item IDs to detect changes (additions, removals, reorders)
    for (int i = 0; i < newItems.size(); ++i) {
        QString newId = newItems[i].toObject().value(QStringLiteral("itemId")).toString();
        QString cachedId = cached.items[i].toObject().value(QStringLiteral("itemId")).toString();
        if (newId != cachedId) {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR item ID mismatch at" << i << ":" << cachedId << "->" << newId;
            return true;
        }
    }
    
    return false;
}

bool LibraryViewModel::isCanonicalCachePayload(const QJsonArray &items) const
{
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            return false;
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("itemId")).toString().isEmpty()
            || item.contains(QStringLiteral("Id"))) {
            return false;
        }
    }
    return true;
}

void LibraryViewModel::updateItemsFromBackground(const QJsonArray &items)
{
    // For SWR: update the model with fresh data while minimizing UI disruption
    // We use beginResetModel/endResetModel but Qt's view should preserve scroll position
    // since we're not changing isLoading state
    
    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (const QJsonValue &val : items) {
        QJsonObject item = val.toObject();
        if (!isEmptyFolder(item)) {
            m_items.append(item);
        }
    }
    endResetModel();
    
    qCDebug(lcViewModels) << "LibraryViewModel: SWR updated model with" << m_items.size() << "items";
}
