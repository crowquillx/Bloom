#pragma once

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
            || operation == ProviderCatalogOperation::SetFavorite) {
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
            || operation == ProviderCatalogOperation::ResolveLibrary) {
            if (!document.isArray()) {
                response.error = QStringLiteral("Jellyfin %1 response must be an array")
                                     .arg(operationName(operation));
                return response;
            }
            response.rawItems = document.array();
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

        response.rawItems = root.value(QStringLiteral("Items")).toArray();
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
        return std::nullopt;
    }

    static std::optional<QString> nativeSortOrder(const QString &canonicalOrder)
    {
        if (canonicalOrder.isEmpty()) {
            return QString();
        }
        if (canonicalOrder == QStringLiteral("ascending")) {
            return QStringLiteral("Ascending");
        }
        if (canonicalOrder == QStringLiteral("descending")) {
            return QStringLiteral("Descending");
        }
        return std::nullopt;
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
        const std::optional<QString> sortOrder = nativeSortOrder(query.sortOrder);
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

    static ProviderCatalogRequest nextUpRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("next up"));
        if (!validation.supported) {
            return validation;
        }
        return supportedRequest(QStringLiteral(
            "/Shows/NextUp?UserId=%1&Limit=10&Fields=Path,Overview,ParentId&EnableUserData=true&EnableImageTypes=Primary,Thumb,Backdrop,Logo")
                                    .arg(encodedPathSegment(query.userId)));
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

    static ProviderCatalogRequest searchRequest(const ProviderCatalogQuery &query)
    {
        const ProviderCatalogRequest validation = requireIds(
            query, true, false, QStringLiteral("search"));
        if (!validation.supported) {
            return validation;
        }
        const int limit = query.limit > 0 ? query.limit : 50;
        return supportedRequest(QStringLiteral(
            "/Items?UserId=%1&SearchTerm=%2&IncludeItemTypes=Movie,Series&Recursive=true&Fields=Path,Overview&EnableUserData=true&Limit=%3&EnableImageTypes=Primary,Backdrop")
                                    .arg(encodedPathSegment(query.userId))
                                    .arg(QString::fromUtf8(
                                        QUrl::toPercentEncoding(query.searchTerm.trimmed())))
                                    .arg(limit));
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
        }
        return QStringLiteral("catalog");
    }
};
