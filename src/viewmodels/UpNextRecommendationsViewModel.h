#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariantList>

class LibraryService;
class SeerrService;

/**
 * @brief Loads the Up Next screen's supporting rows.
 *
 * With a next episode, `upcomingEpisodes` lists that episode and the ones
 * after it in the same season. Without one, `items` holds TV-series
 * recommendations: local-library similar series first because they are
 * immediately available, then Seerr results as discovery/request options when
 * a TMDB id can be resolved from the source series details.
 */
class UpNextRecommendationsViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantList upcomingEpisodes READ upcomingEpisodes NOTIFY upcomingEpisodesChanged)

public:
    explicit UpNextRecommendationsViewModel(QObject *parent = nullptr);

    QVariantList items() const;
    bool loading() const { return m_loading; }
    QVariantList upcomingEpisodes() const { return m_upcomingEpisodes; }

    Q_INVOKABLE void loadForSeries(const QString &seriesId, int limit = 6);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void loadUpcomingEpisodes(const QString &seasonId,
                                          const QString &startEpisodeId,
                                          int limit = 8);
    Q_INVOKABLE void clearUpcomingEpisodes();

    static QVariantList mergeRecommendations(const QVariantList &libraryItems,
                                             const QVariantList &seerrItems,
                                             int limit);
    /// Episodes from @p startEpisodeId onward within its season number, in
    /// episode order, skipping missing (virtual) entries. Empty when the start
    /// episode is not in @p seasonItems.
    static QVariantList selectUpcomingEpisodes(const QVariantList &seasonItems,
                                               const QString &startEpisodeId,
                                               int limit);

signals:
    void itemsChanged();
    void loadingChanged();
    void upcomingEpisodesChanged();

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
    void onSeerrSimilarResultsLoaded(const QString &mediaType, int tmdbId, const QJsonArray &results);
    void onSeerrSimilarResultsFailed(const QString &mediaType, int tmdbId, const QString &error);
    void onSeasonItemsLoaded(const QString &connectionId,
                             const QString &parentId,
                             const QString &queryKey,
                             const QVariantList &items,
                             int totalRecordCount);

private:
    static bool isSeriesItem(const QVariantMap &item);
    static QString dedupeKey(const QVariantMap &item);
    static QString normalizedTitle(const QString &title);
    static int itemYear(const QVariantMap &item);

    void notifyItemsChanged();
    void setLoading(bool loading);
    void finishProviderIfComplete();
    void resetRequestState(const QString &seriesId, int limit);
    void requestSeerrSimilarIfPossible(const QVariantMap &seriesData);
    void requestSeriesDetailsFallback();
    bool matchesSeriesDetailsFallback(const QString &itemId, const QString &requestContext) const;

    LibraryService *m_libraryService = nullptr;
    SeerrService *m_seerrService = nullptr;
    QVariantList m_libraryItems;
    QVariantList m_seerrItems;
    QString m_connectionId;
    QString m_seriesId;
    QString m_seriesDetailsFallbackContext;
    mutable QVariantList m_cachedItems;
    mutable bool m_itemsDirty = true;
    int m_limit = 6;
    int m_pendingSeerrTmdbId = -1;
    bool m_loading = false;
    bool m_waitingForLibrary = false;
    bool m_waitingForSeriesDetails = false;
    bool m_waitingForSeerr = false;

    QVariantList m_upcomingEpisodes;
    QString m_upcomingConnectionId;
    QString m_upcomingSeasonId;
    QString m_upcomingStartEpisodeId;
    int m_upcomingLimit = 8;
};
