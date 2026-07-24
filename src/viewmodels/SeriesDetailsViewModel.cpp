#include "SeriesDetailsViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
#include "../utils/DetailViewCache.h"
#include "../utils/ExternalRatingsHelper.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include "../utils/BloomLogging.h"

namespace {
constexpr qint64 kSeriesMemoryTtlMs = 5 * 60 * 1000;   // 5 minutes
constexpr qint64 kSeriesDiskTtlMs   = 60 * 60 * 1000;  // 1 hour
constexpr qint64 kItemsMemoryTtlMs  = 5 * 60 * 1000;
constexpr qint64 kItemsDiskTtlMs    = 60 * 60 * 1000;
constexpr qint64 kSimilarMemoryTtlMs = 5 * 60 * 1000;
constexpr qint64 kSimilarDiskTtlMs   = 60 * 60 * 1000;

static QHash<QString, DetailViewCache::ObjectCacheEntry> s_seriesCache;
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_itemsCache;  // keyed by connection + parentId
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_similarItemsCache;  // keyed by connection + seriesId
static QString s_memoryCacheScope;

QString configuredConnectionId()
{
    if (auto *config = ServiceLocator::tryGet<ConfigManager>()) {
        const auto connection = config->getActiveConnection();
        if (connection.has_value() && !connection->connectionId.isEmpty()) {
            return connection->connectionId;
        }
    }
    return {};
}

QString scopedCacheKey(const QString &scope, const QString &remoteId)
{
    if (s_memoryCacheScope != scope) {
        s_seriesCache.clear();
        s_itemsCache.clear();
        s_similarItemsCache.clear();
        s_memoryCacheScope = scope;
    }
    return scope + QLatin1Char('\n') + remoteId;
}

QString effectiveScope(const QString &connectionId)
{
    if (!connectionId.isEmpty()) {
        return connectionId;
    }
    const QString configured = configuredConnectionId();
    return configured.isEmpty() ? QStringLiteral("_local") : configured;
}

QString prefetchRequestKey(const QString &connectionId, const QString &seasonId)
{
    return connectionId + QLatin1Char('\n') + seasonId;
}

bool isCanonicalItem(const QJsonObject &item)
{
    return item.contains(QStringLiteral("itemId"))
        && item.contains(QStringLiteral("connectionId"))
        && !item.contains(QStringLiteral("Id"))
        && !item.contains(QStringLiteral("RunTimeTicks"))
        && !item.contains(QStringLiteral("PlaybackPositionTicks"))
        && !item.contains(QStringLiteral("UserData"))
        && !item.contains(QStringLiteral("ImageTags"))
        && !item.contains(QStringLiteral("BackdropImageTags"))
        && !item.contains(QStringLiteral("ProviderIds"));
}

bool isCanonicalItems(const QJsonArray &items)
{
    for (const QJsonValue &value : items) {
        if (!value.isObject() || !isCanonicalItem(value.toObject())) {
            return false;
        }
    }
    return true;
}

QJsonArray variantListToJsonArray(const QVariantList &items)
{
    QJsonArray result;
    for (const QVariant &value : items) {
        if (value.canConvert<QVariantMap>()) {
            result.append(QJsonObject::fromVariantMap(value.toMap()));
        }
    }
    return result;
}

QVariantList jsonArrayToVariantList(const QJsonArray &items)
{
    QVariantList result;
    result.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject()) {
            result.append(value.toObject().toVariantMap());
        }
    }
    return result;
}

QStringList canonicalStringList(const QVariant &value)
{
    QStringList result;
    const QVariantList items = value.toList();
    result.reserve(items.size());
    for (const QVariant &item : items) {
        const QString text = item.toString();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

QVariantList canonicalPeople(const QVariant &value)
{
    const QVariantList people = value.toList();
    QVariantList result;
    result.reserve(qMin(people.size(), qsizetype(18)));
    for (const QVariant &entry : people) {
        const QVariantMap person = entry.toMap();
        if (person.value(QStringLiteral("name")).toString().isEmpty()) {
            continue;
        }
        result.append(person);
        if (result.size() >= 18) {
            break;
        }
    }
    return result;
}
}

static bool hasSpecialPlacementFields(const QJsonArray &items)
{
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("mediaType")).toString() != QStringLiteral("Episode")) {
            continue;
        }
        if (!item.contains(QStringLiteral("airsBeforeSeasonNumber"))
            && !item.contains(QStringLiteral("airsAfterSeasonNumber"))
            && !item.contains(QStringLiteral("airsBeforeEpisodeNumber"))) {
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
    const QString scope = DetailViewCache::connectionScopeCacheKey(effectiveScope(m_connectionId));
    return baseDir + QStringLiteral("/cache/connections/") + scope + QStringLiteral("/series");
}

QString SeriesDetailsViewModel::seriesCachePath(const QString &seriesId) const
{
    if (seriesId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(seriesId) + "_details_canonical.json");
}

QString SeriesDetailsViewModel::itemsCachePath(const QString &parentId) const
{
    if (parentId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(parentId) + "_items_canonical.json");
}

QString SeriesDetailsViewModel::similarItemsCachePath(const QString &seriesId) const
{
    if (seriesId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(seriesId) + "_similar_items_canonical.json");
}

bool SeriesDetailsViewModel::loadSeriesFromCache(const QString &seriesId, QJsonObject &seriesData, bool requireFresh) const
{
    if (!DetailViewCache::loadObjectCache(s_seriesCache,
                                          scopedCacheKey(effectiveScope(m_connectionId), seriesId),
                                          seriesCachePath(seriesId),
                                          kSeriesMemoryTtlMs,
                                          kSeriesDiskTtlMs,
                                          seriesData,
                                          requireFresh)) {
        return false;
    }
    if (!isCanonicalItem(seriesData)) {
        s_seriesCache.remove(scopedCacheKey(effectiveScope(m_connectionId), seriesId));
        const QString path = seriesCachePath(seriesId);
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
        seriesData = QJsonObject();
        return false;
    }
    return true;
}

void SeriesDetailsViewModel::storeSeriesCache(const QString &seriesId, const QJsonObject &seriesData) const
{
    if (!isCanonicalItem(seriesData)) {
        return;
    }
    DetailViewCache::storeObjectCache(s_seriesCache,
                                      scopedCacheKey(effectiveScope(m_connectionId), seriesId),
                                      seriesCachePath(seriesId),
                                      seriesData);
}

bool SeriesDetailsViewModel::loadItemsFromCache(const QString &parentId, QJsonArray &items, bool requireFresh) const
{
    if (!DetailViewCache::loadArrayCache(s_itemsCache,
                                         scopedCacheKey(effectiveScope(m_connectionId), parentId),
                                         itemsCachePath(parentId),
                                         kItemsMemoryTtlMs,
                                         kItemsDiskTtlMs,
                                         items,
                                         requireFresh,
                                         false)) {
        return false;
    }
    if (!isCanonicalItems(items)) {
        clearItemsCache(parentId);
        items = QJsonArray();
        return false;
    }
    return true;
}

void SeriesDetailsViewModel::storeItemsCache(const QString &parentId, const QJsonArray &items) const
{
    if (!isCanonicalItems(items)) {
        return;
    }
    DetailViewCache::storeArrayCache(s_itemsCache,
                                     scopedCacheKey(effectiveScope(m_connectionId), parentId),
                                     itemsCachePath(parentId),
                                     items);
}

void SeriesDetailsViewModel::clearItemsCache(const QString &parentId) const
{
    if (parentId.isEmpty()) {
        return;
    }

    s_itemsCache.remove(scopedCacheKey(effectiveScope(m_connectionId), parentId));

    const QString path = itemsCachePath(parentId);
    if (!path.isEmpty() && QFile::exists(path)) {
        QFile::remove(path);
    }
}

bool SeriesDetailsViewModel::loadSimilarItemsFromCache(const QString &seriesId, QJsonArray &items, bool requireFresh) const
{
    if (!DetailViewCache::loadArrayCache(s_similarItemsCache,
                                         scopedCacheKey(effectiveScope(m_connectionId), seriesId),
                                         similarItemsCachePath(seriesId),
                                         kSimilarMemoryTtlMs,
                                         kSimilarDiskTtlMs,
                                         items,
                                         requireFresh,
                                         true)) {
        return false;
    }
    if (!isCanonicalItems(items)) {
        s_similarItemsCache.remove(
            scopedCacheKey(effectiveScope(m_connectionId), seriesId));
        const QString path = similarItemsCachePath(seriesId);
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
        items = QJsonArray();
        return false;
    }
    return true;
}

void SeriesDetailsViewModel::storeSimilarItemsCache(const QString &seriesId, const QJsonArray &items) const
{
    if (!isCanonicalItems(items)) {
        return;
    }
    DetailViewCache::storeArrayCache(s_similarItemsCache,
                                     scopedCacheKey(effectiveScope(m_connectionId), seriesId),
                                     similarItemsCachePath(seriesId),
                                     items);
}

void SeriesDetailsViewModel::clearMemoryCacheForTest(const QString &id)
{
    const QString cacheKey = scopedCacheKey(effectiveScope(m_connectionId), id);
    s_seriesCache.remove(cacheKey);
    s_itemsCache.remove(cacheKey);
    s_similarItemsCache.remove(cacheKey);
}

void SeriesDetailsViewModel::clearCacheForTest(const QString &id)
{
    clearMemoryCacheForTest(id);

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

    const QVariantMap &item = m_items.at(index.row());

    switch (role) {
    case NameRole:
        return item.value(QStringLiteral("name"));
    case ImageUrlRole:
        return getImageUrl(item);
    case IdRole:
        return item.value(QStringLiteral("itemId"));
    case IndexNumberRole:
        return item.value(QStringLiteral("indexNumber"));
    case EpisodeCountRole:
        return item.value(QStringLiteral("childCount"));
    case UnplayedItemCountRole:
        return item.value(QStringLiteral("unplayedItemCount"));
    case IsPlayedRole:
        return item.value(QStringLiteral("watched"));
    case ModelDataRole:
        return item;
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

void SeasonsModel::setSeasons(const QVariantList &seasons)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(seasons.size());
    for (const QVariant &value : seasons) {
        if (value.canConvert<QVariantMap>()) {
            m_items.append(value.toMap());
        }
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
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    QVariantMap item = m_items.at(index);
    item.insert(QStringLiteral("imageUrl"), getImageUrl(item));
    return item;
}

QString SeasonsModel::getImageUrl(const QVariantMap &item) const
{
    if (!m_libraryService) {
        return {};
    }

    const QVariantMap primary = item.value(QStringLiteral("primaryArtwork")).toMap();
    const QVariantMap fallback = item.value(QStringLiteral("seriesPrimaryArtwork")).toMap();
    const QVariantMap artwork = !primary.isEmpty() ? primary : fallback;
    if (artwork.isEmpty()) {
        return {};
    }
    return m_libraryService->getCachedArtworkUrlForConnection(
        artwork.value(QStringLiteral("connectionId")).toString(),
        artwork.value(QStringLiteral("itemId")).toString(),
        artwork.value(QStringLiteral("kind")).toString(),
        artwork.value(QStringLiteral("index")).toInt(),
        artwork.value(QStringLiteral("tag")).toString(),
        400);
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

    const QVariantMap &item = m_items.at(index.row());

    switch (role) {
    case NameRole:
        return item.value(QStringLiteral("name"));
    case ImageUrlRole:
        return getImageUrl(item);
    case IdRole:
        return item.value(QStringLiteral("itemId"));
    case IndexNumberRole:
        return item.value(QStringLiteral("indexNumber"));
    case ParentIndexNumberRole:
        return item.value(QStringLiteral("parentIndexNumber"));
    case OverviewRole:
        return item.value(QStringLiteral("overview"));
    case DurationMsRole:
        return item.value(QStringLiteral("durationMs"));
    case IsPlayedRole:
        return item.value(QStringLiteral("watched"));
    case PositionMsRole:
        return item.value(QStringLiteral("positionMs"));
    case CommunityRatingRole:
        return item.value(QStringLiteral("communityRating"));
    case PremiereDateRole:
        return item.value(QStringLiteral("premiereDate"));
    case IsFavoriteRole:
        return item.value(QStringLiteral("favorite"));
    case IsSpecialRole:
        return item.value(QStringLiteral("parentIndexNumber")).toInt() == 0;
    case AirsBeforeSeasonRole:
        return item.value(QStringLiteral("airsBeforeSeasonNumber"), -1);
    case AirsAfterSeasonRole:
        return item.value(QStringLiteral("airsAfterSeasonNumber"), -1);
    case AirsBeforeEpisodeRole:
        return item.value(QStringLiteral("airsBeforeEpisodeNumber"), -1);
    case ModelDataRole:
        return item;
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
        {DurationMsRole, "durationMs"},
        {IsPlayedRole, "isPlayed"},
        {PositionMsRole, "positionMs"},
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

void EpisodesModel::setEpisodes(const QVariantList &episodes)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(episodes.size());
    for (const QVariant &value : episodes) {
        if (value.canConvert<QVariantMap>()) {
            m_items.append(value.toMap());
        }
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
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    QVariantMap item = m_items.at(index);
    item.insert(QStringLiteral("imageUrl"), getImageUrl(item));
    return item;
}

QString EpisodesModel::getImageUrl(const QVariantMap &item) const
{
    if (!m_libraryService) {
        return {};
    }

    const QStringList candidates{
        QStringLiteral("thumbArtwork"),
        QStringLiteral("primaryArtwork"),
        QStringLiteral("parentPrimaryArtwork"),
        QStringLiteral("seriesPrimaryArtwork")
    };
    for (const QString &key : candidates) {
        const QVariantMap artwork = item.value(key).toMap();
        if (artwork.isEmpty()) {
            continue;
        }
        return m_libraryService->getCachedArtworkUrlForConnection(
            artwork.value(QStringLiteral("connectionId")).toString(),
            artwork.value(QStringLiteral("itemId")).toString(),
            artwork.value(QStringLiteral("kind")).toString(),
            artwork.value(QStringLiteral("index")).toInt(),
            artwork.value(QStringLiteral("tag")).toString(),
            640);
    }
    return {};
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
        connect(m_libraryService, &LibraryService::canonicalSeriesDetailsLoaded,
                this, &SeriesDetailsViewModel::onSeriesDetailsLoaded);
        connect(m_libraryService, &LibraryService::canonicalSeriesDetailsNotModified,
                this, &SeriesDetailsViewModel::onSeriesDetailsNotModified);
        connect(m_libraryService, &LibraryService::canonicalItemsLoadedForConnection,
                this, &SeriesDetailsViewModel::onItemsLoaded);
        connect(m_libraryService, &LibraryService::canonicalItemsNotModifiedForConnection,
                this, &SeriesDetailsViewModel::onItemsNotModified);
        connect(m_libraryService, &LibraryService::canonicalNextUnplayedEpisodeLoaded,
                this, &SeriesDetailsViewModel::onNextEpisodeLoaded);
        connect(m_libraryService, &LibraryService::nextUnplayedEpisodeFailed,
                this, &SeriesDetailsViewModel::onNextEpisodeFailed);
        connect(m_libraryService, &LibraryService::seriesWatchedStatusChanged,
                this, &SeriesDetailsViewModel::onSeriesWatchedStatusChanged);
        connect(m_libraryService, &LibraryService::favoriteStatusChanged,
                this, &SeriesDetailsViewModel::onFavoriteStatusChanged);
        connect(m_libraryService,
                qOverload<const QString &, const QVariantMap &, const QString &>(&LibraryService::canonicalItemLoaded),
                this, &SeriesDetailsViewModel::onEpisodeDetailsLoaded);
        connect(m_libraryService,
                qOverload<const QString &, const QString &>(&LibraryService::itemNotModified),
                this, &SeriesDetailsViewModel::onEpisodeDetailsNotModified);
        connect(m_libraryService, &LibraryService::itemFailed,
                this, &SeriesDetailsViewModel::onEpisodeDetailsFailed);
        connect(m_libraryService, &LibraryService::canonicalSimilarItemsLoadedForConnection,
                this, &SeriesDetailsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::canonicalSimilarItemsFailedForConnection,
                this, &SeriesDetailsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::canonicalChaptersLoaded,
                this, &SeriesDetailsViewModel::onFocusedEpisodeChaptersLoaded);
        connect(m_libraryService, &LibraryService::chaptersFailed,
                this, &SeriesDetailsViewModel::onFocusedEpisodeChaptersFailed);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &SeriesDetailsViewModel::onErrorOccurred);
    } else {
        qCWarning(lcViewModels) << "SeriesDetailsViewModel: LibraryService not available in ServiceLocator";
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

    const QString requestConnectionId = configuredConnectionId();
    const bool sameSeries = seriesId == m_seriesId
        && !m_seriesId.isEmpty()
        && requestConnectionId == m_connectionId;

    // A connection change is a different remote identity even when item IDs match.
    if (!sameSeries) {
        clear();
    } else {
        clearError();
    }
    
    m_seriesId = seriesId;
    m_connectionId = requestConnectionId;
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
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Serving series details from cache"
                 << (hasFreshSeries ? "FRESH" : "STALE");
        m_seriesData = cachedSeries;
        updateSeriesMetadata(cachedSeries.toVariantMap());
        emit seriesDataChanged();
    }

    if (hasAnySeasons) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Serving seasons from cache"
                 << (hasFreshSeasons ? "FRESH" : "STALE")
                 << "count:" << cachedSeasons.size();
        // Block seriesLoaded() during synchronous cache hydration; the async
        // series request will emit the real completion signal later.
        m_loadingSeries = true;
        m_loadingSeasons = true;
        onSeasonsLoaded(seriesId, jsonArrayToVariantList(cachedSeasons));
    }

    if (hasAnySimilarItems) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Serving similar items from cache"
                 << (hasFreshSimilarItems ? "FRESH" : "STALE")
                 << "count:" << cachedSimilarItems.size();
        m_similarItems = jsonArrayToVariantList(cachedSimilarItems);
        m_similarItemsAttempted = hasFreshSimilarItems;
        m_similarItemsLoading = false;
        emit similarItemsChanged();
        emit similarItemsLoadingChanged();
    }

    m_loadingSeries = !hasFreshSeries;
    m_loadingSeasons = !hasFreshSeasons;
    setLoading(m_loadingSeries || m_loadingSeasons);
    clearError();

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::loadSeriesDetails" << seriesId;
    
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
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Cached episodes missing placement fields, ignoring cache for" << seasonId;
        hasFreshEpisodes = false;
        hasAnyEpisodes = false;
    }

    if (hasAnyEpisodes) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Serving episodes from cache for season"
                 << seasonId << (hasFreshEpisodes ? "FRESH" : "STALE")
                 << "count:" << cachedEpisodes.size();
        m_loadingEpisodes = true;
        onEpisodesLoaded(seasonId, jsonArrayToVariantList(cachedEpisodes));
    }

    // Always keep the network request "live" so cache-backed UI still gets refreshed
    // when playback/user-data state changes on the server.
    m_loadingEpisodes = true;
    setLoading(!hasAnyEpisodes);

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::loadSeasonEpisodes" << seasonId;
    m_episodesTimer.restart();
    m_libraryService->getItems(seasonId, 0, 0, QStringList(), QStringList(), QString(), QString(), /*includeHeavyFields*/false, /*useCacheValidation*/true);
}

void SeriesDetailsViewModel::refreshSeasonEpisodes(const QString &seasonId)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    if (seasonId.isEmpty()) {
        return;
    }

    if (m_selectedSeasonId != seasonId) {
        m_selectedSeasonId = seasonId;
        emit selectedSeasonIdChanged();
    }

    clearItemsCache(seasonId);
    clearError();
    m_loadingEpisodes = true;
    setLoading(true);

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::refreshSeasonEpisodes" << seasonId;
    m_episodesTimer.restart();
    m_libraryService->getItems(seasonId, 0, 0, QStringList(), QStringList(), QString(), QString(),
                               /*includeHeavyFields*/false, /*useCacheValidation*/false);
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

    const QVariantMap &season = m_seasons.at(index);
    m_selectedSeasonId = season.value(QStringLiteral("itemId")).toString();
    m_selectedSeasonName = season.value(QStringLiteral("name")).toString();
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
        const QString seasonId = m_seasons[idx].value(QStringLiteral("itemId")).toString();
        if (seasonId.isEmpty())
            continue;

        // Skip if we already have fresh cache or a request in-flight.
        const QString prefetchKey = prefetchRequestKey(m_connectionId, seasonId);
        if (m_prefetchSeasonIds.contains(prefetchKey))
            continue;

        QJsonArray cached;
        if (loadItemsFromCache(seasonId, cached, /*requireFresh*/true)) {
            continue;
        }

        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Prefetching season episodes for" << seasonId;
        m_prefetchSeasonIds.insert(prefetchKey);
        m_libraryService->getItems(seasonId, 0, 0, QStringList(), QStringList(),
                                   QString(), QString(), /*includeHeavyFields*/false, /*useCacheValidation*/true);
    }
}

void SeriesDetailsViewModel::markAsWatched()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::markAsWatched" << m_seriesId;
    m_libraryService->markSeriesWatched(m_seriesId);
}

void SeriesDetailsViewModel::markAsUnwatched()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::markAsUnwatched" << m_seriesId;
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
        qCWarning(lcViewModels) << "SeriesDetailsViewModel::loadFocusedEpisodeDetails without LibraryService";
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
    m_focusedEpisodeDetails.clear();
    m_focusedEpisodePeople.clear();
    emit focusedEpisodeDetailsChanged();

    setFocusedEpisodeDetailsLoading(true);

    if (m_pendingEpisodeDetailIds.contains(episodeId)) {
        return;
    }

    startEpisodeDetailsRequest(episodeId);
}

void SeriesDetailsViewModel::clearFocusedEpisodeDetails()
{
    const bool hadFocusedEpisode = !m_focusedEpisodeDetailId.isEmpty()
                                   || !m_focusedEpisodeDetails.isEmpty()
                                   || !m_focusedEpisodePeople.isEmpty()
                                   || m_focusedEpisodeDetailsLoading;

    m_focusedEpisodeDetailId.clear();
    m_focusedEpisodeDetails.clear();
    m_focusedEpisodePeople.clear();
    m_pendingEpisodeDetailIds.clear();
    m_episodeDetailRequestTokens.clear();
    m_episodeDetailRetried.clear();
    setFocusedEpisodeDetailsLoading(false);

    if (hadFocusedEpisode) {
        emit focusedEpisodeDetailsChanged();
    }
}

void SeriesDetailsViewModel::loadFocusedEpisodeChapters(const QString &episodeId)
{
    if (!m_libraryService || episodeId.isEmpty()) {
        clearFocusedEpisodeChapters();
        return;
    }

    m_focusedEpisodeChapterId = episodeId;

    const QString requestKey = m_connectionId + QLatin1Char('\n') + episodeId;
    if (m_episodeChapterCache.contains(requestKey)) {
        applyFocusedEpisodeChapters(episodeId, m_episodeChapterCache.value(requestKey));
        return;
    }

    m_focusedEpisodeChapters.clear();
    emit focusedEpisodeChaptersChanged();
    setFocusedEpisodeChaptersLoading(true);

    if (m_pendingEpisodeChapterIds.contains(requestKey)) {
        return;
    }

    m_pendingEpisodeChapterIds.insert(requestKey);
    m_libraryService->getChapters(episodeId);
}

void SeriesDetailsViewModel::clearFocusedEpisodeChapters()
{
    const bool hadFocusedChapters = !m_focusedEpisodeChapterId.isEmpty()
                                    || !m_focusedEpisodeChapters.isEmpty()
                                    || m_focusedEpisodeChaptersLoading;

    m_focusedEpisodeChapterId.clear();
    m_focusedEpisodeChapters.clear();
    m_pendingEpisodeChapterIds.clear();
    setFocusedEpisodeChaptersLoading(false);

    if (hadFocusedChapters) {
        emit focusedEpisodeChaptersChanged();
    }
}

void SeriesDetailsViewModel::clear(bool preserveArtwork)
{
    m_seriesId.clear();
    m_connectionId.clear();
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
    m_nextEpisodeData.clear();
    m_focusedEpisodeDetailId.clear();
    m_focusedEpisodeDetails.clear();
    m_focusedEpisodePeople.clear();
    m_focusedEpisodeDetailsLoading = false;
    m_focusedEpisodeChapterId.clear();
    m_focusedEpisodeChapters.clear();
    m_focusedEpisodeChaptersLoading = false;
    m_episodeDetailsCache.clear();
    m_pendingEpisodeDetailIds.clear();
    m_episodeDetailRequestTokens.clear();
    m_episodeDetailRetried.clear();
    m_episodeChapterCache.clear();
    m_pendingEpisodeChapterIds.clear();
    m_prefetchSeasonIds.clear();

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
    emit seriesDataChanged();
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
    emit focusedEpisodeChaptersChanged();
    emit focusedEpisodeChaptersLoadingChanged();
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
        qCDebug(lcViewModels) << "MDBList ratings updated, count:" << ratingsList.size();
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
            qCDebug(lcViewModels) << "AniList ID not found via Wikidata";
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
            qCDebug(lcViewModels) << "AniList Score found:" << score;

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
    return m_nextEpisodeData;
}

qint64 SeriesDetailsViewModel::nextEpisodePositionMs() const
{
    return m_nextEpisodeData.value(QStringLiteral("positionMs")).toLongLong();
}

void SeriesDetailsViewModel::onSeriesDetailsLoaded(const QString &connectionId,
                                                   const QString &seriesId,
                                                   const QVariantMap &seriesData)
{
    if (connectionId != m_connectionId || seriesId != m_seriesId) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::onSeriesDetailsLoaded" << seriesId
             << "elapsed(ms):" << m_seriesTimer.elapsed();
    m_loadingSeries = false;
    
    m_seriesData = QJsonObject::fromVariantMap(seriesData);
    updateSeriesMetadata(seriesData);
    storeSeriesCache(seriesId, m_seriesData);
    emit seriesDataChanged();

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

void SeriesDetailsViewModel::onSeriesDetailsNotModified(const QString &connectionId,
                                                        const QString &seriesId)
{
    if (connectionId != m_connectionId || seriesId != m_seriesId) {
        return;
    }

    QJsonObject cached;
    if (loadSeriesFromCache(seriesId, cached, /*requireFresh*/false)) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel::onSeriesDetailsNotModified using cached data";
        m_loadingSeries = false;
        m_seriesData = cached;
        updateSeriesMetadata(cached.toVariantMap());
        emit seriesDataChanged();
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
        qCWarning(lcViewModels) << "SeriesDetailsViewModel::onSeriesDetailsNotModified but no cache found";
        setLoading(false);
    }
}

void SeriesDetailsViewModel::onItemsLoaded(const QString &connectionId,
                                           const QString &parentId,
                                           const QString &queryKey,
                                           const QVariantList &items,
                                           int totalRecordCount)
{
    Q_UNUSED(queryKey)
    Q_UNUSED(totalRecordCount)
    const QString prefetchKey = prefetchRequestKey(connectionId, parentId);
    if (connectionId != m_connectionId) {
        m_prefetchSeasonIds.remove(prefetchKey);
        return;
    }
    if (m_loadingSeasons && parentId == m_seriesId) {
        onSeasonsLoaded(parentId, items);
    } else if (m_loadingEpisodes && parentId == m_selectedSeasonId) {
        onEpisodesLoaded(parentId, items);
    } else if (m_prefetchSeasonIds.contains(prefetchKey)) {
        storeItemsCache(parentId, variantListToJsonArray(items));
        m_prefetchSeasonIds.remove(prefetchKey);
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Prefetched episodes for season"
                              << parentId << "count:" << items.size();
    }
}

void SeriesDetailsViewModel::onSeasonsLoaded(const QString &parentId, const QVariantList &items)
{
    if (parentId != m_seriesId || !m_loadingSeasons) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::onSeasonsLoaded" << parentId << items.size()
             << "seasons elapsed(ms):" << m_seasonsTimer.elapsed();
    m_loadingSeasons = false;

    // Store seasons
    m_seasons.clear();
    m_seasons.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("mediaType")).toString() == QStringLiteral("Season")) {
            if (item.contains(QStringLiteral("childCount"))
                && item.value(QStringLiteral("childCount")).toInt() == 0) {
                qCDebug(lcViewModels) << "Filtering out empty season:"
                                      << item.value(QStringLiteral("name")).toString();
                continue;
            }
            m_seasons.append(item);
        }
    }

    std::sort(m_seasons.begin(), m_seasons.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("indexNumber")).toInt()
            < b.value(QStringLiteral("indexNumber")).toInt();
    });

    QVariantList seasons;
    seasons.reserve(m_seasons.size());
    for (const QVariantMap &season : m_seasons) {
        seasons.append(season);
    }
    m_seasonsModel.setSeasons(seasons);
    storeItemsCache(parentId, variantListToJsonArray(seasons));

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

void SeriesDetailsViewModel::onItemsNotModified(const QString &connectionId,
                                                const QString &parentId,
                                                const QString &queryKey)
{
    Q_UNUSED(queryKey)
    const QString prefetchKey = prefetchRequestKey(connectionId, parentId);
    if (connectionId != m_connectionId) {
        m_prefetchSeasonIds.remove(prefetchKey);
        return;
    }
    QJsonArray cached;
    if (!loadItemsFromCache(parentId, cached, /*requireFresh*/false)) {
        m_prefetchSeasonIds.remove(prefetchKey);
        qCWarning(lcViewModels) << "SeriesDetailsViewModel::onItemsNotModified but no cache for" << parentId;
        return;
    }

    if (parentId == m_seriesId) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Seasons not modified, using cached data";
        onSeasonsLoaded(parentId, jsonArrayToVariantList(cached));
    } else if (parentId == m_selectedSeasonId) {
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Episodes not modified, using cached data for season" << parentId;
        onEpisodesLoaded(parentId, jsonArrayToVariantList(cached));
    } else if (m_prefetchSeasonIds.contains(prefetchKey)) {
        storeItemsCache(parentId, cached);
        m_prefetchSeasonIds.remove(prefetchKey);
        qCDebug(lcViewModels) << "SeriesDetailsViewModel: Prefetch not modified for" << parentId;
    }
}

void SeriesDetailsViewModel::onEpisodesLoaded(const QString &parentId, const QVariantList &items)
{
    if (parentId != m_selectedSeasonId || !m_loadingEpisodes) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::onEpisodesLoaded" << parentId << items.size()
             << "episodes elapsed(ms):" << m_episodesTimer.elapsed();
    m_loadingEpisodes = false;
    setLoading(false);

    int currentSeasonNumber = 0;
    for (const QVariantMap &season : m_seasons) {
        if (season.value(QStringLiteral("itemId")).toString() == parentId) {
            currentSeasonNumber = season.value(QStringLiteral("indexNumber")).toInt();
            break;
        }
    }

    QVector<QVariantMap> regularEpisodes;
    QVector<QVariantMap> specialsBefore;
    QVector<QVariantMap> specialsAfter;
    QMap<int, QVector<QVariantMap>> specialsBeforeEpisode;

    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("mediaType")).toString() != QStringLiteral("Episode")) {
            continue;
        }

        if (item.value(QStringLiteral("locationType")).toString() == QStringLiteral("Virtual")) {
            qCDebug(lcViewModels) << "Filtering out missing episode:"
                                  << item.value(QStringLiteral("name")).toString()
                                  << "S" << item.value(QStringLiteral("parentIndexNumber")).toInt()
                                  << "E" << item.value(QStringLiteral("indexNumber")).toInt();
            continue;
        }

        const int parentIndexNumber = item.value(QStringLiteral("parentIndexNumber")).toInt();
        if (parentIndexNumber == 0 && currentSeasonNumber > 0) {
            const int airsAfterSeason = item.value(QStringLiteral("airsAfterSeasonNumber"), -1).toInt();
            const int airsBeforeSeason = item.value(QStringLiteral("airsBeforeSeasonNumber"), -1).toInt();
            const int airsBeforeEpisode = item.value(QStringLiteral("airsBeforeEpisodeNumber"), -1).toInt();

            qCDebug(lcViewModels) << "Special episode:"
                                  << item.value(QStringLiteral("name")).toString()
                                  << "AirsBeforeSeason:" << airsBeforeSeason
                                  << "AirsAfterSeason:" << airsAfterSeason
                                  << "AirsBeforeEpisode:" << airsBeforeEpisode;

            if (airsAfterSeason == currentSeasonNumber) {
                specialsAfter.append(item);
            } else if (airsBeforeSeason == currentSeasonNumber) {
                if (airsBeforeEpisode > 0) {
                    specialsBeforeEpisode[airsBeforeEpisode].append(item);
                } else {
                    specialsBefore.append(item);
                }
            } else {
                regularEpisodes.append(item);
            }
        } else {
            regularEpisodes.append(item);
        }
    }

    const auto sortByIndex = [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("indexNumber")).toInt()
            < b.value(QStringLiteral("indexNumber")).toInt();
    };
    std::sort(regularEpisodes.begin(), regularEpisodes.end(), sortByIndex);
    std::sort(specialsBefore.begin(), specialsBefore.end(), sortByIndex);
    std::sort(specialsAfter.begin(), specialsAfter.end(), sortByIndex);
    for (auto &specials : specialsBeforeEpisode) {
        std::sort(specials.begin(), specials.end(), sortByIndex);
    }

    QVariantList episodes;
    for (const QVariantMap &episode : specialsBefore) {
        episodes.append(episode);
    }
    for (const QVariantMap &episode : regularEpisodes) {
        const int episodeNumber = episode.value(QStringLiteral("indexNumber")).toInt();
        if (specialsBeforeEpisode.contains(episodeNumber)) {
            for (const QVariantMap &special : specialsBeforeEpisode[episodeNumber]) {
                episodes.append(special);
            }
        }
        episodes.append(episode);
    }
    for (const QVariantMap &episode : specialsAfter) {
        episodes.append(episode);
    }

    // Count mid-season specials
    int midSeasonSpecialCount = 0;
    for (const auto &specials : specialsBeforeEpisode) {
        midSeasonSpecialCount += specials.size();
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel: Final episode count:" << episodes.size()
             << "(Regular:" << regularEpisodes.size()
             << "Specials before season:" << specialsBefore.size()
             << "Specials mid-season:" << midSeasonSpecialCount
             << "Specials after season:" << specialsAfter.size() << ")";

    m_episodesModel.setEpisodes(episodes);
    storeItemsCache(parentId, variantListToJsonArray(episodes));

    emit episodesLoaded();
}

void SeriesDetailsViewModel::onNextEpisodeLoaded(const QString &connectionId,
                                                 const QString &seriesId,
                                                 const QVariantMap &episodeData,
                                                 const QString &requestContext)
{
    Q_UNUSED(requestContext)
    if (connectionId != m_connectionId || seriesId != m_seriesId) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::onNextEpisodeLoaded" << seriesId;
    updateNextEpisode(episodeData);
}

void SeriesDetailsViewModel::onNextEpisodeFailed(const QString &seriesId,
                                                 const QString &error,
                                                 const QString &requestContext)
{
    Q_UNUSED(requestContext);
    if (seriesId != m_seriesId) {
        return;
    }

    qCWarning(lcViewModels) << "SeriesDetailsViewModel next episode error:" << error;
}

void SeriesDetailsViewModel::onSeriesWatchedStatusChanged(const QString &seriesId)
{
    if (seriesId != m_seriesId) {
        return;
    }

    qCDebug(lcViewModels) << "SeriesDetailsViewModel::onSeriesWatchedStatusChanged" << seriesId;
    
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
        m_seriesData.insert(QStringLiteral("favorite"), isFavorite);
        QJsonObject userState = m_seriesData.value(QStringLiteral("userState")).toObject();
        userState.insert(QStringLiteral("favorite"), isFavorite);
        m_seriesData.insert(QStringLiteral("userState"), userState);
        storeSeriesCache(itemId, m_seriesData);
    }

    if (favoriteChanged) {
        emit isFavoriteChanged();
    }
}

void SeriesDetailsViewModel::onSimilarItemsLoaded(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QVariantList &items)
{
    if (connectionId != m_connectionId || itemId != m_seriesId) {
        return;
    }

    m_similarItems.clear();
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (!item.value(QStringLiteral("itemId")).toString().isEmpty()) {
            m_similarItems.append(item);
        }
    }
    m_similarItemsAttempted = true;
    m_similarItemsLoading = false;
    storeSimilarItemsCache(itemId, variantListToJsonArray(m_similarItems));
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
}

void SeriesDetailsViewModel::onSimilarItemsFailed(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QString &error)
{
    if (connectionId != m_connectionId || itemId != m_seriesId) {
        return;
    }

    m_similarItemsAttempted = false;
    if (m_similarItemsLoading) {
        m_similarItemsLoading = false;
        emit similarItemsLoadingChanged();
    }

    qCWarning(lcViewModels) << "SeriesDetailsViewModel similar items error:" << error;
}

void SeriesDetailsViewModel::onErrorOccurred(const QString &endpoint, const QString &error)
{
    // Only handle errors for our current requests
    if (endpoint != "getSeriesDetails" && endpoint != "getItems" &&
        endpoint != "markSeriesWatched" &&
        endpoint != "markSeriesUnwatched") {
        return;
    }

    qCWarning(lcViewModels) << "SeriesDetailsViewModel error:" << endpoint << error;
    m_loadingSeries = false;
    m_loadingSeasons = false;
    m_loadingEpisodes = false;
    setLoading(false);
    setError(mapNetworkError(endpoint, error));
    emit loadError(error);
}

void SeriesDetailsViewModel::onEpisodeDetailsLoaded(const QString &itemId,
                                                    const QVariantMap &data,
                                                    const QString &requestContext)
{
    if (data.value(QStringLiteral("connectionId")).toString() != m_connectionId
        || !matchesEpisodeDetailsRequest(itemId, requestContext)) {
        return;
    }

    finishEpisodeDetailsRequest(itemId);
    m_episodeDetailRetried.remove(itemId);
    m_episodeDetailsCache.insert(itemId, data);

    if (itemId == m_focusedEpisodeDetailId) {
        applyFocusedEpisodeDetails(itemId, data);
    }
}

void SeriesDetailsViewModel::onEpisodeDetailsNotModified(const QString &itemId,
                                                         const QString &requestContext)
{
    if (!matchesEpisodeDetailsRequest(itemId, requestContext)) {
        return;
    }

    finishEpisodeDetailsRequest(itemId);

    if (m_episodeDetailsCache.contains(itemId)) {
        m_episodeDetailRetried.remove(itemId);
        if (itemId == m_focusedEpisodeDetailId) {
            applyFocusedEpisodeDetails(itemId, m_episodeDetailsCache.value(itemId));
        }
        return;
    }

    qCWarning(lcViewModels) << "SeriesDetailsViewModel::onEpisodeDetailsNotModified missing local cache for" << itemId;
    if (itemId == m_focusedEpisodeDetailId && m_libraryService) {
        if (m_episodeDetailRetried.contains(itemId)) {
            qCWarning(lcViewModels) << "SeriesDetailsViewModel::onEpisodeDetailsNotModified stopping repeated retry for" << itemId;
            m_episodeDetailRetried.remove(itemId);
            stopFocusedEpisodeDetailsLoadingFor(itemId);
            return;
        }

        m_episodeDetailRetried.insert(itemId);
        m_libraryService->clearItemCacheValidation(itemId);
        startEpisodeDetailsRequest(itemId);
        return;
    }

    stopFocusedEpisodeDetailsLoadingFor(itemId);
}

void SeriesDetailsViewModel::onEpisodeDetailsFailed(const QString &itemId,
                                                    const QString &error,
                                                    const QString &requestContext)
{
    if (!matchesEpisodeDetailsRequest(itemId, requestContext)) {
        return;
    }

    finishEpisodeDetailsRequest(itemId);
    m_episodeDetailRetried.remove(itemId);

    qCWarning(lcViewModels) << "SeriesDetailsViewModel focused episode details error for" << itemId << ":" << error;

    if (itemId == m_focusedEpisodeDetailId || m_pendingEpisodeDetailIds.isEmpty()) {
        stopFocusedEpisodeDetailsLoadingFor(m_focusedEpisodeDetailId == itemId ? itemId : QString());
    }
}

void SeriesDetailsViewModel::onFocusedEpisodeChaptersLoaded(const QString &connectionId,
                                                            const QString &itemId,
                                                            const QVariantList &chapters)
{
    const QString requestKey = connectionId + QLatin1Char('\n') + itemId;
    if (connectionId != m_connectionId || !m_pendingEpisodeChapterIds.contains(requestKey)) {
        return;
    }

    m_pendingEpisodeChapterIds.remove(requestKey);

    QVariantList normalized;
    normalized.reserve(chapters.size());
    for (const QVariant &value : chapters) {
        QVariantMap chapter = value.toMap();
        chapter.insert(QStringLiteral("thumbnailUrl"),
                       buildArtworkUrl(chapter.value(QStringLiteral("artwork")).toMap(), 480));
        normalized.append(chapter);
    }
    m_episodeChapterCache.insert(requestKey, normalized);

    if (itemId == m_focusedEpisodeChapterId) {
        applyFocusedEpisodeChapters(itemId, normalized);
    }
}

void SeriesDetailsViewModel::onFocusedEpisodeChaptersFailed(const QString &connectionId,
                                                            const QString &itemId,
                                                            const QString &error)
{
    const QString requestKey = connectionId + QLatin1Char('\n') + itemId;
    if (!m_pendingEpisodeChapterIds.contains(requestKey)) {
        return;
    }

    m_pendingEpisodeChapterIds.remove(requestKey);
    qCWarning(lcViewModels) << "SeriesDetailsViewModel focused episode chapters error for" << itemId << ":" << error;

    if (connectionId == m_connectionId && itemId == m_focusedEpisodeChapterId) {
        m_focusedEpisodeChapters.clear();
        setFocusedEpisodeChaptersLoading(false);
        emit focusedEpisodeChaptersChanged();
    }
}

void SeriesDetailsViewModel::updateSeriesMetadata(const QVariantMap &data)
{
    m_title = data.value(QStringLiteral("name")).toString();
    m_overview = data.value(QStringLiteral("overview")).toString();
    if (m_overview.isEmpty()) {
        m_overview = QStringLiteral("No synopsis available.");
    }
    m_productionYear = data.value(QStringLiteral("productionYear")).toInt();
    m_isWatched = data.value(QStringLiteral("watched")).toBool();
    m_isFavorite = data.value(QStringLiteral("favorite")).toBool();
    m_officialRating = data.value(QStringLiteral("officialRating")).toString();
    m_recursiveItemCount = data.value(QStringLiteral("recursiveItemCount")).toInt();
    m_status = data.value(QStringLiteral("status")).toString();

    const QString endDate = data.value(QStringLiteral("endDate")).toString();
    m_endDate = endDate.isEmpty() ? QDateTime() : QDateTime::fromString(endDate, Qt::ISODate);

    m_logoUrl = buildArtworkUrl(data.value(QStringLiteral("logoArtwork")).toMap(), 2000);
    m_posterUrl = buildArtworkUrl(data.value(QStringLiteral("primaryArtwork")).toMap(), 400);
    m_backdropUrl = buildArtworkUrl(data.value(QStringLiteral("backdropArtwork")).toMap(), 1920);
    if (m_backdropUrl.isEmpty()) {
        m_backdropUrl = m_posterUrl;
    }

    m_people = canonicalPeople(data.value(QStringLiteral("people")));
    m_genres = canonicalStringList(data.value(QStringLiteral("genres")));

    emit titleChanged();
    emit overviewChanged();
    emit productionYearChanged();
    emit isWatchedChanged();
    emit isFavoriteChanged();
    emit officialRatingChanged();
    emit recursiveItemCountChanged();
    emit statusChanged();
    emit endDateChanged();
    emit logoUrlChanged();
    emit posterUrlChanged();
    emit backdropUrlChanged();
    emit peopleChanged();
    emit genresChanged();

    const QVariantMap providerIds = data.value(QStringLiteral("providerIds")).toMap();
    const QString imdbId = providerIds.value(QStringLiteral("Imdb")).toString();
    const QString tmdbId = providerIds.value(QStringLiteral("Tmdb")).toString();
    m_imdbId = imdbId;
    m_tmdbId = tmdbId;
    emit tmdbIdChanged();
    
    if (!imdbId.isEmpty() || !tmdbId.isEmpty()) {
        fetchMdbListRatings(imdbId, tmdbId, "show");
    } else if (!m_title.isEmpty()) {
        // Fallback to title search if no IDs?
        // Maybe later. For now, rely on IDs.
        qCDebug(lcViewModels) << "No IDs for MDBList, skipping.";
    }
    
    // Trigger AniList fetch if we have IMDb ID
    if (!imdbId.isEmpty()) {
        fetchAniListRating(imdbId, m_title, m_productionYear);
    }
}

void SeriesDetailsViewModel::applyFocusedEpisodeDetails(const QString &episodeId, const QVariantMap &data)
{
    m_focusedEpisodeDetailId = episodeId;
    m_focusedEpisodeDetails = data;
    m_focusedEpisodePeople = canonicalPeople(data.value(QStringLiteral("people")));
    setFocusedEpisodeDetailsLoading(false);
    emit focusedEpisodeDetailsChanged();
}

QString SeriesDetailsViewModel::startEpisodeDetailsRequest(const QString &episodeId)
{
    if (!m_libraryService || episodeId.isEmpty()) {
        return QString();
    }

    const QString requestContext =
        QStringLiteral("SeriesDetails:%1:%2:%3")
            .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(this)), 0, 16)
            .arg(++m_episodeDetailRequestSequence)
            .arg(episodeId);

    m_pendingEpisodeDetailIds.insert(episodeId);
    m_episodeDetailRequestTokens.insert(episodeId, requestContext);
    m_libraryService->getItem(episodeId, requestContext);
    return requestContext;
}

bool SeriesDetailsViewModel::matchesEpisodeDetailsRequest(const QString &itemId,
                                                          const QString &requestContext) const
{
    if (itemId.isEmpty() || requestContext.isEmpty()) {
        return false;
    }

    return m_episodeDetailRequestTokens.value(itemId) == requestContext;
}

void SeriesDetailsViewModel::finishEpisodeDetailsRequest(const QString &itemId)
{
    m_pendingEpisodeDetailIds.remove(itemId);
    m_episodeDetailRequestTokens.remove(itemId);
}

void SeriesDetailsViewModel::stopFocusedEpisodeDetailsLoadingFor(const QString &itemId)
{
    if (!itemId.isEmpty() && itemId != m_focusedEpisodeDetailId) {
        return;
    }

    if (m_pendingEpisodeDetailIds.isEmpty()) {
        setFocusedEpisodeDetailsLoading(false);
        emit focusedEpisodeDetailsChanged();
    }
}

void SeriesDetailsViewModel::setFocusedEpisodeDetailsLoading(bool loading)
{
    if (m_focusedEpisodeDetailsLoading == loading) {
        return;
    }

    m_focusedEpisodeDetailsLoading = loading;
    emit focusedEpisodeDetailsLoadingChanged();
}

void SeriesDetailsViewModel::applyFocusedEpisodeChapters(const QString &episodeId,
                                                         const QVariantList &chapters)
{
    if (episodeId != m_focusedEpisodeChapterId) {
        return;
    }

    m_focusedEpisodeChapters = chapters;
    setFocusedEpisodeChaptersLoading(false);
    emit focusedEpisodeChaptersChanged();
}

void SeriesDetailsViewModel::setFocusedEpisodeChaptersLoading(bool loading)
{
    if (m_focusedEpisodeChaptersLoading == loading) {
        return;
    }

    m_focusedEpisodeChaptersLoading = loading;
    emit focusedEpisodeChaptersLoadingChanged();
}

void SeriesDetailsViewModel::updateNextEpisode(const QVariantMap &episodeData)
{
    m_nextEpisodeData = episodeData;

    if (episodeData.isEmpty()) {
        m_nextEpisodeId.clear();
        m_nextEpisodeName.clear();
        m_nextEpisodeNumber = 0;
        m_nextSeasonNumber = 0;
        m_nextEpisodeImageUrl.clear();
    } else {
        m_nextEpisodeId = episodeData.value(QStringLiteral("itemId")).toString();
        m_nextEpisodeName = episodeData.value(QStringLiteral("name")).toString();
        m_nextEpisodeNumber = episodeData.value(QStringLiteral("indexNumber")).toInt();
        m_nextSeasonNumber = episodeData.value(QStringLiteral("parentIndexNumber")).toInt();

        m_nextEpisodeImageUrl = buildArtworkUrl(
            episodeData.value(QStringLiteral("thumbArtwork")).toMap(), 400);
        if (m_nextEpisodeImageUrl.isEmpty()) {
            m_nextEpisodeImageUrl = buildArtworkUrl(
                episodeData.value(QStringLiteral("primaryArtwork")).toMap(), 400);
        }
        if (m_nextEpisodeImageUrl.isEmpty()) {
            m_nextEpisodeImageUrl = m_posterUrl;
        }
    }

    emit nextEpisodeChanged();
}

QString SeriesDetailsViewModel::buildArtworkUrl(const QVariantMap &artwork, int width) const
{
    if (!m_libraryService || artwork.isEmpty()) {
        return {};
    }
    return m_libraryService->getCachedArtworkUrlForConnection(
        artwork.value(QStringLiteral("connectionId")).toString(),
        artwork.value(QStringLiteral("itemId")).toString(),
        artwork.value(QStringLiteral("kind")).toString(),
        artwork.value(QStringLiteral("index")).toInt(),
        artwork.value(QStringLiteral("tag")).toString(),
        width);
}
