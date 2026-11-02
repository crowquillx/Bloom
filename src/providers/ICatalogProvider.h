#pragma once

#include <optional>
#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

enum class ProviderCatalogOperation {
    Views,
    Items,
    FilterOptions,
    NextUp,
    // Movies and episodes the viewer stopped part-way through, most recently
    // played first (Jellyfin). Providers that fold them into Next Up, such as
    // Silo's Home layout, report this unsupported.
    ResumeItems,
    LatestMedia,
    HomeBackdrops,
    ScreensaverItems,
    Item,
    Chapters,
    ResolveLibrary,
    SimilarItems,
    NextUnplayedEpisode,
    SetWatched,
    SetFavorite,
    Search,
    RandomItems,
    HeroItems,
    HeroOverviews,
    Versions,
    ThemeSongs,
    ThemeSongsCapability,
    ThemeSongPlayback,
    // The viewer's poster badge preferences and the server-wide defaults.
    PosterBadgeSettings,
    PosterBadgeDefaults,
    // The profile's own Home rows (Silo). rawItems holds one object per row
    // with `id`, `title`, `section_type` and wire `items`, in layout order.
    HomeSections,
    // The Home layout's capability document (Silo). capabilityMetadata
    // carries `sectionItemLimitMax`, the largest per-row card cap a
    // HomeSections read accepts, when the server reports one.
    HomeCapabilities,
    // The profile's watchlist, and adding or removing one item.
    Watchlist,
    SetWatchlist,
    // Not interested (Silo): the profile's capability document, and marking
    // or unmarking one movie or series (stateValue marks).
    NotInterestedCapability,
    SetNotInterested,
    // Upcoming episode airings: on Silo the viewer's local days from
    // calendarStart through 30 days later, narrowed by calendarFilter; on
    // Jellyfin the episodes premiering from yesterday on, one page
    // (startIndex, limit; no window, no filter). rawItems holds one wire
    // airing per entry; the adapter maps them (IProviderAdapter::mapAirings).
    // Providers that honor calendarFilter set capabilityMetadata
    // `filterable`; providers that page set `paged` (a full page may have
    // more after it).
    UpcomingAirings,
    // Server-side media requests (Silo): the viewer's request capability,
    // a title search, one title's request detail, submitting a request,
    // and the discovery rows. Responses carry Bloom-shaped maps in
    // capabilityMetadata, so request wire DTOs stay in the provider.
    MediaRequestStatus,
    MediaRequestSearch,
    MediaRequestDetail,
    CreateMediaRequest,
    MediaRequestDiscover,
    MediaRequestDiscoverSection,
    // The viewer's own requests, cancelling one (itemId), following a
    // requested title, and keeping a title the library lacks on the
    // watchlist (stateValue adds or removes).
    MediaRequestList,
    CancelMediaRequest,
    SetMediaRequestFollow,
    SetWatchlistTitle,
    // The genres, studios, or networks to browse (browseKind), and one
    // page of a browsed genre, studio, or network.
    MediaRequestBrowseCategories,
    MediaRequestBrowse,
    // A movie's or series' local extras (Jellyfin special features) and its
    // local trailers. rawItems holds the wire extras. Providers that return
    // extras inside the item detail (Silo) leave both unsupported.
    SpecialFeatures,
    LocalTrailers,
    // One person (itemId) with biography, life dates and photo; their
    // movies and series, newest first, one page at a time (startIndex or
    // cursor); and people whose name matches searchTerm. Responses carry
    // wire people in rawItem/rawItems; the adapter maps them
    // (IProviderAdapter::mapPerson). Whether people search can be limited
    // to movie and series credits the viewer may see comes from
    // CatalogSearchCapabilities.
    Person,
    PersonItems,
    PersonSearch,
    // The rows a profile curates for one library (parentId), in layout
    // order (Silo, ProviderCapability::LibrarySections). limit caps each
    // row's cards (item_limit). rawItems holds one object per row with `id`,
    // `title`, `section_type`, `featured`, `total_count`, `has_more` and
    // wire `items` reduced to movie and series cards; empty rows are gone.
    LibrarySections,
    // One library row's cards (parentId library, itemId section), as many
    // as the row holds; rawItems holds the movie and series cards.
    LibrarySectionItems,
    // The catalog search capability document (Silo), read once for every
    // feature it gates. capabilityMetadata carries `peopleMediaScope`
    // (PersonSearch can be limited to movie and series credits the viewer
    // may see) and `letterIndex` (LibraryLetters is available). Providers
    // whose people search is always scoped and whose letter index needs no
    // capability (Jellyfin) report this unsupported.
    CatalogSearchCapabilities,
    // Where each letter starts in a title-ordered library query with the
    // query's filters (the A-Z rail). capabilityMetadata carries the
    // LetterIndex map (`total`, `letters`, `runs`). Unsupported for other
    // sorts, and on Silo with a search term.
    LibraryLetters,
    // The number of titles LibraryLetters would index (total), read first
    // by providers that build the index from the whole list (Jellyfin), so
    // a large library is never downloaded. Silo answers the index itself
    // and reports this unsupported.
    LibraryLetterCount,
    // The viewer's personalised movie rows in one library (parentId), at
    // most `limit` rows of at most `recommendationItemLimit` cards
    // (Jellyfin). rawItems holds one object per row with
    // `recommendationType`, `baselineItemName`, `categoryId` and wire
    // `items`, in the server's order.
    MovieRecommendations,
    // Which of itemIds lie in the library parentId (Jellyfin). rawItems
    // holds one {"itemId"} object per item found.
    LibraryMembership,
    // The server's public identity (Jellyfin). capabilityMetadata holds
    // `version` and `productName`.
    ServerInfo,
    // Hides one Home card (stateValue true) or shows it again (false) on
    // one Home surface (homeSurface): "continue_watching" needs
    // progressUpdatedAt, "next_up" needs seriesId (Silo).
    SetHomeDismissal,
    // Zeroes the viewer's resume position for itemId without changing
    // its played state, so it leaves the resume list (Jellyfin).
    ClearResumePosition,
    // The subtitle appearance that applies to the acting profile on this
    // device, and writing (stateValue true, subtitleAppearance) or removing
    // (false) this device's override (Silo). capabilityMetadata carries the
    // canonical `appearance` map (only the fields the server's value sets),
    // `hasValue`, `hasDeviceOverride`, and an opaque `document` that
    // LibraryService keeps per scope and passes back on a later write as
    // subtitleAppearanceDocument, so fields Bloom does not map survive. The
    // document never leaves LibraryService.
    SubtitleAppearance,
    SetSubtitleAppearance,
    // Titles recommended because the viewer watched itemId, without what
    // they watched or rated since (Silo). rawItems holds wire catalog cards.
    // Other providers report this unsupported; callers then read
    // SimilarItems.
    BecauseWatched,
    // Sets the viewer's own rating of itemId to ratingValue, or clears it
    // with 0: 1 to 5 stars where ProviderCapability::StarRatings, +1 (like)
    // or -1 (dislike) where ProviderCapability::LikeRatings.
    SetUserRating,
    // One page of what the viewer watched, most recent first. Silo pages
    // its own history by cursor (a page may be short or empty while more
    // remain; nextCursor says so) and rolls episode watches up into series
    // cards. Jellyfin reads played movies and episodes of one library
    // (parentId) by DatePlayed, by startIndex
    // (ProviderCapability::LibraryScopedHistory). rawItems holds movie,
    // series and episode cards only.
    WatchHistory,
    // Hides itemId's watches from the viewer's history (Silo). A series
    // takes its episodes with it. Never replayed.
    RemoveFromWatchHistory,
    // Up to `limit` Watch Tonight taste picks for the viewer (Silo,
    // discover mode). rawItems holds movie, series and episode cards;
    // capabilityMetadata carries `isCold` (a generic fallback rather than
    // picks from the viewer's taste) and `hasMore`.
    WatchTonight,
    // Server shuffles (Silo, ProviderCapability::ServerShuffle): the
    // profile's capability document (capabilityMetadata `allowed` and
    // `scopeKinds`, canonical Bloom::ShuffleScope kinds), starting one over
    // shuffleScopeKind/shuffleCollectionKind and parentId, reading one
    // (shuffleId), moving it on after the item that finished
    // (shuffleContentId), replacing its announced next item
    // (shuffleContentId), and stopping it. Shuffle responses carry the wire
    // shuffle in rawItem with `current` and `next` cards.
    ShuffleCapability,
    CreateShuffle,
    GetShuffle,
    AdvanceShuffle,
    SkipShuffleItem,
    DeleteShuffle,
    // A random page of the movies and episodes under one scope (parentId,
    // shuffleScopeKind) for a shuffle Bloom keeps itself (Jellyfin).
    // rawItems holds the wire items, total the scope's size.
    ShuffleItems,
    // Collections, a browse view over movies and series
    // (ProviderCapability::Collections). LibraryCollections lists the
    // collections of one library (parentId), one page at a time
    // (startIndex, limit; a provider that answers the whole list sets no
    // hasMore). CollectionItems pages one collection's titles (itemId,
    // collectionKind) in canonical sortBy order (empty: the collection's
    // own order), by startIndex or cursor. ItemCollections lists the
    // collections that hold one title (itemId). Collection responses carry
    // wire collections in rawItems; the adapter maps them
    // (IProviderAdapter::mapCollections). CollectionItems carries wire
    // movie and series cards.
    LibraryCollections,
    CollectionItems,
    ItemCollections,
    // The image capability document (Silo getImageCapabilities), read once
    // per connection and profile by LibraryService. capabilityMetadata
    // carries `extendedItemArtwork`: movie and series details may carry
    // every backdrop and thumb, banner, clear art and disc images
    // (IProviderAdapter::mapMediaDetail). Providers whose details need no
    // capability for them (Jellyfin) report this unsupported.
    ImageCapabilities,
    // Taste seeding, the cold-start picker (Silo). TasteSeedFavorites asks
    // whether the acting profile has favorited anything (one favorite is
    // enough); capabilityMetadata carries `hasFavorites`. TasteSeedItems
    // reads one page of candidate titles, most recognizable first, by
    // cursor (limit at most 60); rawItems holds movie and series cards with
    // the profile's favorite state. SubmitTasteSeed records itemIds (1 to
    // 200) as favorites and queues a taste refresh; never replayed.
    // capabilityMetadata carries `added`, the picks newly recorded.
    TasteSeedFavorites,
    TasteSeedItems,
    SubmitTasteSeed,
};

enum class ProviderHttpMethod {
    Get,
    Post,
    Put,
    Delete
};

enum class ProviderCatalogRetrySafety {
    Never,
    Idempotent
};

// What a LibraryCollections read covers. Library: the collections that hold
// titles of query.parentId, as the server scopes them (Silo; Jellyfin
// API 12+). LibraryScan: one page of query.parentId's titles collapsed into
// their box sets, which the caller filters to box sets (Jellyfin before
// API 12, whose BoxSet query ignores ParentId). Ids: the collections listed
// in query.itemIds. Server: every collection on the server.
enum class ProviderCollectionScope {
    Library,
    LibraryScan,
    Ids,
    Server
};

enum class ProviderCatalogTriState {
    Any,
    Yes,
    No
};

struct ProviderCatalogQuery {
    QString userId;
    QString itemId;
    QString parentId;
    QString seriesId;
    QString excludeItemId;
    QString themeId;
    QStringList itemIds;
    QStringList parentIds;
    QStringList seriesIds;

    int startIndex = 0;
    int limit = 0;
    // MovieRecommendations: cards per row (limit counts the rows).
    int recommendationItemLimit = 0;
    std::optional<QString> snapshot;
    QString cursor;
    QString windowCursor;
    QString searchTerm;
    QStringList genres;
    QStringList tags;
    QStringList studios;
    QDate minPremiereDate;
    QDate maxPremiereDate;
    QDate minDateLastSaved;
    double minCommunityRating = 0.0;
    QList<int> years;
    // Library filters: a series lifecycle ("continuing" or "ended") and a
    // video resolution ("4k", or "hd" for HD and up); empty matches any.
    QString seriesStatus;
    QString resolution;
    QString sortBy;
    QString sortOrder;
    QStringList includeItemTypes;
    QStringList fields;
    QString filterFacet;

    // Next Up: also offer series the viewer finished and may start again,
    // and leave out series last watched before this day (invalid: no
    // cutoff). Honored by providers with ProviderCapability::NextUpOptions.
    bool nextUpRewatching = false;
    QDate nextUpCutoff;

    // Media requests: "movie" or "tv", the TMDB id, a 1-based provider page,
    // a discovery row key, the seasons to request (empty: the provider's
    // default), and the title's discovery map for the request body.
    QString requestMediaType;
    qint64 tmdbId = 0;
    int page = 0;
    QString sectionKey;
    QList<int> requestSeasons;
    QVariantMap requestMedia;
    // Browsing: "genre", "studio", or "network", the provider's id for it,
    // and "popularity", "vote_average", or "release_date".
    QString browseKind;
    QString browseId;
    QString browseSort;
    // Upcoming airings: the first local day of the window, the viewer's
    // IANA time zone the days are reckoned in, and the provider's preset
    // (Silo: "following", "trending", "everything", ...; empty for its
    // default, every title).
    QDate calendarStart;
    QString timeZoneId;
    QString calendarFilter;
    // Home dismissals: the surface ("continue_watching" or "next_up") and
    // the card's progress stamp, as the server sent it.
    QString homeSurface;
    QString progressUpdatedAt;
    // Subtitle appearance writes: the canonical appearance map and the
    // opaque document the last read answered.
    QVariantMap subtitleAppearance;
    QString subtitleAppearanceDocument;
    // Shuffles: the canonical scope kind and collection kind
    // (Bloom::ShuffleScope), the shuffle, and the card an advance or a skip
    // is anchored on.
    QString shuffleScopeKind;
    QString shuffleCollectionKind;
    QString shuffleId;
    QString shuffleContentId;
    // Collections: the canonical collectionKind of the collection itemId
    // names ("library" or "user" on Silo, "boxSet" on Jellyfin), and what
    // a LibraryCollections read covers.
    QString collectionKind;
    ProviderCollectionScope collectionScope = ProviderCollectionScope::Library;

    ProviderCatalogTriState watched = ProviderCatalogTriState::Any;
    ProviderCatalogTriState favorite = ProviderCatalogTriState::Any;
    bool recursive = false;
    bool includeHeavyFields = true;
    bool unwatchedOnly = false;
    bool stateValue = false;
    // SetUserRating: the rating to set, or 0 to clear it.
    int ratingValue = 0;

    bool useCacheValidation = false;
    QByteArray etag;
    QByteArray lastModified;
};

struct ProviderCatalogRequest {
    ProviderHttpMethod method = ProviderHttpMethod::Get;
    ProviderCatalogRetrySafety retrySafety = ProviderCatalogRetrySafety::Never;
    QString relativeEndpoint;
    QByteArray body;
    QHash<QByteArray, QByteArray> extraHeaders;
    bool supported = false;
    QString unsupportedReason;
};

struct ProviderCatalogResponse {
    bool valid = false;
    QString error;
    QJsonArray rawItems;
    QJsonObject rawItem;
    int total = 0;
    bool hasMore = false;
    QString nextCursor;
    QString windowCursor;
    QVariantMap snapshot;
    QVariantMap capabilityMetadata;
    QVariantMap filterMetadata;
};

// The library-browse sorts and filters a provider serves, in canonical
// names. Library order (an empty sort) is always available. Views hide the
// rest when a provider leaves them out and drop a saved choice that the
// active connection cannot serve.
struct ProviderLibraryQueryCapabilities {
    // Canonical sortBy values beyond library order. datePlayed is the
    // viewer's last watch, runtime the title's length, and recentlyUpdated
    // when a series last gained an episode.
    QStringList sorts{QStringLiteral("title"), QStringLiteral("releaseDate"),
                      QStringLiteral("dateAdded"), QStringLiteral("rating"),
                      QStringLiteral("year"), QStringLiteral("random"),
                      QStringLiteral("datePlayed"), QStringLiteral("runtime"),
                      QStringLiteral("recentlyUpdated")};
    // Canonical filters: genres, tags, studios, watched, favorite,
    // addedSince (minDateLastSaved), years, rating (minCommunityRating),
    // seriesStatus, and resolution.
    QStringList filters{QStringLiteral("genres"), QStringLiteral("tags"),
                        QStringLiteral("studios"), QStringLiteral("watched"),
                        QStringLiteral("favorite"), QStringLiteral("addedSince"),
                        QStringLiteral("years"), QStringLiteral("rating"),
                        QStringLiteral("seriesStatus"), QStringLiteral("resolution")};
    // Sorts in sorts that cannot order series rows, so a series library
    // leaves them out (Silo dates a last watch on the episode, not its show).
    QStringList seriesExcludedSorts;

    bool operator==(const ProviderLibraryQueryCapabilities &) const = default;
};

class ICatalogProvider
{
public:
    virtual ~ICatalogProvider() = default;

    virtual ProviderLibraryQueryCapabilities libraryQueryCapabilities() const
    {
        return {};
    }

    virtual ProviderCatalogRequest createRequest(
        ProviderCatalogOperation operation,
        const ProviderCatalogQuery &query) const = 0;
    virtual ProviderCatalogResponse parseResponse(
        ProviderCatalogOperation operation,
        const QByteArray &body,
        const QHash<QByteArray, QByteArray> &responseHeaders) const = 0;

    ProviderCatalogResponse parseResponse(
        ProviderCatalogOperation operation, const QByteArray &body) const
    {
        return parseResponse(operation, body, {});
    }
};
