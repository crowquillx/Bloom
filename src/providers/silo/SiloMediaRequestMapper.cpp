#include "SiloMediaRequestMapper.h"

#include <QJsonDocument>
#include <QSet>

namespace {

// Silo returns TMDB image paths; the TMDB CDN serves them at fixed widths.
QString tmdbImageUrl(const QString &path, const char *size)
{
    if (path.isEmpty()) {
        return {};
    }
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"))) {
        return path;
    }
    return QStringLiteral("https://image.tmdb.org/t/p/%1%2").arg(QLatin1String(size), path);
}

// Silo's "series" is Bloom's request type "tv", the name Seerr uses.
QString bloomMediaType(const QString &wireMediaType)
{
    if (wireMediaType == QStringLiteral("movie")) {
        return QStringLiteral("movie");
    }
    if (wireMediaType == QStringLiteral("series")) {
        return QStringLiteral("tv");
    }
    return {};
}

int yearOf(const QJsonObject &wire)
{
    const int year = wire.value(QStringLiteral("year")).toInt();
    if (year > 0) {
        return year;
    }
    for (const QString &key : {QStringLiteral("release_date"), QStringLiteral("first_air_date")}) {
        bool ok = false;
        const int dateYear = wire.value(key).toString().left(4).toInt(&ok);
        if (ok && dateYear > 0) {
            return dateYear;
        }
    }
    return 0;
}

// One status token for cards: "available" once the library has the title,
// otherwise the active request's user-facing state (pending, approved,
// processing), or empty when nobody has requested it.
QString requestStatus(const QJsonObject &wire, const QJsonObject &requestState)
{
    if (wire.value(QStringLiteral("availability")).toString() == QStringLiteral("available")) {
        return QStringLiteral("available");
    }
    return requestState.value(QStringLiteral("state")).toString();
}

// Silo's user-facing request state as Bloom's status token.
QString statusToken(const QString &state)
{
    return state == QStringLiteral("partially_available") ? QStringLiteral("partial") : state;
}

QVariantMap season(const QJsonObject &wireSeason)
{
    return {
        {QStringLiteral("seasonNumber"), wireSeason.value(QStringLiteral("season_number")).toInt()},
        {QStringLiteral("name"), wireSeason.value(QStringLiteral("name")).toString()},
        {QStringLiteral("episodeCount"), wireSeason.value(QStringLiteral("episode_count")).toInt()},
        {QStringLiteral("availability"), wireSeason.value(QStringLiteral("availability")).toString()},
        {QStringLiteral("requested"), wireSeason.value(QStringLiteral("requested")).toBool()},
    };
}

QJsonObject objectBody(const QByteArray &body, bool *ok)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    *ok = document.isObject();
    return document.object();
}

} // namespace

bool SiloMediaRequestMapper::isDiscoverSectionKey(const QString &key)
{
    static const QSet<QString> kKeys{
        QStringLiteral("trending_movies"), QStringLiteral("trending_series"),
        QStringLiteral("popular_movies"),  QStringLiteral("popular_series"),
        QStringLiteral("upcoming_movies"), QStringLiteral("on_air_series"),
    };
    return kKeys.contains(key);
}

QString SiloMediaRequestMapper::browseListName(const QString &browseKind)
{
    if (browseKind == QStringLiteral("genre")) return QStringLiteral("genres");
    if (browseKind == QStringLiteral("studio")) return QStringLiteral("studios");
    if (browseKind == QStringLiteral("network")) return QStringLiteral("networks");
    return {};
}

bool SiloMediaRequestMapper::isBrowseSort(const QString &sort)
{
    return sort == QStringLiteral("popularity") || sort == QStringLiteral("vote_average")
        || sort == QStringLiteral("release_date");
}

QString SiloMediaRequestMapper::wireMediaType(const QString &requestMediaType)
{
    const QString normalized = requestMediaType.trimmed().toLower();
    if (normalized == QStringLiteral("movie")) {
        return QStringLiteral("movie");
    }
    if (normalized == QStringLiteral("tv")) {
        return QStringLiteral("series");
    }
    return {};
}

QJsonObject SiloMediaRequestMapper::createBody(const QString &requestMediaType,
                                               qint64 tmdbId,
                                               const QVariantMap &media,
                                               const QList<int> &seasons)
{
    const QString mediaType = wireMediaType(requestMediaType);
    const QString title = media.value(QStringLiteral("name")).toString().trimmed();
    if (mediaType.isEmpty() || tmdbId <= 0 || title.isEmpty()) {
        return {};
    }

    QJsonObject body{
        {QStringLiteral("media_type"), mediaType},
        {QStringLiteral("tmdb_id"), tmdbId},
        {QStringLiteral("title"), title},
    };
    const int year = media.value(QStringLiteral("productionYear")).toInt();
    if (year > 0) {
        body.insert(QStringLiteral("year"), year);
    }
    const auto addString = [&body, &media](const char *wireKey, const char *bloomKey) {
        const QString value = media.value(QLatin1String(bloomKey)).toString();
        if (!value.isEmpty()) {
            body.insert(QLatin1String(wireKey), value);
        }
    };
    addString("overview", "overview");
    addString("poster_path", "posterPath");
    addString("backdrop_path", "backdropPath");

    if (mediaType == QStringLiteral("series")) {
        QJsonArray seasonNumbers;
        QSet<int> seen;
        for (const int seasonNumber : seasons) {
            // Silo refuses seasons below 1 and caps the list at 200.
            if (seasonNumber >= 1 && !seen.contains(seasonNumber) && seasonNumbers.size() < 200) {
                seen.insert(seasonNumber);
                seasonNumbers.append(seasonNumber);
            }
        }
        if (!seasonNumbers.isEmpty()) {
            body.insert(QStringLiteral("seasons"), seasonNumbers);
        }
    }
    return body;
}

QVariantMap SiloMediaRequestMapper::features(const QJsonObject &wireStatus)
{
    const bool enabled = wireStatus.value(QStringLiteral("requests_enabled")).toBool();
    const bool allowed = wireStatus.value(QStringLiteral("allowed")).toBool();
    const QString state = wireStatus.value(QStringLiteral("state")).toString();
    return {
        {QStringLiteral("available"),
         enabled && allowed && state == QStringLiteral("available")},
        {QStringLiteral("requestsEnabled"), enabled},
        {QStringLiteral("allowed"), allowed},
        {QStringLiteral("state"), state},
        {QStringLiteral("seasonRequestsSupported"),
         wireStatus.value(QStringLiteral("season_requests_supported")).toBool()},
        {QStringLiteral("followSupported"),
         wireStatus.value(QStringLiteral("follow_supported")).toBool()},
        {QStringLiteral("watchlistTitlesSupported"),
         wireStatus.value(QStringLiteral("watchlist_titles_supported")).toBool()},
        // Adding a missing title to the watchlist also requests it.
        {QStringLiteral("watchlistRequests"),
         wireStatus.value(QStringLiteral("watchlist_requests")).toBool()},
    };
}

QVariantMap SiloMediaRequestMapper::result(const QJsonObject &wireResult)
{
    const QString mediaType = bloomMediaType(wireResult.value(QStringLiteral("media_type")).toString());
    const qint64 tmdbId = wireResult.value(QStringLiteral("tmdb_id")).toInteger();
    if (mediaType.isEmpty() || tmdbId <= 0) {
        return {};
    }

    const QJsonObject requestState = wireResult.value(QStringLiteral("request")).toObject();
    const QString posterPath = wireResult.value(QStringLiteral("poster_path")).toString();
    const QString backdropPath = wireResult.value(QStringLiteral("backdrop_path")).toString();
    const bool available = wireResult.value(QStringLiteral("availability")).toString()
        == QStringLiteral("available");

    QVariantMap mapped{
        {QStringLiteral("source"), QStringLiteral("request")},
        {QStringLiteral("requestBackend"), QStringLiteral("silo")},
        {QStringLiteral("itemId"), QStringLiteral("request:%1:%2").arg(mediaType).arg(tmdbId)},
        {QStringLiteral("requestMediaType"), mediaType},
        {QStringLiteral("requestTmdbId"), tmdbId},
        {QStringLiteral("mediaType"),
         mediaType == QStringLiteral("movie") ? QStringLiteral("Movie") : QStringLiteral("Series")},
        {QStringLiteral("name"), wireResult.value(QStringLiteral("title")).toString()},
        {QStringLiteral("overview"), wireResult.value(QStringLiteral("overview")).toString()},
        {QStringLiteral("posterPath"), posterPath},
        {QStringLiteral("backdropPath"), backdropPath},
        {QStringLiteral("imageUrl"), tmdbImageUrl(posterPath, "w342")},
        {QStringLiteral("backdropUrl"), tmdbImageUrl(backdropPath, "w1280")},
        {QStringLiteral("requestStatus"), requestStatus(wireResult, requestState)},
        {QStringLiteral("requestable"), requestState.value(QStringLiteral("requestable")).toBool()},
    };
    const int year = yearOf(wireResult);
    if (year > 0) {
        mapped.insert(QStringLiteral("productionYear"), year);
    }
    const QString libraryItemId = wireResult.value(QStringLiteral("library_content_id")).toString();
    if (available && !libraryItemId.isEmpty()) {
        mapped.insert(QStringLiteral("libraryItemId"), libraryItemId);
    }
    return mapped;
}

QVariantList SiloMediaRequestMapper::results(const QJsonArray &wireResults)
{
    QVariantList mapped;
    QSet<QString> seen;
    for (const QJsonValue &value : wireResults) {
        const QVariantMap item = result(value.toObject());
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty() || seen.contains(itemId)) {
            continue;
        }
        seen.insert(itemId);
        mapped.append(item);
    }
    return mapped;
}

QVariantMap SiloMediaRequestMapper::detail(const QJsonObject &wireDetail)
{
    QVariantMap mapped = result(wireDetail);
    if (mapped.isEmpty()) {
        return {};
    }

    const QJsonObject requestState = wireDetail.value(QStringLiteral("request")).toObject();
    mapped.insert(QStringLiteral("notRequestableReason"),
                  requestState.value(QStringLiteral("reason")).toString());
    mapped.insert(QStringLiteral("requestState"),
                  requestState.value(QStringLiteral("state")).toString());
    mapped.insert(QStringLiteral("requestedByViewer"),
                  requestState.value(QStringLiteral("requested_by_viewer")).toBool());
    mapped.insert(QStringLiteral("following"),
                  requestState.value(QStringLiteral("following")).toBool());
    mapped.insert(QStringLiteral("activeRequestId"),
                  requestState.value(QStringLiteral("request_id")).toString());
    mapped.insert(QStringLiteral("inWatchlist"),
                  wireDetail.value(QStringLiteral("in_watchlist")).toBool());

    QVariantList seasons;
    for (const QJsonValue &value : wireDetail.value(QStringLiteral("seasons")).toArray()) {
        const QVariantMap entry = season(value.toObject());
        if (entry.value(QStringLiteral("seasonNumber")).toInt() >= 1) {
            seasons.append(entry);
        }
    }
    mapped.insert(QStringLiteral("seasons"), seasons);
    mapped.insert(QStringLiteral("seasonCount"),
                  qMax(wireDetail.value(QStringLiteral("number_of_seasons")).toInt(),
                       int(seasons.size())));
    mapped.insert(QStringLiteral("recommendations"),
                  results(wireDetail.value(QStringLiteral("recommendations")).toArray()));
    return mapped;
}

QVariantMap SiloMediaRequestMapper::createdRequest(const QJsonObject &wireRequest)
{
    return {
        {QStringLiteral("requestId"), wireRequest.value(QStringLiteral("id")).toString()},
        {QStringLiteral("requestState"), wireRequest.value(QStringLiteral("state")).toString()},
    };
}

QVariantMap SiloMediaRequestMapper::request(const QJsonObject &wireRequest)
{
    const QString mediaType = bloomMediaType(wireRequest.value(QStringLiteral("media_type")).toString());
    const qint64 tmdbId = wireRequest.value(QStringLiteral("tmdb_id")).toInteger();
    const QString requestId = wireRequest.value(QStringLiteral("id")).toString();
    if (mediaType.isEmpty() || tmdbId <= 0 || requestId.isEmpty()) {
        return {};
    }

    const QString state = wireRequest.value(QStringLiteral("state")).toString();
    const QString outcome = wireRequest.value(QStringLiteral("outcome")).toString();
    const QString posterPath = wireRequest.value(QStringLiteral("poster_path")).toString();
    const QString backdropPath = wireRequest.value(QStringLiteral("backdrop_path")).toString();
    QVariantList seasons;
    for (const QJsonValue &value : wireRequest.value(QStringLiteral("seasons")).toArray()) {
        seasons.append(value.toInt());
    }

    QVariantMap mapped{
        {QStringLiteral("source"), QStringLiteral("request")},
        {QStringLiteral("requestBackend"), QStringLiteral("silo")},
        {QStringLiteral("itemId"), QStringLiteral("request:%1:%2").arg(mediaType).arg(tmdbId)},
        {QStringLiteral("requestMediaType"), mediaType},
        {QStringLiteral("requestTmdbId"), tmdbId},
        {QStringLiteral("mediaType"),
         mediaType == QStringLiteral("movie") ? QStringLiteral("Movie") : QStringLiteral("Series")},
        {QStringLiteral("name"), wireRequest.value(QStringLiteral("title")).toString()},
        {QStringLiteral("overview"), wireRequest.value(QStringLiteral("overview")).toString()},
        {QStringLiteral("posterPath"), posterPath},
        {QStringLiteral("backdropPath"), backdropPath},
        {QStringLiteral("imageUrl"), tmdbImageUrl(posterPath, "w342")},
        {QStringLiteral("backdropUrl"), tmdbImageUrl(backdropPath, "w1280")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("requestState"), state},
        {QStringLiteral("requestStatus"), statusToken(state)},
        {QStringLiteral("requestedSeasons"), seasons},
        {QStringLiteral("createdAt"), wireRequest.value(QStringLiteral("created_at")).toString()},
        // Silo decides; Bloom offers cancelling before downloads start.
        {QStringLiteral("canCancel"),
         outcome == QStringLiteral("active")
             && (state == QStringLiteral("pending") || state == QStringLiteral("approved"))},
        {QStringLiteral("requestable"), false},
    };
    const int year = wireRequest.value(QStringLiteral("year")).toInt();
    if (year > 0) {
        mapped.insert(QStringLiteral("productionYear"), year);
    }
    const QString libraryItemId = wireRequest.value(QStringLiteral("library_content_id")).toString();
    if (!libraryItemId.isEmpty()) {
        mapped.insert(QStringLiteral("libraryItemId"), libraryItemId);
    }
    return mapped;
}

QVariantMap SiloMediaRequestMapper::browseCategory(const QJsonObject &wireBrand)
{
    const QString slug = wireBrand.value(QStringLiteral("slug")).toString();
    const QString name = wireBrand.value(QStringLiteral("display_name")).toString();
    if (slug.isEmpty() || name.isEmpty()) {
        return {};
    }
    return {
        {QStringLiteral("id"), slug},
        {QStringLiteral("name"), name},
        {QStringLiteral("logoUrl"), wireBrand.value(QStringLiteral("logo_url")).toString()},
        {QStringLiteral("seriesSupported"), wireBrand.value(QStringLiteral("series_supported")).toBool()},
    };
}

QVariantMap SiloMediaRequestMapper::discoverRow(const QJsonObject &wireSection)
{
    const int page = qMax(1, wireSection.value(QStringLiteral("page")).toInt(1));
    const int totalPages = wireSection.value(QStringLiteral("total_pages")).toInt();
    // Rating backfill can consume several provider pages; next_page then
    // says where to continue.
    const int nextPage = wireSection.value(QStringLiteral("next_page")).toInt(page + 1);
    return {
        {QStringLiteral("key"), wireSection.value(QStringLiteral("key")).toString()},
        {QStringLiteral("title"), wireSection.value(QStringLiteral("title")).toString()},
        {QStringLiteral("items"), results(wireSection.value(QStringLiteral("results")).toArray())},
        {QStringLiteral("page"), page},
        {QStringLiteral("nextPage"), nextPage},
        {QStringLiteral("hasMore"), nextPage <= totalPages},
    };
}

ProviderCatalogResponse SiloMediaRequestMapper::parseResponse(ProviderCatalogOperation operation,
                                                              const QByteArray &body)
{
    ProviderCatalogResponse response;
    // Follow and watchlist writes answer 204 when removing, or a state
    // document Bloom does not need when adding.
    if (operation == ProviderCatalogOperation::SetMediaRequestFollow
        || operation == ProviderCatalogOperation::SetWatchlistTitle) {
        response.valid = true;
        return response;
    }
    bool ok = false;
    const QJsonObject object = objectBody(body, &ok);
    if (!ok) {
        response.error = QStringLiteral("Invalid Silo request response");
        return response;
    }

    switch (operation) {
    case ProviderCatalogOperation::MediaRequestStatus:
        response.valid = object.value(QStringLiteral("requests_enabled")).isBool()
            && object.value(QStringLiteral("allowed")).isBool()
            && object.value(QStringLiteral("state")).isString();
        response.capabilityMetadata = features(object);
        break;
    case ProviderCatalogOperation::MediaRequestSearch:
        response.valid = object.value(QStringLiteral("results")).isArray();
        response.capabilityMetadata = {
            {QStringLiteral("items"), results(object.value(QStringLiteral("results")).toArray())},
            {QStringLiteral("page"), object.value(QStringLiteral("page")).toInt(1)},
            {QStringLiteral("totalPages"), object.value(QStringLiteral("total_pages")).toInt()},
        };
        break;
    case ProviderCatalogOperation::MediaRequestDetail: {
        const QVariantMap mapped = detail(object);
        response.valid = !mapped.isEmpty();
        response.capabilityMetadata.insert(QStringLiteral("detail"), mapped);
        break;
    }
    case ProviderCatalogOperation::CreateMediaRequest:
        response.valid = !object.value(QStringLiteral("id")).toString().isEmpty();
        response.capabilityMetadata.insert(QStringLiteral("request"), createdRequest(object));
        break;
    case ProviderCatalogOperation::MediaRequestDiscover: {
        QVariantList rows;
        for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray()) {
            const QVariantMap row = discoverRow(value.toObject());
            if (!row.value(QStringLiteral("items")).toList().isEmpty()) {
                rows.append(row);
            }
        }
        response.valid = object.value(QStringLiteral("items")).isArray();
        response.capabilityMetadata.insert(QStringLiteral("rows"), rows);
        break;
    }
    case ProviderCatalogOperation::MediaRequestDiscoverSection:
        response.valid = object.value(QStringLiteral("results")).isArray()
            && !object.value(QStringLiteral("key")).toString().isEmpty();
        response.capabilityMetadata.insert(QStringLiteral("row"), discoverRow(object));
        break;
    case ProviderCatalogOperation::MediaRequestList: {
        QVariantList items;
        for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray()) {
            const QVariantMap mapped = request(value.toObject());
            if (!mapped.isEmpty()) {
                items.append(mapped);
            }
        }
        const QJsonObject page = object.value(QStringLiteral("page")).toObject();
        response.valid = object.value(QStringLiteral("items")).isArray();
        response.capabilityMetadata = {
            {QStringLiteral("items"), items},
            {QStringLiteral("hasMore"), page.value(QStringLiteral("has_more")).toBool()},
            {QStringLiteral("nextCursor"), page.value(QStringLiteral("next_cursor")).toString()},
        };
        break;
    }
    case ProviderCatalogOperation::CancelMediaRequest: {
        const QVariantMap mapped = request(object);
        response.valid = !mapped.isEmpty();
        response.capabilityMetadata.insert(QStringLiteral("request"), mapped);
        break;
    }
    case ProviderCatalogOperation::MediaRequestBrowseCategories: {
        QVariantList items;
        for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray()) {
            const QVariantMap mapped = browseCategory(value.toObject());
            if (!mapped.isEmpty()) {
                items.append(mapped);
            }
        }
        response.valid = object.value(QStringLiteral("items")).isArray();
        response.capabilityMetadata.insert(QStringLiteral("items"), items);
        break;
    }
    case ProviderCatalogOperation::MediaRequestBrowse: {
        QJsonObject section = object;
        // Browse pages say nothing about a next page beyond total_pages.
        section.remove(QStringLiteral("next_page"));
        section.insert(QStringLiteral("key"), object.value(QStringLiteral("slug")));
        section.insert(QStringLiteral("title"), object.value(QStringLiteral("display_name")));
        response.valid = object.value(QStringLiteral("results")).isArray();
        response.capabilityMetadata.insert(QStringLiteral("row"), discoverRow(section));
        break;
    }
    default:
        response.error = QStringLiteral("Not a Silo request operation");
        return response;
    }
    if (!response.valid) {
        response.error = QStringLiteral("Invalid Silo request response");
    }
    return response;
}
