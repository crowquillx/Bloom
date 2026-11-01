#pragma once

#include "BaseViewModel.h"
#include "ItemExtrasState.h"
#include "UserRatingState.h"
#include "network/Types.h"
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
class QTimer;

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

    void setSeasons(const QVariantList &seasons);
    void setLibraryService(LibraryService *libraryService) { m_libraryService = libraryService; }
    void clear();

    Q_INVOKABLE QVariantMap getItem(int index) const;

private:
    friend class SeriesDetailsCacheTest;
    QString getImageUrl(const QVariantMap &item) const;

    LibraryService *m_libraryService = nullptr;
    QVector<QVariantMap> m_items;
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
        DurationMsRole,
        IsPlayedRole,
        PositionMsRole,
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

    void setEpisodes(const QVariantList &episodes);
    void setLibraryService(LibraryService *libraryService) { m_libraryService = libraryService; }
    void clear();

    Q_INVOKABLE QVariantMap getItem(int index) const;

private:
    QString getImageUrl(const QVariantMap &item) const;

    LibraryService *m_libraryService = nullptr;
    QVector<QVariantMap> m_items;
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

    friend class SeriesDetailsCacheTest;
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
    // Watchlist membership; the action shows only where the provider has a
    // watchlist (Silo).
    Q_PROPERTY(bool watchlistSupported READ watchlistSupported NOTIFY watchlistSupportedChanged)
    Q_PROPERTY(bool inWatchlist READ inWatchlist NOTIFY inWatchlistChanged)
    // "Not interested" for the profile's recommendations; the action shows
    // only where the provider offers it to the profile (Silo).
    Q_PROPERTY(bool notInterestedSupported READ notInterestedSupported NOTIFY notInterestedSupportedChanged)
    Q_PROPERTY(bool notInterested READ notInterested NOTIFY notInterestedChanged)
    // The viewer's own rating of the series (stars or likes, per provider).
    Q_PROPERTY(UserRatingState *userRating READ userRating CONSTANT)
    // The next episode to air (canonical airing map) and its hero chip
    // text, e.g. "S2 E5 airs Thu, Nov 6"; empty when none is known.
    Q_PROPERTY(QVariantMap nextAiring READ nextAiring NOTIFY nextAiringChanged)
    Q_PROPERTY(QString nextAiringLabel READ nextAiringLabel NOTIFY nextAiringChanged)
    // True while the provider's calendar is being asked or once the page
    // has an airing, so the hero keeps the chip's slot instead of moving
    // when the answer arrives. Providers without a calendar never reserve
    // it, and an empty or failed answer releases it.
    Q_PROPERTY(bool expectsNextAiring READ expectsNextAiring NOTIFY nextAiringChanged)
    Q_PROPERTY(QString tmdbId READ tmdbId NOTIFY tmdbIdChanged)
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY genresChanged)
    Q_PROPERTY(QVariantList similarItems READ similarItems NOTIFY similarItemsChanged)
    Q_PROPERTY(bool similarItemsLoading READ similarItemsLoading NOTIFY similarItemsLoadingChanged)
    // Canonical local extras (models/MediaExtras.h) and the one Play Trailer
    // starts (empty when the series has no local trailer).
    Q_PROPERTY(QVariantList extras READ extras NOTIFY extrasChanged)
    Q_PROPERTY(QVariantMap trailer READ trailer NOTIFY extrasChanged)
    // True while a requested extras answer is outstanding, so a return
    // state that targets the Extras shelf can wait for it.
    Q_PROPERTY(bool extrasLoading READ extrasLoading NOTIFY extrasLoadingChanged)

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
    Q_PROPERTY(qint64 nextEpisodePositionMs READ nextEpisodePositionMs NOTIFY nextEpisodeChanged)

    // Focused episode details (for episode-centric season view hero/cast updates)
    Q_PROPERTY(QVariantMap focusedEpisodeDetails READ focusedEpisodeDetails NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(QVariantList focusedEpisodePeople READ focusedEpisodePeople NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(QString focusedEpisodeDetailId READ focusedEpisodeDetailId NOTIFY focusedEpisodeDetailsChanged)
    Q_PROPERTY(bool focusedEpisodeDetailsLoading READ focusedEpisodeDetailsLoading NOTIFY focusedEpisodeDetailsLoadingChanged)
    Q_PROPERTY(QVariantList focusedEpisodeChapters READ focusedEpisodeChapters NOTIFY focusedEpisodeChaptersChanged)
    Q_PROPERTY(bool focusedEpisodeChaptersLoading READ focusedEpisodeChaptersLoading NOTIFY focusedEpisodeChaptersLoadingChanged)

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
    bool watchlistSupported() const;
    bool inWatchlist() const { return m_inWatchlist; }
    bool notInterestedSupported() const;
    bool notInterested() const { return m_notInterested; }
    UserRatingState *userRating() const { return m_userRating; }
    QVariantMap nextAiring() const { return m_nextAiring; }
    QString nextAiringLabel() const;
    bool expectsNextAiring() const { return m_nextAiringPending || !m_nextAiring.isEmpty(); }
    // "S2 E5 airs Thu, Nov 6" in the system locale: "today" or "tomorrow"
    // (with the local time when the airing has an instant) for the next
    // two days. Empty for an empty airing.
    static QString nextAiringLabel(const QVariantMap &airing, const QDateTime &now);
    QString tmdbId() const { return m_tmdbId; }
    QVariantList people() const { return m_people; }
    QStringList genres() const { return m_genres; }
    QVariantList similarItems() const { return m_similarItems; }
    bool similarItemsLoading() const { return m_similarItemsLoading; }
    QVariantList extras() const { return m_extras.extras(); }
    QVariantMap trailer() const { return m_extras.trailer(); }
    bool extrasLoading() const { return m_extras.loading(); }

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
    qint64 nextEpisodePositionMs() const;
    QVariantMap focusedEpisodeDetails() const { return m_focusedEpisodeDetails; }
    QVariantList focusedEpisodePeople() const { return m_focusedEpisodePeople; }
    QString focusedEpisodeDetailId() const { return m_focusedEpisodeDetailId; }
    bool focusedEpisodeDetailsLoading() const { return m_focusedEpisodeDetailsLoading; }
    QVariantList focusedEpisodeChapters() const { return m_focusedEpisodeChapters; }
    bool focusedEpisodeChaptersLoading() const { return m_focusedEpisodeChaptersLoading; }

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
    /**
     * @brief Add the series to the watchlist, or remove it.
     *
     * Updates `inWatchlist` at once and restores it if the server refuses
     * (`watchlistUpdateFailed`). Ignored while a change is in flight.
     */
    Q_INVOKABLE void toggleWatchlist();
    /**
     * @brief Mark the series "Not interested", or take the mark back.
     *
     * Updates `notInterested` at once and restores it if the server refuses
     * (`notInterestedUpdateFailed`). Ignored while a change is in flight.
     */
    Q_INVOKABLE void toggleNotInterested();
    Q_INVOKABLE void loadFocusedEpisodeDetails(const QString &episodeId);
    Q_INVOKABLE void clearFocusedEpisodeDetails();
    Q_INVOKABLE void loadFocusedEpisodeChapters(const QString &episodeId);
    Q_INVOKABLE void clearFocusedEpisodeChapters();

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
    void clearMemoryCacheForTest(const QString &id);
    
    // MDBList
    void fetchMdbListRatings(const QString &imdbId, const QString &tmdbId, const QString &type = "show");
    
    // AniList
    void fetchAniListRating(const QString &imdbId, const QString &title, int year);
    void fetchAniListIdFromWikidata(const QString &imdbId, std::function<void(const QString&)> callback);
    void queryAniListById(const QString &anilistId, const QString &requestImdbId);

signals:
    // Series metadata signals
    void seriesIdChanged();
    void seriesDataChanged();
    void titleChanged();
    void overviewChanged();
    void logoUrlChanged();
    void posterUrlChanged();
    void backdropUrlChanged();
    void productionYearChanged();
    void isWatchedChanged();
    void isFavoriteChanged();
    void watchlistSupportedChanged();
    void inWatchlistChanged();
    void watchlistUpdateFailed(const QString &message);
    void notInterestedSupportedChanged();
    void notInterestedChanged();
    void notInterestedUpdateFailed(const QString &message);
    void nextAiringChanged();
    void tmdbIdChanged();
    void peopleChanged();
    void genresChanged();
    void similarItemsChanged();
    void similarItemsLoadingChanged();
    void extrasChanged();
    void extrasLoadingChanged();

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
    void focusedEpisodeChaptersChanged();
    void focusedEpisodeChaptersLoadingChanged();

    // Selected season signals
    void selectedSeasonIndexChanged();
    void selectedSeasonIdChanged();

    // Load completion signals
    void seriesLoaded();
    void seasonsLoaded();
    void episodesLoaded();
    void loadError(const QString &error);

private slots:
    void onSeriesDetailsLoaded(const QString &connectionId,
                               const QString &seriesId,
                               const QVariantMap &seriesData);
    void onSeriesDetailsNotModified(const QString &connectionId, const QString &seriesId);
    void onItemsLoaded(const QString &connectionId,
                       const QString &parentId,
                       const QString &queryKey,
                       const QVariantList &items,
                       int totalRecordCount);
    void onSeasonsLoaded(const QString &parentId, const QVariantList &items);
    void onEpisodesLoaded(const QString &parentId, const QVariantList &items);
    void onItemsNotModified(const QString &connectionId,
                            const QString &parentId,
                            const QString &queryKey);
    void onNextEpisodeLoaded(const QString &connectionId,
                             const QString &seriesId,
                             const QVariantMap &episodeData,
                             const QString &requestContext);
    void onNextEpisodeFailed(const QString &seriesId,
                             const QString &error,
                             const QString &requestContext);
    void onSeriesWatchedStatusChanged(const QString &seriesId);
    void onFavoriteStatusChanged(const QString &itemId, bool isFavorite);
    void onWatchlistStatusChanged(const QString &itemId, bool inWatchlist);
    void onWatchlistStatusFailed(const QString &itemId, bool inWatchlist, const QString &error);
    void onExtrasLoaded(const QString &connectionId,
                        const QString &itemId,
                        const QVariantList &extras,
                        bool complete);
    void onExtrasFailed(const QString &connectionId,
                        const QString &itemId,
                        const QString &error);
    void onNotInterestedStatusChanged(const QString &itemId, bool notInterested);
    void onNotInterestedStatusFailed(const QString &itemId, bool notInterested, const QString &error);
    void onNextAiringLoaded(const QString &seriesId, const QVariantMap &airing);
    void onSimilarItemsLoaded(const QString &connectionId,
                              const QString &itemId,
                              const QVariantList &items);
    void onSimilarItemsFailed(const QString &connectionId,
                              const QString &itemId,
                              const QString &error);
    void onErrorOccurred(const QString &endpoint, const QString &error);
    void onEpisodeDetailsLoaded(const QString &itemId, const QVariantMap &data, const QString &requestContext);
    void onEpisodeDetailsNotModified(const QString &itemId, const QString &requestContext);
    void onEpisodeDetailsFailed(const QString &itemId, const QString &error, const QString &requestContext);
    void onFocusedEpisodeChaptersLoaded(const QString &connectionId,
                                        const QString &itemId,
                                        const QVariantList &chapters);
    void onFocusedEpisodeChaptersFailed(const QString &connectionId,
                                        const QString &itemId,
                                        const QString &error);

private:
    void updateSeriesMetadata(const QVariantMap &data);
    void updateNextEpisode(const QVariantMap &episodeData);
    QString buildArtworkUrl(const QVariantMap &artwork, int width) const;
    void applyFocusedEpisodeDetails(const QString &episodeId, const QVariantMap &data);
    QString startEpisodeDetailsRequest(const QString &episodeId);
    bool matchesEpisodeDetailsRequest(const QString &itemId, const QString &requestContext) const;
    void finishEpisodeDetailsRequest(const QString &itemId);
    void stopFocusedEpisodeDetailsLoadingFor(const QString &itemId);
    void setFocusedEpisodeDetailsLoading(bool loading);
    void applyFocusedEpisodeChapters(const QString &episodeId, const QVariantList &chapters);
    void setFocusedEpisodeChaptersLoading(bool loading);

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
    bool m_inWatchlist = false;
    bool m_watchlistPending = false;
    bool m_notInterested = false;
    bool m_notInterestedPending = false;
    UserRatingState *m_userRating = nullptr;
    QVariantMap m_nextAiring;
    bool m_nextAiringRequested = false;
    bool m_nextAiringPending = false;
    // Refreshes the chip at the next local midnight or air time, whichever
    // comes first, so "tomorrow" becomes "today" and a past airing goes.
    QTimer *m_nextAiringTimer = nullptr;
    bool airingStillAhead(const QVariantMap &airing) const;
    void setNextAiring(const QVariantMap &airing);
    void scheduleNextAiringRefresh();
    void refreshNextAiring();
    QString m_imdbId;
    QString m_tmdbId;
    QVariantList m_people;
    QStringList m_genres;
    QVariantList m_similarItems;
    bool m_similarItemsAttempted = false;
    bool m_similarItemsLoading = false;
    ItemExtrasState m_extras;
    bool m_extrasLoadingNotified = false;
    void notifyExtrasLoading();

    int m_seasonCount = 0;
    QString m_officialRating;
    int m_recursiveItemCount = 0;
    QString m_status;
    QDateTime m_endDate;
    QJsonObject m_seriesData;
    QString m_connectionId;
    
    // The media server's ratings (canonical serverRatings), then MDBList
    // and AniList for other sources.
    QVariantList m_serverRatings;
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
    QVariantMap m_nextEpisodeData;
    QString m_focusedEpisodeDetailId;
    QVariantMap m_focusedEpisodeDetails;
    QVariantList m_focusedEpisodePeople;
    bool m_focusedEpisodeDetailsLoading = false;
    QString m_focusedEpisodeChapterId;
    QVariantList m_focusedEpisodeChapters;
    bool m_focusedEpisodeChaptersLoading = false;
    QHash<QString, QVariantMap> m_episodeDetailsCache;
    QSet<QString> m_pendingEpisodeDetailIds;
    QHash<QString, QString> m_episodeDetailRequestTokens;
    QSet<QString> m_episodeDetailRetried;
    quint64 m_episodeDetailRequestSequence = 0;
    QHash<QString, QVariantList> m_episodeChapterCache;
    QSet<QString> m_pendingEpisodeChapterIds;

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
    QVector<QVariantMap> m_seasons;

    // Prefetch tracking
    QSet<QString> m_prefetchSeasonIds;

    // Timing
    QElapsedTimer m_seriesTimer;
    QElapsedTimer m_seasonsTimer;
    QElapsedTimer m_episodesTimer;
};
