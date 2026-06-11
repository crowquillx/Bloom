#include "LibraryViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
#include "../utils/LibraryCacheStore.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <algorithm>
#include "../utils/BloomLogging.h"

// Static cache initialization
QHash<QString, LibraryCacheEntry> LibraryViewModel::s_libraryCache;

LibraryViewModel::LibraryViewModel(QObject *parent)
    : BaseViewModel(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_configManager = ServiceLocator::tryGet<ConfigManager>();
    
    QString dbPath = cacheDbPath();
    m_cacheStore = std::make_unique<LibraryCacheStore>(dbPath, kDiskCacheTtlMs);
    if (!m_cacheStore->open()) {
        qCWarning(lcViewModels) << "LibraryViewModel: failed to open library cache store at" << dbPath;
    }

    if (m_libraryService) {
        connect(m_libraryService, &LibraryService::viewsLoaded,
                this, &LibraryViewModel::onViewsLoaded);
        connect(m_libraryService, &LibraryService::itemsLoaded,
                this, &LibraryViewModel::onItemsLoaded);
        connect(m_libraryService, &LibraryService::itemsLoadedWithTotal,
                this, &LibraryViewModel::onItemsLoadedWithTotal);
        connect(m_libraryService, &LibraryService::itemsLoadedWithTotalForQuery,
                this, &LibraryViewModel::onItemsLoadedWithTotalForQuery);
        connect(m_libraryService, &LibraryService::filterOptionsLoaded,
                this, &LibraryViewModel::onFilterOptionsLoaded);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &LibraryViewModel::onErrorOccurred);
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
        return item.value("Name").toString();
    case ImageUrlRole:
        return getImageUrl(item);
    case IdRole:
        return item.value("Id").toString();
    case TypeRole:
        return item.value("Type").toString();
    case ModelDataRole:
        return item.toVariantMap();
    case ProductionYearRole:
        return item.value("ProductionYear").toInt();
    case IndexNumberRole:
        return item.value("IndexNumber").toInt();
    case ParentIndexNumberRole:
        return item.value("ParentIndexNumber").toInt();
    case OverviewRole:
        return item.value("Overview").toString();
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

    m_currentParentId = parentId;
    m_currentCollectionType = collectionType;
    m_lastStartIndex = startIndex;
    m_lastLimit = limit;
    // Use lightweight fields for paginated (library) loads; heavy fields for full detail (limit==0)
    m_lastIncludeHeavyFields = (limit == 0);
    m_loadingViews = false;
    m_isBackgroundRefresh = false;
    
    emit currentParentIdChanged();
    
    // SWR Pattern: Check for any cached data (even stale) for initial loads
    LibraryItemQuery query = buildCurrentQuery(startIndex, limit, m_lastIncludeHeavyFields);
    m_activeQueryKey = query.cacheKey();
    query.requestKey = m_activeQueryKey;

    if (startIndex == 0 && hasAnyCachedData(m_activeQueryKey)) {
        LibraryCacheEntry cached = getCachedData(m_activeQueryKey);
        bool isStale = !cached.isValid(kCacheTtlMs);
        
        qCDebug(lcViewModels) << "LibraryViewModel::loadLibrary SWR" << (isStale ? "STALE" : "FRESH") 
                 << "cache for" << m_activeQueryKey
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
        qCDebug(lcViewModels) << "LibraryViewModel: SWR background refresh for" << m_activeQueryKey;
        m_isBackgroundRefresh = true;
        m_loadTimer.restart();
        clearError();
        m_libraryService->getItems(query);
        return;
    }
    
    // No cached data - do a normal blocking load with spinner
    setLoading(true);
    m_loadTimer.restart();
    clearError();

    qCDebug(lcViewModels) << "LibraryViewModel::loadLibrary" << parentId << "queryKey:" << m_activeQueryKey << "startIndex:" << startIndex << "limit:" << limit << "heavyFields:" << m_lastIncludeHeavyFields;
    m_libraryService->getItems(query);
}

void LibraryViewModel::loadViews()
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    m_currentParentId.clear();
    m_currentCollectionType.clear();
    m_activeQueryKey.clear();
    m_lastStartIndex = 0;
    m_lastLimit = 0;
    m_loadingViews = true;
    
    emit currentParentIdChanged();
    setLoading(true);
    clearError();

    qCDebug(lcViewModels) << "LibraryViewModel::loadViews";
    m_libraryService->getViews();
}

void LibraryViewModel::refresh()
{
    if (m_loadingViews || m_currentParentId.isEmpty()) {
        loadViews();
    } else {
        loadLibrary(m_currentParentId, m_lastStartIndex, m_lastLimit);
    }
}

void LibraryViewModel::reload()
{
    refresh();
}

void LibraryViewModel::clear()
{
    if (m_items.isEmpty())
        return;

    beginResetModel();
    m_items.clear();
    endResetModel();

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
    
    // Store the start index for the incremental load handler
    m_lastStartIndex = startIndex;
    m_lastLimit = limit;
    m_lastIncludeHeavyFields = false; // loadMore is always for paginated library loads

    LibraryItemQuery query = buildCurrentQuery(startIndex, limit, m_lastIncludeHeavyFields);
    m_activeQueryKey = query.cacheKey();
    query.requestKey = m_activeQueryKey;
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

void LibraryViewModel::onViewsLoaded(const QJsonArray &views)
{
    qCDebug(lcViewModels) << "LibraryViewModel::onViewsLoaded" << views.size() << "items, loadingViews:" << m_loadingViews;
    
    // Always store views for Settings access (library profile assignments)
    // even if we didn't initiate the request (HomeScreen might have)
    // Filter out "Collections" (boxsets) since they just link to items from other libraries
    QVariantList viewsList;
    for (const auto &view : views) {
        QJsonObject viewObj = view.toObject();
        QString collectionType = viewObj["CollectionType"].toString();
        // Skip "boxsets" (Collections library) - items use their home library's profile
        if (collectionType == "boxsets") {
            qCDebug(lcViewModels) << "LibraryViewModel: Filtering out Collections library from views";
            continue;
        }
        viewsList.append(viewObj.toVariantMap());
    }
    if (m_views != viewsList) {
        m_views = viewsList;
        emit viewsChanged();
    }
    
    // Only update the list model if we were the ones loading views
    if (!m_loadingViews)
        return;

    setLoading(false);
    setTotalRecordCount(views.size());
    setItems(views);
    
    emit loadComplete();
}

void LibraryViewModel::onItemsLoaded(const QString &parentId, const QJsonArray &items)
{
    // This signal is emitted alongside itemsLoadedWithTotal for backward compatibility.
    // We handle everything in onItemsLoadedWithTotal to avoid double-processing.
    Q_UNUSED(parentId)
    Q_UNUSED(items)
}

void LibraryViewModel::onItemsLoadedWithTotal(const QString &parentId, const QJsonArray &items, int totalRecordCount)
{
    if (!m_activeQueryKey.isEmpty())
        return;
    onItemsLoadedWithTotalForQuery(parentId, QString(), items, totalRecordCount);
}

void LibraryViewModel::onItemsLoadedWithTotalForQuery(const QString &parentId, const QString &queryKey, const QJsonArray &items, int totalRecordCount)
{
    if (parentId != m_currentParentId)
        return;
    if (!queryKey.isEmpty() && queryKey != m_activeQueryKey) {
        qCDebug(lcViewModels) << "LibraryViewModel: ignoring stale query result" << queryKey << "active:" << m_activeQueryKey;
        return;
    }

    const QString cacheKey = queryKey.isEmpty() ? parentId : queryKey;

    qCDebug(lcViewModels) << "LibraryViewModel::onItemsLoadedWithTotalForQuery" << parentId << items.size()
             << "items, total:" << totalRecordCount 
             << "backgroundRefresh:" << m_isBackgroundRefresh
             << "queryKey:" << cacheKey;
    
    // Check if this is an incremental load (loadMore) or initial load
    if (m_isLoadingMore) {
        setIsLoadingMore(false);
        setTotalRecordCount(totalRecordCount);
        appendItems(items);
        qCDebug(lcViewModels) << "LibraryViewModel: loadMore completed in" << m_loadMoreTimer.elapsed() << "ms";
        
        // Cache incremental items without rewriting the whole dataset
        QJsonArray filteredItems;
        for (int i = m_lastStartIndex; i < m_items.size(); ++i) {
            filteredItems.append(m_items.at(i));
        }

        LibraryCacheEntry entry = getCachedData(cacheKey);
        if (!entry.hasData()) {
            entry.items = QJsonArray();
        }
        for (const auto &val : filteredItems) {
            entry.items.append(val);
        }
        entry.totalRecordCount = totalRecordCount;
        entry.timestamp = QDateTime::currentMSecsSinceEpoch();
        s_libraryCache[cacheKey] = entry;

        if (m_cacheStore && m_cacheStore->isOpen()) {
            if (!m_cacheStore->upsertItems(cacheKey, filteredItems, totalRecordCount, false, m_lastStartIndex)) {
                qCWarning(lcViewModels) << "LibraryViewModel: failed to upsert paginated cache for" << cacheKey;
            }
        }
        
        emit loadMoreComplete();
        emit canLoadMoreChanged();
    } else if (m_isBackgroundRefresh) {
        // SWR: Background refresh completed
        m_isBackgroundRefresh = false;
        qCDebug(lcViewModels) << "LibraryViewModel: background refresh completed in" << m_loadTimer.elapsed() << "ms";
        
        // Check if data actually changed
        LibraryCacheEntry cached = getCachedData(cacheKey);
        if (hasDataChanged(items, totalRecordCount, cached)) {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR detected changes, updating model";
            setTotalRecordCount(totalRecordCount);
            updateItemsFromBackground(items);
            emit canLoadMoreChanged();
        } else {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR no changes detected, updating timestamp only";
        }
        
        // Always update cache with fresh data and timestamp
        updateCache(cacheKey, items, totalRecordCount);
    } else {
        setLoading(false);
        setTotalRecordCount(totalRecordCount);
        setItems(items);
        qCDebug(lcViewModels) << "LibraryViewModel: initial load completed in" << m_loadTimer.elapsed() << "ms";
        
        // Cache the data for faster back navigation (only for initial loads)
        if (m_lastStartIndex == 0) {
            updateCache(cacheKey, items, totalRecordCount);
        }
        
        emit loadComplete();
        emit canLoadMoreChanged();
    }
}

void LibraryViewModel::onErrorOccurred(const QString &endpoint, const QString &error)
{
    // Only handle errors for our current requests
    if (endpoint != "getViews" && endpoint != "getItems")
        return;

    qCWarning(lcViewModels) << "LibraryViewModel error:" << endpoint << error;
    setLoading(false);
    setError(mapNetworkError(endpoint, error));
    emit loadError(error);
}

void LibraryViewModel::onFilterOptionsLoaded(const QString &parentId,
                                             const QStringList &genres,
                                             const QStringList &tags,
                                             const QStringList &studios)
{
    if (parentId != m_currentParentId)
        return;

    m_availableGenres = genres;
    m_availableTags = tags;
    m_availableStudios = studios;
    setFilterOptionsLoading(false);
    emit filterOptionsChanged();
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
        loadLibrary(m_currentParentId, m_currentCollectionType, 0, m_lastLimit);
    }
}

void LibraryViewModel::loadFilterOptions(const QString &parentId, const QString &collectionType)
{
    if (!m_libraryService || parentId.isEmpty())
        return;
    setFilterOptionsLoading(true);
    m_libraryService->getFilterOptions(parentId, includeItemTypesForCollection(collectionType), true);
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

void LibraryViewModel::setIsLoadingMore(bool loading)
{
    if (m_isLoadingMore == loading)
        return;
    m_isLoadingMore = loading;
    emit isLoadingMoreChanged();
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
    const QString type = item.value("Type").toString();
    
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
        if (item.contains("ChildCount")) {
            int childCount = item.value("ChildCount").toInt();
            if (childCount == 0) {
                qCDebug(lcViewModels) << "Filtering out empty" << type << ":" << item.value("Name").toString();
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

    const QString id = item.value("Id").toString();
    const QString type = item.value("Type").toString();
    const QJsonObject imageTags = item.value("ImageTags").toObject();

    // 1. Try Thumb (Episodes)
    if (type == "Episode" && imageTags.contains("Thumb")) {
        return m_libraryService->getCachedImageUrlWithWidth(id, "Thumb", 640);
    }

    // 2. Try Primary (Episodes/Seasons/Series)
    if (imageTags.contains("Primary")) {
        return m_libraryService->getCachedImageUrlWithWidth(id, "Primary", 640);
    }

    // 3. Fallback to Parent Primary (e.g. Season poster for Episode)
    if (item.contains("ParentPrimaryImageTag")) {
        const QString parentId = item.value("ParentId").toString();
        if (!parentId.isEmpty()) {
            return m_libraryService->getCachedImageUrlWithWidth(parentId, "Primary", 640);
        }
    }

    // 4. Fallback to Series Primary (e.g. Series poster for Season/Episode)
    if (item.contains("SeriesPrimaryImageTag")) {
        const QString seriesId = item.value("SeriesId").toString();
        if (!seriesId.isEmpty()) {
            return m_libraryService->getCachedImageUrlWithWidth(seriesId, "Primary", 640);
        }
    }

    return QString();
}

// ============================================================================
// Cache Management
// ============================================================================

bool LibraryViewModel::hasCachedData(const QString &parentId) const
{
    if (s_libraryCache.contains(parentId) && s_libraryCache[parentId].isValid(kCacheTtlMs)) {
        return true;
    }
    
    // Try SQLite cache if memory cache is missing or stale
    if (m_cacheStore && m_cacheStore->isOpen()) {
        auto slice = m_cacheStore->read(parentId);
        if (slice.hasData() && slice.isFresh(kDiskCacheTtlMs)) {
            LibraryCacheEntry entry;
            entry.items = slice.items;
            entry.totalRecordCount = slice.totalCount;
            entry.timestamp = slice.updatedAtMs;
            s_libraryCache[parentId] = entry;
            return true;
        }
    }

    return false;
}

bool LibraryViewModel::hasAnyCachedData(const QString &parentId) const
{
    if (s_libraryCache.contains(parentId) && s_libraryCache[parentId].hasData()) {
        return true;
    }
    
    // Allow stale disk cache for SWR (serves instantly, revalidates in background)
    if (m_cacheStore && m_cacheStore->isOpen()) {
        auto slice = m_cacheStore->read(parentId);
        if (slice.hasData()) {
            LibraryCacheEntry entry;
            entry.items = slice.items;
            entry.totalRecordCount = slice.totalCount;
            entry.timestamp = slice.updatedAtMs;
            s_libraryCache[parentId] = entry;
            return true;
        }
    }

    return false;
}

LibraryCacheEntry LibraryViewModel::getCachedData(const QString &parentId) const
{
    if (!s_libraryCache.contains(parentId))
        return LibraryCacheEntry();

    return s_libraryCache[parentId];
}

void LibraryViewModel::updateCache(const QString &parentId, const QJsonArray &items, int totalRecordCount)
{
    LibraryCacheEntry entry;
    entry.items = items;
    entry.totalRecordCount = totalRecordCount;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    s_libraryCache[parentId] = entry;
    
    if (m_cacheStore && m_cacheStore->isOpen()) {
        if (!m_cacheStore->replaceAll(parentId, items, totalRecordCount)) {
            qCWarning(lcViewModels) << "LibraryViewModel: failed to persist library cache for" << parentId;
        }
    }
}

void LibraryViewModel::clearCacheEntry(const QString &parentId)
{
    s_libraryCache.remove(parentId);
    if (m_cacheStore && m_cacheStore->isOpen()) {
        m_cacheStore->clearParent(parentId);
    }
}

void LibraryViewModel::clearAllCache()
{
    s_libraryCache.clear();
    if (auto *vm = ServiceLocator::tryGet<LibraryViewModel>()) {
        if (vm->m_cacheStore && vm->m_cacheStore->isOpen()) {
            vm->m_cacheStore->clearAll();
        }
    }
    qCDebug(lcViewModels) << "LibraryViewModel: Cleared all cache";
}

void LibraryViewModel::invalidateCache(const QString &parentId)
{
    if (s_libraryCache.contains(parentId)) {
        s_libraryCache.remove(parentId);
        qCDebug(lcViewModels) << "LibraryViewModel: Invalidated cache for" << parentId;
    }
    if (m_cacheStore && m_cacheStore->isOpen()) {
        m_cacheStore->clearParent(parentId);
    }
}

QString LibraryViewModel::cacheDir() const
{
    QString baseDir;
    if (m_configManager) {
        baseDir = m_configManager->getConfigDir();
    } else {
        // Fallback to GenericCacheLocation
        baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/Bloom";
    }
    return baseDir + "/cache/library";
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
        QString newId = newItems[i].toObject().value("Id").toString();
        QString cachedId = cached.items[i].toObject().value("Id").toString();
        if (newId != cachedId) {
            qCDebug(lcViewModels) << "LibraryViewModel: SWR item ID mismatch at" << i << ":" << cachedId << "->" << newId;
            return true;
        }
    }
    
    return false;
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
