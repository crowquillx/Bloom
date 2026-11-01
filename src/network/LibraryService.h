#pragma once

#include <QObject>
#include <QStringList>
#include <QNetworkReply>
#include <QHash>
#include <QSet>
#include <QList>
#include <QPointer>
#include <QDate>
#include <QDeadlineTimer>
#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "Types.h"  // Shared data structs and error helpers
#include "providers/ICatalogProvider.h"

class AuthenticationService;
class ConfigManager;
class HttpTransport;
class HttpRequestHandle;

// Outcome of LibraryService::pingServer.
enum class ServerReachability {
    Reachable,
    // The server refused the session and could not renew it.
    SessionExpired,
    // No answer, a server error, a deadline, or a renewal that could not
    // reach the server: try again later.
    Unreachable
};

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
    
    // Home's Continue Watching and Next Up items as one list
    // (canonicalNextUpLoaded). Providers with ProviderCapability::ResumeItems
    // (Jellyfin) add their resume list, so in-progress movies appear too,
    // and the list is ordered by ContinueWatching::combine. Other providers
    // (Silo) answer one list in the server's order.
    Q_INVOKABLE virtual void getNextUp();
    // Whether the active provider honors the Next Up options
    // (ConfigManager::nextUpRewatching and nextUpCutoffDays).
    Q_INVOKABLE virtual bool supportsNextUpOptions() const;
    // Next Up options from the saved settings, applied to a Next Up query.
    // Providers without ProviderCapability::NextUpOptions ignore them.
    static void applyNextUpOptions(ProviderCatalogQuery &query,
                                   const ConfigManager *config,
                                   const QDate &today);
    
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
    // Local extras of a movie or series (trailers first). Providers that
    // carry extras in the item detail (Silo) answer an empty list here.
    Q_INVOKABLE virtual void getItemExtras(const QString &itemId);
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
    // "Not interested" for a movie or series, for providers with
    // ProviderCapability::NotInterested (Silo) whose capability document
    // allows it for the selected profile. loadNotInterestedCapability reads
    // that document once per connection and profile and announces the
    // answer with notInterestedSupportChanged; supportsNotInterested is
    // false until then. Results arrive as notInterestedStatusChanged or
    // notInterestedStatusFailed.
    Q_INVOKABLE virtual bool supportsNotInterested() const;
    Q_INVOKABLE virtual void loadNotInterestedCapability();
    Q_INVOKABLE virtual void setItemNotInterested(const QString &itemId, bool notInterested);
    // The series' next airing in the viewer's next 30 local days, for
    // providers with ProviderCapability::AiringCalendar (Silo). Answers
    // nextAiringLoaded, with an empty map when none is known.
    Q_INVOKABLE virtual void getNextAiring(const QString &seriesId);
    // The earliest airing of seriesId among canonical airings that is still
    // ahead of now: by its instant when known, else by its local day.
    static QVariantMap nextAiringFor(const QVariantList &airings,
                                     const QString &seriesId,
                                     const QDateTime &now);
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
    // Providers without ProviderCapability::Watchlist (Jellyfin) get a
    // Favorites row instead, when ConfigManager::homeFavoritesRowEnabled is
    // on and a favorite movie or series exists. Providers without
    // ProviderCapability::HomeSections answer no other rows.
    // A newer call supersedes an older one.
    Q_INVOKABLE virtual void getHomeSections();
    // Whether Home can show the Favorites row for the active provider (it
    // has no watchlist), whatever the setting.
    Q_INVOKABLE virtual bool supportsHomeFavoritesRow() const;
    
    // Search. Libraries in excludedLibraryIds are left out: the remaining
    // libraries are searched one by one and their results interleaved.
    Q_INVOKABLE virtual void search(const QString &searchTerm, int limit = 50,
                                    const QStringList &excludedLibraryIds = {});
    Q_INVOKABLE virtual void getRandomItems(int limit = 20);

    // People. getPerson reads one person's biography, life dates and photo.
    // getPersonItems reads one page of their movies and series, newest
    // first: pass the previous page's nextCursor (Silo) or the next
    // startIndex (Jellyfin); requestKey comes back with the answer.
    // searchPeople finds people by name in movie and series credits; a
    // provider that cannot limit people search to those answers no people.
    Q_INVOKABLE virtual void getPerson(const QString &personId);
    virtual void getPersonItems(const QString &personId, int startIndex,
                                const QString &cursor, int limit,
                                const QString &requestKey);
    Q_INVOKABLE virtual void searchPeople(const QString &searchTerm, int limit = 20);

    // Hero Banner: random library sample (Movies + Series). When parentIds is empty,
    // all libraries are sampled. When unwatchedOnly is true, only unplayed items are
    // returned. Used by the Home screen "Library" hero source.
    Q_INVOKABLE virtual void getHeroLibraryItems(int limit, const QStringList &parentIds, bool unwatchedOnly);
    Q_INVOKABLE virtual void getHeroSeriesOverviews(const QStringList &seriesIds);
    
    // URL helpers and request ownership
    Q_INVOKABLE virtual QString getActiveConnectionId() const;
    // The library sorts and filters the active connection's provider serves;
    // every canonical one when no provider is active.
    virtual ProviderLibraryQueryCapabilities libraryQueryCapabilities() const;
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

    /// Checks that the active server answers and still accepts the session,
    /// through the provider's authenticated identity route (Silo
    /// `/api/v2/account/me`, Jellyfin `/Users/{id}`). A refused access token
    /// is renewed first; expiry is deferred until playback stops. One bounded
    /// attempt, no replay: the caller polls on its own schedule. Returns null
    /// (and never calls done) when no session can be checked; cancel the
    /// handle to drop the result.
    HttpRequestHandle *pingServer(QObject *context,
                                  std::function<void(ServerReachability)> done);

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
    // `complete` is false when one of several provider requests failed:
    // `extras` is then only what arrived and must not be cached as the
    // item's full list.
    void canonicalItemExtrasLoaded(const QString &connectionId,
                                   const QString &itemId,
                                   const QVariantList &extras,
                                   bool complete);
    void canonicalItemExtrasFailed(const QString &connectionId,
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
    void notInterestedSupportChanged();
    void notInterestedStatusChanged(const QString &itemId, bool notInterested);
    void notInterestedStatusFailed(const QString &itemId, bool notInterested, const QString &error);
    void nextAiringLoaded(const QString &seriesId, const QVariantMap &airing);
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
    void canonicalPersonLoaded(const QString &connectionId,
                               const QString &personId,
                               const QVariantMap &person);
    void canonicalPersonFailed(const QString &connectionId,
                               const QString &personId,
                               const QString &error);
    void canonicalPersonItemsLoaded(const QString &connectionId,
                                    const QString &personId,
                                    const QString &requestKey,
                                    const QVariantList &items,
                                    int totalRecordCount,
                                    bool hasMore,
                                    const QString &nextCursor);
    void canonicalPersonItemsFailed(const QString &connectionId,
                                    const QString &personId,
                                    const QString &requestKey,
                                    const QString &error);
    void canonicalPeopleSearchLoaded(const QString &connectionId,
                                     const QString &searchTerm,
                                     const QVariantList &people);
    void canonicalPeopleSearchFailed(const QString &connectionId,
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

    // The profile's Home rows, each capped at itemLimitMax cards when it
    // is positive.
    void readHomeSections(const QString &connectionId, quint64 generation, int itemLimitMax);
    // Adds the Favorites row ahead of rows and publishes them, when Home
    // shows one for this provider.
    void publishHomeSectionsWithFavorites(const QString &connectionId,
                                          quint64 generation,
                                          const QVariantList &rows);

    QSet<QString> m_inFlightChapterRequests;
    QList<QPointer<HttpRequestHandle>> m_catalogRequests;
    quint64 m_requestGeneration = 0;
    quint64 m_heroRequestGeneration = 0;
    quint64 m_homeSectionsGeneration = 0;
    // Newest Next Up refresh; an older one must not publish after it.
    quint64 m_nextUpGeneration = 0;
    /// Per connection, the largest per-row card cap its Home layout read
    /// accepts (0 when it accepts none), once its capabilities were read.
    QHash<QString, int> m_homeSectionItemLimitMax;
    // Not interested capability per connection and profile, and the reads
    // in flight.
    QHash<QString, bool> m_notInterestedAllowed;
    QSet<QString> m_notInterestedCapabilityReads;
    // Newest search; a replaced search (new term or library exclusions)
    // must not publish results after it.
    quint64 m_searchGeneration = 0;
    // Newest people search, and per connection whether people search can be
    // limited to movie and series credits (providers that read it).
    quint64 m_peopleSearchGeneration = 0;
    QHash<QString, bool> m_peopleSearchScoped;
    // A failed capability read is not repeated for every keystroke: until
    // the deadline, people search on that connection shows no people.
    QHash<QString, QDeadlineTimer> m_peopleSearchCapabilityRetry;
    static constexpr int PeopleSearchCapabilityRetryMs = 5 * 60 * 1000;
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
