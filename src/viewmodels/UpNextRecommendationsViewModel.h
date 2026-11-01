#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include "UserRatingState.h"

class LibraryService;
class MediaRequestService;

/**
 * @brief Loads the Up Next screen's supporting rows.
 *
 * With a next episode, `upcomingEpisodes` lists that episode and the ones
 * after it in the same season. Without one, `items` holds TV-series
 * recommendations: local-library similar series first because they are
 * immediately available, then request results (Seerr, or the Silo server's
 * own requests) as discovery options when a TMDB id can be resolved from the
 * source series details.
 *
 * After a movie, `loadForMovie` fills `items` with movies and series
 * recommended because the viewer watched it (Silo's because-watched read,
 * Jellyfin's similar items), `movie` with the movie's canonical details, and
 * `movieRating` with the viewer's rating of it.
 */
class UpNextRecommendationsViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantList upcomingEpisodes READ upcomingEpisodes NOTIFY upcomingEpisodesChanged)
    Q_PROPERTY(QVariantMap movie READ movie NOTIFY movieChanged)
    Q_PROPERTY(UserRatingState *movieRating READ movieRating CONSTANT)

public:
    explicit UpNextRecommendationsViewModel(QObject *parent = nullptr);

    QVariantList items() const;
    bool loading() const { return m_loading; }
    QVariantList upcomingEpisodes() const { return m_upcomingEpisodes; }
    QVariantMap movie() const { return m_movie; }
    UserRatingState *movieRating() const { return m_movieRating; }

    Q_INVOKABLE void loadForSeries(const QString &seriesId, int limit = 6);
    Q_INVOKABLE void loadForMovie(const QString &movieId, int limit = 12);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void loadUpcomingEpisodes(const QString &seasonId,
                                          const QString &startEpisodeId,
                                          int limit = 8);
    Q_INVOKABLE void clearUpcomingEpisodes();

    static QVariantList mergeRecommendations(const QVariantList &libraryItems,
                                             const QVariantList &requestItems,
                                             int limit);
    /// Episodes from @p startEpisodeId onward within its season number, in
    /// episode order, skipping missing (virtual) entries. Empty when the start
    /// episode is not in @p seasonItems.
    static QVariantList selectUpcomingEpisodes(const QVariantList &seasonItems,
                                               const QString &startEpisodeId,
                                               int limit);
    /// The movies and series among @p items, without @p sourceItemId and
    /// duplicates, in their order, at most @p limit.
    static QVariantList selectBecauseWatched(const QVariantList &items,
                                             const QString &sourceItemId,
                                             int limit);

signals:
    void itemsChanged();
    void loadingChanged();
    void upcomingEpisodesChanged();
    void movieChanged();

private slots:
    void onSimilarItemsLoaded(const QString &connectionId,
                              const QString &itemId,
                              const QVariantList &items);
    void onSimilarItemsFailed(const QString &connectionId,
                              const QString &itemId,
                              const QString &error);
    void onSeriesDetailsLoaded(const QString &connectionId,
                               const QString &seriesId,
                               const QVariantMap &seriesData);
    void onSeriesDetailsNotModified(const QString &connectionId, const QString &seriesId);
    void onSeriesDetailsFailed(const QString &connectionId,
                               const QString &seriesId,
                               const QString &error);
    void onSeriesDetailsFallbackLoaded(const QString &itemId,
                                       const QVariantMap &itemData,
                                       const QString &requestContext);
    void onSeriesDetailsFallbackNotModified(const QString &itemId, const QString &requestContext);
    void onSeriesDetailsFallbackFailed(const QString &itemId,
                                       const QString &error,
                                       const QString &requestContext);
    void onRequestSimilarResultsLoaded(const QString &mediaType, int tmdbId, const QVariantList &results);
    void onRequestSimilarResultsFailed(const QString &mediaType, int tmdbId, const QString &error);
    void onBecauseWatchedLoaded(const QString &connectionId,
                                const QString &itemId,
                                const QVariantList &items);
    void onBecauseWatchedFailed(const QString &connectionId,
                                const QString &itemId,
                                const QString &error);
    void onSeasonItemsLoaded(const QString &connectionId,
                             const QString &parentId,
                             const QString &queryKey,
                             const QVariantList &items,
                             int totalRecordCount);

private:
    static bool isSeriesItem(const QVariantMap &item);
    static bool isMovieOrSeriesItem(const QVariantMap &item);
    static QString dedupeKey(const QVariantMap &item);
    static QString normalizedTitle(const QString &title);
    static int itemYear(const QVariantMap &item);

    void notifyItemsChanged();
    void setLoading(bool loading);
    void finishProviderIfComplete();
    void resetRequestState(const QString &seriesId, int limit);
    void requestSimilarTitlesIfPossible(const QVariantMap &seriesData);
    void requestSeriesDetailsFallback();
    bool matchesSeriesDetailsFallback(const QString &itemId, const QString &requestContext) const;

    LibraryService *m_libraryService = nullptr;
    MediaRequestService *m_mediaRequests = nullptr;
    QVariantList m_libraryItems;
    QVariantList m_requestItems;
    QString m_connectionId;
    QString m_sourceId;
    QString m_seriesDetailsFallbackContext;
    mutable QVariantList m_cachedItems;
    mutable bool m_itemsDirty = true;
    int m_limit = 6;
    int m_pendingRequestTmdbId = -1;
    bool m_loading = false;
    bool m_waitingForLibrary = false;
    bool m_waitingForSeriesDetails = false;
    bool m_waitingForRequests = false;
    // After a movie: the movie is the source and items are because-watched.
    bool m_movieMode = false;
    QString m_movieDetailsContext;
    QVariantMap m_movie;
    UserRatingState *m_movieRating = nullptr;

    QVariantList m_upcomingEpisodes;
    QString m_upcomingConnectionId;
    QString m_upcomingSeasonId;
    QString m_upcomingStartEpisodeId;
    int m_upcomingLimit = 8;
};
