#pragma once

#include <QObject>
#include <QStringList>
#include <QNetworkReply>
#include <QHash>
#include <QSet>
#include <QList>
#include <QPointer>
#include <QDate>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "Types.h"  // Shared data structs and error helpers
#include "providers/ICatalogProvider.h"

class AuthenticationService;
class HttpTransport;
class HttpRequestHandle;

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
    QString datasetKey() const;
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
    virtual void getViewsForRequest(const QString &requestKey);
    
    // Items with pagination and filtering
    Q_INVOKABLE virtual void getItems(const QString &parentId, int startIndex = 0, int limit = 0,
                               const QStringList &genres = QStringList(),
                               const QStringList &networks = QStringList(),
                               const QString &sortBy = QString(),
                               const QString &sortOrder = QString(),
                               bool includeHeavyFields = true,
                               bool useCacheValidation = false);
    virtual void getItems(const LibraryItemQuery &query);
    // Hierarchy children. The item type keeps the request unambiguous for
    // providers that route seasons and episodes separately from library
    // browsing (Silo). Results arrive through the getItems signals with
    // the series or season ID as parentId.
    Q_INVOKABLE void getSeasons(const QString &seriesId, bool useCacheValidation = false);
    Q_INVOKABLE void getSeasonEpisodes(const QString &seasonId, bool useCacheValidation = false);
    Q_INVOKABLE virtual void getFilterOptions(const QString &parentId,
                                              const QStringList &includeItemTypes = QStringList(),
                                              bool recursive = true);
    virtual void getFilterOptionsForRequest(
        const QString &parentId,
        const QStringList &includeItemTypes,
        bool recursive,
        const QString &requestKey);
    
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
    // Watchlist membership, for providers with ProviderCapability::Watchlist
    // (Silo). Results arrive as watchlistStatusChanged or
    // watchlistStatusFailed; an unsupported provider reports failure.
    Q_INVOKABLE virtual bool supportsWatchlist() const;
    Q_INVOKABLE virtual void setItemWatchlisted(const QString &itemId, bool inWatchlist);
    Q_INVOKABLE virtual void getThemeSongs(const QString &seriesId);

    // Server-side media requests, for providers with
    // ProviderCapability::MediaRequests (Silo). The handler receives the
    // provider's Bloom-shaped result (capabilityMetadata, documented on
    // SiloMediaRequestMapper::parseResponse). A response for a connection,
    // account, or profile that is no longer active runs neither handler.
    // MediaRequestService routes between these operations and Seerr.
    using MediaRequestHandler = std::function<void(const QVariantMap &)>;
    using MediaRequestFailureHandler = std::function<void(const QString &)>;
    virtual bool supportsMediaRequests() const;
    virtual void sendMediaRequestOperation(ProviderCatalogOperation operation,
                                           const ProviderCatalogQuery &query,
                                           MediaRequestHandler onLoaded,
                                           MediaRequestFailureHandler onFailed);

    // The rows the profile curates for its Home page that Bloom does not
    // already show, in the server's layout order: each {sectionId, title,
    // items} with canonical movie, series and episode items. A Watchlist row
    // comes first when the layout has none and the watchlist is not empty.
    // Providers without ProviderCapability::HomeSections answer no rows.
    // A newer call supersedes an older one.
    Q_INVOKABLE virtual void getHomeSections();
    
    // Search. Libraries in excludedLibraryIds are left out: the remaining
    // libraries are searched one by one and their results interleaved.
    Q_INVOKABLE virtual void search(const QString &searchTerm, int limit = 50,
                                    const QStringList &excludedLibraryIds = {});
    Q_INVOKABLE virtual void getRandomItems(int limit = 20);

    // Hero Banner: random library sample (Movies + Series). When parentIds is empty,
    // all libraries are sampled. When unwatchedOnly is true, only unplayed items are
    // returned. Used by the Home screen "Library" hero source.
    Q_INVOKABLE virtual void getHeroLibraryItems(int limit, const QStringList &parentIds, bool unwatchedOnly);
    Q_INVOKABLE virtual void getHeroSeriesOverviews(const QStringList &seriesIds);
    
    // URL helpers and request ownership
    Q_INVOKABLE virtual QString getActiveConnectionId() const;
    Q_INVOKABLE virtual QString getStreamUrl(const QString &itemId);
    Q_INVOKABLE virtual QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                                int audioStreamIndex = -1, int subtitleStreamIndex = -1);
    Q_INVOKABLE virtual QString getImageUrl(const QString &itemId, const QString &imageType);
    Q_INVOKABLE virtual QString getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);
    Q_INVOKABLE virtual QString getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);
    Q_INVOKABLE virtual QString getCachedArtworkUrl(const QString &itemId,
                                                     const QString &imageType,
                                                     int imageIndex,
                                                     const QString &imageTag,
                                                     int width);
    Q_INVOKABLE virtual QString getCachedArtworkUrlForConnection(const QString &connectionId,
                                                                  const QString &itemId,
                                                                  const QString &imageType,
                                                                  int imageIndex,
                                                                  const QString &imageTag,
                                                                  int width);
    Q_INVOKABLE virtual QString getCachedArtworkUrlFromRef(const QVariantMap &artwork,
                                                           int width);

    QNetworkReply* pingServer();

signals:
    void canonicalViewsLoadedForConnection(const QString &connectionId,
                                           const QVariantList &views);
    void canonicalViewsLoadedForRequest(const QString &connectionId,
                                        const QString &requestKey,
                                        const QVariantList &views);
    void canonicalViewsFailedForRequest(const QString &connectionId,
                                        const QString &requestKey,
                                        const QString &error);
    void canonicalItemsLoadedForConnection(const QString &connectionId,
                                           const QString &parentId,
                                           const QString &queryKey,
                                           const QVariantList &items,
                                           int totalRecordCount);
    void canonicalItemsFailedForConnection(const QString &connectionId,
                                           const QString &parentId,
                                           const QString &requestKey,
                                           const QString &error);
    void canonicalItemsNotModifiedForConnection(const QString &connectionId,
                                                const QString &parentId,
                                                const QString &queryKey);
    void filterOptionsLoaded(const QString &parentId,
                             const QStringList &genres,
                             const QStringList &tags,
                             const QStringList &studios);
    void filterOptionsLoadedForRequest(const QString &connectionId,
                                       const QString &parentId,
                                       const QString &requestKey,
                                       const QStringList &genres,
                                       const QStringList &tags,
                                       const QStringList &studios);
    void filterOptionsFailedForRequest(const QString &connectionId,
                                       const QString &parentId,
                                       const QString &requestKey,
                                       const QString &error);
    
    // Provider-neutral camelCase item projections (mapped once at the service boundary).
    void canonicalItemLoaded(const QString &itemId, const QVariantMap &item);
    void canonicalItemLoaded(const QString &itemId, const QVariantMap &item, const QString &requestContext);
    void itemLibraryResolved(const QString &itemId, const QString &libraryId);
    void itemLibraryResolutionFailed(const QString &itemId, const QString &error);
    void itemNotModified(const QString &itemId);
    void itemNotModified(const QString &itemId, const QString &requestContext);
    void itemFailed(const QString &itemId, const QString &error, const QString &requestContext);
    void canonicalChaptersLoaded(const QString &connectionId,
                                 const QString &itemId,
                                 const QVariantList &chapters);
    void chaptersFailed(const QString &connectionId,
                       const QString &itemId,
                       const QString &error);

    void canonicalNextUpLoaded(const QString &connectionId, const QVariantList &items);
    void canonicalLatestMediaLoaded(const QString &connectionId,
                                    const QString &parentId,
                                    const QVariantList &items);
    void canonicalHomeBackdropItemsLoaded(const QString &connectionId,
                                          const QVariantList &items);
    void canonicalHomeBackdropItemsFailed(const QString &connectionId,
                                          const QString &error);
    void canonicalScreensaverItemsLoaded(const QString &connectionId,
                                         const QVariantList &items);
    void canonicalScreensaverItemsFailed(const QString &connectionId,
                                         const QString &error);
    void canonicalSeriesDetailsLoaded(const QString &connectionId,
                                      const QString &seriesId,
                                      const QVariantMap &seriesData);
    void canonicalSeriesDetailsNotModified(const QString &connectionId,
                                           const QString &seriesId);
    void canonicalSeriesDetailsFailed(const QString &connectionId,
                                      const QString &seriesId,
                                      const QString &error);
    void canonicalSimilarItemsLoadedForConnection(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QVariantList &items);
    void canonicalSimilarItemsFailedForConnection(const QString &connectionId,
                                                  const QString &itemId,
                                                  const QString &error);
    void canonicalNextUnplayedEpisodeLoaded(const QString &connectionId,
                                            const QString &seriesId,
                                            const QVariantMap &episodeData,
                                            const QString &requestContext);
    void nextUnplayedEpisodeFailed(const QString &seriesId,
                                   const QString &error,
                                   const QString &requestContext);
    void seriesWatchedStatusChanged(const QString &seriesId);
    void itemPlayedStatusChanged(const QString &itemId, bool isPlayed);
    void favoriteStatusChanged(const QString &itemId, bool isFavorite);
    void watchlistStatusChanged(const QString &itemId, bool inWatchlist);
    void watchlistStatusFailed(const QString &itemId, bool inWatchlist, const QString &error);
    void canonicalHomeSectionsLoaded(const QString &connectionId, const QVariantList &sections);
    void canonicalHomeSectionsFailed(const QString &connectionId, const QString &error);
    void themeSongPlaybackLoaded(const QString &seriesId, const QStringList &urls, bool requiresFreshGrant);
    void canonicalSearchResultsLoaded(const QString &connectionId,
                                      const QString &searchTerm,
                                      const QVariantList &movies,
                                      const QVariantList &series);
    void canonicalSearchResultsFailed(const QString &connectionId,
                                      const QString &searchTerm,
                                      const QString &error);
    void canonicalRandomItemsLoaded(const QString &connectionId,
                                    const QVariantList &items);
    void canonicalRandomItemsFailed(const QString &connectionId,
                                    const QString &error);
    void canonicalHeroLibraryItemsLoaded(const QString &connectionId,
                                         const QVariantList &items);
    void canonicalHeroLibraryItemsFailed(const QString &connectionId,
                                         const QString &error);
    void canonicalHeroSeriesOverviewsLoaded(const QString &connectionId,
                                            const QVariantMap &overviewsBySeriesId);
    
    // Error signals
    void errorOccurred(const QString &endpoint, const QString &error);
    void networkError(const NetworkError &error);

private:
    struct CatalogRequestIdentity {
        quint64 generation = 0;
        QString connectionId;
        QString userId;
        QString profileId;
        const ICatalogProvider *provider = nullptr;
    };

    using CatalogResponseHandler =
        std::function<void(const ProviderCatalogResponse &)>;
    using CatalogNotModifiedHandler = std::function<void()>;
    using FailureHandler = std::function<void(const NetworkError &)>;

    AuthenticationService *m_authService;
    HttpTransport *m_transport = nullptr;
    RetryPolicy m_retryPolicy;

    ProviderCatalogQuery baseCatalogQuery() const;
    void getNextUnplayedEpisodeFromHierarchy(const QString &seriesId,
                                             const QString &excludeItemId,
                                             const QString &requestContext);
    CatalogRequestIdentity catalogRequestIdentity() const;
    bool isCurrent(const CatalogRequestIdentity &identity) const;
    void sendCatalogRequest(
        const QString &operationName,
        ProviderCatalogOperation operation,
        const ProviderCatalogQuery &query,
        CatalogResponseHandler responseHandler,
        FailureHandler failureHandler = FailureHandler(),
        CatalogNotModifiedHandler notModifiedHandler = CatalogNotModifiedHandler());
    // Random selection for operations a provider cannot answer natively.
    void sendRandomSampleRequest(
        const QString &operationName,
        ProviderCatalogOperation nativeOperation,
        const ProviderCatalogQuery &query,
        CatalogResponseHandler responseHandler,
        FailureHandler failureHandler = FailureHandler());
    void emitCatalogError(const QString &operationName,
                          const QString &message,
                          FailureHandler failureHandler = FailureHandler(),
                          int code = -1);
    void emitError(const NetworkError &error);

    QSet<QString> m_inFlightChapterRequests;
    QList<QPointer<HttpRequestHandle>> m_catalogRequests;
    quint64 m_requestGeneration = 0;
    quint64 m_heroRequestGeneration = 0;
    quint64 m_homeSectionsGeneration = 0;
    // Newest search; a replaced search (new term or library exclusions)
    // must not publish results after it.
    quint64 m_searchGeneration = 0;
    /// Libraries of unsupported types per connection, from the last views
    /// listing; search leaves them out (LibraryKinds).
    QHash<QString, QStringList> m_unsupportedLibraryIds;

    // Cache validation state (per provider-owned endpoint).
    QHash<QString, QHash<int, QString>> m_pageCursors;
    QHash<QString, quint64> m_pageCursorEpochs;
    quint64 m_nextCursorEpoch = 0;
    QHash<QString, ProviderCatalogResponse> m_conditionalResponses;
    quint64 m_themeRequestGeneration = 0;
    QHash<QString, QString> m_etags;
    QHash<QString, QString> m_lastModified;
};
