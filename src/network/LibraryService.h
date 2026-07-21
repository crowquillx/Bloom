#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QNetworkReply>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QHash>
#include <QSet>
#include <QDate>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include "Types.h"  // Shared data structs and error helpers

class AuthenticationService;
class HttpTransport;

struct LibraryItemQuery {
    QString parentId;
    int startIndex = 0;
    int limit = 0;
    QString searchTerm;
    QStringList genres;
    QStringList tags;
    QStringList studios;
    QDate minPremiereDate;
    QDate maxPremiereDate;
    QDate minDateLastSaved;
    enum class TriState {
        Any,
        Yes,
        No
    };
    TriState watched = TriState::Any;
    TriState favorite = TriState::Any;
    double minCommunityRating = 0.0;
    QList<int> years;
    QString sortBy;
    QString sortOrder;
    QStringList includeItemTypes;
    bool recursive = false;
    bool includeHeavyFields = true;
    bool useCacheValidation = false;
    QString requestKey;

    QString normalizedSortBy() const;
    QString cacheKey() const;
};

/**
 * Service for browsing a media library and retrieving items, metadata, and related URLs.
 *
 * Provides methods to:
 * - Fetch library views, paginated item lists, latest media, and next-up episodes.
 * - Retrieve series, season, and episode details, including next unplayed episode lookup.
 * - Update item and series user state (played/unplayed, watched/unwatched, favorite toggles).
 * - Perform searches and fetch random items.
 * - Build stream and image URLs (including cached and width-specific variants).
 *
 * Emits signals for successful loads, not-modified cache cases, user-data changes,
 * progress of long-running parsing operations, and network or endpoint errors.
 */
class LibraryService : public QObject
{
    Q_OBJECT

public:
    explicit LibraryService(AuthenticationService *authService, QObject *parent = nullptr);
    virtual ~LibraryService() = default;
    
    // Library views
    Q_INVOKABLE virtual void getViews();
    
    // Items with pagination and filtering
    Q_INVOKABLE virtual void getItems(const QString &parentId, int startIndex = 0, int limit = 0,
                               const QStringList &genres = QStringList(),
                               const QStringList &networks = QStringList(),
                               const QString &sortBy = QString(),
                               const QString &sortOrder = QString(),
                               bool includeHeavyFields = true,
                               bool useCacheValidation = false);
    virtual void getItems(const LibraryItemQuery &query);
    Q_INVOKABLE virtual void getFilterOptions(const QString &parentId,
                                              const QStringList &includeItemTypes = QStringList(),
                                              bool recursive = true);
    
    // Next up episodes
    Q_INVOKABLE virtual void getNextUp();
    
    // Latest media for a library
    Q_INVOKABLE virtual void getLatestMedia(const QString &parentId);
    
    // Fast global backdrop pool for Home screen rotation (limit <= 0 means all)
    Q_INVOKABLE virtual void getHomeBackdropItems(int limit = 0);

    // Random OLED-safe screensaver candidates with artwork and synopsis metadata.
    Q_INVOKABLE virtual void getScreensaverItems(int limit = 80);
    
    // Generic Item Details
    Q_INVOKABLE virtual void getItem(const QString &itemId);
    virtual void getItem(const QString &itemId, const QString &requestContext);
    Q_INVOKABLE virtual void getChapters(const QString &itemId);
    virtual void clearItemCacheValidation(const QString &itemId);
    Q_INVOKABLE virtual void resolveLibraryForItem(const QString &itemId);

    // Series details and episodes
    Q_INVOKABLE virtual void getSeriesDetails(const QString &seriesId);
    Q_INVOKABLE virtual void getSimilarItems(const QString &itemId, int limit = 12);
    Q_INVOKABLE virtual void getNextUnplayedEpisode(const QString &seriesId,
                                                    const QString &excludeItemId = QString(),
                                                    const QString &requestContext = QString());
    Q_INVOKABLE virtual void markSeriesWatched(const QString &seriesId);
    Q_INVOKABLE virtual void markSeriesUnwatched(const QString &seriesId);
    Q_INVOKABLE virtual void markItemPlayed(const QString &itemId);
    Q_INVOKABLE virtual void markItemUnplayed(const QString &itemId);
    Q_INVOKABLE virtual void markItemFavorite(const QString &itemId);
    Q_INVOKABLE virtual void markItemUnfavorite(const QString &itemId);
    Q_INVOKABLE virtual void toggleFavorite(const QString &itemId, bool isFavorite);
    Q_INVOKABLE virtual void getThemeSongs(const QString &seriesId);
    
    // Search
    Q_INVOKABLE virtual void search(const QString &searchTerm, int limit = 50);
    Q_INVOKABLE virtual void getRandomItems(int limit = 20);

    // Hero Banner: random library sample (Movies + Series). When parentIds is empty,
    // all libraries are sampled. When unwatchedOnly is true, only unplayed items are
    // returned. Used by the Home screen "Library" hero source.
    Q_INVOKABLE virtual void getHeroLibraryItems(int limit, const QStringList &parentIds, bool unwatchedOnly);
    Q_INVOKABLE virtual void getHeroSeriesOverviews(const QStringList &seriesIds);
    
    // URL helpers
    Q_INVOKABLE virtual QString getStreamUrl(const QString &itemId);
    Q_INVOKABLE virtual QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                                int audioStreamIndex = -1, int subtitleStreamIndex = -1);
    Q_INVOKABLE virtual QString getImageUrl(const QString &itemId, const QString &imageType);
    Q_INVOKABLE virtual QString getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);
    Q_INVOKABLE virtual QString getCachedImageUrl(const QString &itemId, const QString &imageType);
    Q_INVOKABLE virtual QString getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);
    Q_INVOKABLE virtual QString getCachedArtworkUrl(const QString &itemId,
                                                     const QString &imageType,
                                                     int imageIndex,
                                                     const QString &imageTag,
                                                     int width);
    Q_INVOKABLE virtual QString getCachedChapterThumbnailUrl(const QString &itemId, int chapterIndex, const QString &imageTag, const QString &imagePath = QString(), int width = 480);

    QNetworkReply* pingServer();

signals:
    void viewsLoaded(const QJsonArray &views);
    void itemsLoaded(const QString &parentId, const QJsonArray &items);
    void itemsLoadedWithTotal(const QString &parentId, const QJsonArray &items, int totalRecordCount);
    void itemsLoadedWithTotalForQuery(const QString &parentId, const QString &queryKey, const QJsonArray &items, int totalRecordCount);
    void itemsNotModified(const QString &parentId);
    void itemsNotModifiedForQuery(const QString &parentId, const QString &queryKey);
    void filterOptionsLoaded(const QString &parentId,
                             const QStringList &genres,
                             const QStringList &tags,
                             const QStringList &studios);
    
    // Generic Item Signals
    void itemLoaded(const QString &itemId, const QJsonObject &data);
    void itemLoaded(const QString &itemId, const QJsonObject &data, const QString &requestContext);
    // Provider-neutral camelCase item projections (mapped once at the service boundary).
    // Raw itemLoaded signals remain for unmigrated callers during the catalog migration.
    void canonicalItemLoaded(const QString &itemId, const QVariantMap &item);
    void canonicalItemLoaded(const QString &itemId, const QVariantMap &item, const QString &requestContext);
    void itemLibraryResolved(const QString &itemId, const QString &libraryId);
    void itemLibraryResolutionFailed(const QString &itemId, const QString &error);
    void itemNotModified(const QString &itemId);
    void itemNotModified(const QString &itemId, const QString &requestContext);
    void itemFailed(const QString &itemId, const QString &error, const QString &requestContext);
    void itemUserDataChanged(const QString &itemId, const QJsonObject &userData);
    void chaptersLoaded(const QString &itemId, const QList<ChapterInfo> &chapters);
    void chaptersFailed(const QString &itemId, const QString &error);

    void nextUpLoaded(const QJsonArray &items);
    void latestMediaLoaded(const QString &parentId, const QJsonArray &items);
    void homeBackdropItemsLoaded(const QJsonArray &items);
    void screensaverItemsLoaded(const QJsonArray &items);
    void seriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData);
    void seriesDetailsNotModified(const QString &seriesId);
    void similarItemsLoaded(const QString &itemId, const QJsonArray &items);
    void canonicalSimilarItemsLoaded(const QString &itemId, const QVariantList &items);
    void similarItemsFailed(const QString &itemId, const QString &error);
    void nextUnplayedEpisodeLoaded(const QString &seriesId,
                                   const QJsonObject &episodeData,
                                   const QString &requestContext);
    void nextUnplayedEpisodeFailed(const QString &seriesId,
                                   const QString &error,
                                   const QString &requestContext);
    void seriesWatchedStatusChanged(const QString &seriesId);
    void itemPlayedStatusChanged(const QString &itemId, bool isPlayed);
    void favoriteStatusChanged(const QString &itemId, bool isFavorite);
    void themeSongsLoaded(const QString &seriesId, const QStringList &urls);
    void searchResultsLoaded(const QString &searchTerm, const QJsonArray &movies, const QJsonArray &series);
    void randomItemsLoaded(const QJsonArray &items);
    void heroLibraryItemsLoaded(const QJsonArray &items);
    void heroSeriesOverviewsLoaded(const QJsonObject &overviewsBySeriesId);
    
    // Error signals
    void errorOccurred(const QString &endpoint, const QString &error);
    void networkError(const NetworkError &error);

    // Progress signals for long-running operations
    void parsingStarted(const QString &operation);
    void parsingProgress(const QString &operation, int processed, int total);
    void parsingFinished(const QString &operation);

private:
    AuthenticationService *m_authService;
    HttpTransport *m_transport = nullptr;
    RetryPolicy m_retryPolicy;
    
    // Retry mechanism types
    using ResponseHandler = std::function<void(QNetworkReply*)>;
    using RequestFactory = std::function<QNetworkReply*()>;
    using FailureHandler = std::function<void(const NetworkError&)>;
    
    void sendRequestWithRetry(const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               FailureHandler failureHandler = FailureHandler(),
                               int attemptNumber = 0);
    
    void emitError(const NetworkError &error);

    // In-flight deduplication for chapter requests
    QSet<QString> m_inFlightChapterRequests;

    // Cache validation state (per endpoint/parent)
    QHash<QString, QString> m_etags;
    QHash<QString, QString> m_lastModified;
};
