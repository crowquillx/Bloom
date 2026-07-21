#include "MockLibraryService.h"
#include "TestModeController.h"
#include "../network/NextEpisodeResolver.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include <QUrl>
#include <QDebug>
#include <algorithm>
#include "../utils/BloomLogging.h"

MockLibraryService::MockLibraryService(QObject *parent)
    : LibraryService(nullptr, parent)  // Pass nullptr for authService since we don't need it in mock
{
}

void MockLibraryService::loadFixture(const QJsonObject &fixture)
{
    m_fixture = fixture;
    m_libraries = fixture["libraries"].toObject();
    m_movies = fixture["movies"].toObject();
    m_series = fixture["series"].toObject();
    m_seasons = fixture["seasons"].toObject();
    m_episodes = fixture["episodes"].toObject();
    m_nextUp = fixture["nextUp"].toObject();
    m_latestItems = fixture["latestItems"].toObject();
    
    qCDebug(lcTest) << "MockLibraryService: Loaded fixture with";
    qCDebug(lcTest) << "  Libraries:" << m_libraries["Items"].toArray().size();
    qCDebug(lcTest) << "  Movies:" << m_movies["Items"].toArray().size();
    qCDebug(lcTest) << "  Series:" << m_series["Items"].toArray().size();
    qCDebug(lcTest) << "  Episodes:" << m_episodes["Items"].toArray().size();
}

void MockLibraryService::getViews()
{
    QJsonArray views = m_libraries["Items"].toArray();
    qCDebug(lcTest) << "MockLibraryService::getViews() ->" << views.size() << "views";
    emit viewsLoaded(views);
}

void MockLibraryService::getItems(const QString &parentId, int startIndex, int limit, 
                                   const QStringList &genres, const QStringList &networks,
                                   const QString &sortBy, const QString &sortOrder,
                                   bool includeHeavyFields, bool useCacheValidation)
{
    LibraryItemQuery query;
    query.parentId = parentId;
    query.startIndex = startIndex;
    query.limit = limit;
    query.genres = genres;
    query.studios = networks;
    query.sortBy = sortBy;
    query.sortOrder = sortOrder;
    query.includeHeavyFields = includeHeavyFields;
    query.useCacheValidation = useCacheValidation;
    getItems(query);
}

void MockLibraryService::getItems(const LibraryItemQuery &query)
{
    QJsonArray items;
    
    // Determine which library type based on parentId
    if (query.parentId == "library-movies") {
        items = m_movies["Items"].toArray();
    } else if (query.parentId == "library-shows") {
        items = m_series["Items"].toArray();
    } else {
        // Return all items for generic queries
        QJsonArray allItems;
        for (const QJsonValue &val : m_movies["Items"].toArray()) {
            allItems.append(val);
        }
        for (const QJsonValue &val : m_series["Items"].toArray()) {
            allItems.append(val);
        }
        items = allItems;
    }

    auto containsAny = [](const QJsonArray &array, const QStringList &needles) {
        if (needles.isEmpty())
            return true;
        for (const auto &value : array) {
            if (needles.contains(value.toString(), Qt::CaseInsensitive)) {
                return true;
            }
            const QJsonObject obj = value.toObject();
            if (needles.contains(obj.value("Name").toString(), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };

    QJsonArray filtered;
    for (const auto &val : items) {
        const QJsonObject item = val.toObject();
        const QString name = item.value("Name").toString();
        if (!query.searchTerm.isEmpty() && !name.contains(query.searchTerm, Qt::CaseInsensitive)) {
            continue;
        }
        if (!query.includeItemTypes.isEmpty() && !query.includeItemTypes.contains(item.value("Type").toString())) {
            continue;
        }
        if (!containsAny(item.value("Genres").toArray(), query.genres)) {
            continue;
        }
        if (!containsAny(item.value("Tags").toArray(), query.tags)) {
            continue;
        }
        if (!containsAny(item.value("Studios").toArray(), query.studios)) {
            continue;
        }
        const QJsonObject userData = item.value("UserData").toObject();
        if (query.watched == LibraryItemQuery::TriState::Yes && !userData.value("Played").toBool()) {
            continue;
        }
        if (query.watched == LibraryItemQuery::TriState::No && userData.value("Played").toBool()) {
            continue;
        }
        if (query.favorite == LibraryItemQuery::TriState::Yes && !userData.value("IsFavorite").toBool()) {
            continue;
        }
        if (query.favorite == LibraryItemQuery::TriState::No && userData.value("IsFavorite").toBool()) {
            continue;
        }
        if (query.minCommunityRating > 0.0 && item.value("CommunityRating").toDouble() < query.minCommunityRating) {
            continue;
        }
        const int year = item.value("ProductionYear").toInt();
        if (query.minPremiereDate.isValid() && year > 0 && year < query.minPremiereDate.year()) {
            continue;
        }
        if (query.maxPremiereDate.isValid() && year > 0 && year > query.maxPremiereDate.year()) {
            continue;
        }
        filtered.append(item);
    }
    items = filtered;

    QList<QJsonObject> sortable;
    sortable.reserve(items.size());
    for (const auto &val : items) {
        sortable.append(val.toObject());
    }
    const bool descending = query.sortOrder.compare("Descending", Qt::CaseInsensitive) == 0;
    const QString sortBy = query.normalizedSortBy();
    std::sort(sortable.begin(), sortable.end(), [sortBy, descending](const QJsonObject &a, const QJsonObject &b) {
        bool less = false;
        bool greater = false;
        if (sortBy.contains("DateCreated")) {
            const int cmp = QString::localeAwareCompare(a.value("DateCreated").toString(), b.value("DateCreated").toString());
            less = cmp < 0;
            greater = cmp > 0;
        } else if (sortBy.contains("PremiereDate")) {
            const int cmp = QString::localeAwareCompare(a.value("PremiereDate").toString(), b.value("PremiereDate").toString());
            less = cmp < 0;
            greater = cmp > 0;
        } else if (sortBy.contains("RunTimeTicks")) {
            const double av = a.value("RunTimeTicks").toDouble();
            const double bv = b.value("RunTimeTicks").toDouble();
            less = av < bv;
            greater = av > bv;
        } else if (sortBy.contains("CommunityRating")) {
            const double av = a.value("CommunityRating").toDouble();
            const double bv = b.value("CommunityRating").toDouble();
            less = av < bv;
            greater = av > bv;
        } else if (sortBy.contains("ProductionYear")) {
            const int av = a.value("ProductionYear").toInt();
            const int bv = b.value("ProductionYear").toInt();
            less = av < bv;
            greater = av > bv;
        } else if (sortBy.contains("IsPlayed")) {
            const bool av = a.value("UserData").toObject().value("Played").toBool();
            const bool bv = b.value("UserData").toObject().value("Played").toBool();
            less = av < bv;
            greater = av > bv;
        } else {
            const int cmp = QString::localeAwareCompare(a.value("Name").toString(), b.value("Name").toString());
            less = cmp < 0;
            greater = cmp > 0;
        }
        return descending ? greater : less;
    });

    const int totalCount = sortable.size();
    const int start = qMax(0, query.startIndex);
    const int end = query.limit > 0 ? qMin(start + query.limit, sortable.size()) : sortable.size();
    QJsonArray paged;
    for (int i = start; i < end; ++i) {
        paged.append(sortable.at(i));
    }
    
    const QString queryKey = query.requestKey.isEmpty() ? query.cacheKey() : query.requestKey;
    qCDebug(lcTest) << "MockLibraryService::getItems(" << query.parentId << ") ->" << paged.size() << "items";
    emit itemsLoadedWithTotal(query.parentId, paged, totalCount);
    emit itemsLoadedWithTotalForQuery(query.parentId, queryKey, paged, totalCount);
}

void MockLibraryService::getFilterOptions(const QString &parentId,
                                          const QStringList &includeItemTypes,
                                          bool recursive)
{
    Q_UNUSED(recursive)
    LibraryItemQuery query;
    query.parentId = parentId;
    query.includeItemTypes = includeItemTypes;

    QStringList genres;
    QStringList tags;
    QStringList studios;
    QJsonArray source;
    if (parentId == "library-movies") {
        source = m_movies["Items"].toArray();
    } else if (parentId == "library-shows") {
        source = m_series["Items"].toArray();
    }
    for (const auto &val : source) {
        const QJsonObject item = val.toObject();
        if (!includeItemTypes.isEmpty() && !includeItemTypes.contains(item.value("Type").toString())) {
            continue;
        }
        for (const auto &genre : item.value("Genres").toArray()) {
            genres.append(genre.toString());
        }
        for (const auto &tag : item.value("Tags").toArray()) {
            tags.append(tag.toString());
        }
        for (const auto &studio : item.value("Studios").toArray()) {
            studios.append(studio.isString() ? studio.toString() : studio.toObject().value("Name").toString());
        }
    }
    auto normalize = [](QStringList list) {
        list.removeAll(QString());
        list.removeDuplicates();
        std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        return list;
    };
    emit filterOptionsLoaded(parentId, normalize(genres), normalize(tags), normalize(studios));
}

void MockLibraryService::getNextUp()
{
    QJsonArray items = m_nextUp["Items"].toArray();
    qCDebug(lcTest) << "MockLibraryService::getNextUp() ->" << items.size() << "items";
    emit nextUpLoaded(items);
}

void MockLibraryService::getLatestMedia(const QString &parentId)
{
    QJsonArray items = m_latestItems["Items"].toArray();
    qCDebug(lcTest) << "MockLibraryService::getLatestMedia(" << parentId << ") ->" << items.size() << "items";
    emit latestMediaLoaded(parentId, items);
}

void MockLibraryService::getHomeBackdropItems(int limit)
{
    QJsonArray allItems;
    for (const QJsonValue &val : m_movies["Items"].toArray()) {
        allItems.append(val);
    }
    for (const QJsonValue &val : m_series["Items"].toArray()) {
        allItems.append(val);
    }
    for (const QJsonValue &val : m_seasons["Items"].toArray()) {
        allItems.append(val);
    }
    for (const QJsonValue &val : m_episodes["Items"].toArray()) {
        allItems.append(val);
    }

    // Return first N items (deterministic for test mode).
    QJsonArray result;
    const int count = (limit > 0) ? qMin(limit, allItems.size()) : allItems.size();
    for (int i = 0; i < count; ++i) {
        result.append(allItems[i]);
    }

    qCDebug(lcTest) << "MockLibraryService::getHomeBackdropItems(" << limit << ") ->" << result.size() << "items";
    emit homeBackdropItemsLoaded(result);
}

void MockLibraryService::getScreensaverItems(int limit)
{
    QJsonArray allItems;
    for (const QJsonValue &val : m_movies["Items"].toArray()) {
        allItems.append(val);
    }
    for (const QJsonValue &val : m_series["Items"].toArray()) {
        allItems.append(val);
    }

    QJsonArray result;
    const int requestedLimit = qBound(10, limit > 0 ? limit : 80, 200);
    for (int i = 0; i < allItems.size() && result.size() < requestedLimit; ++i) {
        QJsonObject item = allItems[i].toObject();
        QString itemId = item.value("Id").toString();
        QString tag;
        const QJsonArray backdropTags = item.value("BackdropImageTags").toArray();
        if (!backdropTags.isEmpty()) {
            tag = backdropTags.first().toString();
        } else {
            tag = item.value("ImageTags").toObject().value("Backdrop").toString();
        }
        const QJsonArray parentBackdropTags = item.value("ParentBackdropImageTags").toArray();
        if (tag.isEmpty() && !parentBackdropTags.isEmpty()) {
            tag = parentBackdropTags.first().toString();
            itemId = item.value("ParentBackdropItemId").toString(
                item.value("SeriesId").toString(item.value("Id").toString()));
        }
        if (itemId.isEmpty() || tag.isEmpty()) {
            continue;
        }
        item.insert(QStringLiteral("BackdropUrl"),
                    getCachedArtworkUrl(itemId,
                                        QStringLiteral("Backdrop"),
                                        0,
                                        tag,
                                        1920));
        if (item.value("ImageTags").toObject().contains(QStringLiteral("Logo"))) {
            item.insert(QStringLiteral("LogoUrl"),
                        getCachedImageUrlWithWidth(item.value("Id").toString(),
                                                   QStringLiteral("Logo"),
                                                   700));
        }
        result.append(item);
    }

    qCDebug(lcTest) << "MockLibraryService::getScreensaverItems(" << limit << ") ->" << result.size() << "items";
    emit screensaverItemsLoaded(result);
}

void MockLibraryService::getItem(const QString &itemId)
{
    getItem(itemId, QString());
}

void MockLibraryService::getItem(const QString &itemId, const QString &requestContext)
{
    QJsonObject item = findItemById(itemId);
    if (!item.isEmpty()) {
        qCDebug(lcTest) << "MockLibraryService::getItem(" << itemId << ") -> found";
        const QVariantMap canonicalItem = JellyfinModelMapper::mediaItem(
            item, QStringLiteral("mock-connection"));
        emit itemLoaded(itemId, item, requestContext);
        emit itemLoaded(itemId, item);
        emit canonicalItemLoaded(itemId, canonicalItem, requestContext);
        emit canonicalItemLoaded(itemId, canonicalItem);
    } else {
        qCWarning(lcTest) << "MockLibraryService::getItem(" << itemId << ") -> not found";
        emit itemFailed(itemId, "Item not found: " + itemId, requestContext);
        emit errorOccurred("getItem", "Item not found: " + itemId);
    }
}

void MockLibraryService::clearItemCacheValidation(const QString &itemId)
{
    if (itemId.isEmpty()) {
        return;
    }

    ++m_clearItemCacheValidationCallCount;
    m_clearedItemCacheValidationIds.insert(itemId);
}

/**
 * @brief Load detailed data for a series and emit the appropriate result signal.
 *
 * Loads the series object for the provided seriesId and emits seriesDetailsLoaded(seriesId, seriesData)
 * where `seriesData` contains the original series object with added `Seasons` and `Episodes` arrays.
 * If the series cannot be found, emits errorOccurred("getSeriesDetails", "Series not found: " + seriesId).
 *
 * @param seriesId Identifier of the series to load details for.
 */
void MockLibraryService::getSeriesDetails(const QString &seriesId)
{
    QJsonObject series = findItemById(seriesId);
    if (!series.isEmpty()) {
        // Add seasons and episodes to the series data
        QJsonArray seasons = findSeasonsBySeriesId(seriesId);
        QJsonArray episodes = findEpisodesBySeriesId(seriesId);
        
        QJsonObject seriesData = series;
        seriesData["Seasons"] = seasons;
        seriesData["Episodes"] = episodes;
        
        qCDebug(lcTest) << "MockLibraryService::getSeriesDetails(" << seriesId << ") -> found with" 
                 << seasons.size() << "seasons and" << episodes.size() << "episodes";
        emit seriesDetailsLoaded(seriesId, seriesData);
    } else {
        qCWarning(lcTest) << "MockLibraryService::getSeriesDetails(" << seriesId << ") -> not found";
        emit errorOccurred("getSeriesDetails", "Series not found: " + seriesId);
    }
}

/**
 * @brief Resolves the best next episode for a series, optionally skipping a specified episode.
 *
 * Searches the fixture's episodes for the given series, applies the canonical next-episode
 * resolution rules, and emits the resulting episode immediately.
 *
 * @param seriesId The series identifier to search episodes for.
 * @param excludeItemId An optional episode Id to skip; pass an empty string to skip none.
 *
 * @remarks Emits nextUnplayedEpisodeLoaded(seriesId, episode, requestContext) with the found episode as a QJsonObject,
 * or with an empty QJsonObject if no eligible episode is found.
 */
void MockLibraryService::getNextUnplayedEpisode(const QString &seriesId,
                                                const QString &excludeItemId,
                                                const QString &requestContext)
{
    const QJsonArray episodes = findEpisodesBySeriesId(seriesId);
    const QJsonObject resolvedEpisode =
        NextEpisodeResolver::resolveBestNextEpisode(episodes, excludeItemId);

    qCDebug(lcTest) << "MockLibraryService::getNextUnplayedEpisode(" << seriesId
             << ", exclude:" << excludeItemId << ") ->"
             << (resolvedEpisode.isEmpty() ? "no eligible episodes" : "resolved");
    emit nextUnplayedEpisodeLoaded(seriesId, resolvedEpisode, requestContext);
}

void MockLibraryService::markSeriesWatched(const QString &seriesId)
{
    qCDebug(lcTest) << "MockLibraryService::markSeriesWatched(" << seriesId << ")";
    emit seriesWatchedStatusChanged(seriesId);
}

void MockLibraryService::markSeriesUnwatched(const QString &seriesId)
{
    qCDebug(lcTest) << "MockLibraryService::markSeriesUnwatched(" << seriesId << ")";
    emit seriesWatchedStatusChanged(seriesId);
}

void MockLibraryService::markItemPlayed(const QString &itemId)
{
    qCDebug(lcTest) << "MockLibraryService::markItemPlayed(" << itemId << ")";
    emit itemPlayedStatusChanged(itemId, true);
}

void MockLibraryService::markItemUnplayed(const QString &itemId)
{
    qCDebug(lcTest) << "MockLibraryService::markItemUnplayed(" << itemId << ")";
    emit itemPlayedStatusChanged(itemId, false);
}

void MockLibraryService::markItemFavorite(const QString &itemId)
{
    qCDebug(lcTest) << "MockLibraryService::markItemFavorite(" << itemId << ")";
    emit favoriteStatusChanged(itemId, true);
}

void MockLibraryService::markItemUnfavorite(const QString &itemId)
{
    qCDebug(lcTest) << "MockLibraryService::markItemUnfavorite(" << itemId << ")";
    emit favoriteStatusChanged(itemId, false);
}

void MockLibraryService::toggleFavorite(const QString &itemId, bool isFavorite)
{
    qCDebug(lcTest) << "MockLibraryService::toggleFavorite(" << itemId << "," << isFavorite << ")";
    emit favoriteStatusChanged(itemId, isFavorite);
}

void MockLibraryService::getThemeSongs(const QString &seriesId)
{
    qCDebug(lcTest) << "MockLibraryService::getThemeSongs(" << seriesId << ")";
    // Return empty list for test mode
    emit themeSongsLoaded(seriesId, QStringList());
}

void MockLibraryService::search(const QString &searchTerm, int limit)
{
    Q_UNUSED(limit)
    
    QJsonArray matchedMovies;
    QJsonArray matchedSeries;
    
    QString term = searchTerm.toLower();
    
    // Search movies
    for (const QJsonValue &val : m_movies["Items"].toArray()) {
        QJsonObject item = val.toObject();
        if (item["Name"].toString().toLower().contains(term)) {
            matchedMovies.append(item);
        }
    }
    
    // Search series
    for (const QJsonValue &val : m_series["Items"].toArray()) {
        QJsonObject item = val.toObject();
        if (item["Name"].toString().toLower().contains(term)) {
            matchedSeries.append(item);
        }
    }
    
    qCDebug(lcTest) << "MockLibraryService::search(" << searchTerm << ") ->" 
             << matchedMovies.size() << "movies," << matchedSeries.size() << "series";
    emit searchResultsLoaded(searchTerm, matchedMovies, matchedSeries);
}

void MockLibraryService::getRandomItems(int limit)
{
    QJsonArray allItems;
    for (const QJsonValue &val : m_movies["Items"].toArray()) {
        allItems.append(val);
    }
    for (const QJsonValue &val : m_series["Items"].toArray()) {
        allItems.append(val);
    }
    
    // Return first N items (deterministic for testing)
    QJsonArray result;
    int count = qMin(limit, allItems.size());
    for (int i = 0; i < count; ++i) {
        result.append(allItems[i]);
    }
    
    qCDebug(lcTest) << "MockLibraryService::getRandomItems(" << limit << ") ->" << result.size() << "items";
    emit randomItemsLoaded(result);
}

void MockLibraryService::getHeroLibraryItems(int limit, const QStringList &parentIds, bool unwatchedOnly)
{
    QJsonArray allItems;
    for (const QJsonValue &val : m_movies["Items"].toArray()) allItems.append(val);
    for (const QJsonValue &val : m_series["Items"].toArray()) allItems.append(val);

    QStringList ids;
    for (const QString &id : parentIds) {
        if (!id.trimmed().isEmpty()) ids.append(id.trimmed());
    }

    QJsonArray filtered;
    for (const QJsonValue &val : allItems) {
        QJsonObject item = val.toObject();
        if (ids.isEmpty()) {
            // No parent filter: include all.
        } else {
            // Match by ParentId or direct Id for series items that represent a library root.
            const QString parentId = item.value("ParentId").toString();
            if (!ids.contains(parentId)) continue;
        }
        if (unwatchedOnly) {
            const QJsonObject userData = item.value("UserData").toObject();
            if (userData.value("Played").toBool(false)) continue;
        }
        filtered.append(item);
    }

    const int count = qMin(qBound(1, limit, 25), static_cast<int>(filtered.size()));
    QJsonArray result;
    for (int i = 0; i < count; ++i) result.append(filtered[i]);

    qCDebug(lcTest) << "MockLibraryService::getHeroLibraryItems(" << limit << "," << ids.size() << "," << unwatchedOnly << ") ->" << result.size() << "items";
    emit heroLibraryItemsLoaded(result);
}

void MockLibraryService::getHeroSeriesOverviews(const QStringList &seriesIds)
{
    QJsonObject result;
    for (const QString &id : seriesIds) {
        const QString trimmed = id.trimmed();
        if (trimmed.isEmpty() || result.contains(trimmed)) {
            continue;
        }
        const QJsonObject item = findItemById(trimmed);
        result[trimmed] = item.value(QStringLiteral("Overview")).toString();
    }

    qCDebug(lcTest) << "MockLibraryService::getHeroSeriesOverviews(" << seriesIds.size() << ") ->" << result.size() << "items";
    emit heroSeriesOverviewsLoaded(result);
}

QString MockLibraryService::getStreamUrl(const QString &itemId)
{
    Q_UNUSED(itemId)
    // Return a placeholder URL for test mode
    return "file:///dev/null";
}

QString MockLibraryService::getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId,
                                                    int audioStreamIndex, int subtitleStreamIndex)
{
    Q_UNUSED(itemId)
    Q_UNUSED(mediaSourceId)
    Q_UNUSED(audioStreamIndex)
    Q_UNUSED(subtitleStreamIndex)
    return "file:///dev/null";
}

QString MockLibraryService::getImageUrl(const QString &itemId, const QString &imageType)
{
    Q_UNUSED(itemId)
    Q_UNUSED(imageType)
    QString imagesPath = TestModeController::instance()->testImagesPath();
    if (imagesPath.isEmpty()) {
        return QString();
    }
    // Use QUrl to correct handle file:// prefix and path separators on all platforms
    return QUrl::fromLocalFile(imagesPath + "/placeholder.svg").toString();
}

QString MockLibraryService::getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    Q_UNUSED(width)
    return getImageUrl(itemId, imageType);
}

QString MockLibraryService::getCachedImageUrl(const QString &itemId, const QString &imageType)
{
    return getImageUrl(itemId, imageType);
}

QString MockLibraryService::getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    return getImageUrlWithWidth(itemId, imageType, width);
}

QString MockLibraryService::getCachedArtworkUrl(const QString &itemId,
                                                const QString &imageType,
                                                int imageIndex,
                                                const QString &imageTag,
                                                int width)
{
    Q_UNUSED(imageIndex)
    Q_UNUSED(imageTag)
    return getImageUrlWithWidth(itemId, imageType, width);
}

QJsonObject MockLibraryService::findItemById(const QString &itemId) const
{
    // Search in all collections
    for (const QJsonValue &val : m_movies["Items"].toArray()) {
        if (val.toObject()["Id"].toString() == itemId) {
            return val.toObject();
        }
    }
    
    for (const QJsonValue &val : m_series["Items"].toArray()) {
        if (val.toObject()["Id"].toString() == itemId) {
            return val.toObject();
        }
    }
    
    for (const QJsonValue &val : m_seasons["Items"].toArray()) {
        if (val.toObject()["Id"].toString() == itemId) {
            return val.toObject();
        }
    }
    
    for (const QJsonValue &val : m_episodes["Items"].toArray()) {
        if (val.toObject()["Id"].toString() == itemId) {
            return val.toObject();
        }
    }
    
    for (const QJsonValue &val : m_libraries["Items"].toArray()) {
        if (val.toObject()["Id"].toString() == itemId) {
            return val.toObject();
        }
    }
    
    return QJsonObject();
}

QJsonArray MockLibraryService::findEpisodesBySeriesId(const QString &seriesId) const
{
    QJsonArray result;
    for (const QJsonValue &val : m_episodes["Items"].toArray()) {
        if (val.toObject()["SeriesId"].toString() == seriesId) {
            result.append(val);
        }
    }
    return result;
}

QJsonArray MockLibraryService::findSeasonsBySeriesId(const QString &seriesId) const
{
    QJsonArray result;
    for (const QJsonValue &val : m_seasons["Items"].toArray()) {
        if (val.toObject()["SeriesId"].toString() == seriesId) {
            result.append(val);
        }
    }
    return result;
}
