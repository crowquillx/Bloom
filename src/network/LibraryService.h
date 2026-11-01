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
#include <optional>

#include "Types.h"  // Shared data structs and error helpers
#include "providers/ICatalogProvider.h"
#include "models/HomeRecommendations.h"

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

// What a Home card's actions can do on one connection (its provider).
struct HomeCardCapabilities {
    // Hide a card from Continue Watching or Next Up, with undo (Silo).
    bool dismissals = false;
    // Zero a resume position so the card leaves Continue Watching
    // (providers with a separate resume list, Jellyfin).
    bool resumeReset = false;
    // A watchlist distinct from favorites (Silo).
    bool watchlist = false;
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
    // True when the provider has an airing calendar
    // (ProviderCapability::AiringCalendar, Silo), so getNextAiring can
    // answer something other than an empty map.
    Q_INVOKABLE virtual bool supportsNextAiring() const;
    // The series' next airing in the viewer's next 30 local days. Answers
    // nextAiringLoaded, with an empty map when none is known or the read
    // fails; an unsupported provider answers at once. The calendar is read
    // once per connection, profile, local day and zone and kept for
    // kAiringCalendarTtlMs, and pages asking while it is read share it.
    Q_INVOKABLE virtual void getNextAiring(const QString &seriesId);
    static constexpr qint64 kAiringCalendarTtlMs = 12 * 60 * 1000;
    // The earliest airing of seriesId among canonical airings that is still
    // ahead of now: by its instant when known, else by its local day.
    static QVariantMap nextAiringFor(const QVariantList &airings,
                                     const QString &seriesId,
                                     const QDateTime &now);
    Q_INVOKABLE virtual void getThemeSongs(const QString &seriesId);

    // Home card writes. Capabilities are read from the provider of
    // connectionId, which may be a merged member that is not live; the
    // writes run on the live connection, so callers route first. Each
    // write answers once with homeCardWriteFinished(requestKey).
    virtual HomeCardCapabilities homeCardCapabilities(const QString &connectionId) const;
    // Hides a card from (dismissed) or shows it again on a Home surface,
    // "continue_watching" or "next_up" (ProviderCapability::HomeDismissals).
    virtual void setHomeItemDismissed(const QString &requestKey,
                                      const QString &surface,
                                      const QString &itemId,
                                      const QString &seriesId,
                                      const QString &progressUpdatedAt,
                                      bool dismissed);
    // Zeroes the resume position without changing the played state.
    virtual void clearResumePosition(const QString &requestKey, const QString &itemId);

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
    // on and a favorite movie or series exists. Providers with
    // ProviderCapability::MovieRecommendations (Jellyfin) add their
    // personalised movie rows for each of movieLibraryIds (the movie
    // libraries Home shows) last, when
    // ConfigManager::homeRecommendationsEnabled is on (HomeRecommendations).
    // Only items in those libraries are kept. The recommendations are
    // reused for the same libraries for
    // HomeRecommendations::CacheLifetimeMs unless reloadRecommendations
    // asks to read them again; a library whose read fails shows what it
    // answered last. Providers without ProviderCapability::HomeSections
    // answer no other rows. A failed read leaves the others; only when
    // every read fails and no row is left does it fail. A newer call
    // supersedes an older one.
    Q_INVOKABLE virtual void getHomeSections(const QStringList &movieLibraryIds = {},
                                             bool reloadRecommendations = false);
    // Whether Home can show the Favorites row for the active provider (it
    // has no watchlist), whatever the setting.
    Q_INVOKABLE virtual bool supportsHomeFavoritesRow() const;

    // The rows the profile curates for one library, for providers with
    // ProviderCapability::LibrarySections (Silo), in the server's order:
    // each {sectionId, title, sectionType, featured, items, totalCount,
    // hasMore} with canonical movie and series items and no empty rows.
    // Rows carry at most 20 cards (or the server's smaller cap) when the
    // Home capabilities report section_item_limit_max. Other providers
    // answer no rows. A newer call supersedes an older one.
    Q_INVOKABLE virtual bool supportsLibrarySections() const;
    Q_INVOKABLE virtual void getLibrarySections(const QString &libraryId);
    // Every card of one library row (canonicalLibrarySectionItemsLoaded).
    Q_INVOKABLE virtual void getLibrarySectionItems(const QString &libraryId,
                                                    const QString &sectionId);

    // Where each letter starts in the title-ordered list of a library query
    // (its filters and order; offset and limit are ignored): a LetterIndex
    // map (`total`, `letters`, `runs`) in libraryLettersLoaded under the
    // query's requestKey. Silo reads queryCatalogLetters when its catalog
    // search capabilities report letter_index (read once per connection; a
    // failed read is not repeated for five minutes); Jellyfin builds the
    // index from the list's sort names. Other sorts, a Silo search, and a
    // server without the index fail with libraryLettersFailed.
    virtual void getLibraryLetters(const LibraryItemQuery &query);
    // Whether Home can show recommendation rows for the active provider,
    // whatever the setting.
    Q_INVOKABLE virtual bool supportsHomeRecommendations() const;
    
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
    // markItemPlayed/markItemUnplayed and the favorite writes were refused
    // or never reached the server.
    void itemPlayedStatusFailed(const QString &itemId, bool isPlayed, const QString &error);
    void favoriteStatusFailed(const QString &itemId, bool isFavorite, const QString &error);
    void homeCardWriteFinished(const QString &connectionId,
                               const QString &requestKey,
                               bool succeeded,
                               const QString &error);
    void canonicalHomeSectionsLoaded(const QString &connectionId, const QVariantList &sections);
    void canonicalHomeSectionsFailed(const QString &connectionId, const QString &error);
    void canonicalLibrarySectionsLoaded(const QString &connectionId,
                                        const QString &libraryId,
                                        const QVariantList &sections);
    void canonicalLibrarySectionsFailed(const QString &connectionId,
                                        const QString &libraryId,
                                        const QString &error);
    void canonicalLibrarySectionItemsLoaded(const QString &connectionId,
                                            const QString &libraryId,
                                            const QString &sectionId,
                                            const QVariantList &items);
    void canonicalLibrarySectionItemsFailed(const QString &connectionId,
                                            const QString &libraryId,
                                            const QString &sectionId,
                                            const QString &error);
    void libraryLettersLoaded(const QString &connectionId,
                              const QString &requestKey,
                              const QVariantMap &letterIndex);
    void libraryLettersFailed(const QString &connectionId,
                              const QString &requestKey,
                              const QString &error);
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
        CatalogNotModifiedHandler notModifiedHandler = CatalogNotModifiedHandler(),
        std::optional<RetryPolicy> retryPolicy = std::nullopt,
        int attemptTimeoutMs = 0);
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

    // Calls proceed with the largest per-row card cap the connection's
    // section reads accept (0: none), reading the Home capabilities first
    // when they are not known yet. proceed runs only while isCurrent holds.
    void withSectionItemLimit(const QString &connectionId,
                              std::function<bool()> isCurrent,
                              std::function<void(int)> proceed);
    // What getHomeSections was asked for Bloom's own rows.
    struct HomeRowsRequest {
        QStringList movieLibraryIds;
        bool reloadRecommendations = false;
    };
    // The profile's Home rows, each capped at itemLimitMax cards when it
    // is positive.
    void readHomeSections(const QString &connectionId, quint64 generation, int itemLimitMax,
                          const HomeRowsRequest &request);
    void readLibrarySections(const QString &connectionId, const QString &libraryId,
                             quint64 generation, int itemLimitMax);
    // The provider query for a library query, without paging cursors.
    ProviderCatalogQuery providerItemsQuery(const LibraryItemQuery &query) const;
    void readLibraryLetters(const QString &connectionId,
                            const ProviderCatalogQuery &providerQuery,
                            const QString &requestKey);
    // Adds Bloom's own rows to rows and publishes them: the Favorites row
    // ahead and the recommendation rows of the request's movie libraries
    // after, when Home shows them for this provider.
    void publishHomeSectionsWithBloomRows(const QString &connectionId,
                                          quint64 generation,
                                          const QVariantList &rows,
                                          const HomeRowsRequest &request);
    // Reads the recommendations of libraryIds (and the server version,
    // once per connection), keeps the items in those libraries, stores
    // them under cacheKey and reports whether any library answered.
    void readHomeRecommendations(const QString &connectionId,
                                 quint64 generation,
                                 const QString &cacheKey,
                                 const QString &librarySet,
                                 const QStringList &libraryIds,
                                 std::function<void(bool, const QString &)> done);
    // The stored recommendation rows for libraryIds, in their order.
    QVariantList homeRecommendationRows(const QString &cacheKey,
                                        const QStringList &libraryIds) const;

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
    /// The last recommendation read per connection and user: the shown
    /// movie libraries it was for (sorted IDs), each library's categories
    /// (kept when a later read of that library fails), the items found in
    /// the shown libraries, whether "similar to" rows are real, and when
    /// it was read (0: read again next time).
    struct HomeRecommendationCache {
        QString librarySet;
        QHash<QString, QList<HomeRecommendations::Category>> categoriesByLibrary;
        QSet<QString> allowedItemIds;
        bool similarRowsWork = false;
        qint64 fetchedAtMs = 0;
    };
    QHash<QString, HomeRecommendationCache> m_homeRecommendations;
    /// Jellyfin PublicSystemInfo.Version per connection, once read.
    QHash<QString, QString> m_serverVersions;
    // Not interested capability per connection and profile, and the reads
    // in flight with the request generation they were sent in. A read from
    // an older generation was cancelled and no longer blocks a new one.
    QHash<QString, bool> m_notInterestedAllowed;
    QHash<QString, quint64> m_notInterestedCapabilityReads;
    // Parsed airing calendars per connection, profile, local day and zone,
    // and the reads in flight with the series waiting on them.
    struct AiringCalendar {
        QVariantList airings;
        QDateTime fetchedAt;
    };
    struct AiringCalendarRead {
        quint64 generation = 0;
        QStringList seriesIds;
    };
    QHash<QString, AiringCalendar> m_airingCalendars;
    QHash<QString, AiringCalendarRead> m_airingCalendarReads;
    void readAiringCalendar(const QString &key, const QDate &start,
                            const QString &timeZoneId, bool zoneFallbackAllowed);
    void finishAiringCalendarRead(const QString &key, quint64 generation,
                                  const QVariantList &airings, bool cache);
    quint64 m_librarySectionsGeneration = 0;
    /// The catalog search capability document of one connection and
    /// profile (Silo), read once for scoped people search and the letter
    /// index. Reads asked for while one is in flight wait for it; a failed
    /// read is not repeated until retryAfter. Cleared with the session.
    struct SearchCapabilities {
        bool known = false;
        bool peopleMediaScope = false;
        bool letterIndex = false;
        bool failed = false;
        QDeadlineTimer retryAfter;
        /// A read is in flight, sent in request generation readGeneration.
        bool reading = false;
        quint64 readGeneration = 0;
        /// Called with the entry once the read in flight answers, and with
        /// the failure when it fails (null otherwise).
        QList<std::function<void(const SearchCapabilities &, const NetworkError *)>> waiters;
    };
    using SearchCapabilitiesHandler =
        std::function<void(const SearchCapabilities &, const NetworkError *)>;
    QHash<QString, SearchCapabilities> m_searchCapabilities;
    static constexpr int SearchCapabilityRetryMs = 5 * 60 * 1000;
    /// Answers @p handler with the active connection's search capabilities:
    /// at once when known or a failed read is still backed off, else after
    /// one shared read.
    void withSearchCapabilities(SearchCapabilitiesHandler handler);
    QString searchCapabilitiesKey() const;
    /// Above this many titles a provider that builds the letter index from
    /// the whole list (Jellyfin) gets no index; the rail shows the loaded
    /// letters instead.
    static constexpr int LetterIndexMaxTitles = 5000;
    /// The whole-list letters read is large: one attempt, a longer deadline.
    static constexpr int LetterIndexAttemptTimeoutMs = 60000;
    // Newest search; a replaced search (new term or library exclusions)
    // must not publish results after it.
    quint64 m_searchGeneration = 0;
    // Newest people search, and per connection whether people search can be
    // limited to movie and series credits (providers that read it).
    quint64 m_peopleSearchGeneration = 0;
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
