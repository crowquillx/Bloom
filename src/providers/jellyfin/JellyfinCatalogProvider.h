#pragma once

#include "models/LetterIndex.h"
#include "providers/ICatalogProvider.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>
#include <algorithm>
#include <optional>

class JellyfinCatalogProvider final : public ICatalogProvider
{
public:
    using ICatalogProvider::parseResponse;
    ProviderCatalogRequest createRequest(
        ProviderCatalogOperation operation,
        const ProviderCatalogQuery &query) const override
    {
        switch (operation) {
        case ProviderCatalogOperation::Views:
            return viewsRequest(query);
        case ProviderCatalogOperation::Items:
            return itemsRequest(query);
        case ProviderCatalogOperation::FilterOptions:
            return filterOptionsRequest(query);
        case ProviderCatalogOperation::NextUp:
            return nextUpRequest(query);
        case ProviderCatalogOperation::ResumeItems:
            return resumeItemsRequest(query);
        case ProviderCatalogOperation::LatestMedia:
            return latestMediaRequest(query);
        case ProviderCatalogOperation::HomeBackdrops:
            return homeBackdropsRequest(query);
        case ProviderCatalogOperation::ScreensaverItems:
            return screensaverItemsRequest(query);
        case ProviderCatalogOperation::Item:
            return itemRequest(query);
        case ProviderCatalogOperation::Chapters:
            return chaptersRequest(query);
        case ProviderCatalogOperation::ResolveLibrary:
            return resolveLibraryRequest(query);
        case ProviderCatalogOperation::SimilarItems:
            return similarItemsRequest(query);
        case ProviderCatalogOperation::NextUnplayedEpisode:
            return nextUnplayedEpisodeRequest(query);
        case ProviderCatalogOperation::SetWatched:
            return stateRequest(query, QStringLiteral("UserPlayedItems"));
        case ProviderCatalogOperation::SetFavorite:
            return stateRequest(query, QStringLiteral("UserFavoriteItems"));
        case ProviderCatalogOperation::Search:
            return searchRequest(query);
        case ProviderCatalogOperation::RandomItems:
            return randomItemsRequest(query);
        case ProviderCatalogOperation::HeroItems:
            return heroItemsRequest(query);
        case ProviderCatalogOperation::HeroOverviews:
            return heroOverviewRequest(query);
        case ProviderCatalogOperation::Versions:
            return versionsRequest(query);
        case ProviderCatalogOperation::ThemeSongs:
            return themeSongsRequest(query);
        case ProviderCatalogOperation::SpecialFeatures:
            return extrasRequest(query, QStringLiteral("SpecialFeatures"));
        case ProviderCatalogOperation::LocalTrailers:
            return extrasRequest(query, QStringLiteral("LocalTrailers"));
        case ProviderCatalogOperation::ThemeSongsCapability:
        case ProviderCatalogOperation::ThemeSongPlayback:
            return unsupported(QStringLiteral("Jellyfin theme songs do not use grants"));
        case ProviderCatalogOperation::PosterBadgeSettings:
        case ProviderCatalogOperation::PosterBadgeDefaults:
            return unsupported(QStringLiteral("Jellyfin has no server-side poster badge preferences"));
        case ProviderCatalogOperation::HomeSections:
        case ProviderCatalogOperation::HomeCapabilities:
            return unsupported(QStringLiteral("Jellyfin has no profile-curated Home rows"));
        case ProviderCatalogOperation::Watchlist:
        case ProviderCatalogOperation::SetWatchlist:
            return unsupported(QStringLiteral("Jellyfin has no watchlist distinct from favorites"));
        case ProviderCatalogOperation::NotInterestedCapability:
        case ProviderCatalogOperation::SetNotInterested:
            return unsupported(QStringLiteral("Jellyfin has no Not interested state"));
        case ProviderCatalogOperation::UpcomingAirings:
            return unsupported(QStringLiteral("Bloom reads no Jellyfin airing calendar"));
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
            return unsupported(QStringLiteral("Jellyfin has no server-side media requests"));
        case ProviderCatalogOperation::Person:
            return personRequest(query);
        case ProviderCatalogOperation::PersonItems:
            return personItemsRequest(query);
        case ProviderCatalogOperation::PersonSearch:
            return personSearchRequest(query);
        case ProviderCatalogOperation::LibrarySections:
        case ProviderCatalogOperation::LibrarySectionItems:
            return unsupported(QStringLiteral("Jellyfin has no profile-curated library rows"));
        case ProviderCatalogOperation::CatalogSearchCapabilities:
            return unsupported(QStringLiteral(
                "Jellyfin people search and letter index need no capability read"));
        case ProviderCatalogOperation::LibraryLetters:
            return lettersRequest(query, false);
        case ProviderCatalogOperation::LibraryLetterCount:
            return lettersRequest(query, true);
        case ProviderCatalogOperation::MovieRecommendations:
            return movieRecommendationsRequest(query);
        case ProviderCatalogOperation::LibraryMembership:
            return libraryMembershipRequest(query);
        case ProviderCatalogOperation::ServerInfo:
            return supportedRequest(QStringLiteral("/System/Info/Public"));
        case ProviderCatalogOperation::SetHomeDismissal:
            return unsupported(QStringLiteral("Jellyfin has no Home dismissals"));
        case ProviderCatalogOperation::ClearResumePosition:
            return clearResumePositionRequest(query);
        case ProviderCatalogOperation::LibraryCollections:
            return libraryCollectionsRequest(query);
        case ProviderCatalogOperation::CollectionItems:
            return collectionItemsRequest(query);
        case ProviderCatalogOperation::ItemCollections:
            return itemCollectionsRequest(query);
        }
        return unsupported(QStringLiteral("Unknown Jellyfin catalog operation"));
    }

    ProviderCatalogResponse parseResponse(
        ProviderCatalogOperation operation,
        const QByteArray &body,
        const QHash<QByteArray, QByteArray> &responseHeaders) const override
    {
        ProviderCatalogResponse response;
        applySnapshot(response, responseHeaders);

        if (operation == ProviderCatalogOperation::SetWatched
            || operation == ProviderCatalogOperation::SetFavorite
            || operation == ProviderCatalogOperation::ClearResumePosition) {
            const QJsonDocument mutationDocument = QJsonDocument::fromJson(body);
            if (mutationDocument.isObject()) {
                response.rawItem = mutationDocument.object();
            } else if (mutationDocument.isArray()) {
                response.rawItems = mutationDocument.array();
                response.total = response.rawItems.size();
            }
            response.valid = true;
            return response;
        }

        if (body.trimmed().isEmpty()) {
            response.error = QStringLiteral("Jellyfin %1 response is empty")
                                 .arg(operationName(operation));
            return response;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            response.error = QStringLiteral("Invalid Jellyfin %1 response: %2")
                                 .arg(operationName(operation), parseError.errorString());
            return response;
        }

        if (operation == ProviderCatalogOperation::LatestMedia
            || operation == ProviderCatalogOperation::ResolveLibrary
            || operation == ProviderCatalogOperation::SpecialFeatures
            || operation == ProviderCatalogOperation::LocalTrailers) {
            if (!document.isArray()) {
                response.error = QStringLiteral("Jellyfin %1 response must be an array")
                                     .arg(operationName(operation));
                return response;
            }
            response.rawItems = document.array();
            if (operation == ProviderCatalogOperation::LocalTrailers) {
                // Every local trailer is a trailer, whatever ExtraType says.
                for (qsizetype index = 0; index < response.rawItems.size(); ++index) {
                    QJsonObject trailer = response.rawItems.at(index).toObject();
                    trailer.insert(QStringLiteral("ExtraType"), QStringLiteral("Trailer"));
                    response.rawItems.replace(index, trailer);
                }
            }
            response.total = response.rawItems.size();
            response.valid = true;
            return response;
        }

        if (operation == ProviderCatalogOperation::MovieRecommendations) {
            if (!document.isArray()) {
                response.error = QStringLiteral("Jellyfin %1 response must be an array")
                                     .arg(operationName(operation));
                return response;
            }
            response.rawItems = recommendationRows(document.array());
            response.total = response.rawItems.size();
            response.valid = true;
            return response;
        }

        if (!document.isObject()) {
            response.error = QStringLiteral("Jellyfin %1 response must be an object")
                                 .arg(operationName(operation));
            return response;
        }

        const QJsonObject root = document.object();
        if (operation == ProviderCatalogOperation::ServerInfo) {
            response.capabilityMetadata = {
                {QStringLiteral("version"), root.value(QStringLiteral("Version")).toString()},
                {QStringLiteral("productName"),
                 root.value(QStringLiteral("ProductName")).toString()}};
            response.valid = true;
            return response;
        }
        if (operation == ProviderCatalogOperation::LibraryMembership) {
            if (!root.value(QStringLiteral("Items")).isArray()) {
                response.error = QStringLiteral("Jellyfin %1 response has no Items array")
                                     .arg(operationName(operation));
                return response;
            }
            for (const QJsonValue &value : root.value(QStringLiteral("Items")).toArray()) {
                const QString itemId = value.toObject().value(QStringLiteral("Id")).toString();
                if (!itemId.isEmpty()) {
                    response.rawItems.append(QJsonObject{{QStringLiteral("itemId"), itemId}});
                }
            }
            response.total = response.rawItems.size();
            response.valid = true;
            return response;
        }
        if (operation == ProviderCatalogOperation::Person) {
            if (root.value(QStringLiteral("Id")).toString().isEmpty()
                || root.value(QStringLiteral("Name")).toString().isEmpty()) {
                response.error = QStringLiteral("Jellyfin person response has no Id or Name");
                return response;
            }
            response.rawItem = root;
            response.valid = true;
            return response;
        }

        if (operation == ProviderCatalogOperation::Item
            || operation == ProviderCatalogOperation::Chapters
            || operation == ProviderCatalogOperation::HeroOverviews
            || operation == ProviderCatalogOperation::Versions) {
            if (root.value(QStringLiteral("Items")).isArray()) {
                response.rawItems = root.value(QStringLiteral("Items")).toArray();
                if (!response.rawItems.isEmpty() && response.rawItems.first().isObject()) {
                    response.rawItem = response.rawItems.first().toObject();
                }
            } else {
                response.rawItem = root;
            }
            response.valid = true;
            return response;
        }

        if (operation == ProviderCatalogOperation::FilterOptions) {
            response.rawItem = root;
            response.rawItems = root.value(QStringLiteral("Items")).toArray();
            response.total = root.value(QStringLiteral("TotalRecordCount"))
                                 .toInt(response.rawItems.size());
            response.filterMetadata = filterMetadata(root);
            response.valid = true;
            return response;
        }

        if (!root.contains(QStringLiteral("Items"))
            || !root.value(QStringLiteral("Items")).isArray()) {
            response.error = QStringLiteral("Jellyfin %1 response has no Items array")
                                 .arg(operationName(operation));
            return response;
        }

        if (operation == ProviderCatalogOperation::LibraryLetterCount) {
            if (!root.value(QStringLiteral("TotalRecordCount")).isDouble()) {
                response.error = QStringLiteral("Jellyfin letter count has no TotalRecordCount");
                return response;
            }
            response.total = root.value(QStringLiteral("TotalRecordCount")).toInt();
            response.valid = true;
            return response;
        }

        response.rawItems = root.value(QStringLiteral("Items")).toArray();
        if (operation == ProviderCatalogOperation::LibraryLetters) {
            // The whole title-ordered list, so each title's position is its
            // index here; the sort name is what Jellyfin orders by.
            const int total = root.value(QStringLiteral("TotalRecordCount"))
                                  .toInt(static_cast<int>(response.rawItems.size()));
            if (response.rawItems.size() < total) {
                response.error = QStringLiteral("Jellyfin answered part of the list; no letter index");
                response.rawItems = {};
                return response;
            }
            QStringList sortKeys;
            sortKeys.reserve(response.rawItems.size());
            for (const QJsonValue &value : std::as_const(response.rawItems)) {
                const QJsonObject item = value.toObject();
                const QString sortName = item.value(QStringLiteral("SortName")).toString();
                sortKeys.append(sortName.isEmpty()
                                    ? item.value(QStringLiteral("Name")).toString() : sortName);
            }
            response.capabilityMetadata = LetterIndex::fromSortKeys(sortKeys);
            response.total = static_cast<int>(sortKeys.size());
            response.rawItems = {};
            response.valid = true;
            return response;
        }
        response.total = root.value(QStringLiteral("TotalRecordCount"))
                             .toInt(response.rawItems.size());
        const int startIndex = root.value(QStringLiteral("StartIndex")).toInt();
        response.hasMore = startIndex + response.rawItems.size() < response.total;
        if (root.value(QStringLiteral("Capabilities")).isObject()) {
            response.capabilityMetadata = root.value(QStringLiteral("Capabilities"))
                                              .toObject().toVariantMap();
        }
        response.valid = true;
        return response;
    }

private:
    static ProviderCatalogRequest supportedRequest(
        const QString &endpoint,
        ProviderHttpMethod method = ProviderHttpMethod::Get)
    {
        ProviderCatalogRequest request;
        request.method = method;
        request.retrySafety = method == ProviderHttpMethod::Get
            ? ProviderCatalogRetrySafety::Idempotent
            : ProviderCatalogRetrySafety::Never;
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

    static ProviderCatalogRequest requireIds(
        const ProviderCatalogQuery &query,
        bool requireUser,
        bool requireItem,
        const QString &operation)
    {
        if (requireUser && query.userId.isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin %1 requires a user ID").arg(operation));
        }
        if (requireItem && query.itemId.isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin %1 requires an item ID").arg(operation));
        }
        return supportedRequest(QString());
    }

    static QString encodedPathSegment(const QString &value)
    {
        return QString::fromLatin1(QUrl::toPercentEncoding(value));
    }

    static QString endpointWithQuery(const QString &path, const QUrlQuery &query)
    {
        QUrl url(path);
        url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    }

    static QStringList sortedValues(QStringList values)
    {
        values.removeAll(QString());
        values.removeDuplicates();
        std::sort(values.begin(), values.end(), [](const QString &left, const QString &right) {
            return left < right;
        });
        return values;
    }

    static void addJoined(
        QUrlQuery &urlQuery,
        const QString &key,
        const QStringList &values,
        const QString &separator = QStringLiteral("|"))
    {
        const QStringList normalized = sortedValues(values);
        if (!normalized.isEmpty()) {
            urlQuery.addQueryItem(key, normalized.join(separator));
        }
    }

    static std::optional<QString> nativeSort(const QString &canonicalSort)
    {
        if (canonicalSort.isEmpty() || canonicalSort == QStringLiteral("libraryOrder")) {
            return QStringLiteral("ParentIndexNumber,IndexNumber,SortName");
        }
        if (canonicalSort == QStringLiteral("title")) {
            return QStringLiteral("SortName");
        }
        if (canonicalSort == QStringLiteral("releaseDate")) {
            return QStringLiteral("PremiereDate");
        }
        if (canonicalSort == QStringLiteral("dateAdded")) {
            return QStringLiteral("DateCreated");
        }
        if (canonicalSort == QStringLiteral("rating")) {
            return QStringLiteral("CommunityRating");
        }
        if (canonicalSort == QStringLiteral("year")) {
            return QStringLiteral("ProductionYear");
        }
        if (canonicalSort == QStringLiteral("random")) {
            return QStringLiteral("Random");
        }
        if (canonicalSort == QStringLiteral("datePlayed")) {
            // Never-played titles share an empty date; the name orders them.
            return QStringLiteral("DatePlayed,SortName");
        }
        return std::nullopt;
    }

    // One order per SortBy key: Jellyfin reuses the first order for keys
    // without their own, so a tie-break key that must stay ascending gets
    // an explicit one.
    static std::optional<QString> nativeSortOrder(const QString &canonicalSort,
                                                  const QString &canonicalOrder)
    {
        QString order;
        if (canonicalOrder.isEmpty()) {
            return QString();
        }
        if (canonicalOrder == QStringLiteral("ascending")) {
            order = QStringLiteral("Ascending");
        } else if (canonicalOrder == QStringLiteral("descending")) {
            order = QStringLiteral("Descending");
        } else {
            return std::nullopt;
        }
        if (canonicalSort == QStringLiteral("datePlayed")) {
            // Never-played titles follow by name, A to Z.
            return order + QStringLiteral(",Ascending");
        }
        return order;
    }

    static QStringList standardItemFields(bool includeHeavyFields)
    {
        QStringList fields = {
            QStringLiteral("DateCreated"),
            QStringLiteral("ChildCount"),
            QStringLiteral("ParentId"),
            QStringLiteral("Overview"),
            QStringLiteral("PrimaryImageAspectRatio"),
            QStringLiteral("Studios"),
            QStringLiteral("Genres"),
            QStringLiteral("Tags"),
            QStringLiteral("SpecialEpisodeNumbers")
        };
        if (includeHeavyFields) {
            fields.prepend(QStringLiteral("Path"));
            fields.prepend(QStringLiteral("MediaSources"));
        }
        return fields;
    }

    static ProviderCatalogRequest viewsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("views"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        return supportedRequest(endpointWithQuery(QStringLiteral("/UserViews"), parameters));
    }

    static QStringList itemDetailFields()
    {
        return {
            QStringLiteral("Overview"), QStringLiteral("Genres"),
            QStringLiteral("Studios"), QStringLiteral("People"),
            QStringLiteral("ChildCount"), QStringLiteral("ParentId"),
            QStringLiteral("ProviderIds"), QStringLiteral("RecursiveItemCount"),
            QStringLiteral("MediaSources")
        };
    }

    static ProviderCatalogRequest itemQueryRequest(
        const ProviderCatalogQuery &query, const QStringList &fields)
    {
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("Ids"), query.itemId);
        if (!fields.isEmpty()) {
            parameters.addQueryItem(QStringLiteral("Fields"), fields.join(QLatin1Char(',')));
        }
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Items"), parameters));
    }

    static void applyCacheHeaders(
        ProviderCatalogRequest &request,
        const ProviderCatalogQuery &query)
    {
        if (!query.etag.isEmpty()) {
            request.extraHeaders.insert(QByteArrayLiteral("If-None-Match"), query.etag);
        }
        if (!query.lastModified.isEmpty()) {
            request.extraHeaders.insert(
                QByteArrayLiteral("If-Modified-Since"), query.lastModified);
        }
    }

    static ProviderCatalogRequest itemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("items"));
        if (!validation.supported) {
            return validation;
        }
        const std::optional<QString> sort = nativeSort(query.sortBy);
        const std::optional<QString> sortOrder = nativeSortOrder(query.sortBy, query.sortOrder);
        if (!sort || !sortOrder) {
            return unsupported(QStringLiteral("Unsupported Jellyfin catalog sort"));
        }

        QUrl url(QStringLiteral("/Items"));
        QUrlQuery urlQuery;
        urlQuery.addQueryItem(QStringLiteral("UserId"), query.userId);
        if (!query.parentId.isEmpty()) {
            urlQuery.addQueryItem(QStringLiteral("ParentId"), query.parentId);
        }
        const QStringList fields = query.fields.isEmpty()
            ? standardItemFields(query.includeHeavyFields) : query.fields;
        urlQuery.addQueryItem(QStringLiteral("Fields"), fields.join(QLatin1Char(',')));
        urlQuery.addQueryItem(
            QStringLiteral("EnableImageTypes"), QStringLiteral("Primary,Backdrop,Thumb,Logo"));
        if (query.startIndex > 0) {
            urlQuery.addQueryItem(QStringLiteral("StartIndex"), QString::number(query.startIndex));
        }
        if (query.limit > 0) {
            urlQuery.addQueryItem(QStringLiteral("Limit"), QString::number(query.limit));
        }
        if (!query.searchTerm.trimmed().isEmpty()) {
            urlQuery.addQueryItem(QStringLiteral("SearchTerm"), query.searchTerm.trimmed());
        }
        addJoined(urlQuery, QStringLiteral("Genres"), query.genres, QStringLiteral(","));
        addJoined(urlQuery, QStringLiteral("Tags"), query.tags, QStringLiteral(","));
        addJoined(urlQuery, QStringLiteral("Studios"), query.studios, QStringLiteral(","));
        if (query.minPremiereDate.isValid()) {
            urlQuery.addQueryItem(
                QStringLiteral("MinPremiereDate"),
                query.minPremiereDate.startOfDay(QTimeZone::UTC).toString(Qt::ISODate));
        }
        if (query.maxPremiereDate.isValid()) {
            urlQuery.addQueryItem(
                QStringLiteral("MaxPremiereDate"),
                query.maxPremiereDate.endOfDay(QTimeZone::UTC).toString(Qt::ISODate));
        }
        if (query.minDateLastSaved.isValid()) {
            urlQuery.addQueryItem(
                QStringLiteral("MinDateLastSaved"),
                query.minDateLastSaved.startOfDay(QTimeZone::UTC).toString(Qt::ISODate));
        }
        if (query.watched != ProviderCatalogTriState::Any) {
            urlQuery.addQueryItem(
                QStringLiteral("IsPlayed"),
                query.watched == ProviderCatalogTriState::Yes
                    ? QStringLiteral("true") : QStringLiteral("false"));
        } else if (query.unwatchedOnly) {
            urlQuery.addQueryItem(QStringLiteral("IsPlayed"), QStringLiteral("false"));
        }
        if (query.favorite != ProviderCatalogTriState::Any) {
            urlQuery.addQueryItem(
                QStringLiteral("IsFavorite"),
                query.favorite == ProviderCatalogTriState::Yes
                    ? QStringLiteral("true") : QStringLiteral("false"));
        }
        if (query.minCommunityRating > 0.0) {
            urlQuery.addQueryItem(
                QStringLiteral("MinCommunityRating"),
                QString::number(query.minCommunityRating, 'f', 1));
        }
        QStringList years;
        years.reserve(query.years.size());
        for (int year : query.years) {
            if (year > 0) {
                years.append(QString::number(year));
            }
        }
        addJoined(urlQuery, QStringLiteral("Years"), years);
        addJoined(
            urlQuery,
            QStringLiteral("IncludeItemTypes"),
            query.includeItemTypes,
            QStringLiteral(","));
        if (query.recursive) {
            urlQuery.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
        }
        urlQuery.addQueryItem(
            QStringLiteral("SortBy"),
            *sort);
        if (!sortOrder->isEmpty()) {
            urlQuery.addQueryItem(QStringLiteral("SortOrder"), *sortOrder);
        }
        url.setQuery(urlQuery);

        ProviderCatalogRequest request = supportedRequest(url.toString(QUrl::FullyEncoded));
        if (query.useCacheValidation) {
            applyCacheHeaders(request, query);
        }
        return request;
    }

    // Jellyfin has no letter index, and NameLessThan compares only the first
    // character of SortName or Name (so "The Matrix" counts under T, not M),
    // which cannot locate a letter in SortName order. The index is built
    // from the sort names of the whole filtered list instead: the library
    // query without paging, images or user data, and only SortName. A
    // server that pages anyway (Silo's compatibility listener defaults to
    // 24) answers fewer items than its total, and has no index. The count
    // read (countOnly) is the same query with Limit=0: only
    // TotalRecordCount, so LibraryService can skip a library too large to
    // download.
    static ProviderCatalogRequest lettersRequest(const ProviderCatalogQuery &query,
                                                 bool countOnly)
    {
        if (query.sortBy != QStringLiteral("title")) {
            return unsupported(QStringLiteral("The letter index counts title order only"));
        }
        ProviderCatalogQuery titles = query;
        titles.startIndex = 0;
        titles.limit = 0;
        titles.fields = {QStringLiteral("SortName")};
        titles.useCacheValidation = false;
        ProviderCatalogRequest request = itemsRequest(titles);
        if (!request.supported) {
            return request;
        }
        QUrl url(request.relativeEndpoint);
        QUrlQuery parameters(url);
        parameters.removeAllQueryItems(QStringLiteral("EnableImageTypes"));
        parameters.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
        if (countOnly) {
            parameters.removeAllQueryItems(QStringLiteral("Fields"));
            parameters.addQueryItem(QStringLiteral("Limit"), QStringLiteral("0"));
        }
        url.setQuery(parameters);
        request.relativeEndpoint = url.toString(QUrl::FullyEncoded);
        return request;
    }

    static ProviderCatalogRequest filterOptionsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("filter options"));
        if (!validation.supported) {
            return validation;
        }

        const QString facet = query.filterFacet.trimmed().toLower();
        QString path = QStringLiteral("/Items/Filters2");
        bool namedFacet = false;
        if (facet == QStringLiteral("genres")) {
            path = QStringLiteral("/Genres");
            namedFacet = true;
        } else if (facet == QStringLiteral("studios")) {
            path = QStringLiteral("/Studios");
            namedFacet = true;
        } else if (!facet.isEmpty() && facet != QStringLiteral("filters")) {
            return unsupported(QStringLiteral("Unsupported Jellyfin filter facet: %1")
                                   .arg(query.filterFacet));
        }

        QUrl url(path);
        QUrlQuery urlQuery;
        urlQuery.addQueryItem(QStringLiteral("UserId"), query.userId);
        if (!query.parentId.isEmpty()) {
            urlQuery.addQueryItem(QStringLiteral("ParentId"), query.parentId);
        }
        if (!query.includeItemTypes.isEmpty()) {
            urlQuery.addQueryItem(
                QStringLiteral("IncludeItemTypes"), query.includeItemTypes.join(QLatin1Char(',')));
        }
        if (query.recursive && !namedFacet) {
            urlQuery.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
        }
        if (namedFacet) {
            urlQuery.addQueryItem(QStringLiteral("Limit"), QStringLiteral("500"));
        }
        url.setQuery(urlQuery);
        return supportedRequest(url.toString(QUrl::FullyEncoded));
    }

    // Fields and artwork of the Home episode and movie cards. Next Up and
    // the resume list ask for the same shape so their cards match.
    static void addHomeCardParameters(QUrlQuery &parameters, int limit)
    {
        parameters.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
        parameters.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Path,Overview,ParentId"));
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
        parameters.addQueryItem(
            QStringLiteral("EnableImageTypes"), QStringLiteral("Primary,Thumb,Backdrop,Logo"));
    }

    static ProviderCatalogRequest nextUpRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("next up"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        // Part-watched next episodes stay in Next Up, so a failed resume
        // read still shows them; ContinueWatching::combine keeps the resume
        // copy. The larger page leaves room for them beside the episodes
        // still to start.
        addHomeCardParameters(parameters, 20);
        parameters.addQueryItem(QStringLiteral("EnableResumable"), QStringLiteral("true"));
        if (query.nextUpRewatching) {
            parameters.addQueryItem(QStringLiteral("EnableRewatching"), QStringLiteral("true"));
        }
        if (query.nextUpCutoff.isValid()) {
            parameters.addQueryItem(
                QStringLiteral("NextUpDateCutoff"),
                query.nextUpCutoff.startOfDay(QTimeZone::UTC).toString(Qt::ISODate));
        }
        return supportedRequest(endpointWithQuery(QStringLiteral("/Shows/NextUp"), parameters));
    }

    static ProviderCatalogRequest resumeItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("resume items"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        addHomeCardParameters(parameters, query.limit > 0 ? qMin(query.limit, 50) : 20);
        // Only the types Bloom's libraries hold; the server orders the list
        // by last played, newest first.
        parameters.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Episode"));
        parameters.addQueryItem(QStringLiteral("MediaTypes"), QStringLiteral("Video"));
        return supportedRequest(
            endpointWithQuery(QStringLiteral("/UserItems/Resume"), parameters));
    }

    static ProviderCatalogRequest latestMediaRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("latest media"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(QStringLiteral(
            "/Items/Latest?UserId=%1&ParentId=%2&Limit=10&Fields=Path,Overview,ParentId&EnableUserData=true&EnableImageTypes=Primary,Backdrop,Thumb,Logo")
                                    .arg(encodedPathSegment(query.userId), encodedPathSegment(query.parentId)));
    }

    static ProviderCatalogRequest homeBackdropsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("home backdrops"));
        if (!validation.supported) {
            return validation;
        }
        const QString fields = QStringLiteral(
            "ParentId");
        if (query.limit > 0) {
            const int limit = qBound(50, query.limit, 20000);
            return supportedRequest(QStringLiteral(
                "/Items?UserId=%1&Recursive=true&IncludeItemTypes=Movie,Series,Season,Episode&SortBy=Random&Fields=%2&EnableImages=true&EnableImageTypes=Backdrop&ImageTypeLimit=1&EnableTotalRecordCount=false&Limit=%3")
                                        .arg(encodedPathSegment(query.userId), fields)
                                        .arg(limit));
        }
        return supportedRequest(QStringLiteral(
            "/Items?UserId=%1&Recursive=true&IncludeItemTypes=Movie,Series,Season,Episode&SortBy=SortName&Fields=%2&EnableImages=true&EnableImageTypes=Backdrop&ImageTypeLimit=1&EnableTotalRecordCount=false&StartIndex=%3&Limit=250")
                                    .arg(encodedPathSegment(query.userId), fields)
                                    .arg(qMax(0, query.startIndex)));
    }

    static ProviderCatalogRequest screensaverItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("screensaver items"));
        if (!validation.supported) {
            return validation;
        }
        const int limit = qBound(10, query.limit > 0 ? query.limit : 80, 200);
        return supportedRequest(QStringLiteral(
            "/Items?UserId=%1&Recursive=true&IncludeItemTypes=Movie,Series&SortBy=Random&Fields=Overview,ParentId&EnableImages=true&EnableImageTypes=Backdrop,Logo&ImageTypeLimit=1&EnableTotalRecordCount=false&Limit=%2")
                                    .arg(encodedPathSegment(query.userId))
                                    .arg(limit));
    }

    static ProviderCatalogRequest itemRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("item"));
        if (!validation.supported) {
            return validation;
        }
        const QStringList fields = query.fields.isEmpty() ? itemDetailFields() : query.fields;
        ProviderCatalogRequest request = itemQueryRequest(query, fields);
        applyCacheHeaders(request, query);
        return request;
    }

    static ProviderCatalogRequest chaptersRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("chapters"));
        if (!validation.supported) {
            return validation;
        }
        ProviderCatalogRequest request = itemQueryRequest(
            query, {QStringLiteral("Chapters")});
        QUrl url(request.relativeEndpoint);
        QUrlQuery parameters(url);
        parameters.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("EnableImageTypes"), QStringLiteral("Chapter"));
        parameters.addQueryItem(QStringLiteral("ImageTypeLimit"), QStringLiteral("100"));
        url.setQuery(parameters);
        request.relativeEndpoint = url.toString(QUrl::FullyEncoded);
        return request;
    }

    static ProviderCatalogRequest resolveLibraryRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("library resolution"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(QStringLiteral("/Items/%1/Ancestors?UserId=%2")
                                    .arg(encodedPathSegment(query.itemId),
                                         encodedPathSegment(query.userId)));
    }

    static ProviderCatalogRequest similarItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("similar items"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(QStringLiteral(
            "/Items/%1/Similar?UserId=%2&Limit=%3&Fields=PrimaryImageAspectRatio,Overview,ChildCount")
                                    .arg(encodedPathSegment(query.itemId),
                                         encodedPathSegment(query.userId))
                                    .arg(qMax(1, query.limit)));
    }

    // GetMovieRecommendations for one library. It takes no image or user
    // data switches; the server sends user data for UserId.
    static ProviderCatalogRequest movieRecommendationsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("movie recommendations"));
        if (!validation.supported) {
            return validation;
        }
        if (query.parentId.isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin movie recommendations require a library ID"));
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("ParentId"), query.parentId);
        parameters.addQueryItem(QStringLiteral("CategoryLimit"),
                                QString::number(qBound(1, query.limit > 0 ? query.limit : 4, 10)));
        parameters.addQueryItem(
            QStringLiteral("ItemLimit"),
            QString::number(qBound(1, query.recommendationItemLimit > 0
                                          ? query.recommendationItemLimit : 15, 50)));
        parameters.addQueryItem(QStringLiteral("Fields"),
                                QStringLiteral("Overview,ParentId,PrimaryImageAspectRatio"));
        return supportedRequest(
            endpointWithQuery(QStringLiteral("/Movies/Recommendations"), parameters));
    }

    // Which of the item IDs the library holds: GetItems scoped to the
    // library, without images, user data or a total count.
    static ProviderCatalogRequest libraryMembershipRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("library membership"));
        if (!validation.supported) {
            return validation;
        }
        if (query.parentId.isEmpty() || query.itemIds.isEmpty()) {
            return unsupported(QStringLiteral(
                "Jellyfin library membership requires a library ID and item IDs"));
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("ParentId"), query.parentId);
        parameters.addQueryItem(QStringLiteral("Ids"), query.itemIds.join(QLatin1Char(',')));
        parameters.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("Limit"), QString::number(query.itemIds.size()));
        parameters.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
        parameters.addQueryItem(QStringLiteral("EnableTotalRecordCount"), QStringLiteral("false"));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Items"), parameters));
    }

    // RecommendationDto rows as {recommendationType, baselineItemName,
    // categoryId, items}; items stay wire BaseItemDto objects.
    static QJsonArray recommendationRows(const QJsonArray &wireRows)
    {
        QJsonArray rows;
        for (const QJsonValue &value : wireRows) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject wireRow = value.toObject();
            rows.append(QJsonObject{
                {QStringLiteral("recommendationType"),
                 wireRow.value(QStringLiteral("RecommendationType")).toString()},
                {QStringLiteral("baselineItemName"),
                 wireRow.value(QStringLiteral("BaselineItemName")).toString()},
                {QStringLiteral("categoryId"), wireRow.value(QStringLiteral("CategoryId")).toString()},
                {QStringLiteral("items"), wireRow.value(QStringLiteral("Items")).toArray()}});
        }
        return rows;
    }

    // GetSpecialFeatures and GetLocalTrailers both answer a bare
    // BaseItemDto array. Remote trailers (RemoteTrailers) are not extras.
    static ProviderCatalogRequest extrasRequest(const ProviderCatalogQuery &query,
                                                const QString &route)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("extras"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(QStringLiteral("/Items/%1/%2?userId=%3")
                                    .arg(encodedPathSegment(query.itemId), route,
                                         encodedPathSegment(query.userId)));
    }

    static ProviderCatalogRequest nextUnplayedEpisodeRequest(
        const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("next unplayed episode"));
        if (!validation.supported) {
            return validation;
        }
        const QString seriesId = query.seriesId.isEmpty() ? query.parentId : query.seriesId;
        if (seriesId.isEmpty()) {
            return unsupported(QStringLiteral(
                "Jellyfin next unplayed episode requires a series ID"));
        }
        return supportedRequest(QStringLiteral(
            "/Items?UserId=%1&ParentId=%2&Recursive=true&IncludeItemTypes=Episode&Fields=SortName,Overview,ParentId,SpecialEpisodeNumbers&SortBy=ParentIndexNumber,IndexNumber,SortName&EnableUserData=true&EnableImageTypes=Primary,Thumb")
                                    .arg(encodedPathSegment(query.userId), encodedPathSegment(seriesId)));
    }

    static ProviderCatalogRequest stateRequest(
        const ProviderCatalogQuery &query,
        const QString &collection)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("state update"));
        if (!validation.supported) {
            return validation;
        }
        ProviderCatalogRequest request = supportedRequest(
            QStringLiteral("/%1/%2?UserId=%3")
                .arg(collection, encodedPathSegment(query.itemId), encodedPathSegment(query.userId)),
            query.stateValue ? ProviderHttpMethod::Post : ProviderHttpMethod::Delete);
        if (query.stateValue) {
            request.extraHeaders.insert(
                QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
        }
        return request;
    }

    // UpdateItemUserData applies only the fields the body carries, so a
    // zero PlaybackPositionTicks leaves Played, PlayCount and IsFavorite
    // as they are; the item leaves the resume list. The server declares no
    // replay semantics, so the write is never retried.
    static ProviderCatalogRequest clearResumePositionRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("resume position update"));
        if (!validation.supported) {
            return validation;
        }
        ProviderCatalogRequest request = supportedRequest(
            QStringLiteral("/UserItems/%1/UserData?UserId=%2")
                .arg(encodedPathSegment(query.itemId), encodedPathSegment(query.userId)),
            ProviderHttpMethod::Post);
        request.body = QByteArrayLiteral(R"({"PlaybackPositionTicks":0})");
        request.extraHeaders.insert(
            QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
        return request;
    }

    static ProviderCatalogRequest searchRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("search"));
        if (!validation.supported) {
            return validation;
        }
        const int limit = query.limit > 0 ? query.limit : 50;
        QString endpoint = QStringLiteral(
            "/Items?UserId=%1&SearchTerm=%2&IncludeItemTypes=Movie,Series&Recursive=true&Fields=Path,Overview&EnableUserData=true&Limit=%3&EnableImageTypes=Primary,Backdrop")
                               .arg(encodedPathSegment(query.userId))
                               .arg(QString::fromUtf8(
                                   QUrl::toPercentEncoding(query.searchTerm.trimmed())))
                               .arg(limit);
        // One library at a time; LibrarySearchScope combines several.
        if (!query.parentId.isEmpty()) {
            endpoint += QStringLiteral("&ParentId=") + encodedPathSegment(query.parentId);
        }
        return supportedRequest(endpoint);
    }

    // GetItem by ID: names are not unique, so the name-keyed /Persons/{name}
    // route could answer another person.
    static ProviderCatalogRequest personRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("person"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("userId"), query.userId);
        return supportedRequest(endpointWithQuery(
            QStringLiteral("/Items/%1").arg(encodedPathSegment(query.itemId)), parameters));
    }

    // The person's movies and series, newest first, one page at a time.
    static ProviderCatalogRequest personItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("person items"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("PersonIds"), query.itemId);
        parameters.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Series"));
        parameters.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("PremiereDate,SortName"));
        parameters.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Descending,Ascending"));
        parameters.addQueryItem(QStringLiteral("Fields"),
                                QStringLiteral("Overview,PrimaryImageAspectRatio,ChildCount"));
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("EnableImageTypes"),
                                QStringLiteral("Primary,Backdrop,Thumb,Logo"));
        parameters.addQueryItem(QStringLiteral("StartIndex"),
                                QString::number(qMax(0, query.startIndex)));
        parameters.addQueryItem(QStringLiteral("Limit"),
                                QString::number(query.limit > 0 ? query.limit : 50));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Items"), parameters));
    }

    // The box sets that hold titles of one library, by name. A BoxSet-only
    // query under a library folder is answered from the library's titles
    // collapsed into their box sets, which is how Jellyfin's own movie
    // library page lists its Collections tab.
    static ProviderCatalogRequest libraryCollectionsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("library collections"));
        if (!validation.supported) {
            return validation;
        }
        if (query.parentId.trimmed().isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin library collections require a library ID"));
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("ParentId"), query.parentId.trimmed());
        parameters.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("BoxSet"));
        parameters.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
        parameters.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
        parameters.addQueryItem(QStringLiteral("Fields"), collectionFields());
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
        parameters.addQueryItem(QStringLiteral("EnableImageTypes"),
                                QStringLiteral("Primary,Backdrop,Thumb"));
        parameters.addQueryItem(QStringLiteral("StartIndex"),
                                QString::number(qMax(0, query.startIndex)));
        parameters.addQueryItem(QStringLiteral("Limit"),
                                QString::number(query.limit > 0 ? query.limit : 100));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Items"), parameters));
    }

    // One box set's movies and series. Without a sort the box set keeps its
    // own display order; release date runs oldest first, the way a
    // franchise is watched.
    static ProviderCatalogRequest collectionItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("collection items"));
        if (!validation.supported) {
            return validation;
        }
        QString sortBy;
        const QString sort = query.sortBy.trimmed();
        if (sort == QStringLiteral("title")) {
            sortBy = QStringLiteral("SortName");
        } else if (sort == QStringLiteral("releaseDate")) {
            sortBy = QStringLiteral("PremiereDate,ProductionYear,SortName");
        } else if (!sort.isEmpty() && sort != QStringLiteral("collectionOrder")) {
            return unsupported(QStringLiteral("Unsupported Jellyfin collection sort"));
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("UserId"), query.userId);
        parameters.addQueryItem(QStringLiteral("ParentId"), query.itemId);
        parameters.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Series"));
        if (!sortBy.isEmpty()) {
            parameters.addQueryItem(QStringLiteral("SortBy"), sortBy);
            parameters.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
        }
        parameters.addQueryItem(QStringLiteral("Fields"),
                                standardItemFields(false).join(QLatin1Char(',')));
        parameters.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("EnableImageTypes"),
                                QStringLiteral("Primary,Backdrop,Thumb,Logo"));
        parameters.addQueryItem(QStringLiteral("StartIndex"),
                                QString::number(qMax(0, query.startIndex)));
        parameters.addQueryItem(QStringLiteral("Limit"),
                                QString::number(query.limit > 0 ? query.limit : 100));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Items"), parameters));
    }

    // GetItemCollections: the collections that include one title (API 12;
    // older servers answer 404).
    static ProviderCatalogRequest itemCollectionsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("item collections"));
        if (!validation.supported) {
            return validation;
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("userId"), query.userId);
        parameters.addQueryItem(QStringLiteral("fields"), collectionFields());
        parameters.addQueryItem(QStringLiteral("limit"),
                                QString::number(query.limit > 0 ? query.limit : 20));
        return supportedRequest(endpointWithQuery(
            QStringLiteral("/Items/%1/Collections").arg(encodedPathSegment(query.itemId)),
            parameters));
    }

    static QString collectionFields()
    {
        return QStringLiteral("Overview,ChildCount,PrimaryImageAspectRatio");
    }

    // GetPersons, not /Search/Hints. GetPersons has no media scope, so
    // personTypes keeps it to film and TV credits (no artists or authors).
    static ProviderCatalogRequest personSearchRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("people search"));
        if (!validation.supported) {
            return validation;
        }
        const QString term = query.searchTerm.trimmed();
        if (term.isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin people search requires a search term"));
        }
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("userId"), query.userId);
        // QUrlQuery leaves '+' as is, which the server reads as a space.
        parameters.addQueryItem(QStringLiteral("searchTerm"),
                                QString::fromUtf8(QUrl::toPercentEncoding(term)));
        parameters.addQueryItem(QStringLiteral("personTypes"),
                                QStringLiteral("Actor,Director,Writer,Producer,GuestStar"));
        parameters.addQueryItem(QStringLiteral("limit"),
                                QString::number(query.limit > 0 ? query.limit : 20));
        parameters.addQueryItem(QStringLiteral("enableImages"), QStringLiteral("true"));
        parameters.addQueryItem(QStringLiteral("enableImageTypes"), QStringLiteral("Primary"));
        return supportedRequest(endpointWithQuery(QStringLiteral("/Persons"), parameters));
    }

    static ProviderCatalogRequest randomItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("random items"));
        if (!validation.supported) {
            return validation;
        }
        const int limit = query.limit > 0 ? query.limit : 50;
        return supportedRequest(QStringLiteral(
            "/Items?UserId=%1&IncludeItemTypes=Movie,Series&Recursive=true&SortBy=Random&Limit=%2&Fields=Overview")
                                    .arg(encodedPathSegment(query.userId))
                                    .arg(limit));
    }

    static ProviderCatalogRequest heroItemsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("hero items"));
        if (!validation.supported) {
            return validation;
        }
        if (query.parentId.isEmpty() && query.parentIds.size() > 1) {
            return unsupported(QStringLiteral(
                "Jellyfin hero items require one request per parent ID"));
        }
        const QString parentId = !query.parentId.isEmpty()
            ? query.parentId
            : (query.parentIds.isEmpty() ? QString() : query.parentIds.constFirst());
        QString endpoint = QStringLiteral(
            "/Items?UserId=%1&IncludeItemTypes=Movie,Series&Recursive=true&SortBy=Random&Limit=%2&Fields=Overview,ParentId,Genres,Studios,Tags&EnableUserData=true&EnableImageTypes=Primary,Backdrop,Thumb,Logo&ImageTypeLimit=1")
                               .arg(encodedPathSegment(query.userId))
                               .arg(qBound(1, query.limit, 25));
        if (!parentId.isEmpty()) {
            endpoint += QStringLiteral("&ParentId=") + encodedPathSegment(parentId);
        }
        if (query.unwatchedOnly) {
            endpoint += QStringLiteral("&IsPlayed=false");
        }
        return supportedRequest(endpoint);
    }

    static ProviderCatalogRequest heroOverviewRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("hero overview"));
        if (!validation.supported) {
            return validation;
        }
        if (query.seriesId.isEmpty() && query.seriesIds.size() > 1) {
            return unsupported(QStringLiteral(
                "Jellyfin hero overviews require one request per series ID"));
        }
        const QString seriesId = !query.seriesId.isEmpty()
            ? query.seriesId
            : (query.seriesIds.isEmpty() ? QString() : query.seriesIds.constFirst());
        if (seriesId.isEmpty()) {
            return unsupported(QStringLiteral("Jellyfin hero overview requires a series ID"));
        }
        ProviderCatalogQuery itemQuery = query;
        itemQuery.itemId = seriesId;
        return itemQueryRequest(itemQuery, {QStringLiteral("Overview")});
    }

    static ProviderCatalogRequest versionsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, true, QStringLiteral("versions"));
        if (!validation.supported) {
            return validation;
        }
        return itemQueryRequest(query, {QStringLiteral("MediaSources")});
    }

    static ProviderCatalogRequest themeSongsRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, false, true, QStringLiteral("theme songs"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(
            QStringLiteral("/Items/%1/ThemeSongs")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(query.itemId))));
    }

    static QByteArray responseHeader(
        const QHash<QByteArray, QByteArray> &headers,
        const QByteArray &name)
    {
        for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
                return it.value();
            }
        }
        return {};
    }

    static void applySnapshot(
        ProviderCatalogResponse &response,
        const QHash<QByteArray, QByteArray> &headers)
    {
        const QByteArray etag = responseHeader(headers, QByteArrayLiteral("ETag"));
        const QByteArray lastModified = responseHeader(
            headers, QByteArrayLiteral("Last-Modified"));
        if (!etag.isEmpty()) {
            response.snapshot.insert(
                QStringLiteral("etag"), QString::fromUtf8(etag));
        }
        if (!lastModified.isEmpty()) {
            response.snapshot.insert(
                QStringLiteral("lastModified"), QString::fromUtf8(lastModified));
        }
    }

    static QVariantList namedValues(const QJsonArray &values)
    {
        QVariantList result;
        result.reserve(values.size());
        for (const QJsonValue &value : values) {
            if (value.isString()) {
                result.append(value.toString());
            } else if (value.isObject()) {
                const QString name = value.toObject().value(QStringLiteral("Name")).toString();
                if (!name.isEmpty()) {
                    result.append(name);
                }
            }
        }
        return result;
    }

    static QVariantMap filterMetadata(const QJsonObject &root)
    {
        QVariantMap metadata;
        if (root.value(QStringLiteral("Genres")).isArray()) {
            metadata.insert(
                QStringLiteral("genres"),
                namedValues(root.value(QStringLiteral("Genres")).toArray()));
        }
        if (root.value(QStringLiteral("Tags")).isArray()) {
            metadata.insert(
                QStringLiteral("tags"),
                namedValues(root.value(QStringLiteral("Tags")).toArray()));
        }
        if (root.value(QStringLiteral("Items")).isArray()) {
            metadata.insert(
                QStringLiteral("namedItems"),
                namedValues(root.value(QStringLiteral("Items")).toArray()));
        }
        if (root.value(QStringLiteral("Filters")).isObject()) {
            metadata.insert(
                QStringLiteral("filters"),
                root.value(QStringLiteral("Filters")).toObject().toVariantMap());
        }
        return metadata;
    }

    static QString operationName(ProviderCatalogOperation operation)
    {
        switch (operation) {
        case ProviderCatalogOperation::Views: return QStringLiteral("views");
        case ProviderCatalogOperation::Items: return QStringLiteral("items");
        case ProviderCatalogOperation::FilterOptions: return QStringLiteral("filter options");
        case ProviderCatalogOperation::NextUp: return QStringLiteral("next up");
        case ProviderCatalogOperation::ResumeItems: return QStringLiteral("resume items");
        case ProviderCatalogOperation::LatestMedia: return QStringLiteral("latest media");
        case ProviderCatalogOperation::HomeBackdrops: return QStringLiteral("home backdrops");
        case ProviderCatalogOperation::ScreensaverItems: return QStringLiteral("screensaver items");
        case ProviderCatalogOperation::Item: return QStringLiteral("item");
        case ProviderCatalogOperation::Chapters: return QStringLiteral("chapters");
        case ProviderCatalogOperation::ResolveLibrary: return QStringLiteral("library resolution");
        case ProviderCatalogOperation::SimilarItems: return QStringLiteral("similar items");
        case ProviderCatalogOperation::NextUnplayedEpisode: return QStringLiteral("next unplayed episode");
        case ProviderCatalogOperation::SetWatched: return QStringLiteral("watched state");
        case ProviderCatalogOperation::SetFavorite: return QStringLiteral("favorite state");
        case ProviderCatalogOperation::Search: return QStringLiteral("search");
        case ProviderCatalogOperation::RandomItems: return QStringLiteral("random items");
        case ProviderCatalogOperation::HeroItems: return QStringLiteral("hero items");
        case ProviderCatalogOperation::HeroOverviews: return QStringLiteral("hero overviews");
        case ProviderCatalogOperation::Versions: return QStringLiteral("versions");
        case ProviderCatalogOperation::ThemeSongs: return QStringLiteral("theme songs");
        case ProviderCatalogOperation::ThemeSongsCapability: return QStringLiteral("theme capability");
        case ProviderCatalogOperation::ThemeSongPlayback: return QStringLiteral("theme playback");
        case ProviderCatalogOperation::PosterBadgeSettings: return QStringLiteral("poster badge settings");
        case ProviderCatalogOperation::PosterBadgeDefaults: return QStringLiteral("poster badge defaults");
        case ProviderCatalogOperation::HomeSections: return QStringLiteral("home sections");
        case ProviderCatalogOperation::HomeCapabilities: return QStringLiteral("home capabilities");
        case ProviderCatalogOperation::Watchlist: return QStringLiteral("watchlist");
        case ProviderCatalogOperation::SetWatchlist: return QStringLiteral("watchlist state");
        case ProviderCatalogOperation::NotInterestedCapability: return QStringLiteral("not interested capability");
        case ProviderCatalogOperation::SetNotInterested: return QStringLiteral("not interested state");
        case ProviderCatalogOperation::UpcomingAirings: return QStringLiteral("upcoming airings");
        case ProviderCatalogOperation::MediaRequestStatus: return QStringLiteral("request capability");
        case ProviderCatalogOperation::MediaRequestSearch: return QStringLiteral("request search");
        case ProviderCatalogOperation::MediaRequestDetail: return QStringLiteral("request detail");
        case ProviderCatalogOperation::CreateMediaRequest: return QStringLiteral("create request");
        case ProviderCatalogOperation::MediaRequestDiscover: return QStringLiteral("discover");
        case ProviderCatalogOperation::MediaRequestDiscoverSection: return QStringLiteral("discover row");
        case ProviderCatalogOperation::MediaRequestList: return QStringLiteral("request list");
        case ProviderCatalogOperation::CancelMediaRequest: return QStringLiteral("cancel request");
        case ProviderCatalogOperation::SetMediaRequestFollow: return QStringLiteral("request follow");
        case ProviderCatalogOperation::SetWatchlistTitle: return QStringLiteral("watchlist title");
        case ProviderCatalogOperation::MediaRequestBrowseCategories: return QStringLiteral("browse categories");
        case ProviderCatalogOperation::MediaRequestBrowse: return QStringLiteral("browse");
        case ProviderCatalogOperation::SpecialFeatures: return QStringLiteral("special features");
        case ProviderCatalogOperation::LocalTrailers: return QStringLiteral("local trailers");
        case ProviderCatalogOperation::Person: return QStringLiteral("person");
        case ProviderCatalogOperation::PersonItems: return QStringLiteral("person items");
        case ProviderCatalogOperation::PersonSearch: return QStringLiteral("people search");
        case ProviderCatalogOperation::LibrarySections: return QStringLiteral("library sections");
        case ProviderCatalogOperation::LibrarySectionItems: return QStringLiteral("library section items");
        case ProviderCatalogOperation::CatalogSearchCapabilities: return QStringLiteral("search capabilities");
        case ProviderCatalogOperation::LibraryLetters: return QStringLiteral("letter index");
        case ProviderCatalogOperation::LibraryLetterCount: return QStringLiteral("letter count");
        case ProviderCatalogOperation::MovieRecommendations: return QStringLiteral("movie recommendations");
        case ProviderCatalogOperation::LibraryMembership: return QStringLiteral("library membership");
        case ProviderCatalogOperation::ServerInfo: return QStringLiteral("server info");
        case ProviderCatalogOperation::SetHomeDismissal: return QStringLiteral("home dismissal");
        case ProviderCatalogOperation::ClearResumePosition: return QStringLiteral("resume position");
        case ProviderCatalogOperation::LibraryCollections: return QStringLiteral("library collections");
        case ProviderCatalogOperation::CollectionItems: return QStringLiteral("collection items");
        case ProviderCatalogOperation::ItemCollections: return QStringLiteral("item collections");
        }
        return QStringLiteral("catalog");
    }
};
