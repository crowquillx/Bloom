#include "MovieDetailsViewModel.h"
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
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>

namespace {
constexpr qint64 kMovieMemoryTtlMs = 5 * 60 * 1000;   // 5 minutes
constexpr qint64 kMovieDiskTtlMs   = 60 * 60 * 1000;  // 1 hour
constexpr qint64 kSimilarMemoryTtlMs = 5 * 60 * 1000;
constexpr qint64 kSimilarDiskTtlMs   = 60 * 60 * 1000;

static QHash<QString, DetailViewCache::ObjectCacheEntry> s_movieCache;
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_similarItemsCache;
}

MovieDetailsViewModel::MovieDetailsViewModel(QObject *parent)
    : BaseViewModel(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_networkManager = new QNetworkAccessManager(this);
    
    if (m_libraryService) {
        // Connect to generalized item loading signals or specific movie signals if available
        // LibraryService emits itemLoaded for individual items
        connect(m_libraryService, &LibraryService::itemLoaded,
                this, &MovieDetailsViewModel::onMovieDetailsLoaded);
        connect(m_libraryService, &LibraryService::itemNotModified,
                this, &MovieDetailsViewModel::onMovieDetailsNotModified);
        connect(m_libraryService, &LibraryService::similarItemsLoaded,
                this, &MovieDetailsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::similarItemsFailed,
                this, &MovieDetailsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &MovieDetailsViewModel::onErrorOccurred);
        // Map generic itemPlayedStatusChanged to internal logic if needed,
        // or rely on next refresh. But here we can bind to itemUserDataChanged if we implement it.
        // For now, let's just listen to itemLoaded which contains UserData.
        // Also listen to itemPlayedStatusChanged to update local state.
         connect(m_libraryService, &LibraryService::itemPlayedStatusChanged,
                this, [this](const QString &itemId, bool played) {
                    if (itemId == m_movieId) {
                        m_isWatched = played;
                        emit isWatchedChanged();
                        // Also update internal QJson object if possible
                        QJsonObject userData = m_movieData["UserData"].toObject();
                        userData["Played"] = played;
                        m_movieData["UserData"] = userData;
                        storeMovieCache(m_movieId, m_movieData);
                    }
                });
    } else {
        qWarning() << "MovieDetailsViewModel: LibraryService not available in ServiceLocator";
    }
}

QString MovieDetailsViewModel::cacheDir() const
{
    QString baseDir;
    if (auto *config = ServiceLocator::tryGet<ConfigManager>()) {
        baseDir = config->getConfigDir();
    } else {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/Bloom";
    }
    return baseDir + "/cache/movies";
}

QString MovieDetailsViewModel::movieCachePath(const QString &movieId) const
{
    if (movieId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(movieId) + "_details.json");
}

QString MovieDetailsViewModel::similarItemsCachePath(const QString &movieId) const
{
    if (movieId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(movieId) + "_similar_items.json");
}

bool MovieDetailsViewModel::loadMovieFromCache(const QString &movieId, QJsonObject &movieData, bool requireFresh) const
{
    return DetailViewCache::loadObjectCache(s_movieCache,
                                            movieId,
                                            movieCachePath(movieId),
                                            kMovieMemoryTtlMs,
                                            kMovieDiskTtlMs,
                                            movieData,
                                            requireFresh);
}

bool MovieDetailsViewModel::loadSimilarItemsFromCache(const QString &movieId, QJsonArray &items, bool requireFresh) const
{
    return DetailViewCache::loadArrayCache(s_similarItemsCache,
                                           movieId,
                                           similarItemsCachePath(movieId),
                                           kSimilarMemoryTtlMs,
                                           kSimilarDiskTtlMs,
                                           items,
                                           requireFresh,
                                           true);
}

void MovieDetailsViewModel::storeMovieCache(const QString &movieId, const QJsonObject &movieData) const
{
    DetailViewCache::storeObjectCache(s_movieCache,
                                      movieId,
                                      movieCachePath(movieId),
                                      movieData);
}

void MovieDetailsViewModel::storeSimilarItemsCache(const QString &movieId, const QJsonArray &items) const
{
    DetailViewCache::storeArrayCache(s_similarItemsCache,
                                     movieId,
                                     similarItemsCachePath(movieId),
                                     items);
}

void MovieDetailsViewModel::loadMovieDetails(const QString &movieId)
{
    if (!m_libraryService) {
        setError("Library service not available");
        emit loadError(errorMessage());
        return;
    }

    if (movieId.isEmpty()) {
        setError("Movie ID is empty");
        emit loadError(errorMessage());
        return;
    }

    const bool sameMovie = (movieId == m_movieId && !m_movieId.isEmpty());

    // Clear only when navigating to a different movie
    if (!sameMovie) {
        clear();
    } else {
        clearError();
    }
    
    m_movieId = movieId;
    emit movieIdChanged();
    
    // Try cache first
    QJsonObject cachedMovie;
    QJsonArray cachedSimilarItems;
    bool hasFresh = loadMovieFromCache(movieId, cachedMovie, /*requireFresh*/true);
    bool hasAny = hasFresh || loadMovieFromCache(movieId, cachedMovie, /*requireFresh*/false);
    bool hasFreshSimilarItems = loadSimilarItemsFromCache(movieId, cachedSimilarItems, /*requireFresh*/true);
    bool hasAnySimilarItems = hasFreshSimilarItems || loadSimilarItemsFromCache(movieId, cachedSimilarItems, /*requireFresh*/false);

    if (hasAny) {
        qDebug() << "MovieDetailsViewModel: Serving movie details from cache"
                 << (hasFresh ? "FRESH" : "STALE");
        m_movieData = cachedMovie;
        updateMovieMetadata(cachedMovie);
    }

    if (hasAnySimilarItems) {
        qDebug() << "MovieDetailsViewModel: Serving similar items from cache"
                 << (hasFreshSimilarItems ? "FRESH" : "STALE")
                 << "count:" << cachedSimilarItems.size();
        QVariantList mappedItems;
        mappedItems.reserve(cachedSimilarItems.size());
        for (const auto &value : cachedSimilarItems) {
            const QJsonObject item = value.toObject();
            if (item.isEmpty()) {
                continue;
            }
            mappedItems.append(item.toVariantMap());
        }
        m_similarItems = mappedItems;
        m_similarItemsAttempted = hasFreshSimilarItems;
        m_similarItemsLoading = false;
        emit similarItemsChanged();
        emit similarItemsLoadingChanged();
    }

    m_loadingMovie = !hasFresh;
    setLoading(m_loadingMovie);
    clearError();

    qDebug() << "MovieDetailsViewModel::loadMovieDetails" << movieId;
    
    // Fetch from server
    // Request typical fields for details view
    m_libraryService->getItem(movieId);
}

void MovieDetailsViewModel::reload()
{
    if (!m_movieId.isEmpty()) {
        m_similarItemsAttempted = false;
        loadMovieDetails(m_movieId);
    }
}

void MovieDetailsViewModel::markAsWatched()
{
    if (!m_libraryService || m_movieId.isEmpty()) return;
    m_libraryService->markItemPlayed(m_movieId);
}

void MovieDetailsViewModel::markAsUnwatched()
{
    if (!m_libraryService || m_movieId.isEmpty()) return;
    m_libraryService->markItemUnplayed(m_movieId);
}

void MovieDetailsViewModel::clear(bool preserveArtwork)
{
    m_movieId.clear();
    m_title.clear();
    m_overview.clear();
    if (!preserveArtwork) {
        m_logoUrl.clear();
        m_posterUrl.clear();
        m_backdropUrl.clear();
    }
    m_productionYear = 0;
    m_isWatched = false;
    m_officialRating.clear();
    m_runtimeTicks = 0;
    m_communityRating = 0.0;
    m_people.clear();
    m_genres.clear();
    m_similarItems.clear();
    m_similarItemsAttempted = false;
    m_similarItemsLoading = false;
    m_playbackPositionTicks = 0;
    m_premiereDate = QDateTime();
    
    m_movieData = QJsonObject();

    if (!preserveArtwork) {
        // Clear ratings data
        m_mdbListRatings.clear();
        m_rawMdbListRatings.clear();
        m_currentAniListImdbId.clear();
        m_aniListRating.clear();
    }

    m_loadingMovie = false;
    setLoading(false);
    clearError();
    
    emit movieIdChanged();
    emit titleChanged();
    emit overviewChanged();
    
    if (!preserveArtwork) {
        emit logoUrlChanged();
        emit posterUrlChanged();
        emit backdropUrlChanged();
    }
    
    emit productionYearChanged();
    emit isWatchedChanged();
    emit officialRatingChanged();
    emit runtimeTicksChanged();
    emit communityRatingChanged();
    emit peopleChanged();
    emit genresChanged();
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
    emit premiereDateChanged();
    emit playbackPositionTicksChanged();
    if (!preserveArtwork) {
        emit mdbListRatingsChanged();
    }
}

QVariantMap MovieDetailsViewModel::getMovieData() const
{
    return m_movieData.toVariantMap();
}

void MovieDetailsViewModel::onMovieDetailsLoaded(const QString &itemId, const QJsonObject &data)
{
    if (itemId != m_movieId) return;
    
    m_loadingMovie = false;
    setLoading(false);
    
    m_movieData = data;
    updateMovieMetadata(data);
    storeMovieCache(itemId, data);

    if (!m_similarItemsAttempted && !m_similarItemsLoading && m_libraryService) {
        m_similarItemsAttempted = true;
        m_similarItemsLoading = true;
        emit similarItemsLoadingChanged();
        m_libraryService->getSimilarItems(itemId);
    }
    
    emit movieLoaded();
}

void MovieDetailsViewModel::onMovieDetailsNotModified(const QString &itemId)
{
    if (itemId != m_movieId) return;
    
    m_loadingMovie = false;
    setLoading(false);
    qDebug() << "MovieDetailsViewModel: Movie details not modified" << itemId;

    if (!m_similarItemsAttempted && !m_similarItemsLoading && m_libraryService) {
        m_similarItemsAttempted = true;
        m_similarItemsLoading = true;
        emit similarItemsLoadingChanged();
        m_libraryService->getSimilarItems(itemId);
    }
}

void MovieDetailsViewModel::onSimilarItemsLoaded(const QString &itemId, const QJsonArray &items)
{
    if (itemId != m_movieId) {
        return;
    }

    m_similarItems = DetailListHelper::mapSimilarItems(items);
    m_similarItemsAttempted = true;
    m_similarItemsLoading = false;
    storeSimilarItemsCache(itemId, items);
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
}

void MovieDetailsViewModel::onSimilarItemsFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_movieId) {
        return;
    }

    m_similarItemsAttempted = true;
    if (m_similarItemsLoading) {
        m_similarItemsLoading = false;
        emit similarItemsLoadingChanged();
    }

    qWarning() << "MovieDetailsViewModel similar items error:" << error;
}

void MovieDetailsViewModel::onErrorOccurred(const QString &endpoint, const QString &error)
{
    // Simple heuristic to check if this error relates to our current loading
    if (m_loadingMovie) {
        m_loadingMovie = false;
        setLoading(false);
        setError(error);
        emit loadError(error);
    }
}



void MovieDetailsViewModel::updateMovieMetadata(const QJsonObject &data)
{
    const auto common = DetailMetadataHelper::extractCommonMetadata(
        data,
        m_movieId,
        [this](const QString &itemId, const QString &imageType, int width) {
            return m_libraryService ? m_libraryService->getCachedImageUrlWithWidth(itemId, imageType, width)
                                    : QString();
        },
        QString(),
        true);

    m_title = common.title;
    m_overview = common.overview;
    m_productionYear = common.productionYear;
    m_officialRating = common.officialRating;
    m_runtimeTicks = data.value("RunTimeTicks").toVariant().toLongLong();
    m_communityRating = data.value("CommunityRating").toDouble();

    if (data.contains("PremiereDate")) {
        m_premiereDate = QDateTime::fromString(data.value("PremiereDate").toString(), Qt::ISODate);
    }

    m_genres = common.genres;
    m_isWatched = common.isWatched;
    m_playbackPositionTicks = common.playbackPositionTicks;
    m_people = common.people;
    m_logoUrl = common.logoUrl;
    m_posterUrl = common.posterUrl;
    m_backdropUrl = common.backdropUrl;

    emit titleChanged();
    emit overviewChanged();
    emit productionYearChanged();
    emit officialRatingChanged();
    emit runtimeTicksChanged();
    emit communityRatingChanged();
    emit peopleChanged();
    emit genresChanged();
    emit premiereDateChanged();
    emit isWatchedChanged();
    emit playbackPositionTicksChanged();
    emit logoUrlChanged();
    emit posterUrlChanged();
    emit backdropUrlChanged();
    
    // Fetch External Ratings
    // Get external IDs
    QString imdbId = data.value("ProviderIds").toObject().value("Imdb").toString();
    QString tmdbId = data.value("ProviderIds").toObject().value("Tmdb").toString();
    
    if (!imdbId.isEmpty() || !tmdbId.isEmpty()) {
        fetchMdbListRatings(imdbId, tmdbId, "movie");
    }
    
    // Try to fetch AniList ratings if it's an anime
    bool isAnime = false;
    for (const auto &genre : m_genres) {
        if (genre.compare("Anime", Qt::CaseInsensitive) == 0 || 
            genre.compare("Animation", Qt::CaseInsensitive) == 0) {
            isAnime = true;
            break;
        }
    }
    
    if (isAnime && !imdbId.isEmpty()) {
        fetchAniListRating(imdbId, m_title, m_productionYear);
    }
}

// ============================================================
// External Ratings Logic (Ported from SeriesDetailsViewModel)
// ============================================================

void MovieDetailsViewModel::fetchMdbListRatings(const QString &imdbId, const QString &tmdbId, const QString &type)
{
    auto *config = ServiceLocator::tryGet<ConfigManager>();
    if (!config) return;

    ExternalRatingsHelper::fetchMdbListRatings(m_networkManager,
                                               this,
                                               config->getMdbListApiKey(),
                                               imdbId,
                                               tmdbId,
                                               type,
                                               [this](const QVariantMap &rawRatings) {
        m_rawMdbListRatings = rawRatings;
        compileRatings();

        const QVariantList ratingsList = m_mdbListRatings.value("ratings").toList();
        qDebug() << "MDBList ratings updated, count:" << ratingsList.size();
    });
}

void MovieDetailsViewModel::fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback)
{
    ExternalRatingsHelper::fetchAniListIdFromWikidata(m_networkManager, this, imdbId, std::move(callback));
}

void MovieDetailsViewModel::queryAniListById(const QString &anilistId)
{
    ExternalRatingsHelper::queryAniListById(m_networkManager, this, anilistId, [this](const QJsonObject &media) {
        const int avgScore = media["averageScore"].toInt();
        const int meanScore = media["meanScore"].toInt();
        const int score = (avgScore > 0) ? avgScore : meanScore;

        if (score > 0) {
            QVariantMap aniRating;
            aniRating["source"] = "AniList";
            aniRating["value"] = score;
            aniRating["score"] = score;
            aniRating["url"] = media["siteUrl"].toString();

            m_aniListRating = aniRating;
            compileRatings();
        }
    });
}

void MovieDetailsViewModel::fetchAniListRating(const QString &imdbId, const QString &title, int year)
{
    if (imdbId.isEmpty()) return;
    
    // Prevent re-querying if we already looked up this ID
    if (m_currentAniListImdbId == imdbId) return;
    m_currentAniListImdbId = imdbId;
    m_aniListRating.clear(); // Clear old data

    // First try mapping via Wikidata
    fetchAniListIdFromWikidata(imdbId, [this, title, year](const QString &foundId) {
        if (!foundId.isEmpty()) {
            qDebug() << "Found AniList ID via Wikidata:" << foundId;
            queryAniListById(foundId);
        } else {
            // Fallback: Search by title if Wikidata fails
            // NOTE: Implementing just the Wikidata path first as it's cleaner. 
            // Title search can be fuzzy.
            qWarning() << "No AniList ID found in Wikidata for" << title;
        }
    });
}

void MovieDetailsViewModel::compileRatings()
{
    const QVariantMap combined = ExternalRatingsHelper::mergeRatings(m_rawMdbListRatings, m_aniListRating);
    if (m_mdbListRatings != combined) {
        m_mdbListRatings = combined;
        emit mdbListRatingsChanged();
    }
}
