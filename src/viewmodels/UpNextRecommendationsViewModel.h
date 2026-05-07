#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariantList>

class LibraryService;
class SeerrService;

/**
 * @brief Loads post-playback TV-series recommendations for the no-next-episode state.
 *
 * Jellyfin similar series are preferred because they are immediately available in the
 * local library. Seerr results are appended as discovery/request options when a TMDB
 * id can be resolved from the source series details.
 */
class UpNextRecommendationsViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit UpNextRecommendationsViewModel(QObject *parent = nullptr);

    QVariantList items() const;
    bool loading() const { return m_loading; }

    Q_INVOKABLE void loadForSeries(const QString &seriesId, int limit = 6);
    Q_INVOKABLE void clear();

    static QJsonArray mergeRecommendations(const QJsonArray &jellyfinItems,
                                           const QJsonArray &seerrItems,
                                           int limit);

signals:
    void itemsChanged();
    void loadingChanged();

private slots:
    void onSimilarItemsLoaded(const QString &itemId, const QJsonArray &items);
    void onSimilarItemsFailed(const QString &itemId, const QString &error);
    void onSeriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData);
    void onSeriesDetailsNotModified(const QString &seriesId);
    void onSeriesDetailsFallbackLoaded(const QString &itemId,
                                       const QJsonObject &itemData,
                                       const QString &requestContext);
    void onSeriesDetailsFallbackNotModified(const QString &itemId, const QString &requestContext);
    void onSeriesDetailsFallbackFailed(const QString &itemId,
                                       const QString &error,
                                       const QString &requestContext);
    void onSeerrSimilarResultsLoaded(const QString &mediaType, int tmdbId, const QJsonArray &results);
    void onSeerrSimilarResultsFailed(const QString &mediaType, int tmdbId, const QString &error);
    void onLibraryErrorOccurred(const QString &endpoint, const QString &error);

private:
    static bool isSeriesItem(const QJsonObject &item);
    static QString dedupeKey(const QJsonObject &item);
    static QString normalizedTitle(const QString &title);
    static int itemYear(const QJsonObject &item);

    void notifyItemsChanged();
    void setLoading(bool loading);
    void finishProviderIfComplete();
    void resetRequestState(const QString &seriesId, int limit);
    void requestSeerrSimilarIfPossible(const QJsonObject &seriesData);
    void requestSeriesDetailsFallback();
    bool matchesSeriesDetailsFallback(const QString &itemId, const QString &requestContext) const;

    LibraryService *m_libraryService = nullptr;
    SeerrService *m_seerrService = nullptr;
    QJsonArray m_jellyfinItems;
    QJsonArray m_seerrItems;
    QString m_seriesId;
    QString m_seriesDetailsFallbackContext;
    int m_limit = 6;
    int m_pendingSeerrTmdbId = -1;
    bool m_loading = false;
    bool m_waitingForJellyfin = false;
    bool m_waitingForSeriesDetails = false;
    bool m_waitingForSeerr = false;
};
