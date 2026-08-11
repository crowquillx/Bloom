#include "LibraryService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "NextEpisodeResolver.h"
#include "models/MediaModels.h"
#include "providers/IPlaybackProvider.h"
#include "utils/ConfigManager.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <QLoggingCategory>
#include "../utils/BloomLogging.h"

namespace {

QString activeConnectionId(const AuthenticationService *authService)
{
    ConfigManager *config = authService ? authService->configManager() : nullptr;
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    return connection.has_value() ? connection->connectionId : QString();
}

QString activeProfileId(const AuthenticationService *authService)
{
    ConfigManager *config = authService ? authService->configManager() : nullptr;
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    return connection.has_value() ? connection->profileId : QString();
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

QStringList metadataStrings(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }
    QStringList result;
    const QVariantList values = value.toList();
    result.reserve(values.size());
    for (const QVariant &entry : values) {
        const QString text = entry.toString();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
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

ProviderCatalogTriState providerTriState(LibraryItemQuery::TriState state)
{
    switch (state) {
    case LibraryItemQuery::TriState::Yes:
        return ProviderCatalogTriState::Yes;
    case LibraryItemQuery::TriState::No:
        return ProviderCatalogTriState::No;
    case LibraryItemQuery::TriState::Any:
        return ProviderCatalogTriState::Any;
    }
    return ProviderCatalogTriState::Any;
}

QHash<QByteArray, QByteArray> responseHeaders(const QNetworkReply *reply)
{
    QHash<QByteArray, QByteArray> headers;
    if (!reply) {
        return headers;
    }
    const auto pairs = reply->rawHeaderPairs();
    for (const auto &pair : pairs) {
        headers.insert(pair.first, pair.second);
    }
    return headers;
}

}

QString LibraryItemQuery::normalizedSortBy() const
{
    return sortBy.isEmpty() ? QStringLiteral("libraryOrder") : sortBy;
}

QString LibraryItemQuery::datasetKey() const
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
    return parts.join(";");
}

QString LibraryItemQuery::cacheKey() const
{
    return datasetKey();
}

LibraryService::LibraryService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_transport(authService ? authService->transport() : nullptr)
    , m_retryPolicy{3, 1000, true}
{
    if (!m_authService) {
        return;
    }

    const auto invalidateRequests = [this]() {
        ++m_requestGeneration;
        for (const QPointer<HttpRequestHandle> &handle : std::as_const(m_catalogRequests)) {
            if (handle) {
                handle->cancel();
            }
        }
        m_catalogRequests.clear();
        m_etags.clear();
        m_lastModified.clear();
        m_inFlightChapterRequests.clear();
    };
    connect(m_authService, &AuthenticationService::authenticationStepChanged,
            this, [this, invalidateRequests]() {
        // Entering the authenticated state enables catalog requests. QML may
        // issue one synchronously from the same notification, so only
        // invalidate while authentication is unavailable.
        if (!m_authService->isAuthenticated()) {
            invalidateRequests();
        }
    });
    connect(m_authService, &AuthenticationService::userIdChanged,
            this, invalidateRequests);
}

ProviderCatalogQuery LibraryService::baseCatalogQuery() const
{
    ProviderCatalogQuery query;
    if (m_authService) {
        query.userId = m_authService->getUserId();
    }
    return query;
}

LibraryService::CatalogRequestIdentity LibraryService::catalogRequestIdentity() const
{
    CatalogRequestIdentity identity;
    identity.generation = m_requestGeneration;
    identity.connectionId = activeConnectionId(m_authService);
    identity.userId = m_authService ? m_authService->getUserId() : QString();
    identity.profileId = activeProfileId(m_authService);
    identity.provider = m_authService ? m_authService->catalogProvider() : nullptr;
    return identity;
}

bool LibraryService::isCurrent(const CatalogRequestIdentity &identity) const
{
    return m_authService
        && m_authService->isAuthenticated()
        && identity.generation == m_requestGeneration
        && identity.connectionId == activeConnectionId(m_authService)
        && identity.userId == m_authService->getUserId()
        && identity.profileId == activeProfileId(m_authService)
        && identity.provider == m_authService->catalogProvider();
}

void LibraryService::sendCatalogRequest(
    const QString &operationName,
    ProviderCatalogOperation operation,
    const ProviderCatalogQuery &query,
    CatalogResponseHandler responseHandler,
    FailureHandler failureHandler,
    CatalogNotModifiedHandler notModifiedHandler)
{
    if (!m_authService || !m_authService->isAuthenticated()) {
        emitCatalogError(operationName, tr("Not authenticated"), std::move(failureHandler));
        return;
    }
    if (!m_transport || !m_authService->networkManager()) {
        emitCatalogError(
            operationName, tr("Network transport is unavailable."), std::move(failureHandler));
        return;
    }

    const CatalogRequestIdentity identity = catalogRequestIdentity();
    if (!identity.provider) {
        emitCatalogError(
            operationName, tr("The active provider has no catalog service."),
            std::move(failureHandler));
        return;
    }

    ProviderCatalogRequest providerRequest =
        identity.provider->createRequest(operation, query);
    if (!providerRequest.supported || providerRequest.relativeEndpoint.isEmpty()) {
        emitCatalogError(
            operationName,
            providerRequest.unsupportedReason.isEmpty()
                ? tr("The active provider does not support this operation.")
                : providerRequest.unsupportedReason,
            std::move(failureHandler),
            -3);
        return;
    }
    if (query.useCacheValidation) {
        const QString etag = m_etags.value(providerRequest.relativeEndpoint);
        const QString lastModified = m_lastModified.value(providerRequest.relativeEndpoint);
        if (!etag.isEmpty()) {
            providerRequest.extraHeaders.insert(
                QByteArrayLiteral("If-None-Match"), etag.toUtf8());
        }
        if (!lastModified.isEmpty()) {
            providerRequest.extraHeaders.insert(
                QByteArrayLiteral("If-Modified-Since"), lastModified.toUtf8());
        }
    }

    HttpRequestOptions options;
    options.retryPolicy = m_retryPolicy;
    options.retrySafety =
        providerRequest.retrySafety == ProviderCatalogRetrySafety::Idempotent
        ? RetrySafety::Idempotent
        : RetrySafety::Never;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    HttpRequestHandle *handle = m_transport->sendWithRetry(
        this,
        providerRequest.relativeEndpoint,
        [this, identity, providerRequest]() -> QNetworkReply * {
            if (!isCurrent(identity)) {
                return nullptr;
            }
            QNetworkRequest request =
                m_authService->createRequest(providerRequest.relativeEndpoint);
            for (auto it = providerRequest.extraHeaders.cbegin();
                 it != providerRequest.extraHeaders.cend();
                 ++it) {
                request.setRawHeader(it.key(), it.value());
            }
            switch (providerRequest.method) {
            case ProviderHttpMethod::Get:
                return m_authService->networkManager()->get(request);
            case ProviderHttpMethod::Post:
                return m_authService->networkManager()->post(request, providerRequest.body);
            case ProviderHttpMethod::Put:
                return m_authService->networkManager()->put(request, providerRequest.body);
            case ProviderHttpMethod::Delete:
                return m_authService->networkManager()->deleteResource(request);
            }
            return nullptr;
        },
        [this, identity, operationName, operation, providerRequest,
         responseHandler = std::move(responseHandler),
         failureHandler, notModifiedHandler = std::move(notModifiedHandler)](
            QNetworkReply *reply) mutable {
            if (!isCurrent(identity)) {
                return;
            }
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 304) {
                if (notModifiedHandler) {
                    notModifiedHandler();
                }
                return;
            }

            const QByteArray body = reply->readAll();
            const QHash<QByteArray, QByteArray> headers = responseHeaders(reply);
            emit parsingStarted(operationName);

            auto *watcher = new QFutureWatcher<ProviderCatalogResponse>(this);
            connect(watcher, &QFutureWatcher<ProviderCatalogResponse>::finished,
                    this,
                    [this, watcher, identity, operationName, operation, providerRequest,
                     responseHandler = std::move(responseHandler), failureHandler]() mutable {
                const ProviderCatalogResponse response = watcher->result();
                watcher->deleteLater();
                emit parsingFinished(operationName);
                if (!isCurrent(identity)) {
                    return;
                }
                if (!response.valid) {
                    emitCatalogError(
                        operationName,
                        response.error.isEmpty()
                            ? tr("Invalid provider response.") : response.error,
                        failureHandler,
                        -2);
                    return;
                }

                const QString etag =
                    response.snapshot.value(QStringLiteral("etag")).toString();
                const QString lastModified =
                    response.snapshot.value(QStringLiteral("lastModified")).toString();
                if (!etag.isEmpty()) {
                    m_etags.insert(providerRequest.relativeEndpoint, etag);
                }
                if (!lastModified.isEmpty()) {
                    m_lastModified.insert(providerRequest.relativeEndpoint, lastModified);
                }
                if (responseHandler) {
                    responseHandler(response);
                }
            });
            watcher->setFuture(QtConcurrent::run(
                [provider = identity.provider, operation, body, headers]() {
                    return provider->parseResponse(operation, body, headers);
                }));
        },
        [this, identity, operationName,
         failureHandler](const NetworkError &transportError) {
            if (!isCurrent(identity)) {
                return;
            }
            NetworkError error = transportError;
            error.endpoint = operationName;
            if (failureHandler) {
                failureHandler(error);
            }
            emitError(error);
        },
        options);
    m_catalogRequests.removeIf(
        [](const QPointer<HttpRequestHandle> &candidate) { return candidate.isNull(); });
    m_catalogRequests.append(handle);
}

void LibraryService::emitCatalogError(
    const QString &operationName,
    const QString &message,
    FailureHandler failureHandler,
    int code)
{
    NetworkError error;
    error.code = code;
    error.endpoint = operationName;
    error.userMessage = message;
    if (failureHandler) {
        failureHandler(error);
    }
    emitError(error);
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
    getViewsForRequest(QString());
}

void LibraryService::getViewsForRequest(const QString &requestKey)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    sendCatalogRequest(
        QStringLiteral("getViews"),
        ProviderCatalogOperation::Views,
        query,
        [this, connectionId, requestKey](const ProviderCatalogResponse &response) {
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            emit canonicalViewsLoadedForConnection(connectionId, items);
            emit canonicalViewsLoadedForRequest(connectionId, requestKey, items);
        },
        [this, connectionId, requestKey](const NetworkError &error) {
            emit canonicalViewsFailedForRequest(
                connectionId, requestKey, error.userMessage);
        });
}

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
    const QString parentId = query.parentId;
    const QString queryKey = query.requestKey.isEmpty() ? query.cacheKey() : query.requestKey;
    const QString connectionId = activeConnectionId(m_authService);

    ProviderCatalogQuery providerQuery = baseCatalogQuery();
    providerQuery.parentId = query.parentId;
    providerQuery.startIndex = query.startIndex;
    providerQuery.limit = query.limit;
    providerQuery.searchTerm = query.searchTerm;
    providerQuery.genres = query.genres;
    providerQuery.tags = query.tags;
    providerQuery.studios = query.studios;
    providerQuery.minPremiereDate = query.minPremiereDate;
    providerQuery.maxPremiereDate = query.maxPremiereDate;
    providerQuery.minDateLastSaved = query.minDateLastSaved;
    providerQuery.watched = providerTriState(query.watched);
    providerQuery.favorite = providerTriState(query.favorite);
    providerQuery.minCommunityRating = query.minCommunityRating;
    providerQuery.years = query.years;
    providerQuery.sortBy = query.sortBy;
    providerQuery.sortOrder = query.sortOrder;
    providerQuery.includeItemTypes = query.includeItemTypes;
    providerQuery.recursive = query.recursive;
    providerQuery.includeHeavyFields = query.includeHeavyFields;
    providerQuery.useCacheValidation = query.useCacheValidation;

    sendCatalogRequest(
        QStringLiteral("getItems"),
        ProviderCatalogOperation::Items,
        providerQuery,
        [this, parentId, queryKey, connectionId](
            const ProviderCatalogResponse &response) {
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            emit canonicalItemsLoadedForConnection(
                connectionId, parentId, queryKey, items, response.total);
        },
        [this, connectionId, parentId, queryKey](const NetworkError &error) {
            emit canonicalItemsFailedForConnection(
                connectionId, parentId, queryKey, error.userMessage);
        },
        [this, parentId, queryKey, connectionId]() {
            emit canonicalItemsNotModifiedForConnection(
                connectionId, parentId, queryKey);
        });
}

void LibraryService::getFilterOptions(const QString &parentId,
                                      const QStringList &includeItemTypes,
                                      bool recursive)
{
    getFilterOptionsForRequest(
        parentId, includeItemTypes, recursive, QString());
}

void LibraryService::getFilterOptionsForRequest(
    const QString &parentId,
    const QStringList &includeItemTypes,
    bool recursive,
    const QString &requestKey)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService || !m_authService->isAuthenticated()) {
        const QString error = tr("Not authenticated");
        emit filterOptionsFailedForRequest(
            connectionId, parentId, requestKey, error);
        emitCatalogError(QStringLiteral("getFilterOptions"), error);
        return;
    }
    const QStringList facets = {
        QString(),
        QStringLiteral("genres"),
        QStringLiteral("studios")
    };
    auto state = std::make_shared<QHash<QString, QStringList>>();
    auto remaining = std::make_shared<int>(facets.size());
    auto successes = std::make_shared<int>(0);
    auto lastError = std::make_shared<QString>();
    const auto finish = [this, connectionId, parentId, requestKey, state,
                         remaining, successes, lastError](bool succeeded,
                                                         const QString &error) {
        if (succeeded) {
            ++(*successes);
        } else if (!error.isEmpty()) {
            *lastError = error;
        }
        if (--(*remaining) > 0) {
            return;
        }
        if (*successes == 0) {
            emit filterOptionsFailedForRequest(
                connectionId, parentId, requestKey,
                lastError->isEmpty()
                    ? tr("Unable to load filter options.") : *lastError);
            return;
        }
        QStringList genres = state->value(QStringLiteral("genres"));
        genres.append(state->value(QStringLiteral("filterGenres")));
        const QStringList normalizedGenres = sortedList(genres);
        const QStringList normalizedTags =
            sortedList(state->value(QStringLiteral("tags")));
        const QStringList normalizedStudios =
            sortedList(state->value(QStringLiteral("studios")));
        emit filterOptionsLoaded(
            parentId,
            normalizedGenres,
            normalizedTags,
            normalizedStudios);
        emit filterOptionsLoadedForRequest(
            connectionId,
            parentId,
            requestKey,
            normalizedGenres,
            normalizedTags,
            normalizedStudios);
    };

    for (const QString &facet : facets) {
        ProviderCatalogQuery providerQuery = baseCatalogQuery();
        providerQuery.parentId = parentId;
        providerQuery.includeItemTypes = includeItemTypes;
        providerQuery.recursive = recursive;
        providerQuery.filterFacet = facet;
        sendCatalogRequest(
            QStringLiteral("getFilterOptions"),
            ProviderCatalogOperation::FilterOptions,
            providerQuery,
            [state, finish, facet](const ProviderCatalogResponse &response) {
                if (facet.isEmpty()) {
                    state->insert(
                        QStringLiteral("filterGenres"),
                        metadataStrings(response.filterMetadata.value(
                            QStringLiteral("genres"))));
                    state->insert(
                        QStringLiteral("tags"),
                        metadataStrings(response.filterMetadata.value(
                            QStringLiteral("tags"))));
                    const QStringList studios = metadataStrings(
                        response.filterMetadata.value(QStringLiteral("studios")));
                    if (!studios.isEmpty()) {
                        state->insert(QStringLiteral("studios"), studios);
                    }
                } else {
                    state->insert(
                        facet,
                        metadataStrings(response.filterMetadata.value(
                            QStringLiteral("namedItems"))));
                }
                finish(true, QString());
            },
            [finish](const NetworkError &error) {
                finish(false, error.userMessage);
            });
    }
}

void LibraryService::getNextUp()
{
    const QString connectionId = activeConnectionId(m_authService);
    sendCatalogRequest(
        QStringLiteral("getNextUp"),
        ProviderCatalogOperation::NextUp,
        baseCatalogQuery(),
        [this, connectionId](const ProviderCatalogResponse &response) {
            emit canonicalNextUpLoaded(
                connectionId,
                m_authService->mapMediaItems(response.rawItems, connectionId));
        });
}

void LibraryService::getLatestMedia(const QString &parentId)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.parentId = parentId;
    sendCatalogRequest(
        QStringLiteral("getLatestMedia"),
        ProviderCatalogOperation::LatestMedia,
        query,
        [this, parentId, connectionId](const ProviderCatalogResponse &response) {
            emit canonicalLatestMediaLoaded(
                connectionId,
                parentId,
                m_authService->mapMediaItems(response.rawItems, connectionId));
        });
}

void LibraryService::getHomeBackdropItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    const quint64 generation = m_requestGeneration;
    const int requestedLimit = limit > 0 ? qBound(50, limit, 20000) : 0;

    if (requestedLimit > 0) {
        ProviderCatalogQuery query = baseCatalogQuery();
        query.limit = requestedLimit;
        sendCatalogRequest(
            QStringLiteral("getHomeBackdropItems"),
            ProviderCatalogOperation::HomeBackdrops,
            query,
            [this, connectionId](const ProviderCatalogResponse &response) {
                emit canonicalHomeBackdropItemsLoaded(
                    connectionId,
                    m_authService->mapMediaItems(response.rawItems, connectionId));
            },
            [this, connectionId](const NetworkError &error) {
                emit canonicalHomeBackdropItemsFailed(
                    connectionId, error.userMessage);
            });
        return;
    }

    auto fetchPage =
        std::make_shared<std::function<void(int, std::optional<QString>)>>();
    *fetchPage = [this, connectionId, generation, fetchPage](
                     int startIndex, std::optional<QString> snapshot) {
        if (generation != m_requestGeneration) {
            return;
        }
        ProviderCatalogQuery query = baseCatalogQuery();
        query.startIndex = startIndex;
        query.snapshot = snapshot;
        sendCatalogRequest(
            QStringLiteral("getHomeBackdropItems"),
            ProviderCatalogOperation::HomeBackdrops,
            query,
            [this, connectionId, generation, fetchPage, startIndex, snapshot](
                const ProviderCatalogResponse &response) {
                if (!response.rawItems.isEmpty()) {
                    emit canonicalHomeBackdropItemsLoaded(
                        connectionId,
                        m_authService->mapMediaItems(response.rawItems, connectionId));
                }
                const bool hasMore = response.hasMore
                    || (m_authService->activeProviderKind() == ProviderKind::Jellyfin
                        && response.rawItems.size() == 250);
                if (generation == m_requestGeneration
                    && hasMore
                    && !response.rawItems.isEmpty()) {
                    const int nextStart = startIndex + response.rawItems.size();
                    std::optional<QString> nextSnapshot = snapshot;
                    const QString responseSnapshot =
                        response.snapshot.value(QStringLiteral("snapshot")).toString();
                    if (!responseSnapshot.isEmpty()) {
                        nextSnapshot = responseSnapshot;
                    }
                    QTimer::singleShot(250, this, [fetchPage, nextStart, nextSnapshot]() {
                        (*fetchPage)(nextStart, nextSnapshot);
                    });
                }
            },
            [this, connectionId](const NetworkError &error) {
                emit canonicalHomeBackdropItemsFailed(
                    connectionId, error.userMessage);
            });
    };
    (*fetchPage)(0, std::nullopt);
}

void LibraryService::getScreensaverItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.limit = qBound(10, limit > 0 ? limit : 80, 200);
    sendCatalogRequest(
        QStringLiteral("getScreensaverItems"),
        ProviderCatalogOperation::ScreensaverItems,
        query,
        [this, connectionId](const ProviderCatalogResponse &response) {
            QVariantList filteredItems;
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
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
        });
}

void LibraryService::getItem(const QString &itemId)
{
    getItem(itemId, QString());
}

void LibraryService::getItem(const QString &itemId, const QString &requestContext)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.useCacheValidation = true;
    sendCatalogRequest(
        QStringLiteral("getItem"),
        ProviderCatalogOperation::Item,
        query,
        [this, itemId, requestContext, connectionId](
            const ProviderCatalogResponse &response) {
            const QVariantMap item =
                m_authService->mapMediaItem(response.rawItem, connectionId);
            emit canonicalItemLoaded(itemId, item, requestContext);
            emit canonicalItemLoaded(itemId, item);
        },
        [this, itemId, requestContext](const NetworkError &error) {
            emit itemFailed(itemId, error.userMessage, requestContext);
        },
        [this, itemId, requestContext]() {
            emit itemNotModified(itemId, requestContext);
            emit itemNotModified(itemId);
        });
}

void LibraryService::clearItemCacheValidation(const QString &itemId)
{
    if (!m_authService || !m_authService->isAuthenticated() || itemId.isEmpty()) {
        return;
    }
    const ICatalogProvider *provider = m_authService->catalogProvider();
    if (!provider) {
        return;
    }
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    const ProviderCatalogRequest request =
        provider->createRequest(ProviderCatalogOperation::Item, query);
    if (!request.supported || request.relativeEndpoint.isEmpty()) {
        return;
    }
    m_etags.remove(request.relativeEndpoint);
    m_lastModified.remove(request.relativeEndpoint);
}

void LibraryService::getChapters(const QString &itemId)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (itemId.isEmpty()) {
        emit chaptersFailed(connectionId, itemId, tr("Item ID is empty"));
        return;
    }
    const QString requestKey = connectionId + QLatin1Char('\n')
        + activeProfileId(m_authService) + QLatin1Char('\n') + itemId;
    if (m_inFlightChapterRequests.contains(requestKey)) {
        return;
    }
    m_inFlightChapterRequests.insert(requestKey);

    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    sendCatalogRequest(
        QStringLiteral("getChapters"),
        ProviderCatalogOperation::Chapters,
        query,
        [this, itemId, connectionId, requestKey](
            const ProviderCatalogResponse &response) {
            m_inFlightChapterRequests.remove(requestKey);
            emit canonicalChaptersLoaded(
                connectionId,
                itemId,
                m_authService->mapChaptersFromItem(
                    response.rawItem, connectionId, itemId));
        },
        [this, itemId, connectionId, requestKey](const NetworkError &error) {
            m_inFlightChapterRequests.remove(requestKey);
            emit chaptersFailed(connectionId, itemId, error.userMessage);
        });
}

void LibraryService::resolveLibraryForItem(const QString &itemId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    sendCatalogRequest(
        QStringLiteral("resolveLibraryForItem"),
        ProviderCatalogOperation::ResolveLibrary,
        query,
        [this, itemId](const ProviderCatalogResponse &response) {
            QString libraryId =
                response.snapshot.value(QStringLiteral("libraryId")).toString();
            if (libraryId.isEmpty()) {
                libraryId =
                    m_authService->mapLibraryIdFromAncestors(response.rawItems);
            }
            if (libraryId.isEmpty()) {
                emit itemLibraryResolutionFailed(
                    itemId, tr("Library ancestor not found"));
                return;
            }
            emit itemLibraryResolved(itemId, libraryId);
        },
        [this, itemId](const NetworkError &error) {
            emit itemLibraryResolutionFailed(itemId, error.userMessage);
        });
}

void LibraryService::getSeriesDetails(const QString &seriesId)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = seriesId;
    query.useCacheValidation = true;
    sendCatalogRequest(
        QStringLiteral("getSeriesDetails"),
        ProviderCatalogOperation::Item,
        query,
        [this, connectionId, seriesId](const ProviderCatalogResponse &response) {
            emit canonicalSeriesDetailsLoaded(
                connectionId,
                seriesId,
                m_authService->mapMediaItem(response.rawItem, connectionId));
        },
        [this, connectionId, seriesId](const NetworkError &error) {
            emit canonicalSeriesDetailsFailed(
                connectionId, seriesId, error.userMessage);
        },
        [this, connectionId, seriesId]() {
            emit canonicalSeriesDetailsNotModified(connectionId, seriesId);
        });
}

void LibraryService::getSimilarItems(const QString &itemId, int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.limit = qMax(1, limit);
    sendCatalogRequest(
        QStringLiteral("getSimilarItems"),
        ProviderCatalogOperation::SimilarItems,
        query,
        [this, itemId, connectionId](const ProviderCatalogResponse &response) {
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            emit canonicalSimilarItemsLoadedForConnection(
                connectionId, itemId, items);
        },
        [this, itemId, connectionId](const NetworkError &error) {
            emit canonicalSimilarItemsFailedForConnection(
                connectionId, itemId, error.userMessage);
        });
}

void LibraryService::getNextUnplayedEpisode(const QString &seriesId,
                                            const QString &excludeItemId,
                                            const QString &requestContext)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.seriesId = seriesId;
    query.excludeItemId = excludeItemId;
    sendCatalogRequest(
        QStringLiteral("getNextUnplayedEpisode"),
        ProviderCatalogOperation::NextUnplayedEpisode,
        query,
        [this, connectionId, seriesId, excludeItemId, requestContext](
            const ProviderCatalogResponse &response) {
            const QVariantMap episode = NextEpisodeResolver::resolveBestNextEpisode(
                m_authService->mapMediaItems(response.rawItems, connectionId),
                excludeItemId);
            emit canonicalNextUnplayedEpisodeLoaded(
                connectionId, seriesId, episode, requestContext);
        },
        [this, seriesId, requestContext](const NetworkError &error) {
            emit nextUnplayedEpisodeFailed(
                seriesId, error.userMessage, requestContext);
        });
}

void LibraryService::markSeriesWatched(const QString &seriesId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = seriesId;
    query.stateValue = true;
    sendCatalogRequest(
        QStringLiteral("markSeriesWatched"),
        ProviderCatalogOperation::SetWatched,
        query,
        [this, seriesId](const ProviderCatalogResponse &) {
            emit seriesWatchedStatusChanged(seriesId);
        });
}

void LibraryService::markSeriesUnwatched(const QString &seriesId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = seriesId;
    query.stateValue = false;
    sendCatalogRequest(
        QStringLiteral("markSeriesUnwatched"),
        ProviderCatalogOperation::SetWatched,
        query,
        [this, seriesId](const ProviderCatalogResponse &) {
            emit seriesWatchedStatusChanged(seriesId);
        });
}

void LibraryService::markItemPlayed(const QString &itemId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = true;
    sendCatalogRequest(
        QStringLiteral("markItemPlayed"),
        ProviderCatalogOperation::SetWatched,
        query,
        [this, itemId](const ProviderCatalogResponse &) {
            emit itemPlayedStatusChanged(itemId, true);
        });
}

void LibraryService::markItemUnplayed(const QString &itemId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = false;
    sendCatalogRequest(
        QStringLiteral("markItemUnplayed"),
        ProviderCatalogOperation::SetWatched,
        query,
        [this, itemId](const ProviderCatalogResponse &) {
            emit itemPlayedStatusChanged(itemId, false);
        });
}

void LibraryService::markItemFavorite(const QString &itemId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = true;
    sendCatalogRequest(
        QStringLiteral("markItemFavorite"),
        ProviderCatalogOperation::SetFavorite,
        query,
        [this, itemId](const ProviderCatalogResponse &) {
            emit favoriteStatusChanged(itemId, true);
        });
}

void LibraryService::markItemUnfavorite(const QString &itemId)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = false;
    sendCatalogRequest(
        QStringLiteral("markItemUnfavorite"),
        ProviderCatalogOperation::SetFavorite,
        query,
        [this, itemId](const ProviderCatalogResponse &) {
            emit favoriteStatusChanged(itemId, false);
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
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = seriesId;
    sendCatalogRequest(
        QStringLiteral("getThemeSongs"),
        ProviderCatalogOperation::ThemeSongs,
        query,
        [this, seriesId, connectionId](const ProviderCatalogResponse &response) {
            QStringList urls;
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            for (const QVariant &value : items) {
                const QString itemId =
                    value.toMap().value(QStringLiteral("itemId")).toString();
                if (!itemId.isEmpty()) {
                    urls.append(getStreamUrl(itemId));
                }
            }
            emit themeSongsLoaded(seriesId, urls);
        });
}

void LibraryService::search(const QString &searchTerm, int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    const QString normalizedSearchTerm = searchTerm.trimmed();
    if (!m_authService || !m_authService->isAuthenticated()) {
        emitCatalogError(
            QStringLiteral("search"),
            tr("Not authenticated"),
            [this, connectionId, normalizedSearchTerm](const NetworkError &error) {
                emit canonicalSearchResultsFailed(
                    connectionId, normalizedSearchTerm, error.userMessage);
            });
        return;
    }
    if (normalizedSearchTerm.isEmpty()) {
        emit canonicalSearchResultsLoaded(
            connectionId, normalizedSearchTerm, {}, {});
        return;
    }
    ProviderCatalogQuery query = baseCatalogQuery();
    query.searchTerm = normalizedSearchTerm;
    query.limit = limit;
    query.includeItemTypes = {
        QStringLiteral("Movie"),
        QStringLiteral("Series")
    };
    sendCatalogRequest(
        QStringLiteral("search"),
        ProviderCatalogOperation::Search,
        query,
        [this, connectionId, normalizedSearchTerm](
            const ProviderCatalogResponse &response) {
            QVariantList movies;
            QVariantList series;
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            for (const QVariant &value : items) {
                const QVariantMap item = value.toMap();
                const QString mediaType =
                    item.value(QStringLiteral("mediaType")).toString();
                if (mediaType == QStringLiteral("Movie")) {
                    movies.append(item);
                } else if (mediaType == QStringLiteral("Series")) {
                    series.append(item);
                }
            }
            emit canonicalSearchResultsLoaded(
                connectionId, normalizedSearchTerm, movies, series);
        },
        [this, connectionId, normalizedSearchTerm](const NetworkError &error) {
            emit canonicalSearchResultsFailed(
                connectionId, normalizedSearchTerm, error.userMessage);
        });
}

void LibraryService::getRandomItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.limit = limit;
    sendCatalogRequest(
        QStringLiteral("getRandomItems"),
        ProviderCatalogOperation::RandomItems,
        query,
        [this, connectionId](const ProviderCatalogResponse &response) {
            emit canonicalRandomItemsLoaded(
                connectionId,
                m_authService->mapMediaItems(response.rawItems, connectionId));
        },
        [this, connectionId](const NetworkError &error) {
            emit canonicalRandomItemsFailed(connectionId, error.userMessage);
        });
}

void LibraryService::getHeroLibraryItems(int limit,
                                         const QStringList &parentIds,
                                         bool unwatchedOnly)
{
    const quint64 heroGeneration = ++m_heroRequestGeneration;
    const QString connectionId = activeConnectionId(m_authService);
    if (!m_authService || !m_authService->isAuthenticated()) {
        emitCatalogError(
            QStringLiteral("getHeroLibraryItems"),
            tr("Not authenticated"),
            [this, connectionId, heroGeneration](const NetworkError &error) {
                if (heroGeneration == m_heroRequestGeneration) {
                    emit canonicalHeroLibraryItemsFailed(
                        connectionId, error.userMessage);
                }
            });
        return;
    }
    const quint64 generation = m_requestGeneration;
    const int clampedLimit = qBound(1, limit, 25);
    QStringList ids;
    for (const QString &id : parentIds) {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed)) {
            ids.append(trimmed);
        }
    }

    if (ids.size() <= 1) {
        ProviderCatalogQuery query = baseCatalogQuery();
        query.limit = clampedLimit;
        query.unwatchedOnly = unwatchedOnly;
        if (!ids.isEmpty()) {
            query.parentId = ids.constFirst();
        }
        sendCatalogRequest(
            QStringLiteral("getHeroLibraryItems"),
            ProviderCatalogOperation::HeroItems,
            query,
            [this, connectionId, heroGeneration](
                const ProviderCatalogResponse &response) {
                if (heroGeneration != m_heroRequestGeneration) {
                    return;
                }
                emit canonicalHeroLibraryItemsLoaded(
                    connectionId,
                    m_authService->mapMediaItems(response.rawItems, connectionId));
            },
            [this, connectionId, heroGeneration](const NetworkError &error) {
                if (heroGeneration == m_heroRequestGeneration) {
                    emit canonicalHeroLibraryItemsFailed(
                        connectionId, error.userMessage);
                }
            });
        return;
    }

    auto aggregate = std::make_shared<QJsonArray>();
    auto remaining = std::make_shared<int>(ids.size());
    auto successful = std::make_shared<int>(0);
    auto failures = std::make_shared<QStringList>();
    const auto finish = [this, aggregate, remaining, successful, failures,
                         clampedLimit, connectionId, generation, heroGeneration]() {
        if (--(*remaining) > 0
            || generation != m_requestGeneration
            || heroGeneration != m_heroRequestGeneration) {
            return;
        }
        if (*successful == 0) {
            const QString message = failures->isEmpty()
                ? tr("No hero items were available.")
                : failures->constFirst();
            emit canonicalHeroLibraryItemsFailed(connectionId, message);
            return;
        }
        QJsonArray items;
        const int count = qMin(aggregate->size(), clampedLimit);
        for (int index = 0; index < count; ++index) {
            items.append(aggregate->at(index));
        }
        emit canonicalHeroLibraryItemsLoaded(
            connectionId, m_authService->mapMediaItems(items, connectionId));
    };
    for (const QString &parentId : ids) {
        ProviderCatalogQuery query = baseCatalogQuery();
        query.parentId = parentId;
        query.limit = clampedLimit;
        query.unwatchedOnly = unwatchedOnly;
        sendCatalogRequest(
            QStringLiteral("getHeroLibraryItems"),
            ProviderCatalogOperation::HeroItems,
            query,
            [aggregate, successful, clampedLimit, finish](
                const ProviderCatalogResponse &response) {
                ++(*successful);
                for (const QJsonValue &value : response.rawItems) {
                    if (aggregate->size() >= clampedLimit) {
                        break;
                    }
                    aggregate->append(value);
                }
                finish();
            },
            [failures, finish](const NetworkError &error) {
                if (!error.userMessage.isEmpty()) {
                    failures->append(error.userMessage);
                }
                finish();
            });
    }
}

void LibraryService::getHeroSeriesOverviews(const QStringList &seriesIds)
{
    const quint64 heroGeneration = ++m_heroRequestGeneration;
    const QString connectionId = activeConnectionId(m_authService);
    const quint64 generation = m_requestGeneration;
    QStringList ids;
    for (const QString &id : seriesIds) {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed)) {
            ids.append(trimmed);
        }
    }
    if (ids.isEmpty()) {
        emit canonicalHeroSeriesOverviewsLoaded(connectionId, {});
        return;
    }
    if (!m_authService || !m_authService->isAuthenticated()) {
        QJsonObject overviews;
        for (const QString &seriesId : ids) {
            overviews.insert(seriesId, QString());
        }
        emit canonicalHeroSeriesOverviewsLoaded(
            connectionId, overviews.toVariantMap());
        emitCatalogError(
            QStringLiteral("getHeroSeriesOverviews"), tr("Not authenticated"));
        return;
    }

    auto overviews = std::make_shared<QJsonObject>();
    auto remaining = std::make_shared<int>(ids.size());
    const auto finish = [this, overviews, remaining, connectionId, generation,
                         heroGeneration]() {
        if (--(*remaining) > 0
            || generation != m_requestGeneration
            || heroGeneration != m_heroRequestGeneration) {
            return;
        }
        emit canonicalHeroSeriesOverviewsLoaded(
            connectionId, overviews->toVariantMap());
    };
    for (const QString &seriesId : ids) {
        ProviderCatalogQuery query = baseCatalogQuery();
        query.itemId = seriesId;
        sendCatalogRequest(
            QStringLiteral("getHeroSeriesOverviews"),
            ProviderCatalogOperation::Item,
            query,
            [this, overviews, finish, seriesId, connectionId](
                const ProviderCatalogResponse &response) {
                const QString overview = m_authService
                    ->mapMediaItem(response.rawItem, connectionId)
                    .value(QStringLiteral("overview"))
                    .toString();
                overviews->insert(seriesId, overview);
                finish();
            },
            [overviews, finish, seriesId](const NetworkError &) {
                overviews->insert(seriesId, QString());
                finish();
            });
    }
}
QString LibraryService::getActiveConnectionId() const
{
    return activeConnectionId(m_authService);
}

QString LibraryService::getStreamUrl(const QString &itemId)
{
    return getStreamUrlWithTracks(itemId, {}, -1, -1);
}

QString LibraryService::getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId,
                                                int audioStreamIndex, int subtitleStreamIndex)
{
    const IPlaybackProvider *provider = m_authService
        ? m_authService->playbackProvider() : nullptr;
    if (!provider || itemId.isEmpty()) {
        if (!provider) {
            qCWarning(lcLibrary) << "getStreamUrlWithTracks: playback provider is unavailable;"
                                    " returning empty URL for itemId=" << itemId;
        }
        return {};
    }

    Bloom::MediaRef media{activeConnectionId(m_authService), itemId};
    const PlaybackProviderContext context{
        QUrl(m_authService->getServerUrl()),
        m_authService->getAccessToken()
    };
    const QVariantMap source{
        {QStringLiteral("id"), mediaSourceId}
    };
    const Bloom::PlaybackDescriptor descriptor = provider->createDescriptor(
        context, media, source, audioStreamIndex, subtitleStreamIndex, 0);
    return descriptor.stream.isValid() ? descriptor.stream.url.toString() : QString();
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

QString LibraryService::getCachedArtworkUrlFromRef(const QVariantMap &artworkMap,
                                                    int width)
{
    Bloom::ArtworkRef artwork = Bloom::ArtworkRef::fromVariantMap(artworkMap);
    if (!artwork.isValid()) {
        return {};
    }

    // Native Silo's fetch location is intentionally absent from the QVariantMap.
    // Recover it from the mapper-created, token-free cache key before changing
    // the requested width, then let cacheKey() register the width-specific key.
    const QString sourceCacheKey = artworkMap.value(QStringLiteral("cacheKey")).toString();
    const Bloom::ArtworkRef sourceIdentity = Bloom::ArtworkRef::fromCacheKey(sourceCacheKey);
    if (sourceIdentity.isValid() && sourceIdentity == artwork) {
        artwork.sourceUrl = Bloom::ArtworkRef::transientSourceUrlForCacheKey(sourceCacheKey);
    }

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

    Bloom::ArtworkRef artwork;
    artwork.connectionId = activeConnectionId(m_authService);
    artwork.itemId = itemId;
    artwork.kind = Bloom::ArtworkKind::Chapter;
    artwork.ownerKind = Bloom::ArtworkOwnerKind::Chapter;
    artwork.index = chapterIndex;
    artwork.tag = imageTag.trimmed();
    artwork.requestedWidth = width;
    artwork.sourceUrl = imagePath.trimmed();
    const QString cachedUrl = cachedArtworkSource(artwork);
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
