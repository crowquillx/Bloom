#include "MovieDetailsViewModel.h"
#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/ConfigManager.h"
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

struct MovieCacheEntry {
    QJsonObject data;
    qint64 timestamp = 0;
    bool hasData() const { return !data.isEmpty(); }
    bool isValid(qint64 ttl) const {
        return timestamp > 0 && (QDateTime::currentMSecsSinceEpoch() - timestamp) <= ttl;
    }
};

static QHash<QString, MovieCacheEntry> s_movieCache;
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
    return dir.filePath(movieId + "_details.json");
}

bool MovieDetailsViewModel::loadMovieFromCache(const QString &movieId, QJsonObject &movieData, bool requireFresh) const
{
    // Memory cache
    if (s_movieCache.contains(movieId)) {
        const auto &entry = s_movieCache[movieId];
        if (entry.hasData() && (!requireFresh || entry.isValid(kMovieMemoryTtlMs))) {
            movieData = entry.data;
            return true;
        }
    }

    // Disk cache
    QString path = movieCachePath(movieId);
    if (path.isEmpty() || !QFile::exists(path))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    MovieCacheEntry entry;
    entry.timestamp = static_cast<qint64>(doc.object().value("timestamp").toDouble());
    entry.data = doc.object().value("data").toObject();

    if (!entry.hasData())
        return false;

    if (requireFresh && !entry.isValid(kMovieDiskTtlMs))
        return false;

    s_movieCache[movieId] = entry;
    movieData = entry.data;
    return true;
}

void MovieDetailsViewModel::storeMovieCache(const QString &movieId, const QJsonObject &movieData) const
{
    MovieCacheEntry entry;
    entry.data = movieData;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    s_movieCache[movieId] = entry;

    QString path = movieCachePath(movieId);
    if (path.isEmpty())
        return;

    QDir dir(cacheDir());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    root.insert("timestamp", static_cast<double>(entry.timestamp));
    root.insert("data", movieData);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
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
        clear(/*preserveArtwork*/true);
    } else {
        clearError();
    }
    
    m_movieId = movieId;
    emit movieIdChanged();
    
    // Try cache first
    QJsonObject cachedMovie;
    bool hasFresh = loadMovieFromCache(movieId, cachedMovie, /*requireFresh*/true);
    bool hasAny = hasFresh || loadMovieFromCache(movieId, cachedMovie, /*requireFresh*/false);

    if (hasAny) {
        qDebug() << "MovieDetailsViewModel: Serving movie details from cache"
                 << (hasFresh ? "FRESH" : "STALE");
        m_movieData = cachedMovie;
        updateMovieMetadata(cachedMovie);
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
    m_genres.clear();
    m_playbackPositionTicks = 0;
    m_premiereDate = QDateTime();
    
    m_movieData = QJsonObject();

    // Clear ratings data
    m_mdbListRatings.clear();
    m_rawMdbListRatings.clear();
    m_currentAniListImdbId.clear();
    m_aniListRating.clear();
    // AniList ratings are cleared in fetchAniListRating if the ID changes
    // avoiding premature clearing when reloading details for the same movie

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
    emit genresChanged();
    emit premiereDateChanged();
    emit playbackPositionTicksChanged();
    emit mdbListRatingsChanged();
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
    
    emit movieLoaded();
}

void MovieDetailsViewModel::onMovieDetailsNotModified(const QString &itemId)
{
    if (itemId != m_movieId) return;
    
    m_loadingMovie = false;
    setLoading(false);
    qDebug() << "MovieDetailsViewModel: Movie details not modified" << itemId;
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
    m_title = data.value("Name").toString();
    m_overview = data.value("Overview").toString();
    m_productionYear = data.value("ProductionYear").toInt();
    m_officialRating = data.value("OfficialRating").toString();
    m_runtimeTicks = data.value("RunTimeTicks").toVariant().toLongLong();
    m_communityRating = data.value("CommunityRating").toDouble();
    
    if (data.contains("PremiereDate")) {
        m_premiereDate = QDateTime::fromString(data.value("PremiereDate").toString(), Qt::ISODate);
    }
    
    m_genres.clear();
    QJsonArray genresArray = data.value("Genres").toArray();
    for (const auto &g : genresArray) {
        m_genres.append(g.toString());
    }
    
    // UserData
    QJsonObject userData = data.value("UserData").toObject();
    m_isWatched = userData.value("Played").toBool();
    m_playbackPositionTicks = userData.value("PlaybackPositionTicks").toVariant().toLongLong();
    
    // Images - use LibraryService cache helpers
    if (m_libraryService) {
        m_logoUrl = m_libraryService->getCachedImageUrlWithWidth(m_movieId, "Logo", 2000);
        m_posterUrl = m_libraryService->getCachedImageUrlWithWidth(m_movieId, "Primary", 400);
        
        // Try Backdrop, fallback to Primary
        QString backdrop = m_libraryService->getCachedImageUrlWithWidth(m_movieId, "Backdrop", 1920);
        if (backdrop.isEmpty()) {
            backdrop = m_posterUrl;
        }
        m_backdropUrl = backdrop;
    }

    emit titleChanged();
    emit overviewChanged();
    emit productionYearChanged();
    emit officialRatingChanged();
    emit runtimeTicksChanged();
    emit communityRatingChanged();
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
    
    QString apiKey = config->getMdbListApiKey();
    if (apiKey.isEmpty()) return;
    
    if (imdbId.isEmpty() && tmdbId.isEmpty()) {
        qWarning() << "No external IDs found for MDBList lookup";
        return;
    }

    qDebug() << "Fetching MDBList ratings for IMDb:" << imdbId << "TMDB:" << tmdbId;
    
    QUrl url;
    QUrlQuery query;
    query.addQueryItem("apikey", apiKey);
    
    // Script uses: https://api.mdblist.com/tmdb/{type}/{id}
    // We try to match that pattern if we have a TMDB ID.
    if (!tmdbId.isEmpty()) {
        url = QUrl("https://api.mdblist.com/tmdb/" + type + "/" + tmdbId);
    } else if (!imdbId.isEmpty()) {
        // Fallback to IMDb endpoint if supported, or legacy search
        url = QUrl("https://api.mdblist.com/imdb/" + imdbId);
    } else {
        qWarning() << "No IDs for MDBList request";
        return;
    }
    
    url.setQuery(query);
    
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_rawMdbListRatings = doc.object().toVariantMap();
                compileRatings();
                
                // Debug log
                QVariantList ratingsList = m_mdbListRatings.value("ratings").toList();
                qDebug() << "MDBList ratings updated, count:" << ratingsList.size();
            }
        } else {
            qWarning() << "MDBList API error:" << reply->errorString();
        }
    });
}

void MovieDetailsViewModel::fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback)
{
    // SPARQL query to find AniList ID from IMDb ID
    QString sparql = QString(
        "SELECT ?anilistId WHERE { "
        "?item wdt:P345 \"%1\". " // P345 is IMDb ID
        "?item wdt:P8729 ?anilistId. " // P8729 is AniList ID
        "}"
    ).arg(imdbId);

    QUrl url("https://query.wikidata.org/sparql");
    QUrlQuery query;
    query.addQueryItem("query", sparql);
    query.addQueryItem("format", "json");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Bloom/1.0 (Qt 6)");

    auto *reply = m_networkManager->get(request);
    QPointer<MovieDetailsViewModel> self = this;
    connect(reply, &QNetworkReply::finished, this, [self, reply, callback]() {
        reply->deleteLater();
        if (!self) return;

        QString anilistId;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray bindings = doc.object()["results"].toObject()["bindings"].toArray();
            if (!bindings.isEmpty()) {
                anilistId = bindings[0].toObject()["anilistId"].toObject()["value"].toString();
            }
        } else {
            qWarning() << "Wikidata query failed:" << reply->errorString();
        }
        if (callback) callback(anilistId);
    });
}

void MovieDetailsViewModel::queryAniListById(const QString &anilistId)
{
    QUrl url("https://graphql.anilist.co");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Query for mean score and average score
    QString query = QString(
        "query { Media(id: %1, type: ANIME) { averageScore meanScore siteUrl } }"
    ).arg(anilistId);

    QJsonObject json;
    json["query"] = query;

    auto *reply = m_networkManager->post(request, QJsonDocument(json).toJson());
    QPointer<MovieDetailsViewModel> self = this;
    connect(reply, &QNetworkReply::finished, this, [self, reply]() {
        reply->deleteLater();
        if (!self) return;

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "AniList API error:" << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject media = doc.object()["data"].toObject()["Media"].toObject();
        
        int avgScore = media["averageScore"].toInt();
        int meanScore = media["meanScore"].toInt();
        int score = (avgScore > 0) ? avgScore : meanScore;
        
        if (score > 0) {
            QVariantMap aniRating;
            aniRating["source"] = "AniList";
            aniRating["value"] = score;
            aniRating["score"] = score;
            aniRating["url"] = media["siteUrl"].toString();
            
            self->m_aniListRating = aniRating;
            self->compileRatings();
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
    // Start with raw MDBList data
    QVariantMap combined = m_rawMdbListRatings;
    QVariantList ratingsList = combined.value("ratings").toList();
    
    // Append AniList if valid
    if (!m_aniListRating.isEmpty()) {
        bool found = false;
        for (int i = 0; i < ratingsList.size(); ++i) {
            QVariantMap r = ratingsList[i].toMap();
            if (r["source"].toString().compare("AniList", Qt::CaseInsensitive) == 0) {
                found = true;
                
                // Merge strategy: Keep the one with the higher score
                int existingScore = r["score"].toInt();
                if (existingScore <= 0) existingScore = r["value"].toInt();
                
                int newScore = m_aniListRating["score"].toInt();
                if (newScore <= 0) newScore = m_aniListRating["value"].toInt();
                
                // If ours is better, replace it
                if (newScore > existingScore) {
                     ratingsList[i] = m_aniListRating;
                }
                break;
            }
        }
        
        if (!found) {
            ratingsList.append(m_aniListRating);
        }
    }
    
    combined["ratings"] = ratingsList;
    
    if (m_mdbListRatings != combined) {
        m_mdbListRatings = combined;
        emit mdbListRatingsChanged();
    }
}
