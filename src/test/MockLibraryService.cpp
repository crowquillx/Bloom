#include "MockLibraryService.h"
#include "TestModeController.h"
#include <QDebug>

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
    
    qDebug() << "MockLibraryService: Loaded fixture with";
    qDebug() << "  Libraries:" << m_libraries["Items"].toArray().size();
    qDebug() << "  Movies:" << m_movies["Items"].toArray().size();
    qDebug() << "  Series:" << m_series["Items"].toArray().size();
    qDebug() << "  Episodes:" << m_episodes["Items"].toArray().size();
}

void MockLibraryService::getViews()
{
    QJsonArray views = m_libraries["Items"].toArray();
    qDebug() << "MockLibraryService::getViews() ->" << views.size() << "views";
    emit viewsLoaded(views);
}

void MockLibraryService::getItems(const QString &parentId, int startIndex, int limit, 
                                   const QStringList &genres, const QStringList &networks,
                                   const QString &sortBy, const QString &sortOrder,
                                   bool includeHeavyFields, bool useCacheValidation)
{
    Q_UNUSED(startIndex)
    Q_UNUSED(limit)
    Q_UNUSED(genres)
    Q_UNUSED(networks)
    Q_UNUSED(sortBy)
    Q_UNUSED(sortOrder)
    Q_UNUSED(includeHeavyFields)
    Q_UNUSED(useCacheValidation)
    
    QJsonArray items;
    int totalCount = 0;
    
    // Determine which library type based on parentId
    if (parentId == "library-movies") {
        items = m_movies["Items"].toArray();
        totalCount = m_movies["TotalRecordCount"].toInt();
    } else if (parentId == "library-shows") {
        items = m_series["Items"].toArray();
        totalCount = m_series["TotalRecordCount"].toInt();
    } else {
        // Return all items for generic queries
        QJsonArray allItems;
        allItems.append(m_movies["Items"].toArray());
        allItems.append(m_series["Items"].toArray());
        totalCount = allItems.size();
    }
    
    qDebug() << "MockLibraryService::getItems(" << parentId << ") ->" << items.size() << "items";
    emit itemsLoadedWithTotal(parentId, items, totalCount);
}

void MockLibraryService::getNextUp()
{
    QJsonArray items = m_nextUp["Items"].toArray();
    qDebug() << "MockLibraryService::getNextUp() ->" << items.size() << "items";
    emit nextUpLoaded(items);
}

void MockLibraryService::getLatestMedia(const QString &parentId)
{
    QJsonArray items = m_latestItems["Items"].toArray();
    qDebug() << "MockLibraryService::getLatestMedia(" << parentId << ") ->" << items.size() << "items";
    emit latestMediaLoaded(parentId, items);
}

void MockLibraryService::getItem(const QString &itemId)
{
    QJsonObject item = findItemById(itemId);
    if (!item.isEmpty()) {
        qDebug() << "MockLibraryService::getItem(" << itemId << ") -> found";
        emit itemLoaded(itemId, item);
    } else {
        qWarning() << "MockLibraryService::getItem(" << itemId << ") -> not found";
        emit errorOccurred("getItem", "Item not found: " + itemId);
    }
}

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
        
        qDebug() << "MockLibraryService::getSeriesDetails(" << seriesId << ") -> found with" 
                 << seasons.size() << "seasons and" << episodes.size() << "episodes";
        emit seriesDetailsLoaded(seriesId, seriesData);
    } else {
        qWarning() << "MockLibraryService::getSeriesDetails(" << seriesId << ") -> not found";
        emit errorOccurred("getSeriesDetails", "Series not found: " + seriesId);
    }
}

void MockLibraryService::getNextUnplayedEpisode(const QString &seriesId)
{
    // Find the next unplayed episode for this series
    QJsonArray episodes = findEpisodesBySeriesId(seriesId);
    for (const QJsonValue &val : episodes) {
        QJsonObject episode = val.toObject();
        QJsonObject userData = episode["UserData"].toObject();
        if (!userData["Played"].toBool()) {
            qDebug() << "MockLibraryService::getNextUnplayedEpisode(" << seriesId << ") -> found";
            emit nextUnplayedEpisodeLoaded(seriesId, episode);
            return;
        }
    }
    
    // No unplayed episodes
    qDebug() << "MockLibraryService::getNextUnplayedEpisode(" << seriesId << ") -> no unplayed episodes";
    emit nextUnplayedEpisodeLoaded(seriesId, QJsonObject());
}

void MockLibraryService::markSeriesWatched(const QString &seriesId)
{
    qDebug() << "MockLibraryService::markSeriesWatched(" << seriesId << ")";
    emit seriesWatchedStatusChanged(seriesId);
}

void MockLibraryService::markSeriesUnwatched(const QString &seriesId)
{
    qDebug() << "MockLibraryService::markSeriesUnwatched(" << seriesId << ")";
    emit seriesWatchedStatusChanged(seriesId);
}

void MockLibraryService::markItemPlayed(const QString &itemId)
{
    qDebug() << "MockLibraryService::markItemPlayed(" << itemId << ")";
    emit itemPlayedStatusChanged(itemId, true);
}

void MockLibraryService::markItemUnplayed(const QString &itemId)
{
    qDebug() << "MockLibraryService::markItemUnplayed(" << itemId << ")";
    emit itemPlayedStatusChanged(itemId, false);
}

void MockLibraryService::markItemFavorite(const QString &itemId)
{
    qDebug() << "MockLibraryService::markItemFavorite(" << itemId << ")";
    emit favoriteStatusChanged(itemId, true);
}

void MockLibraryService::markItemUnfavorite(const QString &itemId)
{
    qDebug() << "MockLibraryService::markItemUnfavorite(" << itemId << ")";
    emit favoriteStatusChanged(itemId, false);
}

void MockLibraryService::toggleFavorite(const QString &itemId, bool isFavorite)
{
    qDebug() << "MockLibraryService::toggleFavorite(" << itemId << "," << isFavorite << ")";
    emit favoriteStatusChanged(itemId, isFavorite);
}

void MockLibraryService::getThemeSongs(const QString &seriesId)
{
    qDebug() << "MockLibraryService::getThemeSongs(" << seriesId << ")";
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
    
    qDebug() << "MockLibraryService::search(" << searchTerm << ") ->" 
             << matchedMovies.size() << "movies," << matchedSeries.size() << "series";
    emit searchResultsLoaded(searchTerm, matchedMovies, matchedSeries);
}

void MockLibraryService::getRandomItems(int limit)
{
    QJsonArray allItems;
    allItems.append(m_movies["Items"].toArray());
    allItems.append(m_series["Items"].toArray());
    
    // Return first N items (deterministic for testing)
    QJsonArray result;
    int count = qMin(limit, allItems.size());
    for (int i = 0; i < count; ++i) {
        result.append(allItems[i]);
    }
    
    qDebug() << "MockLibraryService::getRandomItems(" << limit << ") ->" << result.size() << "items";
    emit randomItemsLoaded(result);
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
    QString imagesPath = TestModeController::instance()->testImagesPath();
    if (imagesPath.isEmpty()) {
        return QString();
    }
    return QString("file://%1/placeholder.svg").arg(imagesPath);
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
