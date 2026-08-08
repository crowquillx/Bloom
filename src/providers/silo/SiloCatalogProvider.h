#pragma once

#include "providers/ICatalogProvider.h"
#include "providers/silo/SiloModelMapper.h"

#include <QJsonDocument>
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
                latest.sortBy = QStringLiteral("added_at");
                latest.sortOrder = QStringLiteral("desc");
            }
            return createCatalogRequest(latest, false);
        }
        case ProviderCatalogOperation::Item:
            return getWithContentId(QStringLiteral("/api/v1/catalog/items/%1"), query.itemId);
        case ProviderCatalogOperation::Chapters:
        case ProviderCatalogOperation::Versions:
            return getWithContentId(QStringLiteral("/api/v1/catalog/items/%1/versions"),
                                    query.itemId);
        case ProviderCatalogOperation::ThemeSongs:
            return unsupported(themeSongsUnsupportedReason());
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
                QStringLiteral("/api/v1/recommendations/similar/%1"), query.itemId);
            if (!request.supported) {
                return request;
            }
            QUrlQuery parameters;
            appendLimit(parameters, query.limit);
            request.relativeEndpoint = endpointWithQuery(request.relativeEndpoint, parameters);
            return request;
        }
        case ProviderCatalogOperation::SetWatched:
            return mutationWithContentId(query.stateValue ? ProviderHttpMethod::Post
                                                          : ProviderHttpMethod::Delete,
                                         QStringLiteral("/api/v1/watched/%1"),
                                         query.itemId);
        case ProviderCatalogOperation::SetFavorite:
            return mutationWithContentId(query.stateValue ? ProviderHttpMethod::Put
                                                          : ProviderHttpMethod::Delete,
                                         QStringLiteral("/api/v1/favorites/%1"),
                                         query.itemId);
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
            return get(QStringLiteral("/api/v1/home/sections/system-next-up/items"));
        case ProviderCatalogOperation::HomeBackdrops:
        case ProviderCatalogOperation::ScreensaverItems:
        case ProviderCatalogOperation::RandomItems:
        case ProviderCatalogOperation::HeroItems:
        case ProviderCatalogOperation::HeroOverviews:
            return unsupported(QStringLiteral(
                "Silo has no native endpoint with the requested selection semantics"));
        case ProviderCatalogOperation::NextUnplayedEpisode:
            return unsupported(QStringLiteral(
                "Silo has no series-scoped next-unplayed-episode endpoint at revision 8044eb84"));
        case ProviderCatalogOperation::ResolveLibrary:
            return unsupported(QStringLiteral(
                "Silo catalog resources do not expose an ancestor-to-library resolution route"));
        }
        return unsupported(QStringLiteral("Unsupported Silo catalog operation"));
    }

    ProviderCatalogResponse parseResponse(
        ProviderCatalogOperation operation,
        const QByteArray &body,
        const QHash<QByteArray, QByteArray> &responseHeaders) const override
    {
        if (operation == ProviderCatalogOperation::ThemeSongs) {
            ProviderCatalogResponse response;
            response.error = themeSongsUnsupportedReason();
            return response;
        }

        // The native numeric-season detail endpoint returns {"season": {...}},
        // unlike the Items operation's array/envelope responses. Parse that
        // envelope as an item even when the request originated as Items.
        const QJsonDocument document = QJsonDocument::fromJson(body);
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
    struct Rule
    {
        QString field;
        QString op;
        QJsonValue value;
    };

    static QString themeSongsUnsupportedReason()
    {
        return QStringLiteral(
            "Silo's native catalog contract has no theme-song endpoint");
    }

    static ProviderCatalogRequest get(const QString &endpoint)
    {
        ProviderCatalogRequest request;
        request.method = ProviderHttpMethod::Get;
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

    static std::optional<QString> normalizedSort(const QString &wireSort)
    {
        const QString sort = wireSort.trimmed().toLower();
        if (sort.isEmpty()) {
            return QString();
        }
        if (sort.contains(QLatin1Char(','))) {
            return std::nullopt;
        }
        if (sort == QStringLiteral("sortname") || sort == QStringLiteral("sort_name")
            || sort == QStringLiteral("title")) {
            return QStringLiteral("title");
        }
        if (sort == QStringLiteral("datecreated") || sort == QStringLiteral("date_created")
            || sort == QStringLiteral("added_at") || sort == QStringLiteral("recently_added")) {
            return QStringLiteral("added_at");
        }
        if (sort == QStringLiteral("premieredate") || sort == QStringLiteral("premiere_date")
            || sort == QStringLiteral("release_date")) {
            return QStringLiteral("release_date");
        }
        if (sort == QStringLiteral("productionyear") || sort == QStringLiteral("production_year")
            || sort == QStringLiteral("year")) {
            return QStringLiteral("year");
        }
        if (sort == QStringLiteral("communityrating")
            || sort == QStringLiteral("community_rating")
            || sort == QStringLiteral("rating") || sort == QStringLiteral("rating_imdb")) {
            return QStringLiteral("rating_imdb");
        }
        if (sort == QStringLiteral("runtime") || sort == QStringLiteral("content_rating")
            || sort == QStringLiteral("last_air_date")
            || sort == QStringLiteral("latest_episode_added")
            || sort == QStringLiteral("resolution") || sort == QStringLiteral("bitrate")
            || sort == QStringLiteral("progress") || sort == QStringLiteral("date_viewed")
            || sort == QStringLiteral("plays") || sort == QStringLiteral("author")
            || sort == QStringLiteral("narrator") || sort == QStringLiteral("series")) {
            return sort;
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
        if (!order.isEmpty() && order != QStringLiteral("asc") && order != QStringLiteral("desc")
            && order != QStringLiteral("ascending") && order != QStringLiteral("descending")) {
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
        for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            const QJsonObject group = groups.at(groupIndex).toObject();
            const QString groupPrefix = QStringLiteral("groups[%1]").arg(groupIndex);
            parameters.addQueryItem(groupPrefix + QStringLiteral("[match]"),
                                    group.value(QStringLiteral("match")).toString());
            const QJsonArray rules = group.value(QStringLiteral("rules")).toArray();
            for (qsizetype ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
                const QJsonObject rule = rules.at(ruleIndex).toObject();
                const QString prefix = groupPrefix
                    + QStringLiteral("[rules][%1]").arg(ruleIndex);
                parameters.addQueryItem(prefix + QStringLiteral("[field]"),
                                        rule.value(QStringLiteral("field")).toString());
                parameters.addQueryItem(prefix + QStringLiteral("[op]"),
                                        rule.value(QStringLiteral("op")).toString());
                const QJsonValue value = rule.value(QStringLiteral("value"));
                if (value.isArray()) {
                    const QJsonArray values = value.toArray();
                    for (qsizetype valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                        parameters.addQueryItem(
                            prefix + QStringLiteral("[value][%1]").arg(valueIndex),
                            values.at(valueIndex).toVariant().toString());
                    }
                } else {
                    parameters.addQueryItem(prefix + QStringLiteral("[value]"),
                                            value.toVariant().toString());
                }
            }
        }
    }

    static ProviderCatalogRequest createViewsRequest(const ProviderCatalogQuery &query)
    {
        if (query.parentId.trimmed().isEmpty()) {
            if (hasCatalogModifiers(query) || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo library discovery does not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v1/user/libraries"));
        }
        if (query.parentId.trimmed().compare(
                QStringLiteral("home"), Qt::CaseInsensitive) == 0) {
            if (hasCatalogModifiers(query) || !query.itemId.trimmed().isEmpty()) {
                return unsupported(QStringLiteral(
                    "Silo home section discovery does not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v1/home/sections"));
        }
        bool libraryIdOk = false;
        const int libraryId = query.parentId.trimmed().toInt(&libraryIdOk);
        if (!libraryIdOk || libraryId <= 0 || hasCatalogModifiers(query)
            || !query.itemId.trimmed().isEmpty()) {
            return unsupported(QStringLiteral(
                "Silo library section discovery requires only a positive numeric library ID"));
        }
        return get(QStringLiteral("/api/v1/library/%1/sections").arg(libraryId));
    }

    static ProviderCatalogRequest createItemsRequest(const ProviderCatalogQuery &query)
    {
        if (query.parentId.trimmed().compare(
                QStringLiteral("home"), Qt::CaseInsensitive) == 0
            && !query.itemId.trimmed().isEmpty()) {
            if (hasCatalogModifiers(query)) {
                return unsupported(QStringLiteral(
                    "Silo home section items do not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v1/home/sections/%1/items")
                           .arg(encodedId(query.itemId)));
        }
        bool sectionLibraryIdOk = false;
        const int sectionLibraryId =
            query.parentId.trimmed().toInt(&sectionLibraryIdOk);
        if (sectionLibraryIdOk && sectionLibraryId > 0
            && !query.itemId.trimmed().isEmpty()) {
            if (hasCatalogModifiers(query)) {
                return unsupported(QStringLiteral(
                    "Silo library section items do not support catalog modifiers"));
            }
            return get(QStringLiteral("/api/v1/library/%1/sections/%2/items")
                           .arg(sectionLibraryId)
                           .arg(encodedId(query.itemId)));
        }
        if (!query.seriesId.trimmed().isEmpty()) {
            if (hasHierarchyModifiers(query)) {
                return unsupported(QStringLiteral(
                    "Silo hierarchy routes do not support catalog filters or pagination"));
            }
            const QString parent = query.parentId.trimmed();
            if (!query.includeItemTypes.isEmpty()) {
                const QString expectedType = parent.isEmpty()
                    ? QStringLiteral("season")
                    : QStringLiteral("episode");
                if (query.includeItemTypes.size() != 1
                    || normalizedItemType(query.includeItemTypes.first()) != expectedType) {
                    return unsupported(QStringLiteral(
                        "Silo hierarchy item type does not match the requested resource"));
                }
            }
            if (!parent.isEmpty()) {
                bool seasonNumberOk = false;
                const int seasonNumber = parent.toInt(&seasonNumberOk);
                if (seasonNumberOk && seasonNumber >= 0) {
                    const QString route = query.includeItemTypes.isEmpty()
                        || normalizedItemType(query.includeItemTypes.first())
                               == QStringLiteral("season")
                        ? QStringLiteral("/api/v1/catalog/series/%1/seasons/%2")
                        : QStringLiteral("/api/v1/catalog/series/%1/seasons/%2/episodes");
                    return get(
                        route.arg(encodedId(query.seriesId), QString::number(seasonNumber)));
                }
                return getWithContentId(QStringLiteral("/api/v1/catalog/items/%1/episodes"),
                                        parent);
            }
            return getWithContentId(QStringLiteral("/api/v1/catalog/series/%1/seasons"),
                                    query.seriesId);
        }

        const QString trimmedParentId = query.parentId.trimmed();
        bool libraryIdOk = true;
        if (!trimmedParentId.isEmpty()) {
            static_cast<void>(trimmedParentId.toInt(&libraryIdOk));
        }
        if (!libraryIdOk && query.includeItemTypes.size() == 1) {
            if (hasHierarchyModifiers(query)) {
                return unsupported(QStringLiteral(
                    "Silo hierarchy routes do not support catalog filters or pagination"));
            }
            const QString type = normalizedItemType(query.includeItemTypes.first());
            if (type == QStringLiteral("season")) {
                return getWithContentId(QStringLiteral("/api/v1/catalog/series/%1/seasons"),
                                        query.parentId);
            }
            if (type == QStringLiteral("episode")) {
                return getWithContentId(QStringLiteral("/api/v1/catalog/items/%1/episodes"),
                                        query.parentId);
            }
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

        const QString trimmedParentId = query.parentId.trimmed();
        bool libraryIdOk = true;
        const int libraryId = trimmedParentId.isEmpty()
            ? 0 : trimmedParentId.toInt(&libraryIdOk);
        if (!libraryIdOk
            || (!trimmedParentId.isEmpty() && libraryId <= 0)) {
            return unsupported(QStringLiteral(
                "Silo library catalog scope requires a positive numeric library ID"));
        }

        const QJsonArray groups = groupsForPost(query);
        const bool hasStructuredFilters = !groups.isEmpty();
        const bool hasSnapshot = query.snapshot.has_value() && !query.snapshot->isEmpty();
        const bool usePost = allowPost && hasStructuredFilters
            && query.searchTerm.trimmed().isEmpty() && query.includeHeavyFields
            && !hasSnapshot;
        const std::optional<QString> sort = normalizedSort(query.sortBy);
        QString order = query.sortOrder.trimmed().toLower();
        if (order == QStringLiteral("ascending")) order = QStringLiteral("asc");
        if (order == QStringLiteral("descending")) order = QStringLiteral("desc");

        if (usePost) {
            QJsonObject body{{QStringLiteral("match"), QStringLiteral("all")},
                             {QStringLiteral("groups"), groups},
                             {QStringLiteral("offset"), query.startIndex}};
            if (libraryId > 0) {
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
            request.relativeEndpoint = QStringLiteral("/api/v1/catalog/query");
            request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
            request.extraHeaders.insert(QByteArrayLiteral("Content-Type"),
                                        QByteArrayLiteral("application/json"));
            request.supported = true;
            return request;
        }

        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("source"), QStringLiteral("query"));
        parameters.addQueryItem(
            QStringLiteral("include_technical"),
            query.includeHeavyFields ? QStringLiteral("true") : QStringLiteral("false"));
        if (libraryId > 0) {
            parameters.addQueryItem(QStringLiteral("library_id"), QString::number(libraryId));
        }
        if (query.snapshot.has_value() && !query.snapshot->isEmpty()) {
            parameters.addQueryItem(QStringLiteral("snapshot"), *query.snapshot);
        }
        if (query.startIndex > 0) {
            parameters.addQueryItem(QStringLiteral("offset"), QString::number(query.startIndex));
        }
        appendLimit(parameters, query.limit);
        if (!query.searchTerm.trimmed().isEmpty()) {
            parameters.addQueryItem(QStringLiteral("q"), query.searchTerm.trimmed());
        }
        if (sort && !sort->isEmpty()) {
            parameters.addQueryItem(QStringLiteral("sort"), *sort);
        }
        if (!order.isEmpty()) {
            parameters.addQueryItem(QStringLiteral("order"), order);
        }
        appendGroupsForGet(parameters, query);

        ProviderCatalogRequest request = get(
            endpointWithQuery(QStringLiteral("/api/v1/catalog"), parameters));
        if (query.useCacheValidation) {
            if (!query.etag.isEmpty()) {
                request.extraHeaders.insert(QByteArrayLiteral("If-None-Match"), query.etag);
            }
            if (!query.lastModified.isEmpty()) {
                request.extraHeaders.insert(QByteArrayLiteral("If-Modified-Since"),
                                            query.lastModified);
            }
        }
        return request;
    }

    static ProviderCatalogRequest createFilterOptionsRequest(
        const ProviderCatalogQuery &query)
    {
        const QString error = validationError(query);
        if (!error.isEmpty()) {
            return unsupported(error);
        }
        bool libraryIdOk = true;
        const int libraryId = query.parentId.trimmed().isEmpty()
            ? 0 : query.parentId.trimmed().toInt(&libraryIdOk);
        if (!libraryIdOk
            || (!query.parentId.trimmed().isEmpty() && libraryId <= 0)) {
            return unsupported(QStringLiteral(
                "Silo filter scope requires a positive numeric library ID"));
        }

        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("source"), QStringLiteral("query"));
        if (libraryId > 0) {
            parameters.addQueryItem(QStringLiteral("library_id"), QString::number(libraryId));
        }
        appendGroupsForGet(parameters, query);
        if (!query.searchTerm.trimmed().isEmpty()) {
            parameters.addQueryItem(QStringLiteral("q"), query.searchTerm.trimmed());
        }
        parameters.addQueryItem(QStringLiteral("include_technical"),
                                query.includeHeavyFields ? QStringLiteral("true")
                                                         : QStringLiteral("false"));

        QString endpoint = QStringLiteral("/api/v1/catalog/filters");
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
            appendLimit(parameters, query.limit);
        }
        return get(endpointWithQuery(endpoint, parameters));
    }
};
