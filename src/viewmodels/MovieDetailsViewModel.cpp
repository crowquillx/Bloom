#include "MovieDetailsViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../models/MediaModels.h"
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
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include "../utils/BloomLogging.h"

namespace {
constexpr qint64 kMovieMemoryTtlMs = 5 * 60 * 1000;   // 5 minutes
constexpr qint64 kMovieDiskTtlMs   = 60 * 60 * 1000;  // 1 hour
constexpr qint64 kSimilarMemoryTtlMs = 5 * 60 * 1000;
constexpr qint64 kSimilarDiskTtlMs   = 60 * 60 * 1000;

static QHash<QString, DetailViewCache::ObjectCacheEntry> s_movieCache;
static QHash<QString, DetailViewCache::ArrayCacheEntry> s_similarItemsCache;
static QString s_memoryCacheScope;

QString activeCacheScope()
{
    if (auto *config = ServiceLocator::tryGet<ConfigManager>()) {
        const auto connection = config->getActiveConnection();
        if (connection.has_value() && !connection->connectionId.isEmpty()) {
            return connection->connectionId;
        }
    }
    return QStringLiteral("_local");
}

QString scopedCacheKey(const QString &remoteId)
{
    const QString scope = activeCacheScope();
    if (s_memoryCacheScope != scope) {
        s_movieCache.clear();
        s_similarItemsCache.clear();
        s_memoryCacheScope = scope;
    }
    return scope + QLatin1Char('\n') + remoteId;
}

QString movieChapterRequestKey(const QString &connectionId, const QString &itemId)
{
    return connectionId + QLatin1Char('\n') + itemId;
}

bool isCanonicalMovieCache(const QJsonObject &movieData)
{
    return movieData.contains(QStringLiteral("itemId"))
        && !movieData.contains(QStringLiteral("Id"))
        && !movieData.contains(QStringLiteral("RunTimeTicks"))
        && !movieData.contains(QStringLiteral("UserData"))
        && !movieData.contains(QStringLiteral("ImageTags"))
        && !movieData.contains(QStringLiteral("BackdropImageTags"))
        && !movieData.contains(QStringLiteral("ProviderIds"));
}

bool isCanonicalSimilarItemsCache(const QJsonArray &items)
{
    if (items.isEmpty()) {
        return true;
    }
    const QJsonObject first = items.at(0).toObject();
    return first.contains(QStringLiteral("itemId"))
        && !first.contains(QStringLiteral("Id"))
        && !first.contains(QStringLiteral("ImageTags"));
}

QJsonArray variantListToJsonArray(const QVariantList &items)
{
    QJsonArray array;
    for (const QVariant &value : items) {
        if (!value.canConvert<QVariantMap>()) {
            continue;
        }
        array.append(QJsonObject::fromVariantMap(value.toMap()));
    }
    return array;
}

QVariantList jsonArrayToVariantList(const QJsonArray &items)
{
    QVariantList mapped;
    mapped.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject()) {
            mapped.append(value.toObject().toVariantMap());
        }
    }
    return mapped;
}

QStringList genresFromCanonical(const QVariant &genresValue)
{
    QStringList genres;
    const QVariantList list = genresValue.toList();
    genres.reserve(list.size());
    for (const QVariant &value : list) {
        const QString genre = value.toString();
        if (!genre.isEmpty()) {
            genres.append(genre);
        }
    }
    return genres;
}

} // namespace

MovieDetailsViewModel::MovieDetailsViewModel(QObject *parent)
    : BaseViewModel(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_networkManager = new QNetworkAccessManager(this);
    
    if (m_libraryService) {
        connect(m_libraryService,
                qOverload<const QString &, const QVariantMap &>(&LibraryService::canonicalItemLoaded),
                this, &MovieDetailsViewModel::onMovieDetailsLoaded);
        connect(m_libraryService,
                qOverload<const QString &>(&LibraryService::itemNotModified),
                this, &MovieDetailsViewModel::onMovieDetailsNotModified);
        connect(m_libraryService, &LibraryService::canonicalSimilarItemsLoaded,
                this, &MovieDetailsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::similarItemsFailed,
                this, &MovieDetailsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::canonicalChaptersLoaded,
                this, &MovieDetailsViewModel::onMovieChaptersLoaded);
        connect(m_libraryService, &LibraryService::chaptersFailed,
                this, &MovieDetailsViewModel::onMovieChaptersFailed);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &MovieDetailsViewModel::onErrorOccurred);
        connect(m_libraryService, &LibraryService::itemPlayedStatusChanged,
                this, [this](const QString &itemId, bool played) {
                    if (itemId != m_movieId) {
                        return;
                    }
                    m_isWatched = played;
                    emit isWatchedChanged();
                    m_movieData.insert(QStringLiteral("watched"), played);
                    QJsonObject userState = m_movieData.value(QStringLiteral("userState")).toObject();
                    userState.insert(QStringLiteral("watched"), played);
                    m_movieData.insert(QStringLiteral("userState"), userState);
                    storeMovieCache(m_movieId, m_movieData);
                });
    } else {
        qCWarning(lcViewModels) << "MovieDetailsViewModel: LibraryService not available in ServiceLocator";
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
    const QString scope = DetailViewCache::connectionScopeCacheKey(activeCacheScope());
    return baseDir + QStringLiteral("/cache/connections/") + scope + QStringLiteral("/movies");
}

QString MovieDetailsViewModel::movieCachePath(const QString &movieId) const
{
    if (movieId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(movieId) + "_details_canonical.json");
}

QString MovieDetailsViewModel::similarItemsCachePath(const QString &movieId) const
{
    if (movieId.isEmpty()) return QString();
    QDir dir(cacheDir());
    dir.mkpath(".");
    return dir.filePath(DetailViewCache::sanitizeCacheKey(movieId) + "_similar_items_canonical.json");
}

bool MovieDetailsViewModel::loadMovieFromCache(const QString &movieId, QJsonObject &movieData, bool requireFresh) const
{
    if (!DetailViewCache::loadObjectCache(s_movieCache,
                                          scopedCacheKey(movieId),
                                          movieCachePath(movieId),
                                          kMovieMemoryTtlMs,
                                          kMovieDiskTtlMs,
                                          movieData,
                                          requireFresh)) {
        return false;
    }
    if (!isCanonicalMovieCache(movieData)) {
        movieData = QJsonObject();
        return false;
    }
    return true;
}

bool MovieDetailsViewModel::loadSimilarItemsFromCache(const QString &movieId, QJsonArray &items, bool requireFresh) const
{
    if (!DetailViewCache::loadArrayCache(s_similarItemsCache,
                                         scopedCacheKey(movieId),
                                         similarItemsCachePath(movieId),
                                         kSimilarMemoryTtlMs,
                                         kSimilarDiskTtlMs,
                                         items,
                                         requireFresh,
                                         true)) {
        return false;
    }
    if (!isCanonicalSimilarItemsCache(items)) {
        items = QJsonArray();
        return false;
    }
    return true;
}

void MovieDetailsViewModel::storeMovieCache(const QString &movieId, const QJsonObject &movieData) const
{
    if (!isCanonicalMovieCache(movieData)) {
        return;
    }
    DetailViewCache::storeObjectCache(s_movieCache,
                                      scopedCacheKey(movieId),
                                      movieCachePath(movieId),
                                      movieData);
}

void MovieDetailsViewModel::storeSimilarItemsCache(const QString &movieId, const QJsonArray &items) const
{
    if (!isCanonicalSimilarItemsCache(items)) {
        return;
    }
    DetailViewCache::storeArrayCache(s_similarItemsCache,
                                     scopedCacheKey(movieId),
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
        qCDebug(lcViewModels) << "MovieDetailsViewModel: Serving movie details from cache"
                 << (hasFresh ? "FRESH" : "STALE");
        m_movieData = cachedMovie;
        updateMovieMetadata(cachedMovie.toVariantMap());
    }

    if (hasAnySimilarItems) {
        qCDebug(lcViewModels) << "MovieDetailsViewModel: Serving similar items from cache"
                 << (hasFreshSimilarItems ? "FRESH" : "STALE")
                 << "count:" << cachedSimilarItems.size();
        m_similarItems = normalizeSimilarItems(jsonArrayToVariantList(cachedSimilarItems));
        m_similarItemsAttempted = hasFreshSimilarItems;
        m_similarItemsLoading = false;
        emit similarItemsChanged();
        emit similarItemsLoadingChanged();
    }

    m_loadingMovie = !hasFresh;
    setLoading(m_loadingMovie);
    clearError();

    qCDebug(lcViewModels) << "MovieDetailsViewModel::loadMovieDetails" << movieId;
    
    m_libraryService->getItem(movieId);
    loadMovieChapters(movieId);
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
    m_durationMs = 0;
    m_positionMs = 0;
    m_communityRating = 0.0;
    m_people.clear();
    m_chapters.clear();
    m_movieChapterId.clear();
    m_pendingMovieChapterIds.clear();
    m_chaptersLoading = false;
    m_genres.clear();
    m_similarItems.clear();
    m_similarItemsAttempted = false;
    m_similarItemsLoading = false;
    m_premiereDate = QDateTime();
    
    m_movieData = QJsonObject();

    if (!preserveArtwork) {
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
    emit durationMsChanged();
    emit positionMsChanged();
    emit communityRatingChanged();
    emit peopleChanged();
    emit chaptersChanged();
    emit chaptersLoadingChanged();
    emit genresChanged();
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
    emit premiereDateChanged();
    if (!preserveArtwork) {
        emit mdbListRatingsChanged();
    }
}

void MovieDetailsViewModel::loadMovieChapters(const QString &movieId)
{
    if (!m_libraryService || movieId.isEmpty()) {
        clearMovieChapters();
        return;
    }

    m_movieChapterId = movieId;
    const QString requestKey = movieChapterRequestKey(activeCacheScope(), movieId);
    if (m_movieChapterCache.contains(requestKey)) {
        applyMovieChapters(movieId, m_movieChapterCache.value(requestKey));
        return;
    }

    m_chapters.clear();
    setMovieChaptersLoading(true);
    emit chaptersChanged();

    if (m_pendingMovieChapterIds.contains(requestKey)) {
        return;
    }

    m_pendingMovieChapterIds.insert(requestKey);
    m_libraryService->getChapters(movieId);
}

void MovieDetailsViewModel::clearMovieChapters()
{
    const bool hadState = !m_movieChapterId.isEmpty() || !m_chapters.isEmpty() || m_chaptersLoading;
    m_movieChapterId.clear();
    m_chapters.clear();
    m_pendingMovieChapterIds.clear();
    setMovieChaptersLoading(false);
    if (hadState) {
        emit chaptersChanged();
    }
}

QVariantMap MovieDetailsViewModel::getMovieData() const
{
    return m_movieData.toVariantMap();
}

void MovieDetailsViewModel::onMovieDetailsLoaded(const QString &itemId, const QVariantMap &data)
{
    if (itemId != m_movieId) return;
    
    m_loadingMovie = false;
    setLoading(false);
    
    m_movieData = QJsonObject::fromVariantMap(data);
    updateMovieMetadata(data);
    storeMovieCache(itemId, m_movieData);

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
    qCDebug(lcViewModels) << "MovieDetailsViewModel: Movie details not modified" << itemId;

    if (!m_similarItemsAttempted && !m_similarItemsLoading && m_libraryService) {
        m_similarItemsAttempted = true;
        m_similarItemsLoading = true;
        emit similarItemsLoadingChanged();
        m_libraryService->getSimilarItems(itemId);
    }
}

void MovieDetailsViewModel::onSimilarItemsLoaded(const QString &itemId, const QVariantList &items)
{
    if (itemId != m_movieId) {
        return;
    }

    m_similarItems = normalizeSimilarItems(items);
    m_similarItemsAttempted = true;
    m_similarItemsLoading = false;
    storeSimilarItemsCache(itemId, variantListToJsonArray(m_similarItems));
    emit similarItemsChanged();
    emit similarItemsLoadingChanged();
}

void MovieDetailsViewModel::onSimilarItemsFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_movieId) {
        return;
    }

    m_similarItemsAttempted = false;
    if (m_similarItemsLoading) {
        m_similarItemsLoading = false;
        emit similarItemsLoadingChanged();
    }

    qCWarning(lcViewModels) << "MovieDetailsViewModel similar items error:" << error;
}

void MovieDetailsViewModel::onMovieChaptersLoaded(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QVariantList &chapters)
{
    const QString requestKey = movieChapterRequestKey(connectionId, itemId);
    if (!m_pendingMovieChapterIds.contains(requestKey)) {
        return;
    }

    m_pendingMovieChapterIds.remove(requestKey);
    if (connectionId != activeCacheScope()) {
        return;
    }
    QVariantList normalized;
    normalized.reserve(chapters.size());
    for (const QVariant &value : chapters) {
        QVariantMap chapter = value.toMap();
        chapter.insert(QStringLiteral("thumbnailUrl"),
                       cachedArtworkUrl(chapter.value(QStringLiteral("artwork")).toMap(), 480));
        normalized.append(chapter);
    }
    m_movieChapterCache.insert(requestKey, normalized);

    if (itemId == m_movieChapterId) {
        applyMovieChapters(itemId, normalized);
    }
}

void MovieDetailsViewModel::onMovieChaptersFailed(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QString &error)
{
    const QString requestKey = movieChapterRequestKey(connectionId, itemId);
    if (!m_pendingMovieChapterIds.contains(requestKey)) {
        return;
    }
    m_pendingMovieChapterIds.remove(requestKey);

    if (connectionId != activeCacheScope() || itemId != m_movieChapterId) {
        return;
    }

    qCWarning(lcViewModels) << "MovieDetailsViewModel movie chapters error for" << itemId << ":" << error;
    m_chapters.clear();
    setMovieChaptersLoading(false);
    emit chaptersChanged();
}

void MovieDetailsViewModel::onErrorOccurred(const QString &endpoint, const QString &error)
{
    Q_UNUSED(endpoint)
    if (m_loadingMovie) {
        m_loadingMovie = false;
        setLoading(false);
        setError(error);
        emit loadError(error);
    }
}

QString MovieDetailsViewModel::cachedArtworkUrl(const QVariantMap &artwork, int width) const
{
    if (!m_libraryService || artwork.isEmpty()) {
        return {};
    }
    const Bloom::ArtworkRef ref = Bloom::ArtworkRef::fromVariantMap(artwork);
    if (!ref.isValid()) {
        return {};
    }
    return m_libraryService->getCachedArtworkUrlForConnection(ref.connectionId,
                                                               ref.itemId,
                                                               Bloom::artworkKindName(ref.kind),
                                                  ref.index,
                                                  ref.tag,
                                                  width);
}

QVariantList MovieDetailsViewModel::mapCanonicalPeople(const QVariantList &people) const
{
    QVariantList mappedPeople;
    mappedPeople.reserve(qMin(people.size(), qsizetype(18)));

    for (const QVariant &value : people) {
        QVariantMap person = value.toMap();
        const QString name = person.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }

        QString subtitle = person.value(QStringLiteral("role")).toString();
        if (subtitle.isEmpty()) {
            subtitle = person.value(QStringLiteral("kind")).toString();
        }
        person.insert(QStringLiteral("subtitle"), subtitle);
        mappedPeople.append(person);

        if (mappedPeople.size() >= 18) {
            break;
        }
    }

    return mappedPeople;
}

QVariantList MovieDetailsViewModel::normalizeSimilarItems(const QVariantList &items) const
{
    QVariantList mappedItems;
    mappedItems.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString().isEmpty()) {
            continue;
        }
        mappedItems.append(item);
    }
    return mappedItems;
}

void MovieDetailsViewModel::updateMovieMetadata(const QVariantMap &data)
{
    m_title = data.value(QStringLiteral("name")).toString();
    m_overview = data.value(QStringLiteral("overview")).toString();
    m_productionYear = data.value(QStringLiteral("productionYear")).toInt();
    m_officialRating = data.value(QStringLiteral("officialRating")).toString();
    m_durationMs = data.value(QStringLiteral("durationMs")).toLongLong();
    m_communityRating = data.value(QStringLiteral("communityRating")).toDouble();
    m_positionMs = data.value(QStringLiteral("positionMs")).toLongLong();
    m_isWatched = data.value(QStringLiteral("watched")).toBool();
    m_genres = genresFromCanonical(data.value(QStringLiteral("genres")));
    m_people = mapCanonicalPeople(data.value(QStringLiteral("people")).toList());

    const QString premiereDate = data.value(QStringLiteral("premiereDate")).toString();
    if (!premiereDate.isEmpty()) {
        m_premiereDate = QDateTime::fromString(premiereDate, Qt::ISODate);
    } else {
        m_premiereDate = QDateTime();
    }

    m_logoUrl = cachedArtworkUrl(data.value(QStringLiteral("logoArtwork")).toMap(), 2000);
    m_posterUrl = cachedArtworkUrl(data.value(QStringLiteral("primaryArtwork")).toMap(), 400);
    m_backdropUrl = cachedArtworkUrl(data.value(QStringLiteral("backdropArtwork")).toMap(), 1920);
    if (m_backdropUrl.isEmpty()) {
        m_backdropUrl = m_posterUrl;
    }

    emit titleChanged();
    emit overviewChanged();
    emit productionYearChanged();
    emit officialRatingChanged();
    emit durationMsChanged();
    emit communityRatingChanged();
    emit peopleChanged();
    emit genresChanged();
    emit premiereDateChanged();
    emit isWatchedChanged();
    emit positionMsChanged();
    emit logoUrlChanged();
    emit posterUrlChanged();
    emit backdropUrlChanged();
    
    const QVariantMap providerIds = data.value(QStringLiteral("providerIds")).toMap();
    const QString imdbId = providerIds.value(QStringLiteral("Imdb")).toString();
    const QString tmdbId = providerIds.value(QStringLiteral("Tmdb")).toString();
    
    if (!imdbId.isEmpty() || !tmdbId.isEmpty()) {
        fetchMdbListRatings(imdbId, tmdbId, "movie");
    }
    
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
        qCDebug(lcViewModels) << "MDBList ratings updated, count:" << ratingsList.size();
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
    
    if (m_currentAniListImdbId == imdbId) return;
    m_currentAniListImdbId = imdbId;
    m_aniListRating.clear();

    fetchAniListIdFromWikidata(imdbId, [this, title, year](const QString &foundId) {
        Q_UNUSED(year)
        if (!foundId.isEmpty()) {
            qCDebug(lcViewModels) << "Found AniList ID via Wikidata:" << foundId;
            queryAniListById(foundId);
        } else {
            qCWarning(lcViewModels) << "No AniList ID found in Wikidata for" << title;
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

void MovieDetailsViewModel::applyMovieChapters(const QString &movieId,
                                               const QVariantList &chapters)
{
    if (movieId != m_movieChapterId) {
        return;
    }

    m_chapters = chapters;
    setMovieChaptersLoading(false);
    emit chaptersChanged();
}

void MovieDetailsViewModel::setMovieChaptersLoading(bool loading)
{
    if (m_chaptersLoading == loading) {
        return;
    }
    m_chaptersLoading = loading;
    emit chaptersLoadingChanged();
}
