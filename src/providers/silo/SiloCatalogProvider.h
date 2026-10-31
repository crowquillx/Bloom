#pragma once

#include "providers/ICatalogProvider.h"
#include "providers/silo/SiloArtworkSizes.h"
#include "providers/silo/SiloMediaRequestMapper.h"
#include "providers/silo/SiloModelMapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <optional>
#include <cmath>

class SiloCatalogProvider final : public ICatalogProvider
{
public:
    using ICatalogProvider::parseResponse;
    ProviderCatalogRequest createRequest(
        ProviderCatalogOperation operation,
        const ProviderCatalogQuery &query) const override
    {
        ProviderCatalogRequest request = createOperationRequest(operation, query);
        if (request.supported && returnsArtwork(operation, request)) {
            request.relativeEndpoint = SiloArtworkSizes::withWidths(request.relativeEndpoint);
        }
        return request;
    }

private:
    static ProviderCatalogRequest createOperationRequest(
        ProviderCatalogOperation operation,
        const ProviderCatalogQuery &query)
    {
        switch (operation) {
        case ProviderCatalogOperation::Views:
            return createViewsRequest(query);
        case ProviderCatalogOperation::Items:
            return createItemsRequest(query);
        case ProviderCatalogOperation::FilterOptions:
            return createFilterOptionsRequest(query);
        case ProviderCatalogOperation::LatestMedia: {
            ProviderCatalogQuery latest = query;
            if (latest.sortBy.trimmed().isEmpty()) {
                latest.sortBy = QStringLiteral("dateAdded");
                latest.sortOrder = QStringLiteral("descending");
            }
            return createCatalogRequest(latest, false);
        }
        case ProviderCatalogOperation::Item:
            return getWithContentId(QStringLiteral("/api/v2/catalog/items/%1"), query.itemId);
        case ProviderCatalogOperation::Chapters:
        case ProviderCatalogOperation::Versions:
            return getWithContentId(
                QStringLiteral("/api/v2/catalog/items/%1/versions"), query.itemId);
        case ProviderCatalogOperation::ThemeSongs:
            return getWithContentId(QStringLiteral("/api/v2/catalog/items/%1"), query.itemId);
        case ProviderCatalogOperation::ThemeSongsCapability:
            return get(QStringLiteral("/api/v2/catalog/themes/capabilities"));
        case ProviderCatalogOperation::PosterBadgeSettings:
            // Profile scoped: the request factory adds the selected profile.
            return get(QStringLiteral(
                "/api/v2/settings/values/effective"
                "?keys=ui.card_overlays&keys=ui.card_overlays_enabled"));
        case ProviderCatalogOperation::PosterBadgeDefaults:
            return get(QStringLiteral("/api/v2/settings/overlay-config"));
        case ProviderCatalogOperation::ThemeSongPlayback:
        {
            if (query.themeId.isEmpty())
                return unsupported(QStringLiteral("A theme ID is required"));
            auto request
                = getWithContentId(QStringLiteral("/api/v2/catalog/items/%1"), query.itemId);
            request.relativeEndpoint
                += QStringLiteral("/themes/%1/playback").arg(encodedId(query.themeId));
            request.method = ProviderHttpMethod::Post;
            request.retrySafety = ProviderCatalogRetrySafety::Never;
            request.body = QJsonDocument(
                QJsonObject { { "accepted_formats",
                    QJsonArray { QJsonObject { { "container", "mp3" }, { "audio_codec", "mp3" } },
                        QJsonObject { { "container", "mp4" }, { "audio_codec", "aac" } },
                        QJsonObject { { "container", "flac" }, { "audio_codec", "flac" } },
                        QJsonObject { { "container", "ogg" }, { "audio_codec", "vorbis" } },
                        QJsonObject { { "container", "opus" }, { "audio_codec", "opus" } },
                        QJsonObject { { "container", "wav" }, { "audio_codec", "pcm" } } } } })
                               .toJson(QJsonDocument::Compact);
            return request;
        }
        case ProviderCatalogOperation::SimilarItems: {
            if (query.limit < 0 || query.startIndex != 0
                || !query.parentId.trimmed().isEmpty()
                || !query.seriesId.trimmed().isEmpty()
                || !query.searchTerm.trimmed().isEmpty()
                || !query.genres.isEmpty() || !query.tags.isEmpty()
                || !query.studios.isEmpty() || !query.years.isEmpty()
                || !query.includeItemTypes.isEmpty()
                || query.watched != ProviderCatalogTriState::Any
                || query.favorite != ProviderCatalogTriState::Any
                || query.unwatchedOnly
                || query.minPremiereDate.isValid() || query.maxPremiereDate.isValid()
                || query.minDateLastSaved.isValid() || query.minCommunityRating > 0.0
                || !query.sortBy.trimmed().isEmpty()
                || !query.sortOrder.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo similar recommendations support only content_id and limit"));
            }
            ProviderCatalogRequest request = getWithContentId(
                QStringLiteral("/api/v2/recommendations/similar/%1"), query.itemId);
            if (!request.supported) {
                return request;
            }
            QUrlQuery parameters;
            appendLimit(parameters, query.limit);
            request.relativeEndpoint = endpointWithQuery(request.relativeEndpoint, parameters);
            return request;
        }
        case ProviderCatalogOperation::SetWatched:
            return mutationWithContentId(
                query.stateValue ? ProviderHttpMethod::Post : ProviderHttpMethod::Delete,
                QStringLiteral("/api/v2/watched/%1"), query.itemId);
        case ProviderCatalogOperation::SetFavorite:
            return mutationWithContentId(
                query.stateValue ? ProviderHttpMethod::Put : ProviderHttpMethod::Delete,
                QStringLiteral("/api/v2/favorites/%1"), query.itemId);
        case ProviderCatalogOperation::Search:
            if (query.searchTerm.trimmed().isEmpty()) {
                return unsupported(QStringLiteral("Silo search requires a non-empty search term"));
            }
            return createCatalogRequest(query, false);
        case ProviderCatalogOperation::NextUp:
            if (hasCatalogModifiers(query)
                || !query.parentId.trimmed().isEmpty()
                || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo's Next Up home section does not accept catalog modifiers"));
            }
            // Section IDs are profile layout data, not fixed routes: Silo
            // folds Next Up cards into its Continue Watching section unless
            // the profile shows Next Up separately. Read the layout and pick
            // both section types in parseResponse.
            return get(QStringLiteral("/api/v2/home/sections"));
        case ProviderCatalogOperation::HomeSections:
            if (hasCatalogModifiers(query)
                || !query.parentId.trimmed().isEmpty()
                || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo's home layout does not accept catalog modifiers"));
            }
            // The aggregate read answers every row with its cards and applies
            // the server's cross-row de-duplication and hide-watched rules.
            return get(QStringLiteral("/api/v2/home/sections"));
        case ProviderCatalogOperation::Watchlist: {
            if (query.limit < 0 || query.startIndex != 0) {
                return unsupported(QStringLiteral(
                    "Silo's watchlist pages by cursor and a positive limit only"));
            }
            QUrlQuery parameters;
            appendLimit(parameters, query.limit);
            if (!query.cursor.isEmpty()) {
                parameters.addQueryItem(QStringLiteral("cursor"), query.cursor);
            }
            return get(endpointWithQuery(QStringLiteral("/api/v2/watchlist"), parameters));
        }
        case ProviderCatalogOperation::SetWatchlist: {
            ProviderCatalogRequest request = mutationWithContentId(
                query.stateValue ? ProviderHttpMethod::Put : ProviderHttpMethod::Delete,
                QStringLiteral("/api/v2/watchlist/%1"), query.itemId);
            // addToWatchlist and deleteWatchlistEntry are declared
            // natural_idempotent: membership is a set, so a replay after a
            // lost response leaves the same state.
            if (request.supported) {
                request.retrySafety = ProviderCatalogRetrySafety::Idempotent;
            }
            return request;
        }
        // Request routes are profile scoped: the request factory adds the
        // selected profile, whose permissions and limits apply.
        case ProviderCatalogOperation::MediaRequestStatus:
            return get(QStringLiteral("/api/v2/requests/status"));
        case ProviderCatalogOperation::MediaRequestSearch: {
            const QString term = query.searchTerm.trimmed();
            if (term.isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo request search requires a non-empty search term"));
            }
            // Percent-encode the term by hand: QUrlQuery leaves '+', which
            // the server would read as a space.
            QString endpoint = QStringLiteral("/api/v2/requests/search?q=%1").arg(encodedId(term));
            if (query.page > 1) {
                endpoint += QStringLiteral("&page=%1").arg(query.page);
            }
            return get(endpoint);
        }
        case ProviderCatalogOperation::MediaRequestDetail: {
            const QString mediaType = SiloMediaRequestMapper::wireMediaType(query.requestMediaType);
            if (mediaType.isEmpty() || query.tmdbId <= 0) {
                return unsupported(QStringLiteral(
                    "Silo request details need a movie or series TMDB id"));
            }
            return get(QStringLiteral("/api/v2/requests/detail/%1/%2")
                           .arg(mediaType)
                           .arg(query.tmdbId));
        }
        case ProviderCatalogOperation::CreateMediaRequest: {
            const QJsonObject body = SiloMediaRequestMapper::createBody(
                query.requestMediaType, query.tmdbId, query.requestMedia, query.requestSeasons);
            if (body.isEmpty()) {
                return unsupported(QStringLiteral(
                    "A Silo request needs a movie or series with a TMDB id and a title"));
            }
            ProviderCatalogRequest request;
            request.method = ProviderHttpMethod::Post;
            // createRequest is declared non_retryable: a replay after a lost
            // response could file a second request.
            request.retrySafety = ProviderCatalogRetrySafety::Never;
            request.relativeEndpoint = QStringLiteral("/api/v2/requests");
            request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
            request.supported = true;
            return request;
        }
        case ProviderCatalogOperation::MediaRequestDiscover:
            return get(QStringLiteral("/api/v2/requests/discover"));
        case ProviderCatalogOperation::MediaRequestDiscoverSection: {
            if (!SiloMediaRequestMapper::isDiscoverSectionKey(query.sectionKey)) {
                return unsupported(QStringLiteral("Unknown Silo discovery row"));
            }
            QString endpoint = QStringLiteral("/api/v2/requests/discover/%1").arg(query.sectionKey);
            if (query.page > 1) {
                endpoint += QStringLiteral("?page=%1").arg(query.page);
            }
            return get(endpoint);
        }
        case ProviderCatalogOperation::MediaRequestList: {
            QUrlQuery parameters;
            // The route's maximum page size.
            parameters.addQueryItem(QStringLiteral("limit"),
                                    QString::number(query.limit > 0 ? qMin(query.limit, 50) : 50));
            if (!query.cursor.isEmpty()) {
                parameters.addQueryItem(QStringLiteral("cursor"), query.cursor);
            }
            return get(endpointWithQuery(QStringLiteral("/api/v2/requests/mine"), parameters));
        }
        case ProviderCatalogOperation::CancelMediaRequest: {
            ProviderCatalogRequest request = mutationWithContentId(
                ProviderHttpMethod::Post, QStringLiteral("/api/v2/requests/%1/cancel"), query.itemId);
            // cancelRequest is non_retryable; the reason is optional.
            request.body = QByteArrayLiteral("{}");
            return request;
        }
        case ProviderCatalogOperation::SetMediaRequestFollow:
        case ProviderCatalogOperation::SetWatchlistTitle: {
            const QString mediaType = SiloMediaRequestMapper::wireMediaType(query.requestMediaType);
            if (mediaType.isEmpty() || query.tmdbId <= 0) {
                return unsupported(QStringLiteral(
                    "Silo needs a movie or series TMDB id for this title"));
            }
            const bool follow = operation == ProviderCatalogOperation::SetMediaRequestFollow;
            ProviderCatalogRequest request = get(
                (follow ? QStringLiteral("/api/v2/requests/follows/%1/%2")
                        : QStringLiteral("/api/v2/watchlist/titles/%1/%2"))
                    .arg(mediaType)
                    .arg(query.tmdbId));
            request.method = query.stateValue ? ProviderHttpMethod::Put : ProviderHttpMethod::Delete;
            // Following is natural_idempotent. Watchlist title writes are
            // non_retryable: they can request the title as a side effect.
            request.retrySafety = follow ? ProviderCatalogRetrySafety::Idempotent
                                         : ProviderCatalogRetrySafety::Never;
            return request;
        }
        case ProviderCatalogOperation::MediaRequestBrowseCategories: {
            const QString list = SiloMediaRequestMapper::browseListName(query.browseKind);
            if (list.isEmpty()) {
                return unsupported(QStringLiteral("Silo browses genres, studios, and networks only"));
            }
            return get(QStringLiteral("/api/v2/requests/discover/%1").arg(list));
        }
        case ProviderCatalogOperation::MediaRequestBrowse: {
            const QString kind = query.browseKind;
            if (SiloMediaRequestMapper::browseListName(kind).isEmpty()
                || query.browseId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral("Silo browsing needs a genre, studio, or network"));
            }
            QUrlQuery parameters;
            if (kind == QStringLiteral("genre")) {
                // Genres browse movies or series; studios are movies and
                // networks are series.
                const QString mediaType = SiloMediaRequestMapper::wireMediaType(query.requestMediaType);
                if (mediaType.isEmpty()) {
                    return unsupported(QStringLiteral("Silo genre browsing needs a media type"));
                }
                parameters.addQueryItem(QStringLiteral("media_type"), mediaType);
            }
            if (SiloMediaRequestMapper::isBrowseSort(query.browseSort)) {
                parameters.addQueryItem(QStringLiteral("sort"), query.browseSort);
            }
            if (query.page > 1) {
                parameters.addQueryItem(QStringLiteral("page"), QString::number(query.page));
            }
            return get(endpointWithQuery(
                QStringLiteral("/api/v2/requests/discover/browse/%1/%2").arg(kind, encodedId(query.browseId)),
                parameters));
        }
        case ProviderCatalogOperation::HomeBackdrops:
        case ProviderCatalogOperation::ScreensaverItems:
        case ProviderCatalogOperation::RandomItems:
        case ProviderCatalogOperation::HeroItems:
        case ProviderCatalogOperation::HeroOverviews:
            return unsupported(QStringLiteral(
                "Silo has no native endpoint with the requested selection semantics"));
        case ProviderCatalogOperation::NextUnplayedEpisode:
            return unsupported(QStringLiteral(
                "Silo v2 has no series-scoped next-unplayed-episode endpoint"));
        case ProviderCatalogOperation::ResolveLibrary:
            return unsupported(QStringLiteral(
                "Silo catalog resources do not expose an ancestor-to-library resolution route"));
        }
        return unsupported(QStringLiteral("Unsupported Silo catalog operation"));
    }

public:
    ProviderCatalogResponse parseResponse(
        ProviderCatalogOperation operation,
        const QByteArray &body,
        const QHash<QByteArray, QByteArray> &responseHeaders) const override
    {
        if (operation == ProviderCatalogOperation::PosterBadgeSettings
            || operation == ProviderCatalogOperation::PosterBadgeDefaults) {
            ProviderCatalogResponse response;
            const QJsonDocument document = QJsonDocument::fromJson(body);
            response.rawItem = document.object();
            response.valid = document.isObject()
                && (operation == ProviderCatalogOperation::PosterBadgeDefaults
                    || response.rawItem.value(QStringLiteral("items")).isArray());
            if (!response.valid) {
                response.error = QStringLiteral("Invalid Silo poster badge settings response");
            }
            return response;
        }
        switch (operation) {
        case ProviderCatalogOperation::MediaRequestStatus:
        case ProviderCatalogOperation::MediaRequestSearch:
        case ProviderCatalogOperation::MediaRequestDetail:
        case ProviderCatalogOperation::CreateMediaRequest:
        case ProviderCatalogOperation::MediaRequestDiscover:
        case ProviderCatalogOperation::MediaRequestDiscoverSection:
        case ProviderCatalogOperation::MediaRequestList:
        case ProviderCatalogOperation::CancelMediaRequest:
        case ProviderCatalogOperation::SetMediaRequestFollow:
        case ProviderCatalogOperation::SetWatchlistTitle:
        case ProviderCatalogOperation::MediaRequestBrowseCategories:
        case ProviderCatalogOperation::MediaRequestBrowse:
            return SiloMediaRequestMapper::parseResponse(operation, body);
        default:
            break;
        }
        if (operation == ProviderCatalogOperation::SetWatchlist) {
            // 204 No Content on success.
            ProviderCatalogResponse response;
            response.valid = true;
            return response;
        }
        if (operation == ProviderCatalogOperation::HomeSections) {
            ProviderCatalogResponse response;
            const QJsonDocument document = QJsonDocument::fromJson(body);
            const QJsonValue sections = document.object().value(QStringLiteral("sections"));
            if (!sections.isArray()) {
                response.error = QStringLiteral("Invalid Silo home sections response");
                return response;
            }
            bool layoutHasWatchlist = false;
            response.rawItems = curatedHomeRows(sections.toArray(), &layoutHasWatchlist);
            response.capabilityMetadata.insert(
                QStringLiteral("layoutHasWatchlist"), layoutHasWatchlist);
            response.total = static_cast<int>(response.rawItems.size());
            response.valid = true;
            return response;
        }
        if (operation == ProviderCatalogOperation::Watchlist) {
            ProviderCatalogResponse response
                = SiloModelMapper::catalogResponse(operation, body, responseHeaders);
            if (response.valid) {
                response.rawItems = videoCards(response.rawItems);
            }
            return response;
        }
        if (operation == ProviderCatalogOperation::ThemeSongs
            || operation == ProviderCatalogOperation::ThemeSongsCapability
            || operation == ProviderCatalogOperation::ThemeSongPlayback)
        {
            ProviderCatalogResponse response;
            const auto document = QJsonDocument::fromJson(body);
            if (!document.isObject())
            {
                response.error = QStringLiteral("Invalid Silo theme response");
                return response;
            }
            const auto object = document.object();
            if (operation == ProviderCatalogOperation::ThemeSongs)
            {
                const auto themes = object.value("themes").toObject();
                response.rawItems = themes.value("items").toArray();
                response.capabilityMetadata.insert(
                    QStringLiteral("ownerId"), themes.value("owner_id").toString());
                response.valid = themes.isEmpty()
                    || (themes.value("items").isArray()
                        && !themes.value("owner_id").toString().isEmpty());
            }
            else if (operation == ProviderCatalogOperation::ThemeSongsCapability)
            {
                response.capabilityMetadata.insert(QStringLiteral("allowed"),
                    object.value("state").toString() == QStringLiteral("available")
                        && object.value("allowed").toBool()
                        && (object.value("delivery").toString() == QStringLiteral("routed")
                            || object.value("delivery").toString()
                                == QStringLiteral("local_direct_play")));
                response.valid
                    = object.value("state").isString() && object.value("allowed").isBool();
                for (auto it = responseHeaders.cbegin(); it != responseHeaders.cend(); ++it)
                {
                    if (it.key().compare("ETag", Qt::CaseInsensitive) == 0)
                        response.snapshot.insert(
                            QStringLiteral("etag"), QString::fromUtf8(it.value()));
                }
            }
            else
            {
                const QString delivery = object.value("delivery").toString();
                const QDateTime expiry = QDateTime::fromString(
                    object.value("expires_at").toString(), Qt::ISODateWithMs);
                response.valid = !object.value("url").toString().isEmpty() && expiry.isValid()
                    && expiry > QDateTime::currentDateTimeUtc()
                    && (delivery == QStringLiteral("original")
                        || delivery == QStringLiteral("converted"));
                response.capabilityMetadata = { { "url", object.value("url").toString() },
                    { "expiresAt", expiry }, { "requiresFreshGrant", true } };
            }
            if (!response.valid)
                response.error = QStringLiteral("Invalid or expired Silo theme response");
            return response;
        }

        // The native numeric-season detail endpoint returns {"season": {...}},
        // unlike the Items operation's array/envelope responses. Parse that
        // envelope as an item even when the request originated as Items.
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (operation == ProviderCatalogOperation::NextUp && document.isObject()
            && document.object().value(QStringLiteral("sections")).isArray()) {
            return SiloModelMapper::catalogResponse(
                operation,
                QJsonDocument(QJsonObject{
                    {QStringLiteral("items"),
                     continueWatchingItems(
                         document.object().value(QStringLiteral("sections")).toArray())}})
                    .toJson(QJsonDocument::Compact),
                responseHeaders);
        }
        if (document.isObject()
            && document.object().value(QStringLiteral("season")).isObject()) {
            ProviderCatalogResponse response = SiloModelMapper::catalogResponse(
                ProviderCatalogOperation::Item, body, responseHeaders);
            if (response.valid) {
                response.rawItem =
                    document.object().value(QStringLiteral("season")).toObject();
                response.capabilityMetadata.insert(
                    QStringLiteral("envelope"), QStringLiteral("season"));
            }
            return response;
        }
        return SiloModelMapper::catalogResponse(operation, body, responseHeaders);
    }

private:
    // These operations read media cards or details on routes that declare
    // the artwork width parameters; library discovery is the one exception.
    static bool returnsArtwork(ProviderCatalogOperation operation,
                               const ProviderCatalogRequest &request)
    {
        switch (operation) {
        case ProviderCatalogOperation::Views:
            return request.relativeEndpoint != QStringLiteral("/api/v2/user/libraries");
        case ProviderCatalogOperation::Items:
        case ProviderCatalogOperation::LatestMedia:
        case ProviderCatalogOperation::Item:
        case ProviderCatalogOperation::Chapters:
        case ProviderCatalogOperation::Versions:
        case ProviderCatalogOperation::SimilarItems:
        case ProviderCatalogOperation::Search:
        case ProviderCatalogOperation::NextUp:
        case ProviderCatalogOperation::HomeSections:
        case ProviderCatalogOperation::Watchlist:
            return true;
        default:
            return false;
        }
    }

    struct Rule
    {
        QString field;
        QString op;
        QJsonValue value;
    };

    // Video cards from the Continue Watching and Next Up home sections, in
    // layout order. Continue Listening/Reading share the continue_watching
    // section type, so only movie and episode cards are kept.
    static QJsonArray continueWatchingItems(const QJsonArray &sections)
    {
        QJsonArray items;
        QSet<QString> seen;
        for (const QJsonValue &sectionValue : sections) {
            const QJsonObject section = sectionValue.toObject();
            const QString sectionType = section.value(QStringLiteral("section_type")).toString();
            if (sectionType != QStringLiteral("continue_watching")
                && sectionType != QStringLiteral("next_up")) {
                continue;
            }
            for (const QJsonValue &itemValue : section.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = itemValue.toObject();
                const QString type = item.value(QStringLiteral("type")).toString().toLower();
                if (type != QStringLiteral("movie") && type != QStringLiteral("episode")) {
                    continue;
                }
                const QString contentId = item.value(QStringLiteral("content_id")).toString();
                if (contentId.isEmpty() || seen.contains(contentId)) {
                    continue;
                }
                seen.insert(contentId);
                items.append(item);
            }
        }
        return items;
    }

    static bool isVideoCard(const QJsonObject &item)
    {
        const QString type = item.value(QStringLiteral("type")).toString().toLower();
        return type == QStringLiteral("movie") || type == QStringLiteral("series")
            || type == QStringLiteral("episode");
    }

    // Movie, series and episode cards only, once each, in their order. Bloom
    // shows no music, audiobook, ebook, manga, podcast or season cards.
    static QJsonArray videoCards(const QJsonArray &items)
    {
        QJsonArray cards;
        QSet<QString> seen;
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString contentId = item.value(QStringLiteral("content_id")).toString();
            if (!isVideoCard(item) || contentId.isEmpty() || seen.contains(contentId)) {
                continue;
            }
            seen.insert(contentId);
            cards.append(item);
        }
        return cards;
    }

    // Section types whose cards Bloom's own Home already shows: Continue
    // Watching and Up Next (Next Up reads both types), and the per-library
    // Recently Added rows (new_to_library is the same newest-first list).
    static bool homeShowsSectionType(const QString &sectionType)
    {
        return sectionType == QStringLiteral("continue_watching")
            || sectionType == QStringLiteral("next_up")
            || sectionType == QStringLiteral("recently_added")
            || sectionType == QStringLiteral("new_to_library");
    }

    // The profile's rows that Bloom adds below its own, in layout order:
    // every section type Bloom does not already show, reduced to video cards,
    // without rows that end up empty. Featured rows (Silo's page hero) are
    // ordinary rows here; Bloom keeps its own hero.
    static QJsonArray curatedHomeRows(const QJsonArray &sections, bool *layoutHasWatchlist)
    {
        QJsonArray rows;
        QSet<QString> seenIds;
        for (const QJsonValue &sectionValue : sections) {
            const QJsonObject section = sectionValue.toObject();
            const QString id = section.value(QStringLiteral("id")).toString();
            const QString sectionType
                = section.value(QStringLiteral("section_type")).toString();
            if (sectionType == QStringLiteral("watchlist") && layoutHasWatchlist) {
                *layoutHasWatchlist = true;
            }
            if (id.isEmpty() || seenIds.contains(id) || homeShowsSectionType(sectionType)) {
                continue;
            }
            const QJsonArray cards = videoCards(section.value(QStringLiteral("items")).toArray());
            if (cards.isEmpty()) {
                continue;
            }
            seenIds.insert(id);
            rows.append(QJsonObject{
                {QStringLiteral("id"), id},
                {QStringLiteral("section_type"), sectionType},
                {QStringLiteral("title"), section.value(QStringLiteral("title")).toString()},
                {QStringLiteral("items"), cards}});
        }
        return rows;
    }

    static ProviderCatalogRequest get(const QString &endpoint)
    {
        ProviderCatalogRequest request;
        request.method = ProviderHttpMethod::Get;
        request.retrySafety = ProviderCatalogRetrySafety::Idempotent;
        request.relativeEndpoint = endpoint;
        request.supported = true;
        return request;
    }

    static ProviderCatalogRequest unsupported(const QString &reason)
    {
        ProviderCatalogRequest request;
        request.unsupportedReason = reason;
        return request;
    }

    static QString encodedId(const QString &id)
    {
        return QString::fromLatin1(QUrl::toPercentEncoding(id.trimmed()));
    }

    static ProviderCatalogRequest getWithContentId(const QString &format,
                                                   const QString &contentId)
    {
        const QString normalized = contentId.trimmed();
        if (normalized.isEmpty()) {
            return unsupported(QStringLiteral("Silo catalog operation requires a content_id"));
        }
        return get(format.arg(encodedId(normalized)));
    }

    static ProviderCatalogRequest mutationWithContentId(ProviderHttpMethod method,
                                                        const QString &format,
                                                        const QString &contentId)
    {
        ProviderCatalogRequest request = getWithContentId(format, contentId);
        if (request.supported) {
            request.method = method;
            request.retrySafety = ProviderCatalogRetrySafety::Never;
        }
        return request;
    }

    static QString endpointWithQuery(const QString &endpoint, const QUrlQuery &query)
    {
        if (query.isEmpty()) {
            return endpoint;
        }
        return endpoint + QLatin1Char('?') + query.query(QUrl::FullyEncoded);
    }

    static void appendLimit(QUrlQuery &parameters, int limit)
    {
        if (limit > 0) {
            parameters.addQueryItem(QStringLiteral("limit"), QString::number(qMin(limit, 100)));
        }
    }

    static QString normalizedItemType(const QString &type)
    {
        const QString normalized = type.trimmed().toLower();
        if (normalized == QStringLiteral("audio") || normalized == QStringLiteral("music")) {
            return {};
        }
        if (normalized == QStringLiteral("episode")
            || normalized == QStringLiteral("manga")
            || normalized == QStringLiteral("movie")
            || normalized == QStringLiteral("season")
            || normalized == QStringLiteral("series")
            || normalized == QStringLiteral("show")
            || normalized == QStringLiteral("book")) {
            if (normalized == QStringLiteral("show")) {
                return QStringLiteral("series");
            }
            if (normalized == QStringLiteral("book")) {
                return QStringLiteral("ebook");
            }
            return normalized;
        }
        return {};
    }

    static std::optional<QString> normalizedSort(const QString &canonicalSort)
    {
        const QString sort = canonicalSort.trimmed();
        if (sort.isEmpty() || sort == QStringLiteral("libraryOrder")) {
            return QString();
        }
        if (sort == QStringLiteral("title")) {
            return QStringLiteral("title");
        }
        if (sort == QStringLiteral("dateAdded")) {
            return QStringLiteral("added_at");
        }
        if (sort == QStringLiteral("releaseDate")) {
            return QStringLiteral("release_date");
        }
        if (sort == QStringLiteral("year")) {
            return QStringLiteral("year");
        }
        if (sort == QStringLiteral("rating")) {
            return QStringLiteral("rating_imdb");
        }
        return std::nullopt;
    }

    static QString validationError(const ProviderCatalogQuery &query)
    {
        if (query.startIndex < 0) {
            return QStringLiteral("Silo catalog offset cannot be negative");
        }
        if (query.limit < 0) {
            return QStringLiteral("Silo catalog limit cannot be negative");
        }
        if (!query.tags.isEmpty()) {
            return QStringLiteral("Silo catalog does not support tag or keyword filtering");
        }
        if (!query.itemIds.isEmpty() || !query.parentIds.isEmpty()
            || !query.seriesIds.isEmpty() || !query.excludeItemId.trimmed().isEmpty()) {
            return QStringLiteral("Silo catalog does not support the requested ID-set filter semantics");
        }
        if (query.minDateLastSaved.isValid()) {
            return QStringLiteral(
                "Silo's added_at filter is exclusive and cannot represent Bloom's inclusive minimum date");
        }
        if (query.minPremiereDate.isValid() != query.maxPremiereDate.isValid()) {
            return QStringLiteral(
                "Silo supports an inclusive premiere-date range, but not either inclusive boundary alone");
        }
        if (query.minPremiereDate.isValid()
            && query.minPremiereDate > query.maxPremiereDate) {
            return QStringLiteral("Silo premiere-date range is reversed");
        }
        if (!std::isfinite(query.minCommunityRating)
            || query.minCommunityRating < 0.0
            || query.minCommunityRating > 10.0) {
            return QStringLiteral(
                "Silo minimum community rating must be finite and between zero and ten");
        }
        if (query.watched == ProviderCatalogTriState::Yes && query.unwatchedOnly) {
            return QStringLiteral("Conflicting watched and unwatched filters");
        }
        if (!normalizedSort(query.sortBy).has_value()) {
            return QStringLiteral("Silo does not support the requested catalog sort");
        }
        const QString order = query.sortOrder.trimmed().toLower();
        if (!order.isEmpty() && order != QStringLiteral("ascending")
            && order != QStringLiteral("descending")) {
            return QStringLiteral("Silo catalog sort order must be ascending or descending");
        }
        for (int year : query.years) {
            if (year <= 0) {
                return QStringLiteral("Silo catalog years must be positive");
            }
        }
        for (const QString &genre : query.genres) {
            if (genre.trimmed().isEmpty()) {
                return QStringLiteral("Silo catalog genres cannot be empty");
            }
        }
        for (const QString &studio : query.studios) {
            if (studio.trimmed().isEmpty()) {
                return QStringLiteral("Silo catalog studios cannot be empty");
            }
        }
        for (const QString &type : query.includeItemTypes) {
            const QString normalized = normalizedItemType(type);
            if (normalized.isEmpty()) {
                return QStringLiteral("Silo does not support requested item type '%1'").arg(type);
            }
            if (normalized == QStringLiteral("season")
                || normalized == QStringLiteral("episode")) {
                return QStringLiteral(
                    "Silo season and episode types require a content hierarchy route");
            }
        }
        return {};
    }

    static QList<Rule> filterRules(const ProviderCatalogQuery &query)
    {
        QList<Rule> rules;
        if (query.genres.size() == 1) {
            rules.append({QStringLiteral("genre"), QStringLiteral("contains"),
                          query.genres.first().trimmed()});
        }
        if (query.studios.size() == 1) {
            rules.append({QStringLiteral("studio"), QStringLiteral("is"),
                          query.studios.first().trimmed()});
        }
        if (query.minPremiereDate.isValid() && query.maxPremiereDate.isValid()) {
            rules.append({QStringLiteral("release_date"), QStringLiteral("between"),
                          QJsonArray{query.minPremiereDate.toString(Qt::ISODate),
                                     query.maxPremiereDate.toString(Qt::ISODate)}});
        }
        if (query.minCommunityRating > 0.0) {
            rules.append({QStringLiteral("rating_imdb"), QStringLiteral("gte"),
                          query.minCommunityRating});
        }
        if (!query.years.isEmpty()) {
            if (query.years.size() == 1) {
                rules.append({QStringLiteral("year"), QStringLiteral("is"), query.years.first()});
            }
        }
        if (query.watched != ProviderCatalogTriState::Any || query.unwatchedOnly) {
            const bool watched = !query.unwatchedOnly
                && query.watched == ProviderCatalogTriState::Yes;
            rules.append({QStringLiteral("watched"), QStringLiteral("is"), watched});
        }
        if (query.favorite != ProviderCatalogTriState::Any) {
            rules.append({QStringLiteral("favorited"), QStringLiteral("is"),
                          query.favorite == ProviderCatalogTriState::Yes});
        }
        return rules;
    }

    static QJsonArray ruleArray(const ProviderCatalogQuery &query)
    {
        QJsonArray rules;
        for (const Rule &rule : filterRules(query)) {
            rules.append(QJsonObject{{QStringLiteral("field"), rule.field},
                                     {QStringLiteral("op"), rule.op},
                                     {QStringLiteral("value"), rule.value}});
        }
        if (query.genres.size() > 1) {
            QJsonArray alternatives;
            for (const QString &genre : query.genres) {
                alternatives.append(
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("genre")},
                                {QStringLiteral("op"), QStringLiteral("contains")},
                                {QStringLiteral("value"), genre.trimmed()}});
            }
            rules.append(QJsonObject{{QStringLiteral("field"), QStringLiteral("__genre_any")},
                                     {QStringLiteral("op"), QStringLiteral("__group")},
                                     {QStringLiteral("value"), alternatives}});
        }
        if (query.studios.size() > 1) {
            QJsonArray alternatives;
            for (const QString &studio : query.studios) {
                alternatives.append(
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("studio")},
                                {QStringLiteral("op"), QStringLiteral("is")},
                                {QStringLiteral("value"), studio.trimmed()}});
            }
            rules.append(QJsonObject{{QStringLiteral("field"), QStringLiteral("__studio_any")},
                                     {QStringLiteral("op"), QStringLiteral("__group")},
                                     {QStringLiteral("value"), alternatives}});
        }
        if (query.years.size() > 1) {
            QJsonArray alternatives;
            for (int year : query.years) {
                alternatives.append(
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("year")},
                                {QStringLiteral("op"), QStringLiteral("is")},
                                {QStringLiteral("value"), year}});
            }
            rules.append(QJsonObject{{QStringLiteral("field"), QStringLiteral("__year_any")},
                                     {QStringLiteral("op"), QStringLiteral("__group")},
                                     {QStringLiteral("value"), alternatives}});
        }
        if (!query.includeItemTypes.isEmpty()) {
            QStringList itemTypes;
            for (const QString &type : query.includeItemTypes) {
                const QString normalized = normalizedItemType(type);
                if (!itemTypes.contains(normalized)) {
                    itemTypes.append(normalized);
                }
            }
            if (itemTypes.size() == 1) {
                rules.append(QJsonObject{{QStringLiteral("field"), QStringLiteral("type")},
                                         {QStringLiteral("op"), QStringLiteral("is")},
                                         {QStringLiteral("value"), itemTypes.first()}});
            } else {
                QJsonArray alternatives;
                for (const QString &type : itemTypes) {
                    alternatives.append(
                        QJsonObject{{QStringLiteral("field"), QStringLiteral("type")},
                                    {QStringLiteral("op"), QStringLiteral("is")},
                                    {QStringLiteral("value"), type}});
                }
                rules.append(
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("__type_any")},
                                {QStringLiteral("op"), QStringLiteral("__group")},
                                {QStringLiteral("value"), alternatives}});
            }
        }
        return rules;
    }

    static QJsonArray groupsForPost(const ProviderCatalogQuery &query)
    {
        QJsonArray groups;
        QJsonArray allRules;
        const QJsonArray encoded = ruleArray(query);
        for (const QJsonValue &value : encoded) {
            const QJsonObject rule = value.toObject();
            if (rule.value(QStringLiteral("op")).toString() == QStringLiteral("__group")) {
                groups.append(QJsonObject{{QStringLiteral("match"), QStringLiteral("any")},
                                          {QStringLiteral("rules"), rule.value(QStringLiteral("value"))}});
            } else {
                allRules.append(rule);
            }
        }
        if (!allRules.isEmpty()) {
            groups.prepend(QJsonObject{{QStringLiteral("match"), QStringLiteral("all")},
                                       {QStringLiteral("rules"), allRules}});
        }
        return groups;
    }

    static void appendGroupsForGet(QUrlQuery &parameters,
                                   const ProviderCatalogQuery &query)
    {
        const QJsonArray groups = groupsForPost(query);
        if (!groups.isEmpty())
        {
            parameters.addQueryItem(QStringLiteral("groups"),
                QString::fromUtf8(QJsonDocument(groups).toJson(QJsonDocument::Compact)));
        }
    }

    static ProviderCatalogRequest createViewsRequest(const ProviderCatalogQuery &query)
    {
        if (query.parentId.trimmed().isEmpty()) {
            if (hasCatalogModifiers(query) || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo library discovery does not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v2/user/libraries"));
        }
        if (query.parentId.trimmed().compare(
                QStringLiteral("home"), Qt::CaseInsensitive) == 0) {
            if (hasCatalogModifiers(query) || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo home section discovery does not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v2/home/sections"));
        }
        if (hasCatalogModifiers(query) || !query.itemId.trimmed().isEmpty())
        {
            return unsupported(QStringLiteral("Silo library sections require only a library ID"));
        }
        return get(QStringLiteral("/api/v2/library/%1/sections").arg(encodedId(query.parentId)));
    }

    static ProviderCatalogRequest createItemsRequest(const ProviderCatalogQuery &query)
    {
        if (!query.itemId.isEmpty() && !query.parentId.isEmpty())
        {
            if (hasCatalogModifiers(query)) {
                return unsupported(
                    QStringLiteral("Silo section items do not accept catalog modifiers"));
            }
            if (query.parentId.compare(QStringLiteral("home"), Qt::CaseInsensitive) == 0)
            {
                return get(
                    QStringLiteral("/api/v2/home/sections/%1/items").arg(encodedId(query.itemId)));
            }
            return get(QStringLiteral("/api/v2/library/%1/sections/%2/items")
                    .arg(encodedId(query.parentId), encodedId(query.itemId)));
        }
        const QString type = query.includeItemTypes.size() == 1
            ? normalizedItemType(query.includeItemTypes.first())
            : QString();
        if (!query.seriesId.isEmpty() || type == QStringLiteral("season")
            || type == QStringLiteral("episode"))
        {
            if (hasHierarchyModifiers(query)) {
                return unsupported(
                    QStringLiteral("Silo hierarchy routes do not accept catalog modifiers"));
            }
            if (!query.seriesId.isEmpty() && !query.includeItemTypes.isEmpty()
                && (query.includeItemTypes.size() != 1
                    || type
                        != (query.parentId.isEmpty() ? QStringLiteral("season")
                                                     : QStringLiteral("episode"))))
            {
                return unsupported(
                    QStringLiteral("Silo hierarchy type does not match the requested resource"));
            }
            if (!query.seriesId.isEmpty() && !query.parentId.isEmpty())
            {
                bool numberOk = false;
                const int number = query.parentId.toInt(&numberOk);
                if (numberOk && number >= 0)
                {
                    const QString route = type == QStringLiteral("episode")
                        ? QStringLiteral("/api/v2/catalog/series/%1/seasons/%2/episodes")
                        : QStringLiteral("/api/v2/catalog/series/%1/seasons/%2");
                    return get(route.arg(encodedId(query.seriesId), QString::number(number)));
                }
                return getWithContentId(
                    QStringLiteral("/api/v2/catalog/items/%1/episodes"), query.parentId);
            }
            if (type == QStringLiteral("episode")) {
                return getWithContentId(
                    QStringLiteral("/api/v2/catalog/items/%1/episodes"), query.parentId);
            }
            return getWithContentId(QStringLiteral("/api/v2/catalog/series/%1/seasons"),
                query.seriesId.isEmpty() ? query.parentId : query.seriesId);
        }
        return createCatalogRequest(query, true);
    }

    static bool hasCatalogModifiers(const ProviderCatalogQuery &query)
    {
        return query.startIndex != 0 || query.limit > 0
            || !query.searchTerm.trimmed().isEmpty()
            || !query.genres.isEmpty() || !query.tags.isEmpty()
            || !query.studios.isEmpty()
            || query.minPremiereDate.isValid() || query.maxPremiereDate.isValid()
            || query.minDateLastSaved.isValid() || query.minCommunityRating > 0.0
            || !query.years.isEmpty() || !query.sortBy.trimmed().isEmpty()
            || !query.sortOrder.trimmed().isEmpty() || query.recursive
            || !query.includeItemTypes.isEmpty()
            || !query.seriesId.trimmed().isEmpty()
            || !query.excludeItemId.trimmed().isEmpty()
            || !query.itemIds.isEmpty() || !query.parentIds.isEmpty()
            || !query.seriesIds.isEmpty()
            || query.watched != ProviderCatalogTriState::Any
            || query.favorite != ProviderCatalogTriState::Any || query.unwatchedOnly;
    }

    static bool hasHierarchyModifiers(const ProviderCatalogQuery &query)
    {
        return query.startIndex != 0 || query.limit > 0
            || !query.searchTerm.trimmed().isEmpty()
            || !query.genres.isEmpty() || !query.tags.isEmpty()
            || !query.studios.isEmpty()
            || query.minPremiereDate.isValid() || query.maxPremiereDate.isValid()
            || query.minDateLastSaved.isValid() || query.minCommunityRating > 0.0
            || !query.years.isEmpty() || !query.sortBy.trimmed().isEmpty()
            || !query.sortOrder.trimmed().isEmpty() || query.recursive
            || query.watched != ProviderCatalogTriState::Any
            || query.favorite != ProviderCatalogTriState::Any || query.unwatchedOnly;
    }

    static ProviderCatalogRequest createCatalogRequest(const ProviderCatalogQuery &query,
                                                        bool allowPost)
    {
        const QString error = validationError(query);
        if (!error.isEmpty()) {
            return unsupported(error);
        }
        if (query.recursive) {
            return unsupported(QStringLiteral(
                "Silo catalog does not expose a recursive-scope switch"));
        }

        const QString libraryId = query.parentId.trimmed();
        const QJsonArray groups = groupsForPost(query);
        const bool usePost = allowPost && !groups.isEmpty();
        const std::optional<QString> sort = normalizedSort(query.sortBy);
        QString order = query.sortOrder.trimmed().toLower();
        if (order == QStringLiteral("ascending")) order = QStringLiteral("asc");
        if (order == QStringLiteral("descending")) order = QStringLiteral("desc");

        if (usePost) {
            QJsonObject body { { QStringLiteral("match"), QStringLiteral("all") },
                { QStringLiteral("groups"), groups } };
            if (!query.cursor.isEmpty())
            {
                body.insert(QStringLiteral("cursor"), query.cursor);
            }
            else if (query.startIndex > 0)
            {
                body.insert(QStringLiteral("seek"), query.startIndex);
                if (!query.windowCursor.isEmpty())
                    body.insert(QStringLiteral("cursor"), query.windowCursor);
            }
            if (!query.searchTerm.isEmpty())
                body.insert(QStringLiteral("q"), query.searchTerm);
            if (!libraryId.isEmpty())
            {
                body.insert(QStringLiteral("library_id"), libraryId);
            }
            if (query.limit > 0) {
                body.insert(QStringLiteral("limit"), qMin(query.limit, 100));
            }
            if (sort && !sort->isEmpty()) {
                body.insert(QStringLiteral("sort"), *sort);
            }
            if (!order.isEmpty()) {
                body.insert(QStringLiteral("order"), order);
            }
            ProviderCatalogRequest request;
            request.method = ProviderHttpMethod::Post;
            request.retrySafety = ProviderCatalogRetrySafety::Idempotent;
            request.relativeEndpoint = QStringLiteral("/api/v2/catalog/query");
            request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
            request.extraHeaders.insert(QByteArrayLiteral("Content-Type"),
                                        QByteArrayLiteral("application/json"));
            request.supported = true;
            return request;
        }

        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("source"), QStringLiteral("query"));
        if (!libraryId.isEmpty())
        {
            parameters.addQueryItem(QStringLiteral("library_id"), libraryId);
        }
        if (!query.cursor.isEmpty())
        {
            parameters.addQueryItem(QStringLiteral("cursor"), query.cursor);
        }
        else if (query.startIndex > 0)
        {
            parameters.addQueryItem(QStringLiteral("seek"), QString::number(query.startIndex));
            if (!query.windowCursor.isEmpty())
                parameters.addQueryItem(QStringLiteral("cursor"), query.windowCursor);
        }
        appendLimit(parameters, query.limit);
        if (!query.searchTerm.trimmed().isEmpty()) {
            parameters.addQueryItem(QStringLiteral("q"), query.searchTerm.trimmed());
        }
        if (sort && !sort->isEmpty()) {
            parameters.addQueryItem(QStringLiteral("sort"),
                (order == QStringLiteral("desc") ? QStringLiteral("-") : QString()) + *sort);
        }
        appendGroupsForGet(parameters, query);

        ProviderCatalogRequest request
            = get(endpointWithQuery(QStringLiteral("/api/v2/catalog"), parameters));
        return request;
    }

    static ProviderCatalogRequest createFilterOptionsRequest(
        const ProviderCatalogQuery &query)
    {
        const QString error = validationError(query);
        if (!error.isEmpty()) {
            return unsupported(error);
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("source"), QStringLiteral("query"));
        if (!query.parentId.trimmed().isEmpty())
        {
            parameters.addQueryItem(QStringLiteral("library_id"), query.parentId.trimmed());
        }
        if (query.includeItemTypes.size() == 1)
        {
            parameters.addQueryItem(
                QStringLiteral("type"), normalizedItemType(query.includeItemTypes.first()));
        }
        parameters.addQueryItem(QStringLiteral("skip_technical"),
            query.includeHeavyFields ? QStringLiteral("false") : QStringLiteral("true"));

        QString endpoint = QStringLiteral("/api/v2/catalog/filters");
        QString facet = query.filterFacet.trimmed().toLower();
        static const QHash<QString, QString> facetAliases{
            {QStringLiteral("genres"), QStringLiteral("genre")},
            {QStringLiteral("studios"), QStringLiteral("studio")},
            {QStringLiteral("networks"), QStringLiteral("network")},
            {QStringLiteral("countries"), QStringLiteral("country")},
            {QStringLiteral("authors"), QStringLiteral("author")},
            {QStringLiteral("narrators"), QStringLiteral("narrator")},
            {QStringLiteral("original_languages"), QStringLiteral("original_language")},
            {QStringLiteral("content_ratings"), QStringLiteral("content_rating")}};
        facet = facetAliases.value(facet, facet);
        if (!facet.isEmpty()) {
            static const QStringList supportedFacets{
                QStringLiteral("author"), QStringLiteral("narrator"),
                QStringLiteral("series"), QStringLiteral("genre"),
                QStringLiteral("studio"), QStringLiteral("network"),
                QStringLiteral("country"), QStringLiteral("original_language"),
                QStringLiteral("content_rating")};
            if (!supportedFacets.contains(facet)) {
                return unsupported(QStringLiteral("Silo does not support filter facet '%1'")
                                       .arg(query.filterFacet));
            }
            endpoint += QStringLiteral("/search");
            parameters.addQueryItem(QStringLiteral("facet"), facet);
            if (!query.searchTerm.isEmpty())
                parameters.addQueryItem(QStringLiteral("q"), query.searchTerm);
            appendLimit(parameters, query.limit);
        }
        return get(endpointWithQuery(endpoint, parameters));
    }
};
