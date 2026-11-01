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
    // Upcoming airings in the viewer's local days from calendarStart
    // (Silo). capabilityMetadata carries canonical `airings` maps.
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
    // Upcoming airings: the first local day of the window and the viewer's
    // IANA time zone the days are reckoned in.
    QDate calendarStart;
    QString timeZoneId;
    // Home dismissals: the surface ("continue_watching" or "next_up") and
    // the card's progress stamp, as the server sent it.
    QString homeSurface;
    QString progressUpdatedAt;

    ProviderCatalogTriState watched = ProviderCatalogTriState::Any;
    ProviderCatalogTriState favorite = ProviderCatalogTriState::Any;
    bool recursive = false;
    bool includeHeavyFields = true;
    bool unwatchedOnly = false;
    bool stateValue = false;

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
    // Canonical sortBy values beyond library order.
    QStringList sorts{QStringLiteral("title"), QStringLiteral("releaseDate"),
                      QStringLiteral("dateAdded"), QStringLiteral("rating"),
                      QStringLiteral("year"), QStringLiteral("random")};
    // Canonical filters: genres, tags, studios, watched, favorite,
    // addedSince (minDateLastSaved), years, and rating
    // (minCommunityRating).
    QStringList filters{QStringLiteral("genres"), QStringLiteral("tags"),
                        QStringLiteral("studios"), QStringLiteral("watched"),
                        QStringLiteral("favorite"), QStringLiteral("addedSince"),
                        QStringLiteral("years"), QStringLiteral("rating")};

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
