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

class ICatalogProvider
{
public:
    virtual ~ICatalogProvider() = default;

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
