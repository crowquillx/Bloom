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
    QStringList itemIds;
    QStringList parentIds;
    QStringList seriesIds;

    int startIndex = 0;
    int limit = 0;
    std::optional<QString> snapshot;
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
