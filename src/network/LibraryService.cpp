#include "LibraryService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "NextEpisodeResolver.h"
#include "models/MediaModels.h"
#include "utils/ConfigManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <algorithm>
#include <QLoggingCategory>
#include <memory>
#include <optional>
#include "../utils/BloomLogging.h"

namespace {

const QStringList kItemFields = {
    "Overview", "ImageTags", "BackdropImageTags", "ParentBackdropImageTags",
    "Genres", "Studios", "People", "UserData",
    "ProductionYear", "PremiereDate", "OfficialRating",
    "RunTimeTicks", "CommunityRating", "ProviderIds"
};

QString buildItemEndpoint(const QString &userId, const QString &itemId)
{
    return QString("/Users/%1/Items/%2?Fields=%3")
        .arg(userId, itemId, kItemFields.join(","));
}

QString buildChaptersEndpoint(const QString &userId, const QString &itemId)
{
    return QString("/Users/%1/Items/%2?Fields=Chapters&EnableImages=true&EnableImageTypes=Chapter&ImageTypeLimit=100")
        .arg(userId, itemId);
}

QString activeConnectionId(const AuthenticationService *authService)
{
    ConfigManager *config = authService ? authService->configManager() : nullptr;
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    return connection.has_value() ? connection->connectionId : QString();
}

QString cachedArtworkSource(const Bloom::ArtworkRef &artwork)
{
    if (!artwork.isValid()) {
        return {};
    }
    return QStringLiteral("image://cached/%1").arg(
        QString::fromUtf8(QUrl::toPercentEncoding(artwork.cacheKey())));
}

QStringList sortedList(QStringList values)
{
    values.removeAll(QString());
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return values;
}

QString triStateKey(LibraryItemQuery::TriState state)
{
    switch (state) {
    case LibraryItemQuery::TriState::Yes:
        return QStringLiteral("yes");
    case LibraryItemQuery::TriState::No:
        return QStringLiteral("no");
    case LibraryItemQuery::TriState::Any:
        break;
    }
    return QStringLiteral("any");
}

void addJoined(QUrlQuery &urlQuery, const QString &key, const QStringList &values, const QString &separator = QStringLiteral("|"))
{
    const QStringList normalized = sortedList(values);
    if (!normalized.isEmpty()) {
        urlQuery.addQueryItem(key, normalized.join(separator));
    }
}

void addFields(QUrlQuery &urlQuery, bool includeHeavyFields)
{
    QStringList fields = {
        "Type",
        "ParentIndexNumber",
        "IndexNumber",
        "LocationType",
        "ImageTags",
        "BackdropImageTags",
        "ParentBackdropImageTags",
        "ParentBackdropImageItemId",
        "ParentBackdropItemId",
        "ParentPrimaryImageTag",
        "ParentPrimaryImageItemId",
        "SeriesPrimaryImageTag",
        "ProductionYear",
        "PremiereDate",
        "DateCreated",
        "ChildCount",
        "ParentId",
        "SeasonId",
        "SeriesId",
        "SeriesName",
        "UserData",
        "RunTimeTicks",
        "Overview",
        "CommunityRating",
        "Studios",
        "Genres",
        "Tags",
        "SpecialEpisodeNumbers",
        "AirsBeforeSeasonNumber",
        "AirsAfterSeasonNumber",
        "AirsBeforeEpisodeNumber"
    };

    if (includeHeavyFields) {
        fields.prepend("Path");
        fields.prepend("MediaSources");
    }

    urlQuery.addQueryItem("Fields", fields.join(","));
    urlQuery.addQueryItem("EnableImageTypes", "Primary,Backdrop,Thumb,Logo");
}

QString buildItemsEndpoint(const QString &userId, const LibraryItemQuery &query)
{
    QUrl url(QStringLiteral("/Users/%1/Items").arg(userId));
    QUrlQuery urlQuery;

    if (!query.parentId.isEmpty()) {
        urlQuery.addQueryItem("ParentId", query.parentId);
    }
    addFields(urlQuery, query.includeHeavyFields);
    if (query.startIndex > 0) {
        urlQuery.addQueryItem("StartIndex", QString::number(query.startIndex));
    }
    if (query.limit > 0) {
        urlQuery.addQueryItem("Limit", QString::number(query.limit));
    }
    if (!query.searchTerm.trimmed().isEmpty()) {
        urlQuery.addQueryItem("SearchTerm", query.searchTerm.trimmed());
    }
    addJoined(urlQuery, "Genres", query.genres, ",");
    addJoined(urlQuery, "Tags", query.tags, ",");
    addJoined(urlQuery, "Studios", query.studios, ",");
    if (query.minPremiereDate.isValid()) {
        urlQuery.addQueryItem("MinPremiereDate", query.minPremiereDate.startOfDay(Qt::UTC).toString(Qt::ISODate));
    }
    if (query.maxPremiereDate.isValid()) {
        urlQuery.addQueryItem("MaxPremiereDate", query.maxPremiereDate.endOfDay(Qt::UTC).toString(Qt::ISODate));
    }
    if (query.minDateLastSaved.isValid()) {
        urlQuery.addQueryItem("MinDateLastSaved", query.minDateLastSaved.startOfDay(Qt::UTC).toString(Qt::ISODate));
    }
    if (query.watched != LibraryItemQuery::TriState::Any) {
        urlQuery.addQueryItem("IsPlayed", query.watched == LibraryItemQuery::TriState::Yes ? "true" : "false");
    }
    if (query.favorite != LibraryItemQuery::TriState::Any) {
        urlQuery.addQueryItem("IsFavorite", query.favorite == LibraryItemQuery::TriState::Yes ? "true" : "false");
    }
    if (query.minCommunityRating > 0.0) {
        urlQuery.addQueryItem("MinCommunityRating", QString::number(query.minCommunityRating, 'f', 1));
    }
    if (!query.years.isEmpty()) {
        QStringList years;
        years.reserve(query.years.size());
        for (int year : query.years) {
            if (year > 0) {
                years.append(QString::number(year));
            }
        }
        addJoined(urlQuery, "Years", years);
    }
    addJoined(urlQuery, "IncludeItemTypes", query.includeItemTypes, ",");
    if (query.recursive) {
        urlQuery.addQueryItem("Recursive", "true");
    }

    urlQuery.addQueryItem("SortBy", query.normalizedSortBy());
    if (!query.sortOrder.isEmpty()) {
        urlQuery.addQueryItem("SortOrder", query.sortOrder);
    }

    url.setQuery(urlQuery);
    return url.toString(QUrl::FullyEncoded);
}

QStringList parseStringList(const QJsonValue &value)
{
    QStringList result;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const auto &entry : array) {
            if (entry.isString()) {
                result.append(entry.toString());
            } else {
                const QJsonObject obj = entry.toObject();
                const QString name = obj.value("Name").toString();
                if (!name.isEmpty()) {
                    result.append(name);
                }
            }
        }
    }
    return sortedList(result);
}

}

QString LibraryItemQuery::normalizedSortBy() const
{
    return sortBy.isEmpty() ? QStringLiteral("ParentIndexNumber,IndexNumber,SortName") : sortBy;
}

QString LibraryItemQuery::cacheKey() const
{
    QStringList parts;
    parts << "parent=" + parentId;
    parts << "search=" + searchTerm.trimmed();
    parts << "genres=" + sortedList(genres).join("|");
    parts << "tags=" + sortedList(tags).join("|");
    parts << "studios=" + sortedList(studios).join("|");
    parts << "minPremiere=" + (minPremiereDate.isValid() ? minPremiereDate.toString(Qt::ISODate) : QString());
    parts << "maxPremiere=" + (maxPremiereDate.isValid() ? maxPremiereDate.toString(Qt::ISODate) : QString());
    parts << "minAdded=" + (minDateLastSaved.isValid() ? minDateLastSaved.toString(Qt::ISODate) : QString());
    parts << "watched=" + triStateKey(watched);
    parts << "favorite=" + triStateKey(favorite);
    parts << "rating=" + QString::number(minCommunityRating, 'f', 1);
    QStringList yearParts;
    for (int year : years) {
        if (year > 0) {
            yearParts.append(QString::number(year));
        }
    }
    parts << "years=" + sortedList(yearParts).join("|");
    parts << "sort=" + normalizedSortBy();
    parts << "order=" + sortOrder;
    parts << "types=" + sortedList(includeItemTypes).join("|");
    parts << QStringLiteral("recursive=%1").arg(recursive ? "1" : "0");
    parts << QStringLiteral("start=%1").arg(startIndex);
    parts << QStringLiteral("limit=%1").arg(limit);
    parts << QStringLiteral("heavy=%1").arg(includeHeavyFields ? "1" : "0");
    return parts.join(";");
}

LibraryService::LibraryService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_transport(authService ? authService->transport() : nullptr)
    , m_retryPolicy{3, 1000, true}
{
    if (m_authService) {
        const auto clearAccountState = [this]() {
            m_etags.clear();
            m_lastModified.clear();
            m_inFlightChapterRequests.clear();
            ++m_heroLibraryRequestGeneration;
        };
        connect(m_authService, &AuthenticationService::loggedOut,
                this, clearAccountState);
        connect(m_authService, &AuthenticationService::loginSuccess,
                this, [clearAccountState](const QString &, const QString &, const QString &) {
            clearAccountState();
        });
    }
}

// ============================================================================
// Request Helpers
// ============================================================================

void LibraryService::sendRequestWithRetry(const QString &endpoint,
                                           RequestFactory requestFactory,
                                           ResponseHandler responseHandler,
                                           FailureHandler failureHandler,
                                           int attemptNumber)
{
    Q_UNUSED(attemptNumber)
    if (!m_transport) {
        NetworkError error;
        error.code = -1;
        error.endpoint = endpoint;
        error.userMessage = tr("Network transport is unavailable.");
        if (failureHandler) {
            failureHandler(error);
        } else {
            emitError(error);
        }
        return;
    }

    HttpRequestOptions options;
    options.retryPolicy = m_retryPolicy;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    m_transport->sendWithRetry(
        this,
        endpoint,
        std::move(requestFactory),
        std::move(responseHandler),
        [this, failureHandler = std::move(failureHandler)](const NetworkError &error) {
            if (failureHandler) {
                failureHandler(error);
            } else {
                emitError(error);
            }
        },
        options);
}

void LibraryService::emitError(const NetworkError &error)
{
    qCWarning(lcLibrary) << "Emitting error for endpoint:" << error.endpoint
                              << "User message:" << error.userMessage;
    emit errorOccurred(error.endpoint, error.userMessage);
    emit networkError(error);
}

// ============================================================================
// Library Views
// ============================================================================

void LibraryService::getViews()
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getViews";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    const QString connectionId = activeConnectionId(m_authService);
    QString endpoint = QString("/Users/%1/Views").arg(m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            
            if (JsonParser::shouldParseAsync(data)) {
                emit parsingStarted("views");
                
                auto *watcher = new QFutureWatcher<ParsedItemsResult>(this);
                connect(watcher, &QFutureWatcher<ParsedItemsResult>::finished, this,
                        [this, watcher, connectionId]() {
                    ParsedItemsResult result = watcher->result();
                    watcher->deleteLater();
                    
                    emit parsingFinished("views");
                    
                    if (result.success) {
                        emit viewsLoaded(result.items);
                        const QVariantList canonicalItems =
                            m_authService->mapMediaItems(result.items, connectionId);
                        emit canonicalViewsLoaded(canonicalItems);
                        emit canonicalViewsLoadedForConnection(connectionId, canonicalItems);
                    } else {
                        NetworkError error;
                        error.endpoint = "getViews";
                        error.code = -2;
                        error.userMessage = tr("Failed to parse server response");
                        emitError(error);
                    }
                });
                
                QFuture<ParsedItemsResult> future = QtConcurrent::run([data]() {
                    return JsonParser::parseItemsResponse(data, QString());
                });
                watcher->setFuture(future);
            } else {
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getViews";
                    error.code = -2;
                    error.userMessage = tr("Invalid server response");
                    emitError(error);
                    return;
                }
                QJsonObject obj = doc.object();
                QJsonArray items = obj["Items"].toArray();
                emit viewsLoaded(items);
                const QVariantList canonicalItems =
                    m_authService->mapMediaItems(items, connectionId);
                emit canonicalViewsLoaded(canonicalItems);
                emit canonicalViewsLoadedForConnection(connectionId, canonicalItems);
            }
        });
}

// ============================================================================
// Items with Pagination
// ============================================================================

void LibraryService::getItems(const QString &parentId, int startIndex, int limit,
                               const QStringList &genres, const QStringList &networks,
                               const QString &sortBy, const QString &sortOrder,
                               bool includeHeavyFields, bool useCacheValidation)
{
    LibraryItemQuery query;
    query.parentId = parentId;
    query.startIndex = startIndex;
    query.limit = limit;
    query.genres = genres;
    query.studios = networks;
    query.sortBy = sortBy;
    query.sortOrder = sortOrder;
    query.includeHeavyFields = includeHeavyFields;
    query.useCacheValidation = useCacheValidation;
    getItems(query);
}

void LibraryService::getItems(const LibraryItemQuery &query)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }

    const QString parentId = query.parentId;
    const QString queryKey = query.requestKey.isEmpty() ? query.cacheKey() : query.requestKey;
    const QString connectionId = activeConnectionId(m_authService);
    const QString endpoint = buildItemsEndpoint(m_authService->getUserId(), query);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint, query]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            if (query.useCacheValidation) {
                if (m_etags.contains(endpoint)) {
                    request.setRawHeader("If-None-Match", m_etags.value(endpoint).toUtf8());
                }
                if (m_lastModified.contains(endpoint)) {
                    request.setRawHeader("If-Modified-Since", m_lastModified.value(endpoint).toUtf8());
                }
            }
            return m_authService->networkManager()->get(request);
        },
        [this, parentId, endpoint, query, queryKey, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 304 && query.useCacheValidation) {
                emit itemsNotModified(parentId);
                emit itemsNotModifiedForQuery(parentId, queryKey);
                emit canonicalItemsNotModifiedForConnection(connectionId, parentId, queryKey);
                return;
            }

            if (query.useCacheValidation) {
                QByteArray etag = reply->rawHeader("ETag");
                if (!etag.isEmpty()) {
                    m_etags[endpoint] = QString::fromUtf8(etag);
                }
                QByteArray lastMod = reply->rawHeader("Last-Modified");
                if (!lastMod.isEmpty()) {
                    m_lastModified[endpoint] = QString::fromUtf8(lastMod);
                }
            }
            
            if (JsonParser::shouldParseAsync(data)) {
                emit parsingStarted("library");
                
                auto *watcher = new QFutureWatcher<ParsedItemsResult>(this);
                connect(watcher, &QFutureWatcher<ParsedItemsResult>::finished, this,
                        [this, watcher, connectionId]() {
                    ParsedItemsResult result = watcher->result();
                    watcher->deleteLater();
                    emit parsingFinished("library");
                    
                    if (result.success) {
                        emit itemsLoaded(result.parentId, result.items);
                        emit itemsLoadedWithTotal(result.parentId, result.items, result.totalRecordCount);
                        emit itemsLoadedWithTotalForQuery(result.parentId, result.queryKey, result.items, result.totalRecordCount);
                        const QVariantList canonicalItems =
                            m_authService->mapMediaItems(result.items, connectionId);
                        emit canonicalItemsLoadedWithTotalForQuery(
                            result.parentId,
                            result.queryKey,
                            canonicalItems,
                            result.totalRecordCount);
                        emit canonicalItemsLoadedForConnection(
                            connectionId,
                            result.parentId,
                            result.queryKey,
                            canonicalItems,
                            result.totalRecordCount);
                    } else {
                        NetworkError error;
                        error.endpoint = "getItems";
                        error.code = -2;
                        error.userMessage = tr("Failed to parse library data");
                        emitError(error);
                    }
                });
                
                QFuture<ParsedItemsResult> future = QtConcurrent::run([data, parentId, queryKey]() {
                    auto result = JsonParser::parseItemsResponse(data, parentId);
                    result.queryKey = queryKey;
                    return result;
                });
                watcher->setFuture(future);
            } else {
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getItems";
                    error.code = -2;
                    error.userMessage = tr("Invalid library response");
                    emitError(error);
                    return;
                }
                QJsonObject obj = doc.object();
                QJsonArray items = obj["Items"].toArray();
                int totalRecordCount = obj["TotalRecordCount"].toInt();
                emit itemsLoaded(parentId, items);
                emit itemsLoadedWithTotal(parentId, items, totalRecordCount);
                emit itemsLoadedWithTotalForQuery(parentId, queryKey, items, totalRecordCount);
                const QVariantList canonicalItems =
                    m_authService->mapMediaItems(items, connectionId);
                emit canonicalItemsLoadedWithTotalForQuery(
                    parentId,
                    queryKey,
                    canonicalItems,
                    totalRecordCount);
                emit canonicalItemsLoadedForConnection(
                    connectionId,
                    parentId,
                    queryKey,
                    canonicalItems,
                    totalRecordCount);
            }
        });
}

void LibraryService::getFilterOptions(const QString &parentId,
                                      const QStringList &includeItemTypes,
                                      bool recursive)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getFilterOptions";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }

    auto buildFacetEndpoint = [this, parentId, includeItemTypes, recursive](const QString &path) {
        QUrl url(path);
        QUrlQuery query;
        query.addQueryItem("UserId", m_authService->getUserId());
        if (!parentId.isEmpty()) {
            query.addQueryItem("ParentId", parentId);
        }
        if (!includeItemTypes.isEmpty()) {
            query.addQueryItem("IncludeItemTypes", includeItemTypes.join(","));
        }
        if (recursive) {
            query.addQueryItem("Recursive", "true");
        }
        query.addQueryItem("Limit", "500");
        url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    };

    const QString filtersEndpoint = buildFacetEndpoint("/Items/Filters");
    const QString genresEndpoint = buildFacetEndpoint("/Genres");
    const QString studiosEndpoint = buildFacetEndpoint("/Studios");

    auto state = std::make_shared<QHash<QString, QStringList>>();
    auto remaining = std::make_shared<int>(3);
    auto finish = [this, parentId, state, remaining]() {
        --(*remaining);
        if (*remaining > 0) {
            return;
        }
        QStringList genres = state->value("genres");
        genres.append(state->value("filterGenres"));
        QStringList tags = state->value("tags");
        QStringList studios = state->value("studios");
        genres = sortedList(genres);
        tags = sortedList(tags);
        studios = sortedList(studios);
        emit filterOptionsLoaded(parentId, genres, tags, studios);
    };

    sendRequestWithRetry(filtersEndpoint,
        [this, filtersEndpoint]() {
            return m_authService->networkManager()->get(m_authService->createRequest(filtersEndpoint));
        },
        [state, finish](QNetworkReply *reply) {
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            state->insert("filterGenres", parseStringList(obj.value("Genres")));
            state->insert("tags", parseStringList(obj.value("Tags")));
            finish();
        },
        [finish](const NetworkError &) {
            finish();
        });

    sendRequestWithRetry(genresEndpoint,
        [this, genresEndpoint]() {
            return m_authService->networkManager()->get(m_authService->createRequest(genresEndpoint));
        },
        [state, finish](QNetworkReply *reply) {
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            state->insert("genres", parseStringList(obj.value("Items")));
            finish();
        },
        [finish](const NetworkError &) {
            finish();
        });

    sendRequestWithRetry(studiosEndpoint,
        [this, studiosEndpoint]() {
            return m_authService->networkManager()->get(m_authService->createRequest(studiosEndpoint));
        },
        [state, finish](QNetworkReply *reply) {
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            state->insert("studios", parseStringList(obj.value("Items")));
            finish();
        },
        [finish](const NetworkError &) {
            finish();
        });
}

// ============================================================================
// Next Up & Latest Media
// ============================================================================

void LibraryService::getNextUp()
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getNextUp";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    const QString connectionId = activeConnectionId(m_authService);
    QString endpoint = QString("/Shows/NextUp?UserId=%1&Limit=10&Fields=Path,Overview,SeriesName,ImageTags,ParentId,SeriesId,SeriesPrimaryImageTag,SeriesThumbImageTag,ParentThumbItemId,ParentThumbImageTag,ParentPrimaryImageTag,BackdropImageTags,ParentBackdropImageTags,ParentBackdropItemId,UserData,RunTimeTicks&EnableImageTypes=Primary,Thumb,Backdrop,Logo")
        .arg(m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getNextUp";
                error.code = -2;
                error.userMessage = tr("Invalid next up response");
                emitError(error);
                return;
            }
            QJsonObject obj = doc.object();
            QJsonArray items = obj["Items"].toArray();
            emit nextUpLoaded(items);
            emit canonicalNextUpLoaded(
                connectionId, m_authService->mapMediaItems(items, connectionId));
        });
}

void LibraryService::getLatestMedia(const QString &parentId)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getLatestMedia";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    const QString connectionId = activeConnectionId(m_authService);
    QString endpoint = QString("/Users/%1/Items/Latest?ParentId=%2&Limit=10&Fields=Path,Overview,SeriesName,ImageTags,ParentId,SeriesId,SeriesPrimaryImageTag,SeriesThumbImageTag,ParentThumbItemId,ParentThumbImageTag,ParentPrimaryImageTag,BackdropImageTags,ParentBackdropImageTags,ParentBackdropItemId,ProductionYear,Status,EndDate,ParentIndexNumber,IndexNumber,UserData,RunTimeTicks&EnableImageTypes=Primary,Backdrop,Thumb,Logo")
        .arg(m_authService->getUserId(), parentId);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, parentId, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isArray()) {
                NetworkError error;
                error.endpoint = "getLatestMedia";
                error.code = -2;
                error.userMessage = tr("Invalid latest media response");
                emitError(error);
                return;
            }
            QJsonArray items = doc.array();
            emit latestMediaLoaded(parentId, items);
            emit canonicalLatestMediaLoaded(
                connectionId, parentId, m_authService->mapMediaItems(items, connectionId));
        });
}

void LibraryService::getHomeBackdropItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getHomeBackdropItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalHomeBackdropItemsFailed(connectionId, error.userMessage);
        emitError(error);
        return;
    }

    const QStringList fields = {
        "Id",
        "ImageTags",
        "BackdropImageTags",
        "ParentBackdropImageTags",
        "ParentBackdropItemId",
        "SeriesId"
    };
    const int requestedLimit = (limit > 0) ? qBound(50, limit, 20000) : 0;

    // Fast starter query: small random sample so Home can show a backdrop quickly.
    if (requestedLimit > 0) {
        QString endpoint = QString("/Users/%1/Items?Recursive=true&IncludeItemTypes=Movie,Series,Season,Episode&SortBy=Random&Fields=%2&EnableImages=true&EnableImageTypes=Backdrop&ImageTypeLimit=1&EnableTotalRecordCount=false&Limit=%3")
                               .arg(m_authService->getUserId())
                               .arg(fields.join(","))
                               .arg(requestedLimit);

        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, requestedLimit, connectionId](QNetworkReply *reply) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getHomeBackdropItems";
                    error.code = -2;
                    error.userMessage = tr("Invalid home backdrop response");
                    emit canonicalHomeBackdropItemsFailed(connectionId, error.userMessage);
                    emitError(error);
                    return;
                }
                QJsonArray items = doc.object()["Items"].toArray();
                qCDebug(lcLibrary) << "getHomeBackdropItems starter sample size:" << items.size()
                                        << "requestedLimit:" << requestedLimit;
                emit homeBackdropItemsLoaded(items);
                emit canonicalHomeBackdropItemsLoaded(
                    connectionId, m_authService->mapMediaItems(items, connectionId));
            },
            [this, connectionId](const NetworkError &error) {
                emit canonicalHomeBackdropItemsFailed(connectionId, error.userMessage);
                emitError(error);
            });
        return;
    }

    const int pageSize = 250;
    const int pageDelayMs = 250;

    auto aggregate = std::make_shared<QJsonArray>();
    auto fetchPage = std::make_shared<std::function<void(int)>>();
    *fetchPage = [this, fields, pageSize, requestedLimit, aggregate, fetchPage, connectionId](int startIndex) {
        if (activeConnectionId(m_authService) != connectionId) {
            return;
        }
        QString endpoint = QString("/Users/%1/Items?Recursive=true&IncludeItemTypes=Movie,Series,Season,Episode&SortBy=SortName&Fields=%2&EnableImages=true&EnableImageTypes=Backdrop&ImageTypeLimit=1&EnableTotalRecordCount=false&StartIndex=%3&Limit=%4")
                               .arg(m_authService->getUserId())
                               .arg(fields.join(","))
                               .arg(startIndex)
                               .arg(pageSize);

        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, startIndex, pageSize, requestedLimit, aggregate, fetchPage, connectionId](QNetworkReply *reply) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getHomeBackdropItems";
                    error.code = -2;
                    error.userMessage = tr("Invalid home backdrop response");
                    emit canonicalHomeBackdropItemsFailed(connectionId, error.userMessage);
                    emitError(error);
                    return;
                }

                QJsonObject obj = doc.object();
                QJsonArray pageItems = obj["Items"].toArray();
                qCDebug(lcLibrary) << "getHomeBackdropItems page startIndex:" << startIndex
                                        << "pageItems:" << pageItems.size()
                                        << "requestedLimit:" << requestedLimit;

                // Emit progressively so Home can render backdrops immediately.
                if (!pageItems.isEmpty()) {
                    emit homeBackdropItemsLoaded(pageItems);
                    emit canonicalHomeBackdropItemsLoaded(
                        connectionId, m_authService->mapMediaItems(pageItems, connectionId));
                }

                for (const QJsonValue &value : pageItems) {
                    aggregate->append(value);
                }

                const bool reachedRequestedLimit = requestedLimit > 0 && aggregate->size() >= requestedLimit;
                const bool reachedServerEndByPage = pageItems.size() < pageSize;

                if (reachedRequestedLimit || reachedServerEndByPage) {
                    qCDebug(lcLibrary) << "getHomeBackdropItems completed aggregate size:" << aggregate->size()
                                            << "requestedLimit:" << requestedLimit;
                    return;
                }

                const int nextStart = startIndex + pageItems.size();
                QTimer::singleShot(pageDelayMs, this, [fetchPage, nextStart]() {
                    (*fetchPage)(nextStart);
                });
            },
            [this, connectionId](const NetworkError &error) {
                emit canonicalHomeBackdropItemsFailed(connectionId, error.userMessage);
                emitError(error);
            });
    };

    (*fetchPage)(0);
}

void LibraryService::getScreensaverItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getScreensaverItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalScreensaverItemsFailed(connectionId, error.userMessage);
        emitError(error);
        return;
    }

    const int requestedLimit = qBound(10, limit > 0 ? limit : 80, 200);
    const QString requestUserId = m_authService->getUserId();
    const QStringList fields = {
        "Id",
        "Name",
        "Overview",
        "Type",
        "SeriesName",
        "SeriesId",
        "ImageTags",
        "BackdropImageTags",
        "ParentBackdropImageTags",
        "ParentBackdropItemId",
        "ParentId",
        "ProductionYear"
    };

    const QString endpoint = QString("/Users/%1/Items?Recursive=true&IncludeItemTypes=Movie,Series&SortBy=Random&Fields=%2&EnableImages=true&EnableImageTypes=Backdrop,Logo&ImageTypeLimit=1&EnableTotalRecordCount=false&Limit=%3")
                                 .arg(requestUserId)
                                 .arg(fields.join(","))
                                 .arg(requestedLimit);

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, connectionId, requestUserId](QNetworkReply *reply) {
            if (!m_authService->isAuthenticated() || m_authService->getUserId() != requestUserId) {
                return;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getScreensaverItems";
                error.code = -2;
                error.userMessage = tr("Invalid screensaver response");
                emit canonicalScreensaverItemsFailed(connectionId, error.userMessage);
                emitError(error);
                return;
            }

            QVariantList filteredItems;
            const QVariantList items = m_authService->mapMediaItems(
                doc.object().value(QStringLiteral("Items")).toArray(), connectionId);
            for (const QVariant &value : items) {
                const QVariantMap item = value.toMap();
                if (!item.value(QStringLiteral("backdropArtwork")).toMap().isEmpty()) {
                    filteredItems.append(item);
                }
            }
            emit canonicalScreensaverItemsLoaded(connectionId, filteredItems);
        },
        [this, connectionId](const NetworkError &error) {
            emit canonicalScreensaverItemsFailed(connectionId, error.userMessage);
            emitError(error);
        });
}

// ============================================================================
// Generic Item Details
// ============================================================================

void LibraryService::getItem(const QString &itemId)
{
    getItem(itemId, QString());
}

void LibraryService::getItem(const QString &itemId, const QString &requestContext)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getItem";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit itemFailed(itemId, error.userMessage, requestContext);
        emitError(error);
        return;
    }

    const QString connectionId = activeConnectionId(m_authService);
    const QString endpoint = buildItemEndpoint(m_authService->getUserId(), itemId);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint, itemId]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            if (m_etags.contains(endpoint)) {
                request.setRawHeader("If-None-Match", m_etags.value(endpoint).toUtf8());
            }
            if (m_lastModified.contains(endpoint)) {
                request.setRawHeader("If-Modified-Since", m_lastModified.value(endpoint).toUtf8());
            }
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, endpoint, requestContext, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 304) {
                emit itemNotModified(itemId, requestContext);
                emit itemNotModified(itemId);
                return;
            }

            QByteArray etag = reply->rawHeader("ETag");
            if (!etag.isEmpty()) {
                m_etags[endpoint] = QString::fromUtf8(etag);
            }
            QByteArray lastMod = reply->rawHeader("Last-Modified");
            if (!lastMod.isEmpty()) {
                m_lastModified[endpoint] = QString::fromUtf8(lastMod);
            }

            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getItem";
                error.code = -2;
                error.userMessage = tr("Invalid item response");
                emit itemFailed(itemId, error.userMessage, requestContext);
                emitError(error);
                return;
            }
            const QJsonObject wireItem = doc.object();
            const QVariantMap canonicalItem = m_authService
                ? m_authService->mapMediaItem(wireItem, connectionId)
                : QVariantMap{};
            emit itemLoaded(itemId, wireItem, requestContext);
            emit itemLoaded(itemId, wireItem);
            emit canonicalItemLoaded(itemId, canonicalItem, requestContext);
            emit canonicalItemLoaded(itemId, canonicalItem);
        },
        [this, itemId, requestContext](const NetworkError &error) {
            emit itemFailed(itemId, error.userMessage, requestContext);
            emitError(error);
        });
}

void LibraryService::clearItemCacheValidation(const QString &itemId)
{
    if (!m_authService || !m_authService->isAuthenticated() || itemId.isEmpty()) {
        return;
    }

    const QString endpoint = buildItemEndpoint(m_authService->getUserId(), itemId);
    m_etags.remove(endpoint);
    m_lastModified.remove(endpoint);
}

void LibraryService::getChapters(const QString &itemId)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        emit chaptersFailed(connectionId, itemId, tr("Not authenticated"));
        return;
    }
    if (itemId.isEmpty()) {
        emit chaptersFailed(connectionId, itemId, tr("Item ID is empty"));
        return;
    }

    const QString requestKey = connectionId + QLatin1Char('\n') + itemId;
    if (m_inFlightChapterRequests.contains(requestKey)) {
        qCDebug(lcLibrary) << "LibraryService: Skipping duplicate chapter request for item" << itemId;
        return;
    }
    m_inFlightChapterRequests.insert(requestKey);

    const QString endpoint = buildChaptersEndpoint(m_authService->getUserId(), itemId);
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, connectionId, requestKey](QNetworkReply *reply) {
            m_inFlightChapterRequests.remove(requestKey);
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                qCWarning(lcLibrary) << "LibraryService: Invalid chapter response for item" << itemId;
                emit chaptersFailed(connectionId, itemId, tr("Invalid chapter response"));
                return;
            }

            const QJsonArray array = doc.object().value(QStringLiteral("Chapters")).toArray();
            qCInfo(lcLibrary) << "LibraryService: Loaded chapter array for item" << itemId
                    << "count" << array.size();
            emit canonicalChaptersLoaded(
                connectionId,
                itemId,
                m_authService->mapChapters(array, connectionId, itemId));
        },
        [this, itemId, connectionId, requestKey](const NetworkError &error) {
            m_inFlightChapterRequests.remove(requestKey);
            emit chaptersFailed(connectionId, itemId, error.userMessage);
        });
}

void LibraryService::resolveLibraryForItem(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) {
        emit itemLibraryResolutionFailed(itemId, tr("Not authenticated"));
        return;
    }
    if (itemId.isEmpty()) {
        emit itemLibraryResolutionFailed(itemId, tr("Item ID is empty"));
        return;
    }

    const QString endpoint = QString("/Items/%1/Ancestors?UserId=%2")
        .arg(itemId, m_authService->getUserId());

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isArray()) {
                emit itemLibraryResolutionFailed(itemId, tr("Invalid ancestors response"));
                return;
            }

            const QJsonArray ancestors = doc.array();
            QString libraryId;

            for (const QJsonValue &value : ancestors) {
                const QJsonObject ancestor = value.toObject();
                const QString type = ancestor.value(QStringLiteral("Type")).toString();
                const QString collectionType = ancestor.value(QStringLiteral("CollectionType")).toString();
                if (type == QStringLiteral("CollectionFolder") || !collectionType.isEmpty()) {
                    libraryId = ancestor.value(QStringLiteral("Id")).toString();
                    break;
                }
            }

            if (libraryId.isEmpty() && !ancestors.isEmpty()) {
                libraryId = ancestors.last().toObject().value(QStringLiteral("Id")).toString();
            }

            if (libraryId.isEmpty()) {
                emit itemLibraryResolutionFailed(itemId, tr("Library ancestor not found"));
                return;
            }

            emit itemLibraryResolved(itemId, libraryId);
        });
}

// ============================================================================
// Series Details
/**
 * Loads detailed metadata for the specified series and emits the result or an error.
 *
 * Sends a GET request for the series item fields and, on success, emits the parsed JSON object.
 * If a 304 Not Modified response is returned the method emits seriesDetailsNotModified(seriesId).
 * On authentication failure or invalid server response it emits an error via emitError.
 * When present, the response ETag and Last-Modified headers are stored in the service's cache.
 *
 * @param seriesId The identifier of the series to fetch.
 */

void LibraryService::getSeriesDetails(const QString &seriesId)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getSeriesDetails";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalSeriesDetailsFailed(connectionId, seriesId, error.userMessage);
        emitError(error);
        return;
    }
    
    const QStringList fields = {
        "Overview", "ImageTags", "BackdropImageTags", "ParentBackdropImageTags",
        "Genres", "Studios", "People", "ChildCount", "ParentId", "UserData",
        "ProductionYear", "PremiereDate", "EndDate", "ProviderIds",
        "RecursiveItemCount", "Status"
    };
    
    QString endpoint = QString("/Users/%1/Items/%2?Fields=%3")
        .arg(m_authService->getUserId(), seriesId, fields.join(","));
    
    sendRequestWithRetry(endpoint,
        [this, endpoint, seriesId]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            if (m_etags.contains(endpoint)) {
                request.setRawHeader("If-None-Match", m_etags.value(endpoint).toUtf8());
            }
            if (m_lastModified.contains(endpoint)) {
                request.setRawHeader("If-Modified-Since", m_lastModified.value(endpoint).toUtf8());
            }
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId, endpoint, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 304) {
                emit seriesDetailsNotModified(seriesId);
                emit canonicalSeriesDetailsNotModified(connectionId, seriesId);
                return;
            }

            QByteArray etag = reply->rawHeader("ETag");
            if (!etag.isEmpty()) {
                m_etags[endpoint] = QString::fromUtf8(etag);
            }
            QByteArray lastMod = reply->rawHeader("Last-Modified");
            if (!lastMod.isEmpty()) {
                m_lastModified[endpoint] = QString::fromUtf8(lastMod);
            }

            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getSeriesDetails";
                error.code = -2;
                error.userMessage = tr("Invalid series details response");
                emit canonicalSeriesDetailsFailed(connectionId, seriesId, error.userMessage);
                emitError(error);
                return;
            }
            const QJsonObject wireSeries = doc.object();
            emit seriesDetailsLoaded(seriesId, wireSeries);
            emit canonicalSeriesDetailsLoaded(
                connectionId,
                seriesId,
                m_authService->mapMediaItem(wireSeries, connectionId));
        },
        [this, connectionId, seriesId](const NetworkError &error) {
            emit canonicalSeriesDetailsFailed(connectionId, seriesId, error.userMessage);
            emitError(error);
        });
}

void LibraryService::getSimilarItems(const QString &itemId, int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getSimilarItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit similarItemsFailed(itemId, error.userMessage);
        emit canonicalSimilarItemsFailedForConnection(
            connectionId, itemId, error.userMessage);
        return;
    }

    if (itemId.isEmpty()) {
        const QString error = tr("Item ID is empty");
        emit similarItemsFailed(itemId, error);
        emit canonicalSimilarItemsFailedForConnection(connectionId, itemId, error);
        return;
    }

    const QStringList fields = {
        "Type",
        "ImageTags",
        "PrimaryImageAspectRatio",
        "ProductionYear",
        "PremiereDate",
        "Overview",
        "UserData",
        "ChildCount"
    };

    QString endpoint = QString("/Items/%1/Similar?UserId=%2&Limit=%3&Fields=%4&EnableImageTypes=Primary")
                           .arg(itemId, m_authService->getUserId())
                           .arg(qMax(1, limit))
                           .arg(fields.join(","));

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, itemId, connectionId](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getSimilarItems";
                error.code = -2;
                error.userMessage = tr("Invalid similar items response");
                emit similarItemsFailed(itemId, error.userMessage);
                emit canonicalSimilarItemsFailedForConnection(
                    connectionId, itemId, error.userMessage);
                return;
            }

            const QJsonObject root = doc.object();
            if (!root.contains("Items") || !root.value("Items").isArray()) {
                NetworkError error;
                error.endpoint = "getSimilarItems";
                error.code = -2;
                error.userMessage = tr("Invalid similar items response");
                emit similarItemsFailed(itemId, error.userMessage);
                emit canonicalSimilarItemsFailedForConnection(
                    connectionId, itemId, error.userMessage);
                return;
            }

            const QJsonArray wireItems = root.value("Items").toArray();
            const QVariantList canonicalItems = m_authService
                ? m_authService->mapMediaItems(wireItems, connectionId)
                : QVariantList{};
            emit similarItemsLoaded(itemId, wireItems);
            emit canonicalSimilarItemsLoaded(itemId, canonicalItems);
            emit canonicalSimilarItemsLoadedForConnection(connectionId, itemId, canonicalItems);
        },
        [this, itemId, connectionId](const NetworkError &error) {
            emit similarItemsFailed(itemId, error.userMessage);
            emit canonicalSimilarItemsFailedForConnection(
                connectionId, itemId, error.userMessage);
        });
}

/**
 * @brief Resolves the best next episode for a series, optionally skipping a specific episode.
 *
 * Fetches the recursive episode list for the series, resolves a canonical series order locally,
 * and emits the best canonical next episode based on watch state. The episode map is empty when
 * no eligible episode is available.
 *
 * @param seriesId The series identifier to query.
 * @param excludeItemId If non-empty, the returned episode will not have this Id; pass an empty string to allow any episode.
 * @param requestContext Optional internal request ownership tag used by playback-prefetch callers.
 */
void LibraryService::getNextUnplayedEpisode(const QString &seriesId,
                                            const QString &excludeItemId,
                                            const QString &requestContext)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getNextUnplayedEpisode";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit nextUnplayedEpisodeFailed(seriesId, error.userMessage, requestContext);
        return;
    }
    
    const QStringList fields = {
        QStringLiteral("Name"),
        QStringLiteral("SortName"),
        QStringLiteral("Overview"),
        QStringLiteral("UserData"),
        QStringLiteral("RunTimeTicks"),
        QStringLiteral("ImageTags"),
        QStringLiteral("ParentId"),
        QStringLiteral("SeasonId"),
        QStringLiteral("SeriesId"),
        QStringLiteral("SeriesName"),
        QStringLiteral("IndexNumber"),
        QStringLiteral("ParentIndexNumber"),
        QStringLiteral("PremiereDate"),
        QStringLiteral("LocationType"),
        QStringLiteral("AirsBeforeSeasonNumber"),
        QStringLiteral("AirsAfterSeasonNumber"),
        QStringLiteral("AirsBeforeEpisodeNumber")
    };
    const QString connectionId = activeConnectionId(m_authService);
    QString endpoint = QString("/Users/%1/Items?ParentId=%2&Recursive=true&IncludeItemTypes=Episode&Fields=%3&SortBy=ParentIndexNumber,IndexNumber,SortName&EnableImageTypes=Primary,Thumb")
        .arg(m_authService->getUserId(), seriesId, fields.join(','));
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId, excludeItemId, requestContext, connectionId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getNextUnplayedEpisode";
                error.code = -2;
                error.userMessage = tr("Invalid next unplayed episode response");
                emit nextUnplayedEpisodeFailed(seriesId, error.userMessage, requestContext);
                return;
            }
            const QJsonObject root = doc.object();
            if (!root.contains("Items") || !root.value("Items").isArray()) {
                NetworkError error;
                error.endpoint = "getNextUnplayedEpisode";
                error.code = -2;
                error.userMessage = tr("Invalid next unplayed episode response");
                emit nextUnplayedEpisodeFailed(seriesId, error.userMessage, requestContext);
                return;
            }
            const QJsonArray wireItems = root.value("Items").toArray();
            const QVariantMap selectedEpisode = NextEpisodeResolver::resolveBestNextEpisode(
                m_authService->mapMediaItems(wireItems, connectionId), excludeItemId);
            emit canonicalNextUnplayedEpisodeLoaded(
                connectionId, seriesId, selectedEpisode, requestContext);
        },
        [this, seriesId, requestContext](const NetworkError &error) {
            emit nextUnplayedEpisodeFailed(seriesId, error.userMessage, requestContext);
        });
}

void LibraryService::markSeriesWatched(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), seriesId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit seriesWatchedStatusChanged(seriesId);
        }
    });
}

void LibraryService::markSeriesUnwatched(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), seriesId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit seriesWatchedStatusChanged(seriesId);
        }
    });
}

void LibraryService::markItemPlayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit itemPlayedStatusChanged(itemId, true);
        }
    });
}

void LibraryService::markItemUnplayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit itemPlayedStatusChanged(itemId, false);
        }
    });
}

void LibraryService::markItemFavorite(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/FavoriteItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit favoriteStatusChanged(itemId, true);
        }
    });
}

void LibraryService::markItemUnfavorite(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/FavoriteItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (m_authService->checkForSessionExpiry(reply)) return;
        if (reply->error() == QNetworkReply::NoError) {
            emit favoriteStatusChanged(itemId, false);
        }
    });
}

void LibraryService::toggleFavorite(const QString &itemId, bool isFavorite)
{
    if (isFavorite) {
        markItemFavorite(itemId);
    } else {
        markItemUnfavorite(itemId);
    }
}

void LibraryService::getThemeSongs(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Items/%1/ThemeSongs?UserId=%2").arg(seriesId, m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getThemeSongs";
                error.code = -2;
                error.userMessage = tr("Invalid theme songs response");
                emitError(error);
                return;
            }
            QJsonArray items = doc.object()["Items"].toArray();
            
            QStringList urls;
            for (const auto &item : items) {
                QString itemId = item.toObject()["Id"].toString();
                if (!itemId.isEmpty()) {
                    urls.append(getStreamUrl(itemId));
                }
            }
            emit themeSongsLoaded(seriesId, urls);
        });
}

// ============================================================================
// Search
// ============================================================================

void LibraryService::search(const QString &searchTerm, int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    const QString normalizedSearchTerm = searchTerm.trimmed();
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "search";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalSearchResultsFailed(connectionId, normalizedSearchTerm, error.userMessage);
        emitError(error);
        return;
    }

    if (normalizedSearchTerm.isEmpty()) {
        emit canonicalSearchResultsLoaded(connectionId, normalizedSearchTerm, {}, {});
        return;
    }

    const QStringList fields = {"Path", "Overview", "ImageTags", "BackdropImageTags", "ProductionYear", "CommunityRating", "UserData"};

    QString endpoint = QString("/Users/%1/Items?SearchTerm=%2&IncludeItemTypes=Movie,Series&Recursive=true&Fields=%3&Limit=%4&EnableImageTypes=Primary,Backdrop")
                           .arg(m_authService->getUserId())
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(normalizedSearchTerm)))
                           .arg(fields.join(","))
                           .arg(limit);

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, connectionId, normalizedSearchTerm](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "search";
                error.code = -2;
                error.userMessage = tr("Invalid search response");
                emit canonicalSearchResultsFailed(connectionId, normalizedSearchTerm, error.userMessage);
                emitError(error);
                return;
            }

            QVariantList movies;
            QVariantList series;
            const QVariantList items = m_authService->mapMediaItems(
                doc.object().value(QStringLiteral("Items")).toArray(), connectionId);
            for (const QVariant &value : items) {
                const QVariantMap item = value.toMap();
                const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
                if (mediaType == QStringLiteral("Movie")) {
                    movies.append(item);
                } else if (mediaType == QStringLiteral("Series")) {
                    series.append(item);
                }
            }

            emit canonicalSearchResultsLoaded(connectionId, normalizedSearchTerm, movies, series);
        },
        [this, connectionId, normalizedSearchTerm](const NetworkError &error) {
            emit canonicalSearchResultsFailed(connectionId, normalizedSearchTerm, error.userMessage);
            emitError(error);
        });
}

void LibraryService::getRandomItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getRandomItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalRandomItemsFailed(connectionId, error.userMessage);
        emitError(error);
        return;
    }

    const QStringList fields = {"Overview", "ImageTags", "BackdropImageTags", "ProductionYear"};

    QString endpoint = QString("/Users/%1/Items?IncludeItemTypes=Movie,Series&Recursive=true&SortBy=Random&Limit=%2&Fields=%3")
                           .arg(m_authService->getUserId())
                           .arg(limit)
                           .arg(fields.join(","));

    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, connectionId](QNetworkReply *reply) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                NetworkError error;
                error.endpoint = "getRandomItems";
                error.code = -2;
                error.userMessage = tr("Invalid random items response");
                emit canonicalRandomItemsFailed(connectionId, error.userMessage);
                emitError(error);
                return;
            }
            emit canonicalRandomItemsLoaded(
                connectionId,
                m_authService->mapMediaItems(
                    doc.object().value(QStringLiteral("Items")).toArray(), connectionId));
        },
        [this, connectionId](const NetworkError &error) {
            emit canonicalRandomItemsFailed(connectionId, error.userMessage);
            emitError(error);
        });
}

// ============================================================================
// URL Helpers
// ============================================================================

void LibraryService::getHeroLibraryItems(int limit, const QStringList &parentIds, bool unwatchedOnly)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getHeroLibraryItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emit canonicalHeroLibraryItemsFailed(connectionId, error.userMessage);
        emitError(error);
        return;
    }

    const quint64 requestGeneration = ++m_heroLibraryRequestGeneration;
    const int clampedLimit = qBound(1, limit, 25);
    const QStringList fields = {
        "Overview", "SeriesName", "ImageTags", "BackdropImageTags", "ParentBackdropImageTags",
        "ParentBackdropItemId", "ParentId", "SeriesId", "SeriesPrimaryImageTag",
        "ParentPrimaryImageTag", "ProductionYear", "PremiereDate", "UserData",
        "RunTimeTicks", "CommunityRating", "OfficialRating", "Genres", "Studios", "Tags"
    };

    // Filter out empty parentIds. When none remain, sample across all libraries.
    QStringList ids;
    for (const QString &id : parentIds) {
        if (!id.trimmed().isEmpty()) ids.append(id.trimmed());
    }

    auto buildEndpoint = [this, clampedLimit, unwatchedOnly, fields](const QString &parentId) {
        QString base = QString("/Users/%1/Items?IncludeItemTypes=Movie,Series&Recursive=true"
                               "&SortBy=Random&Limit=%2&Fields=%3"
                               "&EnableImageTypes=Primary,Backdrop,Thumb,Logo&ImageTypeLimit=1")
                           .arg(m_authService->getUserId())
                           .arg(clampedLimit)
                           .arg(fields.join(","));
        if (!parentId.isEmpty()) {
            base += "&ParentId=" + parentId;
        }
        if (unwatchedOnly) {
            base += "&IsPlayed=false";
        }
        return base;
    };

    // No parent filters: single request across all libraries.
    if (ids.isEmpty()) {
        const QString endpoint = buildEndpoint(QString());
        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, connectionId, requestGeneration](QNetworkReply *reply) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getHeroLibraryItems";
                    error.code = -2;
                    error.userMessage = tr("Invalid hero library items response");
                    emit canonicalHeroLibraryItemsFailed(connectionId, error.userMessage);
                    emitError(error);
                    return;
                }
                const QJsonArray items = doc.object()["Items"].toArray();
                emit heroLibraryItemsLoaded(items);
                emit canonicalHeroLibraryItemsLoaded(
                    connectionId, m_authService->mapMediaItems(items, connectionId));
            },
            [this, connectionId, requestGeneration](const NetworkError &error) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                emit canonicalHeroLibraryItemsFailed(connectionId, error.userMessage);
                emitError(error);
            });
        return;
    }

    // Single parentId: one request.
    if (ids.size() == 1) {
        const QString endpoint = buildEndpoint(ids.first());
        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, connectionId, requestGeneration](QNetworkReply *reply) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    NetworkError error;
                    error.endpoint = "getHeroLibraryItems";
                    error.code = -2;
                    error.userMessage = tr("Invalid hero library items response");
                    emit canonicalHeroLibraryItemsFailed(connectionId, error.userMessage);
                    emitError(error);
                    return;
                }
                const QJsonArray items = doc.object()["Items"].toArray();
                emit heroLibraryItemsLoaded(items);
                emit canonicalHeroLibraryItemsLoaded(
                    connectionId, m_authService->mapMediaItems(items, connectionId));
            },
            [this, connectionId, requestGeneration](const NetworkError &error) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                emit canonicalHeroLibraryItemsFailed(connectionId, error.userMessage);
                emitError(error);
            });
        return;
    }

    // Multiple parentIds: fan out concurrently and aggregate. Each request asks
    // for `clampedLimit` items so the union is a bounded random sample across the
    // selected libraries; the provider caps the final list client-side.
    auto aggregate = std::make_shared<QJsonArray>();
    auto remaining = std::make_shared<int>(ids.size());
    for (const QString &parentId : ids) {
        const QString endpoint = buildEndpoint(parentId);
        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, aggregate, remaining, clampedLimit, connectionId, requestGeneration](QNetworkReply *reply) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (doc.isObject()) {
                    const QJsonArray items = doc.object()["Items"].toArray();
                    for (const QJsonValue &v : items) {
                        if (aggregate->size() >= clampedLimit) break;
                        aggregate->append(v);
                    }
                }
                if (--(*remaining) <= 0) {
                    // Trim to the requested cap before emitting.
                    QJsonArray trimmed;
                    const int total = qMin(aggregate->size(), clampedLimit);
                    for (int i = 0; i < total; ++i) trimmed.append(aggregate->at(i));
                    emit heroLibraryItemsLoaded(trimmed);
                    emit canonicalHeroLibraryItemsLoaded(
                        connectionId, m_authService->mapMediaItems(trimmed, connectionId));
                }
            },
            [this, aggregate, remaining, clampedLimit, connectionId, requestGeneration](const NetworkError &) {
                if (requestGeneration != m_heroLibraryRequestGeneration) {
                    return;
                }
                if (--(*remaining) <= 0) {
                    QJsonArray trimmed;
                    const int total = qMin(aggregate->size(), clampedLimit);
                    for (int i = 0; i < total; ++i) {
                        trimmed.append(aggregate->at(i));
                    }
                    emit heroLibraryItemsLoaded(trimmed);
                    emit canonicalHeroLibraryItemsLoaded(
                        connectionId, m_authService->mapMediaItems(trimmed, connectionId));
                }
            });
    }
}

void LibraryService::getHeroSeriesOverviews(const QStringList &seriesIds)
{
    const QString connectionId = activeConnectionId(m_authService);
    QStringList ids;
    for (const QString &id : seriesIds) {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed)) {
            ids.append(trimmed);
        }
    }

    if (ids.isEmpty()) {
        emit heroSeriesOverviewsLoaded(QJsonObject());
        emit canonicalHeroSeriesOverviewsLoaded(connectionId, QVariantMap());
        return;
    }

    if (!m_authService->isAuthenticated()) {
        QJsonObject overviews;
        for (const QString &id : ids) {
            overviews.insert(id, QString());
        }
        emit heroSeriesOverviewsLoaded(overviews);
        emit canonicalHeroSeriesOverviewsLoaded(connectionId, overviews.toVariantMap());

        NetworkError error;
        error.endpoint = "getHeroSeriesOverviews";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }

    auto overviews = std::make_shared<QJsonObject>();
    auto remaining = std::make_shared<int>(ids.size());
    for (const QString &seriesId : ids) {
        const QString endpoint = QStringLiteral("/Users/%1/Items/%2?Fields=Overview")
                                     .arg(m_authService->getUserId(), seriesId);
        sendRequestWithRetry(endpoint,
            [this, endpoint]() {
                QNetworkRequest request = m_authService->createRequest(endpoint);
                return m_authService->networkManager()->get(request);
            },
            [this, overviews, remaining, seriesId, connectionId](QNetworkReply *reply) {
                const QByteArray data = reply->readAll();
                const QJsonDocument doc = QJsonDocument::fromJson(data);
                QString overview;
                if (doc.isObject()) {
                    overview = doc.object().value(QStringLiteral("Overview")).toString();
                }
                overviews->insert(seriesId, overview);
                if (--(*remaining) <= 0) {
                    emit heroSeriesOverviewsLoaded(*overviews);
                    emit canonicalHeroSeriesOverviewsLoaded(connectionId,
                                                            overviews->toVariantMap());
                }
            },
            [this, overviews, remaining, seriesId, connectionId](const NetworkError &) {
                overviews->insert(seriesId, QString());
                if (--(*remaining) <= 0) {
                    emit heroSeriesOverviewsLoaded(*overviews);
                    emit canonicalHeroSeriesOverviewsLoaded(connectionId,
                                                            overviews->toVariantMap());
                }
            });
    }
}

QString LibraryService::getActiveConnectionId() const
{
    return activeConnectionId(m_authService);
}

QString LibraryService::getStreamUrl(const QString &itemId)
{
    return QString("%1/Videos/%2/stream?Container=mp4,mkv&Static=true&api_key=%3")
        .arg(m_authService->getServerUrl(), itemId, m_authService->getAccessToken());
}

QString LibraryService::getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId,
                                                int audioStreamIndex, int subtitleStreamIndex)
{
    QString url = QString("%1/Videos/%2/stream?Static=true&api_key=%3")
        .arg(m_authService->getServerUrl(), itemId, m_authService->getAccessToken());
    
    if (!mediaSourceId.isEmpty()) {
        url += QString("&MediaSourceId=%1").arg(mediaSourceId);
    }
    if (audioStreamIndex >= 0) {
        url += QString("&AudioStreamIndex=%1").arg(audioStreamIndex);
    }
    if (subtitleStreamIndex >= 0) {
        url += QString("&SubtitleStreamIndex=%1").arg(subtitleStreamIndex);
    }
    
    return url;
}

QString LibraryService::getImageUrl(const QString &itemId, const QString &imageType)
{
    return getCachedArtworkUrl(itemId, imageType, 0, QString(), 400);
}

QString LibraryService::getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    return getCachedArtworkUrl(itemId, imageType, 0, QString(), width);
}

QString LibraryService::getCachedImageUrl(const QString &itemId, const QString &imageType)
{
    return getCachedArtworkUrl(itemId, imageType, 0, QString(), 400);
}

QString LibraryService::getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    return getCachedArtworkUrl(itemId, imageType, 0, QString(), width);
}

QString LibraryService::getCachedArtworkUrl(const QString &itemId,
                                            const QString &imageType,
                                            int imageIndex,
                                            const QString &imageTag,
                                            int width)
{
    return getCachedArtworkUrlForConnection(activeConnectionId(m_authService),
                                              itemId,
                                              imageType,
                                              imageIndex,
                                              imageTag,
                                              width);
}

QString LibraryService::getCachedArtworkUrlForConnection(const QString &connectionId,
                                                          const QString &itemId,
                                                          const QString &imageType,
                                                          int imageIndex,
                                                          const QString &imageTag,
                                                          int width)
{
    Bloom::ArtworkRef artwork;
    artwork.connectionId = connectionId.trimmed();
    artwork.itemId = itemId;
    artwork.kind = Bloom::artworkKindFromName(imageType);
    artwork.index = qMax(0, imageIndex);
    artwork.tag = imageTag.trimmed();
    artwork.requestedWidth = width > 0 ? width : 1920;
    return cachedArtworkSource(artwork);
}

QString LibraryService::getCachedChapterThumbnailUrl(const QString &itemId, int chapterIndex, const QString &imageTag, const QString &imagePath, int width)
{
    if (itemId.isEmpty() || chapterIndex < 0) {
        qCWarning(lcLibrary) << "LibraryService: Refusing chapter thumbnail URL"
                   << "item" << itemId
                   << "index" << chapterIndex;
        return QString();
    }
    if (imageTag.trimmed().isEmpty() && imagePath.trimmed().isEmpty()) {
        return QString();
    }

    if (width <= 0) {
        width = 480;
    }

    const QString cachedUrl = getCachedArtworkUrl(itemId,
                                                   QStringLiteral("chapter"),
                                                   chapterIndex,
                                                   imageTag,
                                                   width);
    qCInfo(lcLibrary) << "LibraryService: Chapter thumbnail request"
            << "item" << itemId
            << "index" << chapterIndex
            << "imageTagEmpty" << imageTag.trimmed().isEmpty()
            << "imagePathEmpty" << imagePath.trimmed().isEmpty();
    return cachedUrl;
}

QNetworkReply* LibraryService::pingServer()
{
    QNetworkRequest request = m_authService->createRequest("/System/Info");
    return m_authService->networkManager()->get(request);
}
