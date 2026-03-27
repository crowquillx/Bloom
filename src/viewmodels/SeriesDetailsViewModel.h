#pragma once

#include "BaseViewModel.h"
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QNetworkAccessManager>
#include <functional>

class LibraryService;

/**
 * @brief Model for displaying seasons in a series.
 *
 * Provides a QAbstractListModel interface for efficient list rendering
 * of seasons within a series.
 */
class SeasonsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ImageUrlRole,
        IdRole,
        IndexNumberRole,
        EpisodeCountRole,
        UnplayedItemCountRole,
        IsPlayedRole,
        ModelDataRole
    };
    Q_ENUM(Roles)

    explicit SeasonsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSeasons(const QJsonArray &seasons);
    void setLibraryService(LibraryService *libraryService) { m_libraryService = libraryService; }
    void clear();

    Q_INVOKABLE QVariantMap getItem(int index) const;

private:
    friend class SeriesDetailsCacheTest;
    QString getImageUrl(const QJsonObject &item) const;

    LibraryService *m_libraryService = nullptr;
    QVector<QJsonObject> m_items;
};


/**
 * @brief Model for displaying episodes in a season.
 *
 * Provides a QAbstractListModel interface for efficient list rendering
 * of episodes within a season.
 */
class EpisodesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ImageUrlRole,
        IdRole,
        IndexNumberRole,
        ParentIndexNumberRole,
        OverviewRole,
        RuntimeTicksRole,
        IsPlayedRole,
        PlaybackPositionTicksRole,
        CommunityRatingRole,
        PremiereDateRole,
        IsFavoriteRole,
        // Special episode placement fields
        IsSpecialRole,
        AirsBeforeSeasonRole,
        AirsAfterSeasonRole,
        AirsBeforeEpisodeRole,
        ModelDataRole
    };
    Q_ENUM(Roles)

    explicit EpisodesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEpisodes(const QJsonArray &episodes);
    void setLibraryService(LibraryService *libraryService) { m_libraryService = libraryService; }
    void clear();

    Q_INVOKABLE QVariantMap getItem(int index) const;

private:
    QString getImageUrl(const QJsonObject &item) const;

    LibraryService *m_libraryService = nullptr;
    QVector<QJsonObject> m_items;
};


/**
 * @brief ViewModel for series details display in SeriesDetailsView.
 *
 * This class provides series metadata (title, overview, logo, etc.) and
 * separate models for seasons and episodes. It handles the nested data
 * hierarchy: Series -> Seasons -> Episodes.
 *
 * ## Usage in QML
 * ```qml
 * SeriesDetailsView {
 *     seriesTitle: SeriesDetailsViewModel.title
 *     seriesOverview: SeriesDetailsViewModel.overview
 *     
 *     GridView {
 *         model: SeriesDetailsViewModel.seasonsModel
 *     }
 *     
 *     ListView {
 *         model: SeriesDetailsViewModel.episodesModel
 *     }
 * }
 * ```
 */
class SeriesDetailsViewModel : public BaseViewModel
{
    Q_OBJECT

    friend class SimilarItemsRetryTest;

    // Series metadata properties
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY seriesIdChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString overview READ overview NOTIFY overviewChanged)
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY logoUrlChanged)
    Q_PROPERTY(QString posterUrl READ posterUrl NOTIFY posterUrlChanged)
    Q_PROPERTY(QString backdropUrl READ backdropUrl NOTIFY backdropUrlChanged)
    Q_PROPERTY(int productionYear READ productionYear NOTIFY productionYearChanged)
    Q_PROPERTY(bool isWatched READ isWatched NOTIFY isWatchedChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite NOTIFY isFavoriteChanged)
    Q_PROPERTY(QString tmdbId READ tmdbId NOTIFY tmdbIdChanged)
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY genresChanged)
    Q_PROPERTY(QVariantList similarItems READ similarItems NOTIFY similarItemsChanged)
    Q_PROPERTY(bool similarItemsLoading READ similarItemsLoading NOTIFY similarItemsLoadingChanged)

    Q_PROPERTY(int seasonCount READ seasonCount NOTIFY seasonCountChanged)
    Q_PROPERTY(QString officialRating READ officialRating NOTIFY officialRatingChanged)
    Q_PROPERTY(int recursiveItemCount READ recursiveItemCount NOTIFY recursiveItemCountChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QDateTime endDate READ endDate NOTIFY endDateChanged)
    
    // MDBList Ratings
    Q_PROPERTY(QVariantMap mdbListRatings READ mdbListRatings NOTIFY mdbListRatingsChanged)

    // Next episode properties
    Q_PROPERTY(QString nextEpisodeId READ nextEpisodeId NOTIFY nextEpisodeChanged)
    Q_PROPERTY(QString nextEpisodeName READ nextEpisodeName NOTIFY nextEpisodeChanged)
    Q_PROPERTY(int nextEpisodeNumber READ nextEpisodeNumber NOTIFY nextEpisodeChanged)
    Q_PROPERTY(int nextSeasonNumber READ nextSeasonNumber NOTIFY nextEpisodeChanged)
    Q_PROPERTY(QString nextEpisodeImageUrl READ nextEpisodeImageUrl NOTIFY nextEpisodeChanged)
    Q_PROPERTY(bool hasNextEpisode READ hasNextEpisode NOTIFY nextEpisodeChanged)
    Q_PROPERTY(qint64 nextEpisodePlaybackPositionTicks READ nextEpisodePlaybackPositionTicks NOTIFY nextEpisodeChanged)

    // Focused episode details (for episode-centric season view hero/cast updates)
    Q_PROPERTY(QVariantMap focusedEpisodeDetails READ focusedEpisodeDetails NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(QVariantList focusedEpisodePeople READ focusedEpisodePeople NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(QString focusedEpisodeDetailId READ focusedEpisodeDetailId NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(bool focusedEpisodeDetailsLoading READ focusedEpisodeDetailsLoading NOTIFY focusedEpisodeDetailsLoadingChanged)

    // Models
    Q_PROPERTY(SeasonsModel* seasonsModel READ seasonsModel CONSTANT)
    Q_PROPERTY(EpisodesModel* episodesModel READ episodesModel CONSTANT)

    // Currently selected season
    Q_PROPERTY(int selectedSeasonIndex READ selectedSeasonIndex WRITE setSelectedSeasonIndex NOTIFY selectedSeasonIndexChanged)
    Q_PROPERTY(QString selectedSeasonId READ selectedSeasonId NOTIFY selectedSeasonIdChanged)
    Q_PROPERTY(QString selectedSeasonName READ selectedSeasonName NOTIFY selectedSeasonIdChanged)

public:
    explicit SeriesDetailsViewModel(QObject *parent = nullptr);
    ~SeriesDetailsViewModel() override = default;

    // Property accessors - Series metadata
    QString seriesId() const { return m_seriesId; }
    QString title() const { return m_title; }
    QString overview() const { return m_overview; }
    QString logoUrl() const { return m_logoUrl; }
    QString posterUrl() const { return m_posterUrl; }
    QString backdropUrl() const { return m_backdropUrl; }
    int productionYear() const { return m_productionYear; }
    bool isWatched() const { return m_isWatched; }
    bool isFavorite() const { return m_isFavorite; }
    QString tmdbId() const { return m_tmdbId; }
    QVariantList people() const { return m_people; }
    QStringList genres() const { return m_genres; }
    QVariantList similarItems() const { return m_similarItems; }
    bool similarItemsLoading() const { return m_similarItemsLoading; }

    int seasonCount() const { return m_seasonCount; }
    QString officialRating() const { return m_officialRating; }
    int recursiveItemCount() const { return m_recursiveItemCount; }
    QString status() const { return m_status; }
    QDateTime endDate() const { return m_endDate; }
    
    // Property accessors - MDBList
    QVariantMap mdbListRatings() const { return m_mdbListRatings; }

    // Property accessors - Next episode
    QString nextEpisodeId() const { return m_nextEpisodeId; }
    QString nextEpisodeName() const { return m_nextEpisodeName; }
    int nextEpisodeNumber() const { return m_nextEpisodeNumber; }
    int nextSeasonNumber() const { return m_nextSeasonNumber; }
    QString nextEpisodeImageUrl() const { return m_nextEpisodeImageUrl; }
    bool hasNextEpisode() const { return !m_nextEpisodeId.isEmpty(); }
    qint64 nextEpisodePlaybackPositionTicks() const;
    QVariantMap focusedEpisodeDetails() const { return m_focusedEpisodeDetails.toVariantMap(); }
    QVariantList focusedEpisodePeople() const { return m_focusedEpisodePeople; }
    QString focusedEpisodeDetailId() const { return m_focusedEpisodeDetailId; }
    bool focusedEpisodeDetailsLoading() const { return m_focusedEpisodeDetailsLoading; }

    // Property accessors - State
    // Property accessors - Models
    SeasonsModel* seasonsModel() { return &m_seasonsModel; }
    EpisodesModel* episodesModel() { return &m_episodesModel; }

    // Property accessors - Selected season
    int selectedSeasonIndex() const { return m_selectedSeasonIndex; }
    void setSelectedSeasonIndex(int index);
    QString selectedSeasonId() const { return m_selectedSeasonId; }
    QString selectedSeasonName() const { return m_selectedSeasonName; }

    /**
     * @brief Load series details and seasons for a given series ID.
     * @param seriesId The Jellyfin series ID
     */
    Q_INVOKABLE void loadSeriesDetails(const QString &seriesId);

    /**
     * @brief Reload hook for QML retry flows.
     */
    Q_INVOKABLE void reload() override;

    /**
     * @brief Load episodes for a specific season.
     * @param seasonId The Jellyfin season ID
     */
    Q_INVOKABLE void loadSeasonEpisodes(const QString &seasonId);

    /**
     * @brief Force a network refresh for season episodes, bypassing local cache.
     * @param seasonId The Jellyfin season ID
     */
    Q_INVOKABLE void refreshSeasonEpisodes(const QString &seasonId);

    /**
     * @brief Select a season by index and load its episodes.
     * @param index The season index in the seasons model
     */
    Q_INVOKABLE void selectSeason(int index);

    /**
     * @brief Prefetch episodes for adjacent seasons to improve navigation speed.
     * @param startIndex The season index that currently has focus
     * @param radius How many seasons ahead to prefetch (default 2)
     */
    Q_INVOKABLE void prefetchSeasonsAround(int startIndex, int radius = 2);

    /**
     * @brief Mark the series as watched.
     */
    Q_INVOKABLE void markAsWatched();

    /**
     * @brief Mark the series as unwatched.
     */
    Q_INVOKABLE void markAsUnwatched();
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE void loadFocusedEpisodeDetails(const QString &episodeId);
    Q_INVOKABLE void clearFocusedEpisodeDetails();

    /**
     * @brief Clear all data and reset state.
     * @param preserveArtwork If true, keep existing logo/poster/backdrop URLs until new data arrives to avoid UI flashes.
     */
    Q_INVOKABLE void clear(bool preserveArtwork = false);

    /**
     * @brief Get full series data as a QVariantMap.
     */
    Q_INVOKABLE QVariantMap getSeriesData() const;

    /**
     * @brief Get next episode data as a QVariantMap.
     */
    Q_INVOKABLE QVariantMap getNextEpisodeData() const;

    // Cache helpers (non-QML)
    bool loadSeriesFromCache(const QString &seriesId, QJsonObject &seriesData, bool requireFresh) const;
    void storeSeriesCache(const QString &seriesId, const QJsonObject &seriesData) const;
    bool loadItemsFromCache(const QString &parentId, QJsonArray &items, bool requireFresh) const;
    void storeItemsCache(const QString &parentId, const QJsonArray &items) const;
    void clearItemsCache(const QString &parentId) const;
    bool loadSimilarItemsFromCache(const QString &seriesId, QJsonArray &items, bool requireFresh) const;
    void storeSimilarItemsCache(const QString &seriesId, const QJsonArray &items) const;
    QString cacheDir() const;
    QString seriesCachePath(const QString &seriesId) const;
    QString itemsCachePath(const QString &parentId) const;
    QString similarItemsCachePath(const QString &seriesId) const;
    void clearCacheForTest(const QString &id);
    
    // MDBList
    void fetchMdbListRatings(const QString &imdbId, const QString &tmdbId, const QString &type = "show");
    
    // AniList
    void fetchAniListRating(const QString &imdbId, const QString &title, int year);
    void fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback);
    void queryAniListById(const QString &anilistId, const QString &requestImdbId);

signals:
    // Series metadata signals
    void seriesIdChanged();
    void titleChanged();
    void overviewChanged();
    void logoUrlChanged();
    void posterUrlChanged();
    void backdropUrlChanged();
    void productionYearChanged();
    void isWatchedChanged();
    void isFavoriteChanged();
    void tmdbIdChanged();
    void peopleChanged();
    void genresChanged();
    void similarItemsChanged();
    void similarItemsLoadingChanged();

    void seasonCountChanged();
    void officialRatingChanged();
    void recursiveItemCountChanged();
    void statusChanged();
    void endDateChanged();
    void mdbListRatingsChanged();

    // Next episode signals
    void nextEpisodeChanged();
    void focusedEpisodeDetailsChanged();
    void focusedEpisodeDetailsLoadingChanged();

    // Selected season signals
    void selectedSeasonIndexChanged();
    void selectedSeasonIdChanged();

    // Load completion signals
    void seriesLoaded();
    void seasonsLoaded();
    void episodesLoaded();
    void loadError(const QString &error);

private slots:
    void onSeriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData);
    void onSeriesDetailsNotModified(const QString &seriesId);
    void onSeasonsLoaded(const QString &parentId, const QJsonArray &items);
    void onEpisodesLoaded(const QString &parentId, const QJsonArray &items);
    void onItemsNotModified(const QString &parentId);
    void onNextEpisodeLoaded(const QString &seriesId,
                             const QJsonObject &episodeData,
                             const QString &requestContext);
    void onNextEpisodeFailed(const QString &seriesId,
                             const QString &error,
                             const QString &requestContext);
    void onSeriesWatchedStatusChanged(const QString &seriesId);
    void onFavoriteStatusChanged(const QString &itemId, bool isFavorite);
    void onSimilarItemsLoaded(const QString &itemId, const QJsonArray &items);
    void onSimilarItemsFailed(const QString &itemId, const QString &error);
    void onErrorOccurred(const QString &endpoint, const QString &error);
    void onEpisodeDetailsLoaded(const QString &itemId, const QJsonObject &data, const QString &requestContext);
    void onEpisodeDetailsNotModified(const QString &itemId, const QString &requestContext);
    void onEpisodeDetailsFailed(const QString &itemId, const QString &error, const QString &requestContext);

private:
    void updateSeriesMetadata(const QJsonObject &data);
    void updateNextEpisode(const QJsonObject &episodeData);
    QString buildImageUrl(const QString &itemId, const QString &imageType, int width = 400) const;
    void applyFocusedEpisodeDetails(const QString &episodeId, const QJsonObject &data);
    QString startEpisodeDetailsRequest(const QString &episodeId);
    bool matchesEpisodeDetailsRequest(const QString &itemId, const QString &requestContext) const;
    void finishEpisodeDetailsRequest(const QString &itemId);
    void stopFocusedEpisodeDetailsLoadingFor(const QString &itemId);
    void setFocusedEpisodeDetailsLoading(bool loading);

private:
    LibraryService *m_libraryService = nullptr;

    // Series metadata
    QString m_seriesId;
    QString m_title;
    QString m_overview;
    QString m_logoUrl;
    QString m_posterUrl;
    QString m_backdropUrl;
    int m_productionYear = 0;
    bool m_isWatched = false;
    bool m_isFavorite = false;
    QString m_imdbId;
    QString m_tmdbId;
    QVariantList m_people;
    QStringList m_genres;
    QVariantList m_similarItems;
    bool m_similarItemsAttempted = false;
    bool m_similarItemsLoading = false;

    int m_seasonCount = 0;
    QString m_officialRating;
    int m_recursiveItemCount = 0;
    QString m_status;
    QDateTime m_endDate;
    QJsonObject m_seriesData;
    
    // MDBList
    QVariantMap m_mdbListRatings;
    QVariantMap m_rawMdbListRatings;
    QVariantMap m_aniListRating;
    QString m_currentAniListImdbId;

    // ... helper
    void compileRatings();
    QNetworkAccessManager *m_networkManager = nullptr;

    // Next episode
    QString m_nextEpisodeId;
    QString m_nextEpisodeName;
    int m_nextEpisodeNumber = 0;
    int m_nextSeasonNumber = 0;
    QString m_nextEpisodeImageUrl;
    QJsonObject m_nextEpisodeData;
    QString m_focusedEpisodeDetailId;
    QJsonObject m_focusedEpisodeDetails;
    QVariantList m_focusedEpisodePeople;
    bool m_focusedEpisodeDetailsLoading = false;
    QHash<QString, QJsonObject> m_episodeDetailsCache;
    QSet<QString> m_pendingEpisodeDetailIds;
    QHash<QString, QString> m_episodeDetailRequestTokens;
    QSet<QString> m_episodeDetailRetried;
    quint64 m_episodeDetailRequestSequence = 0;

    // State
    bool m_loadingSeries = false;
    bool m_loadingSeasons = false;
    bool m_loadingEpisodes = false;

    // Models
    SeasonsModel m_seasonsModel;
    EpisodesModel m_episodesModel;

    // Season selection
    int m_selectedSeasonIndex = -1;
    QString m_selectedSeasonId;
    QString m_selectedSeasonName;
    QVector<QJsonObject> m_seasons;

    // Prefetch tracking
    QSet<QString> m_prefetchSeasonIds;

    // Timing
    QElapsedTimer m_seriesTimer;
    QElapsedTimer m_seasonsTimer;
    QElapsedTimer m_episodesTimer;
};
