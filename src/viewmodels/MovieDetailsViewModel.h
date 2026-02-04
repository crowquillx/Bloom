#pragma once

#include "BaseViewModel.h"
#include <QJsonObject>
#include <QString>
#include <QList>
#include <QNetworkAccessManager>
#include <functional>

class LibraryService;

/**
 * @brief ViewModel for movie details display in MovieDetailsView.
 *
 * This class provides movie metadata (title, overview, logo, etc.) and
 * handles fetching external ratings from MDBList and AniList.
 */
class MovieDetailsViewModel : public BaseViewModel
{
    Q_OBJECT

    // Movie metadata properties
    Q_PROPERTY(QString movieId READ movieId NOTIFY movieIdChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString overview READ overview NOTIFY overviewChanged)
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY logoUrlChanged)
    Q_PROPERTY(QString posterUrl READ posterUrl NOTIFY posterUrlChanged)
    Q_PROPERTY(QString backdropUrl READ backdropUrl NOTIFY backdropUrlChanged)
    Q_PROPERTY(int productionYear READ productionYear NOTIFY productionYearChanged)
    Q_PROPERTY(bool isWatched READ isWatched NOTIFY isWatchedChanged)
    
    Q_PROPERTY(QString officialRating READ officialRating NOTIFY officialRatingChanged)
    Q_PROPERTY(qint64 runtimeTicks READ runtimeTicks NOTIFY runtimeTicksChanged)
    Q_PROPERTY(double communityRating READ communityRating NOTIFY communityRatingChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY genresChanged)
    Q_PROPERTY(QDateTime premiereDate READ premiereDate NOTIFY premiereDateChanged)
    Q_PROPERTY(qint64 playbackPositionTicks READ playbackPositionTicks NOTIFY playbackPositionTicksChanged)

    // MDBList Ratings
    Q_PROPERTY(QVariantMap mdbListRatings READ mdbListRatings NOTIFY mdbListRatingsChanged)

public:
    explicit MovieDetailsViewModel(QObject *parent = nullptr);
    ~MovieDetailsViewModel() override = default;

    // Property accessors
    QString movieId() const { return m_movieId; }
    QString title() const { return m_title; }
    QString overview() const { return m_overview; }
    QString logoUrl() const { return m_logoUrl; }
    QString posterUrl() const { return m_posterUrl; }
    QString backdropUrl() const { return m_backdropUrl; }
    int productionYear() const { return m_productionYear; }
    bool isWatched() const { return m_isWatched; }
    
    QString officialRating() const { return m_officialRating; }
    qint64 runtimeTicks() const { return m_runtimeTicks; }
    double communityRating() const { return m_communityRating; }
    QStringList genres() const { return m_genres; }
    QDateTime premiereDate() const { return m_premiereDate; }
    qint64 playbackPositionTicks() const { return m_playbackPositionTicks; }

    // Property accessors - MDBList
    QVariantMap mdbListRatings() const { return m_mdbListRatings; }

    /**
     * @brief Load movie details for a given movie ID.
     * @param movieId The Jellyfin item ID
     */
    Q_INVOKABLE void loadMovieDetails(const QString &movieId);

    /**
     * @brief Reload hook for QML retry flows.
     */
    Q_INVOKABLE void reload() override;

    /**
     * @brief Mark the movie as watched.
     */
    Q_INVOKABLE void markAsWatched();

    /**
     * @brief Mark the movie as unwatched.
     */
    Q_INVOKABLE void markAsUnwatched();

    /**
     * @brief Clear all data and reset state.
     * @param preserveArtwork If true, keep existing logo/poster/backdrop URLs until new data arrives.
     */
    Q_INVOKABLE void clear(bool preserveArtwork = false);

    /**
     * @brief Get full movie data as a QVariantMap.
     */
    Q_INVOKABLE QVariantMap getMovieData() const;

    // Cache helpers
    QString cacheDir() const;
    QString movieCachePath(const QString &movieId) const;
    bool loadMovieFromCache(const QString &movieId, QJsonObject &movieData, bool requireFresh) const;
    void storeMovieCache(const QString &movieId, const QJsonObject &movieData) const;

    // MDBList
    void fetchMdbListRatings(const QString &imdbId, const QString &tmdbId, const QString &type = "movie");
    
    // AniList
    void fetchAniListRating(const QString &imdbId, const QString &title, int year);
    void fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback);
    void queryAniListById(const QString &anilistId);

signals:
    void movieIdChanged();
    void titleChanged();
    void overviewChanged();
    void logoUrlChanged();
    void posterUrlChanged();
    void backdropUrlChanged();
    void productionYearChanged();
    void isWatchedChanged();
    
    void officialRatingChanged();
    void runtimeTicksChanged();
    void communityRatingChanged();
    void genresChanged();
    void premiereDateChanged();
    void playbackPositionTicksChanged();
    void mdbListRatingsChanged();

    void movieLoaded();
    void loadError(const QString &error);

private slots:
    void onMovieDetailsLoaded(const QString &itemId, const QJsonObject &data);
    void onMovieDetailsNotModified(const QString &itemId);
    void onErrorOccurred(const QString &endpoint, const QString &error);

private:
    void updateMovieMetadata(const QJsonObject &data);
    void compileRatings();

    LibraryService *m_libraryService = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;

    // Movie metadata
    QString m_movieId;
    QString m_title;
    QString m_overview;
    QString m_logoUrl;
    QString m_posterUrl;
    QString m_backdropUrl;
    int m_productionYear = 0;
    bool m_isWatched = false;
    
    QString m_officialRating;
    qint64 m_runtimeTicks = 0;
    double m_communityRating = 0.0;
    QStringList m_genres;
    QDateTime m_premiereDate;
    qint64 m_playbackPositionTicks = 0;

    QJsonObject m_movieData;
    
    // MDBList
    QVariantMap m_mdbListRatings;
    QVariantMap m_rawMdbListRatings;
    QVariantMap m_aniListRating;
    QString m_currentAniListImdbId;
    
    // State
    bool m_loadingMovie = false;
};
