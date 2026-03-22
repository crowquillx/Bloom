#include "SeriesDetailsViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
#include "../utils/DetailViewCache.h"
#include "../utils/DetailListHelper.h"
#include "../utils/DetailMetadataHelper.h"
#include "../utils/ExternalRatingsHelper.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>

namespace {
constexpr qint64 kSeriesMemoryTtlMs = 5 * 60 * 1000;   // 5 minutes
constexpr qint64 kSeriesDiskTtlMs   = 60 * 60 * 1000;  // 1 hour
constexpr qint64 kItemsMemoryTtlMs  = 5 * 60 * 1000;
constexpr qint64 kItemsDiskTtlMs    = 60 * 60 * 1000;
constexpr qint64 kSimilarMemoryTtlMs = 5 * 60 * 1000;
constexpr qint64 kSimilarDiskTtlMs   = 60 * 60 * 1000;

static QHash<QString, DetailViewCache::ObjectCacheEntry> s_seriesCache;
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_itemsCache;  // keyed by parentId (series -> seasons, season -> episodes)
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_similarItemsCache;  // keyed by seriesId
}

static bool hasSpecialPlacementFields(const QJsonArray &items)
{
    for (const auto &val : items) {
        const QJsonObject obj = val.toObject();
        // Only care about episodes (Type == Episode) and specials (ParentIndexNumber == 0)
        const QString type = obj.value("Type").toString();
        if (type != "Episode")
            continue;
        if (!obj.contains("AirsBeforeSeasonNumber") &&
            !obj.contains("AirsAfterSeasonNumber") &&
            !obj.contains("AirsBeforeEpisodeNumber")) {
            return false;
        }
    }
    return true;
}

QString SeriesDetailsViewModel::cacheDir() const
{
    // Prefer config dir if available (for portability across devices), else fallback to generic cache
    QString baseDir;
    if (auto *config = ServiceLocator::tryGet<ConfigManager>()) {
        baseDir = config->getConfigDir();
    } else {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/Bloom";
    }
    return baseDir + "/cache/series";
}

QString SeriesDetailsViewModel::seriesCachePath(const QString &seriesId) const
{
    if (seriesId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(seriesId) + "_details.json");
}

QString SeriesDetailsViewModel::itemsCachePath(const QString &parentId) const
{
    if (parentId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(parentId) + "_items.json");
}

QString SeriesDetailsViewModel::similarItemsCachePath(const QString &seriesId) const
{
    if (seriesId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(seriesId) + "_similar_items.json");
}

bool SeriesDetailsViewModel::loadSeriesFromCache(const QString &seriesId, QJsonObject &seriesData, bool requireFresh) const
{
    return DetailViewCache::loadObjectCache(s_seriesCache,
                                            seriesId,
                                            seriesCachePath(seriesId),
                                            kSeriesMemoryTtlMs,
                                            kSeriesDiskTtlMs,
                                            seriesData,
                                            requireFresh);
}

void SeriesDetailsViewModel::storeSeriesCache(const QString &seriesId, const QJsonObject &seriesData) const
{
    DetailViewCache::storeObjectCache(s_seriesCache,
                                      seriesId,
                                      seriesCachePath(seriesId),
                                      seriesData);
}

bool SeriesDetailsViewModel::loadItemsFromCache(const QString &parentId, QJsonArray &items, bool requireFresh) const
{
    return DetailViewCache::loadArrayCache(s_itemsCache,
                                           parentId,
                                           itemsCachePath(parentId),
                                           kItemsMemoryTtlMs,
                                           kItemsDiskTtlMs,
                                           items,
                                           requireFresh,
                                           false);
}

void SeriesDetailsViewModel::storeItemsCache(const QString &parentId, const QJsonArray &items) const
{
    DetailViewCache::storeArrayCache(s_itemsCache,
                                     parentId,
                                     itemsCachePath(parentId),
                                     items);
}

bool SeriesDetailsViewModel::loadSimilarItemsFromCache(const QString &seriesId, QJsonArray &items, bool requireFresh) const
{
    return DetailViewCache::loadArrayCache(s_similarItemsCache,
                                           seriesId,
                                           similarItemsCachePath(seriesId),
                                           kSimilarMemoryTtlMs,
                                           kSimilarDiskTtlMs,
                                           items,
                                           requireFresh,
                                           true);
}

void SeriesDetailsViewModel::storeSimilarItemsCache(const QString &seriesId, const QJsonArray &items) const
{
    DetailViewCache::storeArrayCache(s_similarItemsCache,
                                     seriesId,
                                     similarItemsCachePath(seriesId),
                                     items);
}

void SeriesDetailsViewModel::clearCacheForTest(const QString &id)
{
    s_seriesCache.remove(id);
    s_itemsCache.remove(id);
    s_similarItemsCache.remove(id);

    QString seriesPath = seriesCachePath(id);
    if (!seriesPath.isEmpty() && QFile::exists(seriesPath)) {
        QFile::remove(seriesPath);
    }

    QString itemsPath = itemsCachePath(id);
    if (!itemsPath.isEmpty() && QFile::exists(itemsPath)) {
        QFile::remove(itemsPath);
    }

    QString similarPath = similarItemsCachePath(id);
    if (!similarPath.isEmpty() && QFile::exists(similarPath)) {
        QFile::remove(similarPath);
    }
}

// ============================================================
// SeasonsModel Implementation
// ============================================================

SeasonsModel::SeasonsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SeasonsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant SeasonsModel::data(const QModelIndex &index, int role) const
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
    case IndexNumberRole:
        return item.value("IndexNumber").toInt();
    case EpisodeCountRole:
        return item.value("ChildCount").toInt();
    case UnplayedItemCountRole: {
        QJsonObject userData = item.value("UserData").toObject();
        return userData.value("UnplayedItemCount").toInt(0);
    }
    case IsPlayedRole: {
        QJsonObject userData = item.value("UserData").toObject();
        return userData.value("Played").toBool(false);
    }
    case ModelDataRole:
        return item.toVariantMap();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> SeasonsModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ImageUrlRole, "imageUrl"},
        {IdRole, "itemId"},
        {IndexNumberRole, "indexNumber"},
        {EpisodeCountRole, "episodeCount"},
        {UnplayedItemCountRole, "unplayedItemCount"},
        {IsPlayedRole, "isPlayed"},
        {ModelDataRole, "modelData"}
    };
}

void SeasonsModel::setSeasons(const QJsonArray &seasons)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(seasons.size());
    for (const QJsonValue &val : seasons) {
        m_items.append(val.toObject());
    }
    endResetModel();
}

void SeasonsModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

QVariantMap SeasonsModel::getItem(int index) const
{
    if (index < 0 || index >= m_items.size())
        return QVariantMap();
    return m_items.at(index).toVariantMap();
}

QString SeasonsModel::getImageUrl(const QJsonObject &item) const
{
    if (!m_libraryService)
        return QString();

    const QString id = item.value("Id").toString();
    const QJsonObject imageTags = item.value("ImageTags").toObject();

    // Try Primary image
    if (imageTags.contains("Primary")) {
        return m_libraryService->getCachedImageUrlWithWidth(id, "Primary", 400);
    }

    // Fallback to series primary
    if (item.contains("SeriesPrimaryImageTag")) {
        const QString seriesId = item.value("SeriesId").toString();
        if (!seriesId.isEmpty()) {
            return m_libraryService->getCachedImageUrlWithWidth(seriesId, "Primary", 400);
        }
    }

    return QString();
}


// ============================================================
// EpisodesModel Implementation
// ============================================================

EpisodesModel::EpisodesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EpisodesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant EpisodesModel::data(const QModelIndex &index, int role) const
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
    case IndexNumberRole:
        return item.value("IndexNumber").toInt();
    case ParentIndexNumberRole:
        return item.value("ParentIndexNumber").toInt();
    case OverviewRole:
        return item.value("Overview").toString();
    case RuntimeTicksRole:
        return item.value("RunTimeTicks").toVariant().toLongLong();
    case IsPlayedRole: {
        const QJsonObject userData = item.value("UserData").toObject();
        return userData.value("Played").toBool();
    }
    case PlaybackPositionTicksRole: {
        const QJsonObject userData = item.value("UserData").toObject();
        return userData.value("PlaybackPositionTicks").toVariant().toLongLong();
    }
    case CommunityRatingRole:
        return item.value("CommunityRating").toDouble();
    case PremiereDateRole:
        return item.value("PremiereDate").toString();
    case IsFavoriteRole: {
        const QJsonObject userData = item.value("UserData").toObject();
        return userData.value("IsFavorite").toBool();
    }
    // Special episode fields
    case IsSpecialRole:
        // Episode is a special if it belongs to Season 0 (ParentIndexNumber == 0)
        return item.value("ParentIndexNumber").toInt() == 0;
    case AirsBeforeSeasonRole:
        return item.value("AirsBeforeSeasonNumber").toInt(-1);
    case AirsAfterSeasonRole:
        return item.value("AirsAfterSeasonNumber").toInt(-1);
    case AirsBeforeEpisodeRole:
        return item.value("AirsBeforeEpisodeNumber").toInt(-1);
    case ModelDataRole:
        return item.toVariantMap();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> EpisodesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ImageUrlRole, "imageUrl"},
        {IdRole, "itemId"},
        {IndexNumberRole, "indexNumber"},
        {ParentIndexNumberRole, "parentIndexNumber"},
        {OverviewRole, "overview"},
        {RuntimeTicksRole, "runtimeTicks"},
        {IsPlayedRole, "isPlayed"},
        {PlaybackPositionTicksRole, "playbackPositionTicks"},
        {CommunityRatingRole, "communityRating"},
        {PremiereDateRole, "premiereDate"},
        {IsFavoriteRole, "isFavorite"},
        // Special episode fields
        {IsSpecialRole, "isSpecial"},
        {AirsBeforeSeasonRole, "airsBeforeSeason"},
        {AirsAfterSeasonRole, "airsAfterSeason"},
        {AirsBeforeEpisodeRole, "airsBeforeEpisode"},
        {ModelDataRole, "modelData"}
    };
}

void EpisodesModel::setEpisodes(const QJsonArray &episodes)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(episodes.size());
    for (const QJsonValue &val : episodes) {
        m_items.append(val.toObject());
    }
    endResetModel();
}

void EpisodesModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

QVariantMap EpisodesModel::getItem(int index) const
{
    if (index < 0 || index >= m_items.size())
        return QVariantMap();
    return m_items.at(index).toVariantMap();
}

QString EpisodesModel::getImageUrl(const QJsonObject &item) const
{
    if (!m_libraryService)
        return QString();

    const QString id = item.value("Id").toString();
    const QJsonObject imageTags = item.value("ImageTags").toObject();

    // Try Thumb first (episodes usually have thumb)
    if (imageTags.contains("Thumb")) {
        return m_libraryService->getCachedImageUrlWithWidth(id, "Thumb", 640);
    }

    // Try Primary
    if (imageTags.contains("Primary")) {
        return m_libraryService->getCachedImageUrlWithWidth(id, "Primary", 640);
    }

    // Fallback to parent (season) primary
    if (item.contains("ParentPrimaryImageTag")) {
        const QString parentId = item.value("ParentId").toString();
        if (!parentId.isEmpty()) {
            return m_libraryService->getCachedImageUrlWithWidth(parentId, "Primary", 640);
        }
    }

    // Fallback to series primary
    if (item.contains("SeriesPrimaryImageTag")) {
        const QString seriesId = item.value("SeriesId").toString();
        if (!seriesId.isEmpty()) {
            return m_libraryService->getCachedImageUrlWithWidth(seriesId, "Primary", 640);
        }
    }

    return QString();
}


// ============================================================
// SeriesDetailsViewModel Implementation
// ============================================================

SeriesDetailsViewModel::SeriesDetailsViewModel(QObject *parent)
    : BaseViewModel(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    
    // Set service on child models
    m_seasonsModel.setLibraryService(m_libraryService);
    m_episodesModel.setLibraryService(m_libraryService);

    m_networkManager = new QNetworkAccessManager(this);
    
    
    if (m_libraryService) {
        connect(m_libraryService, &LibraryService::seriesDetailsLoaded,
                this, &SeriesDetailsViewModel::onSeriesDetailsLoaded);
        connect(m_libraryService, &LibraryService::seriesDetailsNotModified,
                this, &SeriesDetailsViewModel::onSeriesDetailsNotModified);
        connect(m_libraryService, &LibraryService::itemsLoaded,
                this, &SeriesDetailsViewModel::onSeasonsLoaded);
        connect(m_libraryService, &LibraryService::itemsLoadedWithTotal,
                this, [this](const QString &parentId, const QJsonArray &items, int) {
                    // Route to appropriate handler based on what we're loading
                    if (m_loadingSeasons && parentId == m_seriesId) {
                        onSeasonsLoaded(parentId, items);
                    } else if (m_loadingEpisodes && parentId == m_selectedSeasonId) {
                        onEpisodesLoaded(parentId, items);
                    } else if (m_prefetchSeasonIds.contains(parentId)) {
                        // Prefetch responses should be cached but not bound to UI
                        storeItemsCache(parentId, items);
                        m_prefetchSeasonIds.remove(parentId);
                        qDebug() << "SeriesDetailsViewModel: Prefetched episodes for season" << parentId
                                 << "count:" << items.size();
                    }
                });
        connect(m_libraryService, &LibraryService::itemsNotModified,
                this, &SeriesDetailsViewModel::onItemsNotModified);
        connect(m_libraryService, &LibraryService::nextUnplayedEpisodeLoaded,
                this, &SeriesDetailsViewModel::onNextEpisodeLoaded);
        connect(m_libraryService, &LibraryService::nextUnplayedEpisodeFailed,
                this, &SeriesDetailsViewModel::onNextEpisodeFailed);
        connect(m_libraryService, &LibraryService::seriesWatchedStatusChanged,
                this, &SeriesDetailsViewModel::onSeriesWatchedStatusChanged);
        connect(m_libraryService, &LibraryService::favoriteStatusChanged,
                this, &SeriesDetailsViewModel::onFavoriteStatusChanged);
        connect(m_libraryService, &LibraryService::itemLoaded,
                this, &SeriesDetailsViewModel::onEpisodeDetailsLoaded);
        connect(m_libraryService, &LibraryService::itemNotModified,
                this, &SeriesDetailsViewModel::onEpisodeDetailsNotModified);
        connect(m_libraryService, &LibraryService::similarItemsLoaded,
                this, &SeriesDetailsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::similarItemsFailed,
                this, &SeriesDetailsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &SeriesDetailsViewModel::onErrorOccurred);
    } else {
        qWarning() << "SeriesDetailsViewModel: LibraryService not available in ServiceLocator";
    }
}

void SeriesDetailsViewModel::loadSeriesDetails(const QString &seriesId)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    if (seriesId.isEmpty()) {
        setError("Series ID is empty");
        emit loadError(errorMessage());
        return;
    }

    const bool sameSeries = (seriesId == m_seriesId && !m_seriesId.isEmpty());

    // Clear only when navigating to a different series
    if (!sameSeries) {
        clear();
    } else {
        clearError();
    }
    
    m_seriesId = seriesId;
    emit seriesIdChanged();
    
    // Try cache first (serve stale-while-revalidate)
    QJsonObject cachedSeries;
    QJsonArray cachedSeasons;
    QJsonArray cachedSimilarItems;
    bool hasFreshSeries = loadSeriesFromCache(seriesId, cachedSeries, /*requireFresh*/true);
    bool hasAnySeries = hasFreshSeries || loadSeriesFromCache(seriesId, cachedSeries, /*requireFresh*/false);
    bool hasFreshSeasons = loadItemsFromCache(seriesId, cachedSeasons, /*requireFresh*/true);
    bool hasAnySeasons = hasFreshSeasons || loadItemsFromCache(seriesId, cachedSeasons, /*requireFresh*/false);
    bool hasFreshSimilarItems = loadSimilarItemsFromCache(seriesId, cachedSimilarItems, /*requireFresh*/true);
    bool hasAnySimilarItems = hasFreshSimilarItems || loadSimilarItemsFromCache(seriesId, cachedSimilarItems, /*requireFresh*/false);

    if (hasAnySeries) {
        qDebug() << "SeriesDetailsViewModel: Serving series details from cache"
                 << (hasFreshSeries ? "FRESH" : "STALE");
        m_seriesData = cachedSeries;
        updateSeriesMetadata(cachedSeries);
    }

    if (hasAnySeasons) {
        qDebug() << "SeriesDetailsViewModel: Serving seasons from cache"
                 << (hasFreshSeasons ? "FRESH" : "STALE")
                 << "count:" << cachedSeasons.size();
        // Block seriesLoaded() during synchronous cache hydration; the async
        // series request will emit the real completion signal later.
        m_loadingSeries = true;
        m_loadingSeasons = true;
        onSeasonsLoaded(seriesId, cachedSeasons);
    }

    if (hasAnySimilarItems) {
        qDebug() << "SeriesDetailsViewModel: Serving similar items from cache"
                 << (hasFreshSimilarItems ? "FRESH" : "STALE")
                 << "count:" << cachedSimilarItems.size();
        m_similarItems = DetailListHelper::mapSimilarItems(cachedSimilarItems);
        m_similarItemsAttempted = hasFreshSimilarItems;
        m_similarItemsLoading = false;
        emit similarItemsChanged();
        emit similarItemsLoadingChanged();
    }

    m_loadingSeries = !hasFreshSeries;
    m_loadingSeasons = !hasFreshSeasons;
    setLoading(m_loadingSeries || m_loadingSeasons);
    clearError();

    qDebug() << "SeriesDetailsViewModel::loadSeriesDetails" << seriesId;
    
    // Load series details
    m_seriesTimer.restart();
    m_libraryService->getSeriesDetails(seriesId);
    
    // Load seasons
    m_seasonsTimer.restart();
    m_libraryService->getItems(seriesId, 0, 0, QStringList(), QStringList(), QString(), QString(), /*includeHeavyFields*/false, /*useCacheValidation*/true);
    
    // Load next unplayed episode
    m_libraryService->getNextUnplayedEpisode(seriesId);
}

void SeriesDetailsViewModel::reload()
{
    if (!m_seriesId.isEmpty()) {
        loadSeriesDetails(m_seriesId);
    }
}

void SeriesDetailsViewModel::loadSeasonEpisodes(const QString &seasonId)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    if (seasonId.isEmpty()) {
        return;
    }

    m_selectedSeasonId = seasonId;
    emit selectedSeasonIdChanged();
    
    // Try cache for episodes (stale-while-revalidate)
    QJsonArray cachedEpisodes;
    bool hasFreshEpisodes = loadItemsFromCache(seasonId, cachedEpisodes, /*requireFresh*/true);
    bool hasAnyEpisodes = hasFreshEpisodes || loadItemsFromCache(seasonId, cachedEpisodes, /*requireFresh*/false);

    // If cached data is missing special placement fields, treat as stale
    if (hasAnyEpisodes && !hasSpecialPlacementFields(cachedEpisodes)) {
        qDebug() << "SeriesDetailsViewModel: Cached episodes missing placement fields, ignoring cache for" << seasonId;
        hasFreshEpisodes = false;
        hasAnyEpisodes = false;
    }

    if (hasAnyEpisodes) {
        qDebug() << "SeriesDetailsViewModel: Serving episodes from cache for season"
                 << seasonId << (hasFreshEpisodes ? "FRESH" : "STALE")
                 << "count:" << cachedEpisodes.size();
        m_loadingEpisodes = true;
        onEpisodesLoaded(seasonId, cachedEpisodes);
    }

    m_loadingEpisodes = !hasFreshEpisodes;
    setLoading(m_loadingEpisodes);

    qDebug() << "SeriesDetailsViewModel::loadSeasonEpisodes" << seasonId;
    m_episodesTimer.restart();
    m_libraryService->getItems(seasonId, 0, 0, QStringList(), QStringList(), QString(), QString(), /*includeHeavyFields*/false, /*useCacheValidation*/true);
}

void SeriesDetailsViewModel::setSelectedSeasonIndex(int index)
{
    if (index == m_selectedSeasonIndex) {
        return;
    }
    selectSeason(index);
}

void SeriesDetailsViewModel::selectSeason(int index)
{
    if (index < 0 || index >= m_seasons.size()) {
        return;
    }

    m_selectedSeasonIndex = index;
    emit selectedSeasonIndexChanged();

    const QJsonObject &season = m_seasons.at(index);
    m_selectedSeasonId = season.value("Id").toString();
    m_selectedSeasonName = season.value("Name").toString();
    emit selectedSeasonIdChanged();

    loadSeasonEpisodes(m_selectedSeasonId);
}

void SeriesDetailsViewModel::prefetchSeasonsAround(int startIndex, int radius)
{
    if (!m_libraryService || m_seasons.isEmpty() || radius <= 0)
        return;

    // Prefetch next few seasons (forward only to avoid unnecessary churn)
    for (int i = 1; i <= radius; ++i) {
        int idx = startIndex + i;
        if (idx >= m_seasons.size())
            break;
        const QString seasonId = m_seasons[idx].value("Id").toString();
        if (seasonId.isEmpty())
            continue;

        // Skip if we already have fresh cache or a request in-flight
        if (m_prefetchSeasonIds.contains(seasonId))
            continue;

        QJsonArray cached;
        if (loadItemsFromCache(seasonId, cached, /*requireFresh*/true)) {
            continue;
        }

        qDebug() << "SeriesDetailsViewModel: Prefetching season episodes for" << seasonId;
        m_prefetchSeasonIds.insert(seasonId);
        m_libraryService->getItems(seasonId, 0, 0, QStringList(), QStringList(),
                                   QString(), QString(), /*includeHeavyFields*/false, /*useCacheValidation*/true);
    }
}

void SeriesDetailsViewModel::markAsWatched()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::markAsWatched" << m_seriesId;
    m_libraryService->markSeriesWatched(m_seriesId);
}

void SeriesDetailsViewModel::markAsUnwatched()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::markAsUnwatched" << m_seriesId;
    m_libraryService->markSeriesUnwatched(m_seriesId);
}

void SeriesDetailsViewModel::toggleFavorite()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        return;
    }

    m_libraryService->toggleFavorite(m_seriesId, !m_isFavorite);
}

void SeriesDetailsViewModel::loadFocusedEpisodeDetails(const QString &episodeId)
{
    if (!m_libraryService) {
        qWarning() << "SeriesDetailsViewModel::loadFocusedEpisodeDetails without LibraryService";
        clearFocusedEpisodeDetails();
        return;
    }

    if (episodeId.isEmpty()) {
        clearFocusedEpisodeDetails();
        return;
    }

    if (m_episodeDetailsCache.contains(episodeId)) {
        applyFocusedEpisodeDetails(episodeId, m_episodeDetailsCache.value(episodeId));
        return;
    }

    m_focusedEpisodeDetailId = episodeId;
    m_focusedEpisodeDetails = QJsonObject();
    m_focusedEpisodePeople.clear();
    emit focusedEpisodeDetailsChanged();

    setFocusedEpisodeDetailsLoading(true);

    if (m_pendingEpisodeDetailIds.contains(episodeId)) {
        return;
    }

    m_pendingEpisodeDetailIds.insert(episodeId);
    m_libraryService->getItem(episodeId);
}

void SeriesDetailsViewModel::clearFocusedEpisodeDetails()
{
    const bool hadFocusedEpisode = !m_focusedEpisodeDetailId.isEmpty()
                                   || !m_focusedEpisodeDetails.isEmpty()
                                   || !m_focusedEpisodePeople.isEmpty()
                                   || m_focusedEpisodeDetailsLoading;

    m_focusedEpisodeDetailId.clear();
    m_focusedEpisodeDetails = QJsonObject();
    m_focusedEpisodePeople.clear();
    setFocusedEpisodeDetailsLoading(false);

    if (hadFocusedEpisode) {
        emit focusedEpisodeDetailsChanged();
    }
}

void SeriesDetailsViewModel::clear(bool preserveArtwork)
{
    m_seriesId.clear();
    m_title.clear();
    m_overview.clear();
    if (!preserveArtwork) {
        m_logoUrl.clear();
        m_posterUrl.clear();
        m_backdropUrl.clear();
    }
    m_productionYear = 0;
    m_isWatched = false;
    m_isFavorite = false;
    m_imdbId.clear();
    m_tmdbId.clear();
    m_people.clear();
    m_genres.clear();
    m_similarItems.clear();
    m_similarItemsAttempted = false;
    m_similarItemsLoading = false;
    m_seasonCount = 0;
    m_seriesData = QJsonObject();

    if (!preserveArtwork) {
        // Clear ratings data
        m_mdbListRatings.clear();
        m_rawMdbListRatings.clear();
        m_aniListRating.clear();
        m_currentAniListImdbId.clear();
    }

    m_nextEpisodeId.clear();
    m_nextEpisodeName.clear();
    m_nextEpisodeNumber = 0;
    m_nextSeasonNumber = 0;
    m_nextEpisodeImageUrl.clear();
    m_nextEpisodeData = QJsonObject();
    m_focusedEpisodeDetailId.clear();
    m_focusedEpisodeDetails = QJsonObject();
    m_focusedEpisodePeople.clear();
    m_focusedEpisodeDetailsLoading = false;
    m_episodeDetailsCache.clear();
    m_pendingEpisodeDetailIds.clear();

    m_selectedSeasonIndex = -1;
    m_selectedSeasonId.clear();
    m_selectedSeasonName.clear();
    m_seasons.clear();

    m_seasonsModel.clear();
    m_episodesModel.clear();

    m_loadingSeries = false;
    m_loadingSeasons = false;
    m_loadingEpisodes = false;
    setLoading(false);
    clearError();

    emit seriesIdChanged();
    emit titleChanged();
    emit overviewChanged();
    emit logoUrlChanged();
    emit posterUrlChanged();
    emit backdropUrlChanged();
    emit productionYearChanged();
    emit isWatchedChanged();
    emit isFavoriteChanged();
    emit tmdbIdChanged();
    emit peopleChanged();
    emit genresChanged();
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
    emit seasonCountChanged();
    emit nextEpisodeChanged();
    emit focusedEpisodeDetailsChanged();
    emit focusedEpisodeDetailsLoadingChanged();
    emit selectedSeasonIndexChanged();
    emit selectedSeasonIdChanged();
    emit officialRatingChanged();
    emit recursiveItemCountChanged();
    emit statusChanged();
    emit endDateChanged();
    if (!preserveArtwork) {
        emit mdbListRatingsChanged();
    }
}

// MDBList
void SeriesDetailsViewModel::fetchMdbListRatings(const QString &imdbId, const QString &tmdbId, const QString &type)
{
    auto *config = ServiceLocator::tryGet<ConfigManager>();
    if (!config) return;

    const QString requestedImdbId = imdbId;
    const QString requestedTmdbId = tmdbId;

    ExternalRatingsHelper::fetchMdbListRatings(m_networkManager,
                                               this,
                                               config->getMdbListApiKey(),
                                               imdbId,
                                               tmdbId,
                                               type,
                                               [this, requestedImdbId, requestedTmdbId](const QVariantMap &rawRatings) {
        if (requestedImdbId != m_imdbId || requestedTmdbId != m_tmdbId) {
            return;
        }

        m_rawMdbListRatings = rawRatings;
        compileRatings();

        const QVariantList ratingsList = m_mdbListRatings.value("ratings").toList();
        qDebug() << "MDBList ratings updated, count:" << ratingsList.size();
    });
}

void SeriesDetailsViewModel::compileRatings()
{
    const QVariantMap combined = ExternalRatingsHelper::mergeRatings(m_rawMdbListRatings, m_aniListRating);
    if (m_mdbListRatings != combined) {
        m_mdbListRatings = combined;
        emit mdbListRatingsChanged();
    }
}

void SeriesDetailsViewModel::fetchAniListRating(const QString &imdbId, const QString &title, int year)
{
    Q_UNUSED(title)
    Q_UNUSED(year)

    if (imdbId.isEmpty()) {
        return;
    }

    // Only force clear if requesting a DIFFERENT show.
    // If it's the same show, we keep existing rating until new one arrives (or fails).
    if (m_currentAniListImdbId != imdbId) {
        m_aniListRating.clear();
        compileRatings();
        m_currentAniListImdbId = imdbId;
    }

    const QString requestedImdbId = imdbId;
    fetchAniListIdFromWikidata(imdbId, [this, requestedImdbId](const QString &anilistId) {
        if (requestedImdbId != m_imdbId || requestedImdbId != m_currentAniListImdbId) {
            return;
        }

        if (!anilistId.isEmpty()) {
            queryAniListById(anilistId, requestedImdbId);
        } else {
            qDebug() << "AniList ID not found via Wikidata";
        }
    });
}

void SeriesDetailsViewModel::fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback)
{
    ExternalRatingsHelper::fetchAniListIdFromWikidata(m_networkManager, this, imdbId, std::move(callback));
}

void SeriesDetailsViewModel::queryAniListById(const QString &anilistId, const QString &requestImdbId)
{
    ExternalRatingsHelper::queryAniListById(m_networkManager, this, anilistId, [this, requestImdbId](const QJsonObject &media) {
        if (requestImdbId != m_imdbId || requestImdbId != m_currentAniListImdbId) {
            return;
        }

        const int avgScore = media["averageScore"].toInt();
        const int meanScore = media["meanScore"].toInt();
        const int score = (avgScore > 0) ? avgScore : meanScore;

        if (score > 0) {
            qDebug() << "AniList Score found:" << score;

            QVariantMap anilistRating;
            anilistRating["source"] = "AniList";
            anilistRating["value"] = score;
            anilistRating["score"] = score;
            anilistRating["url"] = media["siteUrl"].toString();

            m_aniListRating = anilistRating;
            compileRatings();
        }
    });
}

QVariantMap SeriesDetailsViewModel::getSeriesData() const
{
    return m_seriesData.toVariantMap();
}

QVariantMap SeriesDetailsViewModel::getNextEpisodeData() const
{
    return m_nextEpisodeData.toVariantMap();
}

qint64 SeriesDetailsViewModel::nextEpisodePlaybackPositionTicks() const
{
    if (m_nextEpisodeData.isEmpty()) {
        return 0;
    }
    const QJsonObject userData = m_nextEpisodeData.value("UserData").toObject();
    return userData.value("PlaybackPositionTicks").toVariant().toLongLong();
}

void SeriesDetailsViewModel::onSeriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData)
{
    if (seriesId != m_seriesId) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::onSeriesDetailsLoaded" << seriesId
             << "elapsed(ms):" << m_seriesTimer.elapsed();
    m_loadingSeries = false;
    
    m_seriesData = seriesData;
    updateSeriesMetadata(seriesData);
    storeSeriesCache(seriesId, seriesData);

    if (!m_similarItemsAttempted && !m_similarItemsLoading && m_libraryService) {
        m_similarItemsAttempted = true;
        m_similarItemsLoading = true;
        emit similarItemsLoadingChanged();
        m_libraryService->getSimilarItems(seriesId);
    }
    
    // Check if all loading is complete
    if (!m_loadingSeries && !m_loadingSeasons) {
        setLoading(false);
        emit seriesLoaded();
    }
}

void SeriesDetailsViewModel::onSeriesDetailsNotModified(const QString &seriesId)
{
    if (seriesId != m_seriesId) {
        return;
    }

    QJsonObject cached;
    if (loadSeriesFromCache(seriesId, cached, /*requireFresh*/false)) {
        qDebug() << "SeriesDetailsViewModel::onSeriesDetailsNotModified using cached data";
        m_loadingSeries = false;
        m_seriesData = cached;
        updateSeriesMetadata(cached);
        if (!m_similarItemsAttempted && !m_similarItemsLoading && m_libraryService) {
            m_similarItemsAttempted = true;
            m_similarItemsLoading = true;
            emit similarItemsLoadingChanged();
            m_libraryService->getSimilarItems(seriesId);
        }
        if (!m_loadingSeasons) {
            setLoading(false);
            emit seriesLoaded();
        }
    } else {
        qWarning() << "SeriesDetailsViewModel::onSeriesDetailsNotModified but no cache found";
        setLoading(false);
    }
}

void SeriesDetailsViewModel::onSeasonsLoaded(const QString &parentId, const QJsonArray &items)
{
    if (parentId != m_seriesId || !m_loadingSeasons) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::onSeasonsLoaded" << parentId << items.size()
             << "seasons elapsed(ms):" << m_seasonsTimer.elapsed();
    m_loadingSeasons = false;

    // Store seasons
    m_seasons.clear();
    m_seasons.reserve(items.size());
    for (const QJsonValue &val : items) {
        QJsonObject item = val.toObject();
        // Only include Season items that are not empty
        if (item.value("Type").toString() == "Season") {
            // Filter out empty seasons (ChildCount == 0)
            if (item.contains("ChildCount") && item.value("ChildCount").toInt() == 0) {
                qDebug() << "Filtering out empty season:" << item.value("Name").toString();
                continue;
            }
            m_seasons.append(item);
        }
    }

    // Sort seasons by IndexNumber
    std::sort(m_seasons.begin(), m_seasons.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("IndexNumber").toInt() < b.value("IndexNumber").toInt();
    });

    // Convert to QJsonArray for model
    QJsonArray seasonsArray;
    for (const QJsonObject &season : m_seasons) {
        seasonsArray.append(season);
    }
    m_seasonsModel.setSeasons(seasonsArray);
    storeItemsCache(parentId, seasonsArray);

    m_seasonCount = m_seasons.size();
    emit seasonCountChanged();
    emit seasonsLoaded();

    // Auto-select first season if none selected
    if (m_selectedSeasonIndex < 0 && !m_seasons.isEmpty()) {
        selectSeason(0);
    }

    // Check if all loading is complete
    if (!m_loadingSeries && !m_loadingSeasons) {
        setLoading(false);
        emit seriesLoaded();
    }
}

void SeriesDetailsViewModel::onItemsNotModified(const QString &parentId)
{
    QJsonArray cached;
    if (!loadItemsFromCache(parentId, cached, /*requireFresh*/false)) {
        qWarning() << "SeriesDetailsViewModel::onItemsNotModified but no cache for" << parentId;
        return;
    }

    if (parentId == m_seriesId) {
        qDebug() << "SeriesDetailsViewModel: Seasons not modified, using cached data";
        onSeasonsLoaded(parentId, cached);
    } else if (parentId == m_selectedSeasonId) {
        qDebug() << "SeriesDetailsViewModel: Episodes not modified, using cached data for season" << parentId;
        onEpisodesLoaded(parentId, cached);
    } else if (m_prefetchSeasonIds.contains(parentId)) {
        storeItemsCache(parentId, cached);
        m_prefetchSeasonIds.remove(parentId);
        qDebug() << "SeriesDetailsViewModel: Prefetch not modified for" << parentId;
    }
}

void SeriesDetailsViewModel::onEpisodesLoaded(const QString &parentId, const QJsonArray &items)
{
    if (parentId != m_selectedSeasonId || !m_loadingEpisodes) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::onEpisodesLoaded" << parentId << items.size()
             << "episodes elapsed(ms):" << m_episodesTimer.elapsed();
    m_loadingEpisodes = false;
    setLoading(false);

    // Get the current season number for special placement
    int currentSeasonNumber = 0;
    for (const auto &season : m_seasons) {
        if (season.value("Id").toString() == parentId) {
            currentSeasonNumber = season.value("IndexNumber").toInt();
            break;
        }
    }

    // Filter Episode items, excluding missing episodes (LocationType == "Virtual")
    QVector<QJsonObject> regularEpisodes;
    QVector<QJsonObject> specialsBefore;  // Specials that air before the season or at start
    QVector<QJsonObject> specialsAfter;   // Specials that air after the season
    // Map: episode index -> list of specials that air before that episode
    QMap<int, QVector<QJsonObject>> specialsBeforeEpisode;

    for (const QJsonValue &val : items) {
        QJsonObject item = val.toObject();
        if (item.value("Type").toString() != "Episode") {
            continue;
        }

        // Filter out missing episodes - they have LocationType == "Virtual"
        QString locationType = item.value("LocationType").toString();
        if (locationType == "Virtual") {
            qDebug() << "Filtering out missing episode:" << item.value("Name").toString()
                     << "S" << item.value("ParentIndexNumber").toInt()
                     << "E" << item.value("IndexNumber").toInt();
            continue;
        }

        int parentIndexNumber = item.value("ParentIndexNumber").toInt();
        
        // Check if this is a special (Season 0) that's placed within this season
        if (parentIndexNumber == 0 && currentSeasonNumber > 0) {
            // This is a special - check where it should be placed
            int airsAfterSeason = item.contains("AirsAfterSeasonNumber") ? 
                                  item.value("AirsAfterSeasonNumber").toInt() : -1;
            int airsBeforeSeason = item.contains("AirsBeforeSeasonNumber") ? 
                                   item.value("AirsBeforeSeasonNumber").toInt() : -1;
            int airsBeforeEpisode = item.contains("AirsBeforeEpisodeNumber") ? 
                                    item.value("AirsBeforeEpisodeNumber").toInt() : -1;
            
            qDebug() << "Special episode:" << item.value("Name").toString()
                     << "AirsBeforeSeason:" << airsBeforeSeason
                     << "AirsAfterSeason:" << airsAfterSeason
                     << "AirsBeforeEpisode:" << airsBeforeEpisode;

            if (airsAfterSeason == currentSeasonNumber) {
                // Airs after this season (at the end)
                specialsAfter.append(item);
            } else if (airsBeforeSeason == currentSeasonNumber) {
                if (airsBeforeEpisode > 0) {
                    // Airs before a specific episode
                    specialsBeforeEpisode[airsBeforeEpisode].append(item);
                } else {
                    // Airs at the start of the season (before episode 1)
                    specialsBefore.append(item);
                }
            } else {
                // Fallback: just add to regular episodes
                regularEpisodes.append(item);
            }
        } else {
            // Regular episode for this season
            regularEpisodes.append(item);
        }
    }

    // Sort regular episodes by IndexNumber
    std::sort(regularEpisodes.begin(), regularEpisodes.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("IndexNumber").toInt() < b.value("IndexNumber").toInt();
    });

    // Sort specials lists by their original IndexNumber (S00E01 before S00E03, etc.)
    auto sortByIndex = [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("IndexNumber").toInt() < b.value("IndexNumber").toInt();
    };
    std::sort(specialsBefore.begin(), specialsBefore.end(), sortByIndex);
    std::sort(specialsAfter.begin(), specialsAfter.end(), sortByIndex);
    for (auto &specials : specialsBeforeEpisode) {
        std::sort(specials.begin(), specials.end(), sortByIndex);
    }

    // Build the final sorted list
    QJsonArray episodesArray;

    // Add specials that air before the season
    for (const QJsonObject &ep : specialsBefore) {
        episodesArray.append(ep);
    }

    // Add regular episodes, inserting mid-season specials at the right positions
    for (const QJsonObject &ep : regularEpisodes) {
        int episodeNumber = ep.value("IndexNumber").toInt();
        
        // Insert any specials that air before this episode
        if (specialsBeforeEpisode.contains(episodeNumber)) {
            for (const QJsonObject &special : specialsBeforeEpisode[episodeNumber]) {
                episodesArray.append(special);
            }
        }
        
        episodesArray.append(ep);
    }

    // Add specials that air after the season
    for (const QJsonObject &ep : specialsAfter) {
        episodesArray.append(ep);
    }

    // Count mid-season specials
    int midSeasonSpecialCount = 0;
    for (const auto &specials : specialsBeforeEpisode) {
        midSeasonSpecialCount += specials.size();
    }

    qDebug() << "SeriesDetailsViewModel: Final episode count:" << episodesArray.size()
             << "(Regular:" << regularEpisodes.size()
             << "Specials before season:" << specialsBefore.size()
             << "Specials mid-season:" << midSeasonSpecialCount
             << "Specials after season:" << specialsAfter.size() << ")";

    m_episodesModel.setEpisodes(episodesArray);
    storeItemsCache(parentId, episodesArray);

    emit episodesLoaded();
}

void SeriesDetailsViewModel::onNextEpisodeLoaded(const QString &seriesId, const QJsonObject &episodeData)
{
    if (seriesId != m_seriesId) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::onNextEpisodeLoaded" << seriesId;
    updateNextEpisode(episodeData);
}

void SeriesDetailsViewModel::onNextEpisodeFailed(const QString &seriesId, const QString &error)
{
    if (seriesId != m_seriesId) {
        return;
    }

    qWarning() << "SeriesDetailsViewModel next episode error:" << error;
}

void SeriesDetailsViewModel::onSeriesWatchedStatusChanged(const QString &seriesId)
{
    if (seriesId != m_seriesId) {
        return;
    }

    qDebug() << "SeriesDetailsViewModel::onSeriesWatchedStatusChanged" << seriesId;
    
    // Toggle watched state (we don't get the actual value from the signal)
    m_isWatched = !m_isWatched;
    emit isWatchedChanged();
    
    // Refresh next episode data
    if (m_libraryService) {
        m_libraryService->getNextUnplayedEpisode(m_seriesId);
    }
}

void SeriesDetailsViewModel::onFavoriteStatusChanged(const QString &itemId, bool isFavorite)
{
    if (itemId != m_seriesId) {
        return;
    }

    const bool favoriteChanged = (m_isFavorite != isFavorite);

    m_isFavorite = isFavorite;

    if (!m_seriesData.isEmpty()) {
        QJsonObject userData = m_seriesData.value("UserData").toObject();
        userData.insert("IsFavorite", isFavorite);
        m_seriesData.insert("UserData", userData);
        storeSeriesCache(itemId, m_seriesData);
    }

    if (favoriteChanged) {
        emit isFavoriteChanged();
    }
}

void SeriesDetailsViewModel::onSimilarItemsLoaded(const QString &itemId, const QJsonArray &items)
{
    if (itemId != m_seriesId) {
        return;
    }

    m_similarItems = DetailListHelper::mapSimilarItems(items);
    m_similarItemsAttempted = true;
    m_similarItemsLoading = false;
    storeSimilarItemsCache(itemId, items);
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
}

void SeriesDetailsViewModel::onSimilarItemsFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_seriesId) {
        return;
    }

    m_similarItemsAttempted = false;
    if (m_similarItemsLoading) {
        m_similarItemsLoading = false;
        emit similarItemsLoadingChanged();
    }

    qWarning() << "SeriesDetailsViewModel similar items error:" << error;
}

void SeriesDetailsViewModel::onErrorOccurred(const QString &endpoint, const QString &error)
{
    // Only handle errors for our current requests
    if (endpoint != "getSeriesDetails" && endpoint != "getItems" &&
        endpoint != "markSeriesWatched" &&
        endpoint != "markSeriesUnwatched") {
        return;
    }

    qWarning() << "SeriesDetailsViewModel error:" << endpoint << error;
    m_loadingSeries = false;
    m_loadingSeasons = false;
    m_loadingEpisodes = false;
    setLoading(false);
    setError(mapNetworkError(endpoint, error));
    emit loadError(error);
}

void SeriesDetailsViewModel::onEpisodeDetailsLoaded(const QString &itemId, const QJsonObject &data)
{
    if (!m_pendingEpisodeDetailIds.contains(itemId) && itemId != m_focusedEpisodeDetailId) {
        return;
    }

    m_pendingEpisodeDetailIds.remove(itemId);
    m_episodeDetailsCache.insert(itemId, data);

    if (itemId == m_focusedEpisodeDetailId) {
        applyFocusedEpisodeDetails(itemId, data);
    }
}

void SeriesDetailsViewModel::onEpisodeDetailsNotModified(const QString &itemId)
{
    if (!m_pendingEpisodeDetailIds.contains(itemId) && itemId != m_focusedEpisodeDetailId) {
        return;
    }

    m_pendingEpisodeDetailIds.remove(itemId);

    if (m_episodeDetailsCache.contains(itemId)) {
        if (itemId == m_focusedEpisodeDetailId) {
            applyFocusedEpisodeDetails(itemId, m_episodeDetailsCache.value(itemId));
        }
        return;
    }

    qWarning() << "SeriesDetailsViewModel::onEpisodeDetailsNotModified missing local cache for" << itemId;
    if (itemId == m_focusedEpisodeDetailId) {
        setFocusedEpisodeDetailsLoading(false);
        emit focusedEpisodeDetailsChanged();
    }
}

void SeriesDetailsViewModel::updateSeriesMetadata(const QJsonObject &data)
{
    const auto common = DetailMetadataHelper::extractCommonMetadata(
        data,
        m_seriesId,
        [this](const QString &itemId, const QString &imageType, int width) {
            return buildImageUrl(itemId, imageType, width);
        },
        QStringLiteral("No synopsis available."),
        false);

    m_title = common.title;
    emit titleChanged();

    m_overview = common.overview;
    emit overviewChanged();

    m_productionYear = common.productionYear;
    emit productionYearChanged();

    // Keep the local parse for series-only flags not exposed by CommonDetailMetadata.
    const QJsonObject userData = data.value("UserData").toObject();
    m_isWatched = common.isWatched;
    emit isWatchedChanged();
    m_isFavorite = userData.value("IsFavorite").toBool();
    emit isFavoriteChanged();

    m_officialRating = common.officialRating;
    emit officialRatingChanged();

    // Cumulative item count (episodes)
    m_recursiveItemCount = data.value("RecursiveItemCount").toInt();
    emit recursiveItemCountChanged();

    m_status = data.value("Status").toString();
    emit statusChanged();

    // EndDate parsing
    if (data.contains("EndDate")) {
        m_endDate = QDateTime::fromString(data.value("EndDate").toString(), Qt::ISODate);
    } else {
        m_endDate = QDateTime();
    }
    emit endDateChanged();

    const QJsonObject imageTags = data.value("ImageTags").toObject();

    if (imageTags.contains("Logo")) {
        m_logoUrl = common.logoUrl;
    } else {
        m_logoUrl.clear();
    }
    emit logoUrlChanged();

    if (imageTags.contains("Primary")) {
        m_posterUrl = common.posterUrl;
    } else {
        m_posterUrl.clear();
    }
    emit posterUrlChanged();

    const QJsonArray backdropTags = data.value("BackdropImageTags").toArray();
    if (!backdropTags.isEmpty()) {
        m_backdropUrl = common.backdropUrl;
    } else {
        m_backdropUrl.clear();
    }
    emit backdropUrlChanged();

    m_people = common.people;
    emit peopleChanged();

    m_genres = common.genres;
    emit genresChanged();

    // Trigger MDBList fetch
    QJsonObject providerIds = data.value("ProviderIds").toObject();
    QString imdbId = providerIds.value("Imdb").toString();
    QString tmdbId = providerIds.value("Tmdb").toString();
    m_imdbId = imdbId;
    m_tmdbId = tmdbId;
    emit tmdbIdChanged();
    
    if (!imdbId.isEmpty() || !tmdbId.isEmpty()) {
        fetchMdbListRatings(imdbId, tmdbId, "show");
    } else if (!m_title.isEmpty()) {
        // Fallback to title search if no IDs?
        // Maybe later. For now, rely on IDs.
        qDebug() << "No IDs for MDBList, skipping.";
    }
    
    // Trigger AniList fetch if we have IMDb ID
    if (!imdbId.isEmpty()) {
        fetchAniListRating(imdbId, m_title, m_productionYear);
    }
}

void SeriesDetailsViewModel::applyFocusedEpisodeDetails(const QString &episodeId, const QJsonObject &data)
{
    m_focusedEpisodeDetailId = episodeId;
    m_focusedEpisodeDetails = data;
    m_focusedEpisodePeople = DetailMetadataHelper::mapPeople(data.value("People").toArray());
    setFocusedEpisodeDetailsLoading(false);
    emit focusedEpisodeDetailsChanged();
}

void SeriesDetailsViewModel::setFocusedEpisodeDetailsLoading(bool loading)
{
    if (m_focusedEpisodeDetailsLoading == loading) {
        return;
    }

    m_focusedEpisodeDetailsLoading = loading;
    emit focusedEpisodeDetailsLoadingChanged();
}

void SeriesDetailsViewModel::updateNextEpisode(const QJsonObject &episodeData)
{
    m_nextEpisodeData = episodeData;

    if (episodeData.isEmpty()) {
        m_nextEpisodeId.clear();
        m_nextEpisodeName.clear();
        m_nextEpisodeNumber = 0;
        m_nextSeasonNumber = 0;
        m_nextEpisodeImageUrl.clear();
    } else {
        m_nextEpisodeId = episodeData.value("Id").toString();
        m_nextEpisodeName = episodeData.value("Name").toString();
        m_nextEpisodeNumber = episodeData.value("IndexNumber").toInt();
        m_nextSeasonNumber = episodeData.value("ParentIndexNumber").toInt();

        // Build image URL for next episode
        const QJsonObject imageTags = episodeData.value("ImageTags").toObject();
        if (imageTags.contains("Thumb")) {
            m_nextEpisodeImageUrl = buildImageUrl(m_nextEpisodeId, "Thumb", 400);
        } else if (imageTags.contains("Primary")) {
            m_nextEpisodeImageUrl = buildImageUrl(m_nextEpisodeId, "Primary", 400);
        } else {
            // Fallback to series poster
            m_nextEpisodeImageUrl = m_posterUrl;
        }
    }

    emit nextEpisodeChanged();
}

QString SeriesDetailsViewModel::buildImageUrl(const QString &itemId, const QString &imageType, int width) const
{
    if (!m_libraryService || itemId.isEmpty()) {
        return QString();
    }
    return m_libraryService->getCachedImageUrlWithWidth(itemId, imageType, width);
}
