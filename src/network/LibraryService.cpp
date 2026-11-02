#include "LibraryService.h"
#include "AuthenticationService.h"
#include "HttpTransport.h"
#include "models/ContinueWatching.h"
#include "models/HomeRecommendations.h"
#include "models/LibraryKinds.h"
#include "LibrarySearchScope.h"
#include "NextEpisodeResolver.h"
#include "models/MediaModels.h"
#include "providers/IPlaybackProvider.h"
#include "utils/ConfigManager.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
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

// Window read per page when a provider has no native random or backdrop
// selection; the Silo catalog caps GET pages Bloom sends at 100.
constexpr int kRandomSampleWindow = 100;

// One recovery ping attempt; the recovery timer restarts only after an answer.
constexpr int kServerPingTimeoutMs = 10000;

// Cards per Home row Bloom reads: the Watchlist row Home adds when the
// profile's layout has none, and each of the profile's Home and library rows
// when the server accepts a cap (Silo's own rows default to 100).
constexpr int kHomeRowCardLimit = 20;
// Bloom names that row; it is not a server section.
const QString kHomeWatchlistRowId = QStringLiteral("bloom:watchlist");
// The row of favorite movies and series Home shows for providers without a
// watchlist (Jellyfin), most recently played first.
const QString kHomeFavoritesRowId = QStringLiteral("bloom:favorites");
// In-progress movies and episodes read beside Next Up.
constexpr int kResumeItemLimit = 20;

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

// The viewer's IANA zone, or empty when the system names none the server
// knows (an abbreviation, a bare offset, an unknown id).
QString viewerTimeZoneId()
{
    const QByteArray id = QTimeZone::systemTimeZoneId();
    if (id.isEmpty() || !QTimeZone::isTimeZoneIdAvailable(id)) {
        return {};
    }
    static const QRegularExpression iana(
        QStringLiteral(R"(^(?:UTC|[A-Za-z]+(?:/[A-Za-z0-9_+\-]+)+)$)"));
    const QString name = QString::fromUtf8(id);
    return iana.match(name).hasMatch() ? name : QString();
}

// The Etc zone for the viewer's current whole-hour offset. Etc/GMT names
// invert the sign: UTC-5 is Etc/GMT+5. Empty for an offset off the hour,
// which no Etc zone names.
QString offsetTimeZoneId(const QDateTime &now)
{
    const int offset = now.offsetFromUtc();
    if (offset % 3600 != 0) {
        return {};
    }
    const int hours = offset / 3600;
    if (hours == 0) {
        return QStringLiteral("Etc/UTC");
    }
    if (hours < -12 || hours > 14) {
        return {};
    }
    return QStringLiteral("Etc/GMT%1%2")
        .arg(hours > 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(qAbs(hours));
}

// Silo answers an unknown zone with a validation problem at query.timezone.
bool rejectsTimeZone(const NetworkError &error)
{
    if (error.httpStatus != 400 && error.httpStatus != 422) {
        return false;
    }
    const QJsonArray errors = QJsonDocument::fromJson(error.responseBody).object()
        .value(QStringLiteral("errors")).toArray();
    return std::any_of(errors.cbegin(), errors.cend(), [](const QJsonValue &value) {
        return value.toObject().value(QStringLiteral("location")).toString()
            == QStringLiteral("query.timezone");
    });
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
        m_pageCursors.clear();
        m_pageCursorEpochs.clear();
        m_conditionalResponses.clear();
        ++m_themeRequestGeneration;
        m_lastModified.clear();
        m_inFlightChapterRequests.clear();
        // The search capability document is profile scoped; reads in
        // flight were cancelled and their waiters belong to the old session.
        m_searchCapabilities.clear();
        // Cancelled reads answer nothing; a later page asks again.
        m_notInterestedCapabilityReads.clear();
        for (auto it = m_airingCalendarReads.cbegin(); it != m_airingCalendarReads.cend(); ++it) {
            for (const QString &seriesId : it->seriesIds) {
                emit nextAiringLoaded(seriesId, {});
            }
        }
        m_airingCalendarReads.clear();
        m_airingCalendars.clear();
        // Cancelled writes answer nothing on their own.
        failStaleHomeCardWrites();
        failStaleSubtitleAppearanceRequests();
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
    // A reply sent before the connection, profile or provider changed is
    // dropped (isCurrent), so its Home card write is answered here.
    for (const auto signal : {&AuthenticationService::providerSelectionChanged,
                              &AuthenticationService::profilesChanged,
                              &AuthenticationService::authSessionsChanged}) {
        connect(m_authService, signal, this, [this]() {
            failStaleHomeCardWrites();
            failStaleSubtitleAppearanceRequests();
        });
    }
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
    CatalogNotModifiedHandler notModifiedHandler,
    std::optional<RetryPolicy> retryPolicy,
    int attemptTimeoutMs)
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
    options.retryPolicy = retryPolicy.value_or(m_retryPolicy);
    options.retrySafety =
        providerRequest.retrySafety == ProviderCatalogRetrySafety::Idempotent
        ? RetrySafety::Idempotent
        : RetrySafety::Never;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    if (attemptTimeoutMs > 0) {
        options.attemptTimeoutMs = attemptTimeoutMs;
    }
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
                } else if (m_conditionalResponses.contains(providerRequest.relativeEndpoint)) {
                    if (responseHandler) responseHandler(m_conditionalResponses.value(providerRequest.relativeEndpoint));
                } else {
                    emitCatalogError(operationName, tr("The server returned a validator without a cached response."), failureHandler);
                }
                return;
            }

            const QByteArray body = reply->readAll();
            const QHash<QByteArray, QByteArray> headers = responseHeaders(reply);

            auto *watcher = new QFutureWatcher<ProviderCatalogResponse>(this);
            connect(watcher, &QFutureWatcher<ProviderCatalogResponse>::finished,
                    this,
                    [this, watcher, identity, operationName, operation, providerRequest,
                     responseHandler = std::move(responseHandler), failureHandler]() mutable {
                const ProviderCatalogResponse response = watcher->result();
                watcher->deleteLater();
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
                if (operation == ProviderCatalogOperation::ThemeSongsCapability) {
                    if (m_conditionalResponses.size() >= 32) m_conditionalResponses.clear();
                    m_conditionalResponses.insert(providerRequest.relativeEndpoint, response);
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

void LibraryService::sendRandomSampleRequest(
    const QString &operationName,
    ProviderCatalogOperation nativeOperation,
    const ProviderCatalogQuery &query,
    CatalogResponseHandler responseHandler,
    FailureHandler failureHandler)
{
    const CatalogRequestIdentity identity = catalogRequestIdentity();
    if (!identity.provider || identity.provider->createRequest(nativeOperation, query).supported) {
        sendCatalogRequest(operationName, nativeOperation, query,
                           std::move(responseHandler), std::move(failureHandler));
        return;
    }

    // No native random selection (Silo): count the matching movies and
    // series, read one window at a random offset, and shuffle it here.
    ProviderCatalogQuery sampleQuery = baseCatalogQuery();
    sampleQuery.parentId = query.parentId;
    sampleQuery.unwatchedOnly = query.unwatchedOnly;
    sampleQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    sampleQuery.limit = 1;
    const int limit = qMax(1, query.limit);
    sendCatalogRequest(
        operationName,
        ProviderCatalogOperation::Items,
        sampleQuery,
        [this, operationName, sampleQuery, limit, responseHandler, failureHandler](
            const ProviderCatalogResponse &countResponse) {
            const int total = qMax(countResponse.total, int(countResponse.rawItems.size()));
            if (total <= countResponse.rawItems.size()) {
                responseHandler(countResponse);
                return;
            }
            ProviderCatalogQuery windowQuery = sampleQuery;
            windowQuery.limit = kRandomSampleWindow;
            windowQuery.startIndex = total > kRandomSampleWindow
                ? int(QRandomGenerator::global()->bounded(total - kRandomSampleWindow + 1))
                : 0;
            sendCatalogRequest(
                operationName,
                ProviderCatalogOperation::Items,
                windowQuery,
                [limit, responseHandler](const ProviderCatalogResponse &windowResponse) {
                    QList<QJsonValue> items(windowResponse.rawItems.cbegin(),
                                            windowResponse.rawItems.cend());
                    std::shuffle(items.begin(), items.end(), *QRandomGenerator::global());
                    ProviderCatalogResponse sampled = windowResponse;
                    sampled.rawItems = {};
                    for (qsizetype index = 0; index < qMin<qsizetype>(limit, items.size());
                         ++index) {
                        sampled.rawItems.append(items.at(index));
                    }
                    sampled.total = int(sampled.rawItems.size());
                    sampled.hasMore = false;
                    sampled.nextCursor.clear();
                    responseHandler(sampled);
                },
                failureHandler);
        },
        failureHandler);
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
            // Only movie and series libraries exist in the app.
            const QVariantList allViews =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            m_unsupportedLibraryIds.insert(
                connectionId, LibraryKinds::unsupportedLibraryIds(allViews));
            const QVariantList items = LibraryKinds::supportedLibraries(allViews);
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

void LibraryService::getSeasons(const QString &seriesId, bool useCacheValidation)
{
    LibraryItemQuery query;
    query.parentId = seriesId;
    query.includeItemTypes = {QStringLiteral("Season")};
    query.includeHeavyFields = false;
    query.useCacheValidation = useCacheValidation;
    getItems(query);
}

void LibraryService::getSeasonEpisodes(const QString &seasonId, bool useCacheValidation)
{
    LibraryItemQuery query;
    query.parentId = seasonId;
    query.includeItemTypes = {QStringLiteral("Episode")};
    query.includeHeavyFields = false;
    query.useCacheValidation = useCacheValidation;
    getItems(query);
}

ProviderCatalogQuery LibraryService::providerItemsQuery(const LibraryItemQuery &query) const
{
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
    return providerQuery;
}

void LibraryService::getItems(const LibraryItemQuery &query)
{
    const QString parentId = query.parentId;
    const QString queryKey = query.requestKey.isEmpty() ? query.cacheKey() : query.requestKey;
    const QString connectionId = activeConnectionId(m_authService);

    ProviderCatalogQuery providerQuery = providerItemsQuery(query);
    const QString datasetKey = query.datasetKey();
    if (query.startIndex == 0 || !m_pageCursorEpochs.contains(datasetKey)) {
        if (m_pageCursorEpochs.size() >= 128 && !m_pageCursorEpochs.contains(datasetKey)) {
            m_pageCursors.clear();
            m_pageCursorEpochs.clear();
        }
        m_pageCursorEpochs.insert(datasetKey, ++m_nextCursorEpoch);
        m_pageCursors.remove(datasetKey);
    }
    const quint64 cursorEpoch = m_pageCursorEpochs.value(datasetKey);
    providerQuery.cursor = m_pageCursors.value(datasetKey).value(query.startIndex);
    providerQuery.windowCursor = m_pageCursors.value(datasetKey).value(0);

    sendCatalogRequest(
        QStringLiteral("getItems"),
        ProviderCatalogOperation::Items,
        providerQuery,
        [this, parentId, queryKey, connectionId, query, datasetKey, cursorEpoch](
            const ProviderCatalogResponse &response) {
            const QVariantList items =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            if (m_pageCursorEpochs.value(datasetKey) == cursorEpoch) {
                auto &cursors = m_pageCursors[datasetKey];
                if (cursors.size() >= 256) cursors.clear();
                if (!response.windowCursor.isEmpty()) cursors.insert(0, response.windowCursor);
                if (!response.nextCursor.isEmpty()) cursors.insert(query.startIndex + items.size(), response.nextCursor);
            }
            const int total = response.capabilityMetadata.value(QStringLiteral("hasMorePresent")).toBool()
                ? (response.hasMore ? qMax(response.total, query.startIndex + int(items.size()) + 1)
                                    : query.startIndex + int(items.size()))
                : response.total;
            emit canonicalItemsLoadedForConnection(
                connectionId, parentId, queryKey, items, total);
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

void LibraryService::applyNextUpOptions(ProviderCatalogQuery &query,
                                        const ConfigManager *config,
                                        const QDate &today)
{
    if (!config) {
        return;
    }
    query.nextUpRewatching = config->getNextUpRewatching();
    const int cutoffDays = config->getNextUpCutoffDays();
    query.nextUpCutoff = cutoffDays > 0 && today.isValid() ? today.addDays(-cutoffDays) : QDate();
}

bool LibraryService::supportsNextUpOptions() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::NextUpOptions);
}

void LibraryService::getNextUp()
{
    const QString connectionId = activeConnectionId(m_authService);
    const quint64 generation = ++m_nextUpGeneration;
    ProviderCatalogQuery nextUpQuery = baseCatalogQuery();
    applyNextUpOptions(nextUpQuery,
                       m_authService ? m_authService->configManager() : nullptr,
                       QDate::currentDate());
    const bool readResume = m_authService
        && m_authService->supportsCapability(ProviderCapability::ResumeItems);
    if (!readResume) {
        sendCatalogRequest(
            QStringLiteral("getNextUp"),
            ProviderCatalogOperation::NextUp,
            nextUpQuery,
            [this, connectionId, generation](const ProviderCatalogResponse &response) {
                if (generation != m_nextUpGeneration) {
                    return;
                }
                // One list from the server (Silo's Home layout), kept in
                // its order.
                emit canonicalNextUpLoaded(
                    connectionId,
                    m_authService->mapMediaItems(response.rawItems, connectionId));
            });
        return;
    }

    // Next Up and the resume list load side by side. Without Next Up the
    // rows keep what they show; a failed resume list leaves Next Up alone.
    struct Pending {
        std::optional<QVariantList> nextUp;
        std::optional<QVariantList> resume;
        bool nextUpFailed = false;
        int outstanding = 2;
    };
    auto pending = std::make_shared<Pending>();
    const auto finish = [this, connectionId, generation, pending]() {
        if (--pending->outstanding > 0 || pending->nextUpFailed
            || generation != m_nextUpGeneration) {
            return;
        }
        emit canonicalNextUpLoaded(
            connectionId,
            ContinueWatching::combine(pending->resume.value_or(QVariantList()),
                                      pending->nextUp.value_or(QVariantList())));
    };
    sendCatalogRequest(
        QStringLiteral("getNextUp"),
        ProviderCatalogOperation::NextUp,
        nextUpQuery,
        [this, connectionId, pending, finish](const ProviderCatalogResponse &response) {
            pending->nextUp = m_authService->mapMediaItems(response.rawItems, connectionId);
            finish();
        },
        [pending, finish](const NetworkError &) {
            pending->nextUpFailed = true;
            finish();
        });
    ProviderCatalogQuery resumeQuery = baseCatalogQuery();
    resumeQuery.limit = kResumeItemLimit;
    sendCatalogRequest(
        QStringLiteral("getResumeItems"),
        ProviderCatalogOperation::ResumeItems,
        resumeQuery,
        [this, connectionId, pending, finish](const ProviderCatalogResponse &response) {
            pending->resume = m_authService->mapMediaItems(response.rawItems, connectionId);
            finish();
        },
        [finish](const NetworkError &) {
            finish();
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
        sendRandomSampleRequest(
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
        // Without a native backdrop index (Silo), page movies and series
        // through the plain catalog instead.
        const CatalogRequestIdentity identity = catalogRequestIdentity();
        const bool native = !identity.provider
            || identity.provider->createRequest(
                   ProviderCatalogOperation::HomeBackdrops, query).supported;
        if (!native) {
            query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
            query.limit = kRandomSampleWindow;
        }
        sendCatalogRequest(
            QStringLiteral("getHomeBackdropItems"),
            native ? ProviderCatalogOperation::HomeBackdrops
                   : ProviderCatalogOperation::Items,
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
    sendRandomSampleRequest(
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

void LibraryService::getItemExtras(const QString &itemId)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (itemId.isEmpty()) {
        emit canonicalItemExtrasFailed(connectionId, itemId, tr("Item ID is empty"));
        return;
    }
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;

    // Local trailers lead, then the special features in server order.
    QList<ProviderCatalogOperation> operations;
    const ICatalogProvider *provider = catalogRequestIdentity().provider;
    for (const ProviderCatalogOperation operation :
         {ProviderCatalogOperation::LocalTrailers, ProviderCatalogOperation::SpecialFeatures}) {
        if (provider && provider->createRequest(operation, query).supported) {
            operations.append(operation);
        }
    }
    if (operations.isEmpty()) {
        emit canonicalItemExtrasLoaded(connectionId, itemId, {}, true);
        return;
    }

    struct State {
        QList<QVariantList> results;
        int pending = 0;
        int failures = 0;
        QString error;
    };
    auto state = std::make_shared<State>();
    state->results.resize(operations.size());
    state->pending = static_cast<int>(operations.size());
    const auto finish = [this, connectionId, itemId, state]() {
        if (--state->pending > 0) {
            return;
        }
        if (state->failures == state->results.size()) {
            emit canonicalItemExtrasFailed(connectionId, itemId, state->error);
            return;
        }
        QVariantList extras;
        QSet<QString> seen;
        for (const QVariantList &result : std::as_const(state->results)) {
            for (const QVariant &extra : result) {
                const QString extraId = extra.toMap().value(QStringLiteral("itemId")).toString();
                if (!seen.contains(extraId)) {
                    seen.insert(extraId);
                    extras.append(extra);
                }
            }
        }
        // A partial answer still shows what arrived, flagged so it is not
        // cached in place of the full list.
        emit canonicalItemExtrasLoaded(connectionId, itemId, extras, state->failures == 0);
    };
    for (qsizetype index = 0; index < operations.size(); ++index) {
        sendCatalogRequest(
            QStringLiteral("getItemExtras"),
            operations.at(index),
            query,
            [this, connectionId, itemId, state, index, finish](
                const ProviderCatalogResponse &response) {
                state->results[index] =
                    m_authService->mapExtras(response.rawItems, connectionId, itemId);
                finish();
            },
            [state, finish](const NetworkError &error) {
                ++state->failures;
                state->error = error.userMessage;
                finish();
            });
    }
}

void LibraryService::getNextUnplayedEpisode(const QString &seriesId,
                                            const QString &excludeItemId,
                                            const QString &requestContext)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.seriesId = seriesId;
    query.excludeItemId = excludeItemId;
    const CatalogRequestIdentity identity = catalogRequestIdentity();
    if (identity.provider
        && !identity.provider->createRequest(
               ProviderCatalogOperation::NextUnplayedEpisode, query).supported) {
        getNextUnplayedEpisodeFromHierarchy(seriesId, excludeItemId, requestContext);
        return;
    }
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

void LibraryService::getNextUnplayedEpisodeFromHierarchy(const QString &seriesId,
                                                         const QString &excludeItemId,
                                                         const QString &requestContext)
{
    // Providers without a series-wide episode route (Silo) list seasons, then
    // each season's episodes, and resolve over the whole series so specials
    // placement and the excluded current episode behave as with Jellyfin.
    const QString operationName = QStringLiteral("getNextUnplayedEpisode");
    const QString connectionId = activeConnectionId(m_authService);
    struct State {
        QVariantList episodes;
        int pending = 0;
        bool failed = false;
    };
    auto state = std::make_shared<State>();
    const auto fail = [this, seriesId, requestContext, state](const NetworkError &error) {
        if (state->failed) {
            return;
        }
        state->failed = true;
        emit nextUnplayedEpisodeFailed(seriesId, error.userMessage, requestContext);
    };

    ProviderCatalogQuery seasonsQuery = baseCatalogQuery();
    seasonsQuery.parentId = seriesId;
    seasonsQuery.includeItemTypes = {QStringLiteral("Season")};
    sendCatalogRequest(
        operationName,
        ProviderCatalogOperation::Items,
        seasonsQuery,
        [this, operationName, connectionId, seriesId, excludeItemId, requestContext,
         state, fail](const ProviderCatalogResponse &response) {
            QStringList seasonIds;
            for (const QVariant &season :
                 m_authService->mapMediaItems(response.rawItems, connectionId)) {
                const QString seasonId = season.toMap().value(QStringLiteral("itemId")).toString();
                if (!seasonId.isEmpty() && !seasonIds.contains(seasonId)) {
                    seasonIds.append(seasonId);
                }
            }
            if (seasonIds.isEmpty()) {
                emit canonicalNextUnplayedEpisodeLoaded(
                    connectionId, seriesId, QVariantMap(), requestContext);
                return;
            }
            state->pending = seasonIds.size();
            for (const QString &seasonId : std::as_const(seasonIds)) {
                ProviderCatalogQuery episodesQuery = baseCatalogQuery();
                episodesQuery.parentId = seasonId;
                episodesQuery.includeItemTypes = {QStringLiteral("Episode")};
                sendCatalogRequest(
                    operationName,
                    ProviderCatalogOperation::Items,
                    episodesQuery,
                    [this, connectionId, seriesId, excludeItemId, requestContext, state](
                        const ProviderCatalogResponse &episodesResponse) {
                        if (state->failed) {
                            return;
                        }
                        state->episodes.append(
                            m_authService->mapMediaItems(episodesResponse.rawItems, connectionId));
                        if (--state->pending > 0) {
                            return;
                        }
                        emit canonicalNextUnplayedEpisodeLoaded(
                            connectionId, seriesId,
                            NextEpisodeResolver::resolveBestNextEpisode(
                                state->episodes, excludeItemId),
                            requestContext);
                    },
                    fail);
            }
        },
        fail);
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
        },
        [this, itemId](const NetworkError &error) {
            emit itemPlayedStatusFailed(itemId, true, error.userMessage);
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
        },
        [this, itemId](const NetworkError &error) {
            emit itemPlayedStatusFailed(itemId, false, error.userMessage);
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
        },
        [this, itemId](const NetworkError &error) {
            emit favoriteStatusFailed(itemId, true, error.userMessage);
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
        },
        [this, itemId](const NetworkError &error) {
            emit favoriteStatusFailed(itemId, false, error.userMessage);
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

bool LibraryService::supportsWatchlist() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::Watchlist);
}

void LibraryService::setItemWatchlisted(const QString &itemId, bool inWatchlist)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = inWatchlist;
    sendCatalogRequest(
        QStringLiteral("setItemWatchlisted"),
        ProviderCatalogOperation::SetWatchlist,
        query,
        [this, itemId, inWatchlist](const ProviderCatalogResponse &) {
            emit watchlistStatusChanged(itemId, inWatchlist);
        },
        [this, itemId, inWatchlist](const NetworkError &error) {
            emit watchlistStatusFailed(itemId, inWatchlist, error.userMessage);
        });
}

bool LibraryService::supportsNotInterested() const
{
    if (!m_authService || !m_authService->isAuthenticated()
        || !m_authService->supportsCapability(ProviderCapability::NotInterested)) {
        return false;
    }
    return m_notInterestedAllowed.value(
        activeConnectionId(m_authService) + QLatin1Char('\n') + activeProfileId(m_authService));
}

void LibraryService::loadNotInterestedCapability()
{
    if (!m_authService || !m_authService->isAuthenticated()
        || !m_authService->supportsCapability(ProviderCapability::NotInterested)) {
        return;
    }
    // The document is profile scoped: each profile may be allowed or not.
    const QString key =
        activeConnectionId(m_authService) + QLatin1Char('\n') + activeProfileId(m_authService);
    if (m_notInterestedAllowed.contains(key)) {
        return;
    }
    // A read sent before the session last changed was cancelled with it.
    const auto inFlight = m_notInterestedCapabilityReads.constFind(key);
    if (inFlight != m_notInterestedCapabilityReads.cend() && *inFlight == m_requestGeneration) {
        return;
    }
    const quint64 generation = m_requestGeneration;
    m_notInterestedCapabilityReads.insert(key, generation);
    const auto finish = [this, key, generation]() {
        const auto read = m_notInterestedCapabilityReads.constFind(key);
        if (read == m_notInterestedCapabilityReads.cend() || *read != generation) {
            return false;
        }
        m_notInterestedCapabilityReads.erase(read);
        return true;
    };
    sendCatalogRequest(
        QStringLiteral("getNotInterestedCapability"),
        ProviderCatalogOperation::NotInterestedCapability,
        baseCatalogQuery(),
        [this, key, finish](const ProviderCatalogResponse &response) {
            if (!finish()) {
                return;
            }
            m_notInterestedAllowed.insert(
                key, response.capabilityMetadata.value(QStringLiteral("allowed")).toBool());
            emit notInterestedSupportChanged();
        },
        [this, key, finish](const NetworkError &error) {
            if (!finish()) {
                return;
            }
            // A server without the route never offers it. Other failures
            // leave the action hidden and ask again on the next page.
            if (error.httpStatus == 404 || error.httpStatus == 405 || error.httpStatus == 501) {
                m_notInterestedAllowed.insert(key, false);
            }
        });
}

void LibraryService::setItemNotInterested(const QString &itemId, bool notInterested)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = notInterested;
    sendCatalogRequest(
        QStringLiteral("setItemNotInterested"),
        ProviderCatalogOperation::SetNotInterested,
        query,
        [this, itemId, notInterested](const ProviderCatalogResponse &) {
            emit notInterestedStatusChanged(itemId, notInterested);
        },
        [this, itemId, notInterested](const NetworkError &error) {
            emit notInterestedStatusFailed(itemId, notInterested, error.userMessage);
        });
}

bool LibraryService::supportsNextAiring() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::AiringCalendar);
}

void LibraryService::getNextAiring(const QString &seriesId)
{
    if (seriesId.isEmpty() || !supportsNextAiring()) {
        emit nextAiringLoaded(seriesId, {});
        return;
    }
    // The calendar's days are the viewer's own, so the window starts today
    // in the system zone and the server reckons days in the same zone. A
    // system zone the server would not know stands in as its whole-hour
    // offset.
    const QDate today = QDate::currentDate();
    QString timeZoneId = viewerTimeZoneId();
    const bool zoneFallbackAllowed = !timeZoneId.isEmpty();
    if (timeZoneId.isEmpty()) {
        timeZoneId = offsetTimeZoneId(QDateTime::currentDateTime());
    }
    const QString key = QStringList{activeConnectionId(m_authService),
                                    activeProfileId(m_authService),
                                    today.toString(Qt::ISODate), timeZoneId}
                            .join(QLatin1Char('\n'));

    const auto cached = m_airingCalendars.constFind(key);
    if (cached != m_airingCalendars.cend()
        && cached->fetchedAt.msecsTo(QDateTime::currentDateTimeUtc()) < kAiringCalendarTtlMs) {
        emit nextAiringLoaded(seriesId,
                              nextAiringFor(cached->airings, seriesId, QDateTime::currentDateTime()));
        return;
    }
    // Pages asking while the calendar is read wait for the same answer.
    auto inFlight = m_airingCalendarReads.find(key);
    if (inFlight != m_airingCalendarReads.end() && inFlight->generation == m_requestGeneration) {
        if (!inFlight->seriesIds.contains(seriesId)) {
            inFlight->seriesIds.append(seriesId);
        }
        return;
    }
    m_airingCalendarReads.insert(key, {m_requestGeneration, {seriesId}});
    readAiringCalendar(key, today, timeZoneId, zoneFallbackAllowed);
}

void LibraryService::readAiringCalendar(const QString &key,
                                        const QDate &start,
                                        const QString &timeZoneId,
                                        bool zoneFallbackAllowed)
{
    const quint64 generation = m_requestGeneration;
    ProviderCatalogQuery query = baseCatalogQuery();
    query.calendarStart = start;
    query.timeZoneId = timeZoneId;
    sendCatalogRequest(
        QStringLiteral("getNextAiring"),
        ProviderCatalogOperation::UpcomingAirings,
        query,
        [this, key, generation](const ProviderCatalogResponse &response) {
            finishAiringCalendarRead(
                key, generation,
                response.capabilityMetadata.value(QStringLiteral("airings")).toList(), true);
        },
        [this, key, generation, start, zoneFallbackAllowed](const NetworkError &error) {
            // A zone the server does not know is retried once as the
            // viewer's whole-hour offset.
            if (zoneFallbackAllowed && rejectsTimeZone(error)) {
                const QString fallback = offsetTimeZoneId(QDateTime::currentDateTime());
                const auto read = m_airingCalendarReads.constFind(key);
                if (!fallback.isEmpty() && read != m_airingCalendarReads.cend()
                    && read->generation == generation) {
                    readAiringCalendar(key, start, fallback, false);
                    return;
                }
            }
            // No chip, and nothing else on the page depends on it.
            finishAiringCalendarRead(key, generation, {}, false);
        });
}

void LibraryService::finishAiringCalendarRead(const QString &key,
                                              quint64 generation,
                                              const QVariantList &airings,
                                              bool cache)
{
    const auto read = m_airingCalendarReads.constFind(key);
    if (read == m_airingCalendarReads.cend() || read->generation != generation) {
        return;
    }
    const QStringList seriesIds = read->seriesIds;
    m_airingCalendarReads.erase(read);
    if (cache) {
        // A handful of keys at most (one per profile, day and zone).
        if (m_airingCalendars.size() >= 8) {
            m_airingCalendars.clear();
        }
        m_airingCalendars.insert(key, {airings, QDateTime::currentDateTimeUtc()});
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &seriesId : seriesIds) {
        emit nextAiringLoaded(seriesId, nextAiringFor(airings, seriesId, now));
    }
}

QVariantMap LibraryService::nextAiringFor(const QVariantList &airings,
                                          const QString &seriesId,
                                          const QDateTime &now)
{
    const QDate today = now.toLocalTime().date();
    QVariantMap best;
    QDateTime bestAt;
    for (const QVariant &value : airings) {
        const QVariantMap airing = value.toMap();
        if (airing.value(QStringLiteral("seriesId")).toString() != seriesId) {
            continue;
        }
        const QDate day = QDate::fromString(airing.value(QStringLiteral("airDate")).toString(),
                                            Qt::ISODate);
        const QDateTime instant = QDateTime::fromString(
            airing.value(QStringLiteral("airAt")).toString(), Qt::ISODate);
        // Without an instant an airing counts until its local day is over.
        QDateTime at;
        if (instant.isValid()) {
            if (instant <= now) {
                continue;
            }
            at = instant;
        } else {
            if (!day.isValid() || day < today) {
                continue;
            }
            at = day.startOfDay();
        }
        if (best.isEmpty() || at < bestAt) {
            best = airing;
            bestAt = at;
        }
    }
    return best;
}

HomeCardCapabilities LibraryService::homeCardCapabilities(const QString &connectionId) const
{
    HomeCardCapabilities capabilities;
    const IProviderAdapter *adapter =
        m_authService ? m_authService->providerForConnection(connectionId) : nullptr;
    if (!adapter) {
        return capabilities;
    }
    capabilities.dismissals = adapter->supportsCapability(ProviderCapability::HomeDismissals);
    capabilities.resumeReset = adapter->supportsCapability(ProviderCapability::ResumeItems)
        && !m_resumeResetMissing.contains(connectionId);
    capabilities.watchlist = adapter->supportsCapability(ProviderCapability::Watchlist);
    capabilities.notInterested =
        connectionId == activeConnectionId(m_authService) && supportsNotInterested();
    return capabilities;
}

void LibraryService::sendHomeCardWrite(const QString &requestKey,
                                       const QString &operationName,
                                       ProviderCatalogOperation operation,
                                       const ProviderCatalogQuery &query,
                                       std::function<void()> onSuccess,
                                       std::function<void(const NetworkError &)> onFailure)
{
    const CatalogRequestIdentity identity = catalogRequestIdentity();
    m_homeCardWrites.insert(requestKey, identity);
    // False once the write was answered (or failed as stale).
    const auto take = [this, requestKey]() { return m_homeCardWrites.remove(requestKey) > 0; };
    sendCatalogRequest(
        operationName,
        operation,
        query,
        [this, identity, requestKey, take, onSuccess](const ProviderCatalogResponse &) {
            if (!take()) {
                return;
            }
            if (onSuccess) {
                onSuccess();
            }
            emit homeCardWriteFinished(identity.connectionId, requestKey, true, QString());
        },
        [this, identity, requestKey, take, onFailure](const NetworkError &error) {
            if (!take()) {
                return;
            }
            if (onFailure) {
                onFailure(error);
            }
            emit homeCardWriteFinished(identity.connectionId, requestKey, false,
                                       error.userMessage);
        });
}

void LibraryService::failStaleHomeCardWrites()
{
    QList<std::pair<QString, QString>> stale;
    for (auto it = m_homeCardWrites.begin(); it != m_homeCardWrites.end();) {
        if (isCurrent(it.value())) {
            ++it;
            continue;
        }
        stale.append({it.key(), it->connectionId});
        it = m_homeCardWrites.erase(it);
    }
    for (const auto &[requestKey, connectionId] : std::as_const(stale)) {
        emit homeCardWriteFinished(connectionId, requestKey, false,
                                   tr("The connection changed before the server answered."));
    }
}

void LibraryService::setHomeItemDismissed(const QString &requestKey,
                                          const QString &surface,
                                          const QString &itemId,
                                          const QString &seriesId,
                                          const QString &progressUpdatedAt,
                                          bool dismissed)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.seriesId = seriesId;
    query.homeSurface = surface;
    query.progressUpdatedAt = progressUpdatedAt;
    query.stateValue = dismissed;
    sendHomeCardWrite(
        requestKey,
        dismissed ? QStringLiteral("dismissHomeItem") : QStringLiteral("undismissHomeItem"),
        ProviderCatalogOperation::SetHomeDismissal,
        query, {}, {});
}

QString LibraryService::subtitleAppearanceScope() const
{
    if (!m_authService || !m_authService->isAuthenticated()
        || !m_authService->supportsCapability(ProviderCapability::SubtitleAppearance)) {
        return {};
    }
    return activeConnectionId(m_authService) + QLatin1Char('\n') + activeProfileId(m_authService);
}

bool LibraryService::hasSubtitleAppearanceDocument(const QString &scope) const
{
    return !scope.isEmpty() && m_subtitleAppearanceDocuments.contains(scope);
}

void LibraryService::getSubtitleAppearance(SubtitleAppearanceHandler handler)
{
    sendSubtitleAppearanceRequest(
        QStringLiteral("getSubtitleAppearance"),
        ProviderCatalogOperation::SubtitleAppearance,
        baseCatalogQuery(),
        std::move(handler));
}

void LibraryService::setSubtitleAppearance(const QVariantMap &appearance,
                                           SubtitleAppearanceHandler handler)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.stateValue = true;
    query.subtitleAppearance = appearance;
    query.subtitleAppearanceDocument =
        m_subtitleAppearanceDocuments.value(subtitleAppearanceScope());
    sendSubtitleAppearanceRequest(
        QStringLiteral("setSubtitleAppearance"),
        ProviderCatalogOperation::SetSubtitleAppearance,
        query,
        std::move(handler));
}

void LibraryService::resetSubtitleAppearance(SubtitleAppearanceHandler handler)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.stateValue = false;
    sendSubtitleAppearanceRequest(
        QStringLiteral("resetSubtitleAppearance"),
        ProviderCatalogOperation::SetSubtitleAppearance,
        query,
        std::move(handler));
}

void LibraryService::sendSubtitleAppearanceRequest(const QString &operationName,
                                                   ProviderCatalogOperation operation,
                                                   const ProviderCatalogQuery &query,
                                                   SubtitleAppearanceHandler handler)
{
    const quint64 id = ++m_nextSubtitleAppearanceRequest;
    const QString scope = subtitleAppearanceScope();
    m_subtitleAppearanceRequests.insert(id, {catalogRequestIdentity(), std::move(handler)});
    // The handler, once: null when already answered (or failed as stale).
    const auto take = [this, id]() {
        return m_subtitleAppearanceRequests.take(id).handler;
    };
    const bool removal = operation == ProviderCatalogOperation::SetSubtitleAppearance
        && !query.stateValue;
    sendCatalogRequest(
        operationName,
        operation,
        query,
        [this, take, scope, removal](const ProviderCatalogResponse &response) {
            const SubtitleAppearanceHandler handler = take();
            if (!handler) {
                return;
            }
            QVariantMap result = response.capabilityMetadata;
            const QString document = result.take(QStringLiteral("document")).toString();
            if (removal) {
                // The next read brings the profile-wide document.
                m_subtitleAppearanceDocuments.remove(scope);
            } else if (!scope.isEmpty()) {
                m_subtitleAppearanceDocuments.insert(scope, document);
            }
            handler(true, result);
        },
        [take](const NetworkError &) {
            if (const SubtitleAppearanceHandler handler = take()) {
                handler(false, {});
            }
        });
}

void LibraryService::failStaleSubtitleAppearanceRequests()
{
    QList<SubtitleAppearanceHandler> stale;
    for (auto it = m_subtitleAppearanceRequests.begin(); it != m_subtitleAppearanceRequests.end();) {
        if (isCurrent(it->identity)) {
            ++it;
            continue;
        }
        stale.append(it->handler);
        it = m_subtitleAppearanceRequests.erase(it);
    }
    for (const SubtitleAppearanceHandler &handler : std::as_const(stale)) {
        handler(false, {{QStringLiteral("stale"), true}});
    }
}

void LibraryService::clearResumePosition(const QString &requestKey, const QString &itemId)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    sendHomeCardWrite(
        requestKey,
        QStringLiteral("clearResumePosition"),
        ProviderCatalogOperation::ClearResumePosition,
        query,
        {},
        [this, connectionId](const NetworkError &error) {
            // Silo's compatibility listener has no POST UserData route.
            if (error.httpStatus == 404 || error.httpStatus == 405
                || error.httpStatus == 501) {
                m_resumeResetMissing.insert(connectionId);
            }
        });
}

void LibraryService::setHomeItemState(const QString &requestKey,
                                      const QString &field,
                                      const QString &itemId,
                                      bool value)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = itemId;
    query.stateValue = value;
    if (field == QStringLiteral("watched")) {
        sendHomeCardWrite(
            requestKey,
            value ? QStringLiteral("markItemPlayed") : QStringLiteral("markItemUnplayed"),
            ProviderCatalogOperation::SetWatched, query,
            [this, itemId, value]() { emit itemPlayedStatusChanged(itemId, value); },
            [this, itemId, value](const NetworkError &error) {
                emit itemPlayedStatusFailed(itemId, value, error.userMessage);
            });
    } else if (field == QStringLiteral("favorite")) {
        sendHomeCardWrite(
            requestKey,
            value ? QStringLiteral("markItemFavorite") : QStringLiteral("markItemUnfavorite"),
            ProviderCatalogOperation::SetFavorite, query,
            [this, itemId, value]() { emit favoriteStatusChanged(itemId, value); },
            [this, itemId, value](const NetworkError &error) {
                emit favoriteStatusFailed(itemId, value, error.userMessage);
            });
    } else if (field == QStringLiteral("inWatchlist")) {
        sendHomeCardWrite(
            requestKey, QStringLiteral("setItemWatchlisted"),
            ProviderCatalogOperation::SetWatchlist, query,
            [this, itemId, value]() { emit watchlistStatusChanged(itemId, value); },
            [this, itemId, value](const NetworkError &error) {
                emit watchlistStatusFailed(itemId, value, error.userMessage);
            });
    } else if (field == QStringLiteral("notInterested")) {
        sendHomeCardWrite(
            requestKey, QStringLiteral("setItemNotInterested"),
            ProviderCatalogOperation::SetNotInterested, query,
            [this, itemId, value]() { emit notInterestedStatusChanged(itemId, value); },
            [this, itemId, value](const NetworkError &error) {
                emit notInterestedStatusFailed(itemId, value, error.userMessage);
            });
    } else {
        emit homeCardWriteFinished(activeConnectionId(m_authService), requestKey, false,
                                   tr("Unknown card state."));
    }
}

// ============================================================================
// Server-side media requests
// ============================================================================

bool LibraryService::supportsMediaRequests() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::MediaRequests);
}

void LibraryService::sendMediaRequestOperation(ProviderCatalogOperation operation,
                                               const ProviderCatalogQuery &query,
                                               MediaRequestHandler onLoaded,
                                               MediaRequestFailureHandler onFailed)
{
    QString operationName;
    switch (operation) {
    case ProviderCatalogOperation::MediaRequestStatus:
        operationName = QStringLiteral("getMediaRequestStatus");
        break;
    case ProviderCatalogOperation::MediaRequestSearch:
        operationName = QStringLiteral("searchMediaRequests");
        break;
    case ProviderCatalogOperation::MediaRequestDetail:
        operationName = QStringLiteral("getMediaRequestDetail");
        break;
    case ProviderCatalogOperation::CreateMediaRequest:
        operationName = QStringLiteral("createMediaRequest");
        break;
    case ProviderCatalogOperation::MediaRequestDiscover:
        operationName = QStringLiteral("getMediaRequestDiscover");
        break;
    case ProviderCatalogOperation::MediaRequestDiscoverSection:
        operationName = QStringLiteral("getMediaRequestDiscoverSection");
        break;
    case ProviderCatalogOperation::MediaRequestList:
        operationName = QStringLiteral("getMediaRequestList");
        break;
    case ProviderCatalogOperation::CancelMediaRequest:
        operationName = QStringLiteral("cancelMediaRequest");
        break;
    case ProviderCatalogOperation::SetMediaRequestFollow:
        operationName = QStringLiteral("setMediaRequestFollow");
        break;
    case ProviderCatalogOperation::SetWatchlistTitle:
        operationName = QStringLiteral("setWatchlistTitle");
        break;
    case ProviderCatalogOperation::MediaRequestBrowseCategories:
        operationName = QStringLiteral("getMediaRequestBrowseCategories");
        break;
    case ProviderCatalogOperation::MediaRequestBrowse:
        operationName = QStringLiteral("getMediaRequestBrowse");
        break;
    default:
        if (onFailed) {
            onFailed(tr("Not a media request operation."));
        }
        return;
    }
    if (!supportsMediaRequests()) {
        if (onFailed) {
            onFailed(tr("This server does not take media requests."));
        }
        return;
    }

    ProviderCatalogQuery requestQuery = query;
    requestQuery.userId = baseCatalogQuery().userId;
    sendCatalogRequest(
        operationName,
        operation,
        requestQuery,
        [onLoaded = std::move(onLoaded)](const ProviderCatalogResponse &response) {
            if (onLoaded) {
                onLoaded(response.capabilityMetadata);
            }
        },
        [onFailed = std::move(onFailed)](const NetworkError &error) {
            if (onFailed) {
                onFailed(error.userMessage);
            }
        });
}

// ============================================================================
// Profile-curated Home rows
// ============================================================================

void LibraryService::getHomeSections(const QStringList &movieLibraryIds,
                                     bool reloadRecommendations)
{
    const HomeRowsRequest request{movieLibraryIds, reloadRecommendations};
    const QString connectionId = activeConnectionId(m_authService);
    const quint64 generation = ++m_homeSectionsGeneration;
    if (!m_authService || !m_authService->isAuthenticated()) {
        emit canonicalHomeSectionsLoaded(connectionId, {});
        return;
    }
    if (!m_authService->supportsCapability(ProviderCapability::HomeSections)) {
        // Providers without a curated layout (Jellyfin) add no rows besides
        // Bloom's Favorites and recommendation rows.
        publishHomeSectionsWithBloomRows(connectionId, generation, {}, request);
        return;
    }

    // Silo caps each row's cards only when its Home capabilities report the
    // largest cap it accepts; older servers reject item_limit. Next Up reads
    // the layout separately and uncapped, so no resume card is dropped.
    withSectionItemLimit(
        connectionId,
        [this, connectionId, generation] {
            return generation == m_homeSectionsGeneration
                && activeConnectionId(m_authService) == connectionId;
        },
        [this, connectionId, generation, request](int itemLimitMax) {
            readHomeSections(connectionId, generation, itemLimitMax, request);
        });
}

void LibraryService::withSectionItemLimit(const QString &connectionId,
                                          std::function<bool()> isCurrent,
                                          std::function<void(int)> proceed)
{
    // The Home capabilities are read once per connection; the cap applies to
    // library section reads too.
    const auto itemLimitMax = m_homeSectionItemLimitMax.constFind(connectionId);
    if (itemLimitMax != m_homeSectionItemLimitMax.cend()) {
        proceed(*itemLimitMax);
        return;
    }
    sendCatalogRequest(
        QStringLiteral("getHomeCapabilities"),
        ProviderCatalogOperation::HomeCapabilities,
        baseCatalogQuery(),
        [this, connectionId, isCurrent, proceed](const ProviderCatalogResponse &capabilities) {
            const int limitMax = capabilities.capabilityMetadata
                .value(QStringLiteral("sectionItemLimitMax")).toInt();
            m_homeSectionItemLimitMax.insert(connectionId, limitMax);
            if (isCurrent()) {
                proceed(limitMax);
            }
        },
        [this, connectionId, isCurrent, proceed](const NetworkError &error) {
            // A server without the route has no cap, which is remembered so
            // each load does not pay the read again. Other failures read the
            // rows uncapped and ask again next time.
            if (error.httpStatus == 404 || error.httpStatus == 405 || error.httpStatus == 501) {
                m_homeSectionItemLimitMax.insert(connectionId, 0);
            }
            if (isCurrent()) {
                proceed(0);
            }
        });
}

void LibraryService::readHomeSections(const QString &connectionId,
                                      quint64 generation,
                                      int itemLimitMax,
                                      const HomeRowsRequest &request)
{
    const auto mapRows = [this, connectionId](const QJsonArray &wireRows) {
        QVariantList rows;
        for (const QJsonValue &value : wireRows) {
            const QJsonObject row = value.toObject();
            const QVariantList items = m_authService->mapMediaItems(
                row.value(QStringLiteral("items")).toArray(), connectionId);
            if (items.isEmpty()) {
                continue;
            }
            rows.append(QVariantMap{
                {QStringLiteral("sectionId"), row.value(QStringLiteral("id")).toString()},
                {QStringLiteral("title"), row.value(QStringLiteral("title")).toString()},
                {QStringLiteral("items"), items}});
        }
        return rows;
    };

    ProviderCatalogQuery layoutQuery = baseCatalogQuery();
    if (itemLimitMax > 0) {
        layoutQuery.limit = qMin(kHomeRowCardLimit, itemLimitMax);
    }

    sendCatalogRequest(
        QStringLiteral("getHomeSections"),
        ProviderCatalogOperation::HomeSections,
        layoutQuery,
        [this, connectionId, generation, mapRows, request](
            const ProviderCatalogResponse &response) {
            if (generation != m_homeSectionsGeneration) {
                return;
            }
            const QVariantList rows = mapRows(response.rawItems);
            if (!m_authService->supportsCapability(ProviderCapability::Watchlist)) {
                publishHomeSectionsWithBloomRows(connectionId, generation, rows, request);
                return;
            }
            const bool layoutHasWatchlist = response.capabilityMetadata
                .value(QStringLiteral("layoutHasWatchlist")).toBool();
            if (layoutHasWatchlist) {
                emit canonicalHomeSectionsLoaded(connectionId, rows);
                return;
            }

            // The layout has no Watchlist row: Home adds one ahead of the
            // profile's rows.
            ProviderCatalogQuery watchlistQuery = baseCatalogQuery();
            watchlistQuery.limit = kHomeRowCardLimit;
            sendCatalogRequest(
                QStringLiteral("getWatchlist"),
                ProviderCatalogOperation::Watchlist,
                watchlistQuery,
                [this, connectionId, generation, rows](const ProviderCatalogResponse &watchlist) {
                    if (generation != m_homeSectionsGeneration) {
                        return;
                    }
                    QVariantList withWatchlist = rows;
                    const QVariantList items
                        = m_authService->mapMediaItems(watchlist.rawItems, connectionId);
                    if (!items.isEmpty()) {
                        withWatchlist.prepend(QVariantMap{
                            {QStringLiteral("sectionId"), kHomeWatchlistRowId},
                            {QStringLiteral("title"), tr("Watchlist")},
                            {QStringLiteral("items"), items}});
                    }
                    emit canonicalHomeSectionsLoaded(connectionId, withWatchlist);
                },
                [this, connectionId, generation, rows](const NetworkError &) {
                    // The profile's rows still show without the watchlist.
                    if (generation == m_homeSectionsGeneration) {
                        emit canonicalHomeSectionsLoaded(connectionId, rows);
                    }
                });
        },
        [this, connectionId, generation, itemLimitMax, request](const NetworkError &error) {
            if (generation != m_homeSectionsGeneration) {
                return;
            }
            // A remembered cap can outlive the server that reported it (a
            // rollback or reconfiguration). If the capped read is rejected,
            // forget the cap and read once without it.
            if (itemLimitMax > 0 && (error.httpStatus == 400 || error.httpStatus == 422)) {
                m_homeSectionItemLimitMax.insert(connectionId, 0);
                if (activeConnectionId(m_authService) == connectionId) {
                    readHomeSections(connectionId, generation, 0, request);
                    return;
                }
            }
            emit canonicalHomeSectionsFailed(connectionId, error.userMessage);
        });
}

bool LibraryService::supportsHomeFavoritesRow() const
{
    return m_authService && m_authService->isAuthenticated()
        && !m_authService->supportsCapability(ProviderCapability::Watchlist);
}

bool LibraryService::supportsHomeRecommendations() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::MovieRecommendations);
}

void LibraryService::publishHomeSectionsWithBloomRows(const QString &connectionId,
                                                      quint64 generation,
                                                      const QVariantList &rows,
                                                      const HomeRowsRequest &request)
{
    const ConfigManager *config = m_authService ? m_authService->configManager() : nullptr;
    const bool readFavorites = supportsHomeFavoritesRow() && config
        && config->getHomeFavoritesRowEnabled();
    QStringList recommendationLibraries;
    if (supportsHomeRecommendations() && config && config->getHomeRecommendationsEnabled()) {
        for (const QString &libraryId : request.movieLibraryIds) {
            if (!libraryId.isEmpty() && !recommendationLibraries.contains(libraryId)) {
                recommendationLibraries.append(libraryId);
            }
        }
    }
    if (!readFavorites && recommendationLibraries.isEmpty()) {
        emit canonicalHomeSectionsLoaded(connectionId, rows);
        return;
    }

    // The server picks its seed titles at random, so recommendations are
    // read again only when Home asks for it (a new Home, after playback,
    // the setting turned on), when the shown movie libraries change, or
    // once the last read is CacheLifetimeMs old. Other refreshes (a
    // favorite change, returning to Home) reuse it.
    const QString cacheKey = connectionId + QLatin1Char('\n') + m_authService->getUserId();
    QStringList sortedLibraries = recommendationLibraries;
    sortedLibraries.sort();
    const QString librarySet = sortedLibraries.join(QLatin1Char('\n'));
    if (request.reloadRecommendations) {
        // A newer refresh can replace this one before its read lands; it
        // must read again rather than reuse what this reload replaces.
        const auto stale = m_homeRecommendations.find(cacheKey);
        if (stale != m_homeRecommendations.end()) {
            stale->fetchedAtMs = 0;
        }
    }
    const auto cached = m_homeRecommendations.constFind(cacheKey);
    const bool reuseRecommendations = !recommendationLibraries.isEmpty()
        && !request.reloadRecommendations && cached != m_homeRecommendations.cend()
        && cached->librarySet == librarySet
        && HomeRecommendations::cacheFresh(cached->fetchedAtMs,
                                           QDateTime::currentMSecsSinceEpoch());
    const bool readRecommendations = !recommendationLibraries.isEmpty() && !reuseRecommendations;

    // The reads run side by side and publish once, when the last answers.
    // A failed read leaves the others' rows.
    struct BloomRowsRead {
        QVariantList favoritesRow;
        int pending = 0;
        int succeeded = 0;
        QString error;
    };
    const auto read = std::make_shared<BloomRowsRead>();
    read->pending = (readFavorites ? 1 : 0) + (readRecommendations ? 1 : 0);
    if (reuseRecommendations) {
        ++read->succeeded;
    }
    const auto publish = [this, connectionId, generation, rows, read, cacheKey,
                          recommendationLibraries]() {
        if (generation != m_homeSectionsGeneration) {
            return;
        }
        if (read->succeeded == 0 && rows.isEmpty()) {
            emit canonicalHomeSectionsFailed(connectionId, read->error);
            return;
        }
        // The Favorites row first, then the profile's rows, then the
        // recommendations.
        QVariantList published = read->favoritesRow;
        published.append(rows);
        published.append(homeRecommendationRows(cacheKey, recommendationLibraries));
        emit canonicalHomeSectionsLoaded(connectionId, published);
    };
    const auto finish = [read, publish]() {
        if (--read->pending == 0) {
            publish();
        }
    };

    if (readFavorites) {
        // Favorite movies and series only (LibraryKinds), most recently
        // played first; never-played favorites follow, ordered by name.
        ProviderCatalogQuery favoritesQuery = baseCatalogQuery();
        favoritesQuery.favorite = ProviderCatalogTriState::Yes;
        favoritesQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
        favoritesQuery.recursive = true;
        favoritesQuery.sortBy = QStringLiteral("datePlayed");
        favoritesQuery.sortOrder = QStringLiteral("descending");
        favoritesQuery.limit = kHomeRowCardLimit;
        favoritesQuery.includeHeavyFields = false;
        sendCatalogRequest(
            QStringLiteral("getHomeFavorites"),
            ProviderCatalogOperation::Items,
            favoritesQuery,
            [this, connectionId, generation, read, finish](const ProviderCatalogResponse &response) {
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                ++read->succeeded;
                const QVariantList items
                    = m_authService->mapMediaItems(response.rawItems, connectionId);
                if (!items.isEmpty()) {
                    read->favoritesRow = {QVariantMap{
                        {QStringLiteral("sectionId"), kHomeFavoritesRowId},
                        {QStringLiteral("title"), tr("Favorites")},
                        {QStringLiteral("items"), items}}};
                }
                finish();
            },
            [this, generation, read, finish](const NetworkError &error) {
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                read->error = error.userMessage;
                finish();
            });
    }

    if (readRecommendations) {
        readHomeRecommendations(
            connectionId, generation, cacheKey, librarySet, recommendationLibraries,
            [read, finish](bool succeeded, const QString &error) {
                if (succeeded) {
                    ++read->succeeded;
                } else {
                    read->error = error;
                }
                finish();
            });
    }
    if (read->pending == 0) {
        publish();
    }
}

void LibraryService::readHomeRecommendations(const QString &connectionId,
                                             quint64 generation,
                                             const QString &cacheKey,
                                             const QString &librarySet,
                                             const QStringList &libraryIds,
                                             std::function<void(bool, const QString &)> done)
{
    struct RecommendationsRead {
        QHash<QString, QList<HomeRecommendations::Category>> categoriesByLibrary;
        int pending = 0;
        bool anySucceeded = false;
        // Every read answered; otherwise the next refresh reads again.
        bool complete = true;
        QString error;
        QSet<QString> allowedItemIds;
    };
    const auto read = std::make_shared<RecommendationsRead>();
    const auto shared = std::make_shared<std::function<void(bool, const QString &)>>(
        std::move(done));

    // Stores the read and reports it.
    const auto store = [this, cacheKey, librarySet, connectionId, read, shared]() {
        HomeRecommendationCache &entry = m_homeRecommendations[cacheKey];
        // A failed membership read keeps only items known to lie in these
        // libraries: never one from a library Home hides.
        QSet<QString> allowed = read->allowedItemIds;
        if (!read->complete && entry.librarySet == librarySet) {
            allowed.unite(entry.allowedItemIds);
        }
        for (auto it = read->categoriesByLibrary.cbegin();
             it != read->categoriesByLibrary.cend(); ++it) {
            entry.categoriesByLibrary.insert(it.key(), it.value());
        }
        entry.librarySet = librarySet;
        entry.allowedItemIds = allowed;
        entry.similarRowsWork = HomeRecommendations::similarRowsWork(
            m_serverVersions.value(connectionId));
        entry.fetchedAtMs = read->complete ? QDateTime::currentMSecsSinceEpoch() : 0;
        (*shared)(read->anySucceeded, read->error);
    };

    // Jellyfin finds similar titles and people's films in every library
    // the user may open; ParentId only picks the seed titles. Only items
    // in a library Home shows are kept.
    const auto checkMembership = [this, generation, libraryIds, read, store]() {
        QList<QList<HomeRecommendations::Category>> categories;
        for (const QString &libraryId : libraryIds) {
            categories.append(read->categoriesByLibrary.value(libraryId));
        }
        const QStringList itemIds = HomeRecommendations::itemIds(categories);
        QList<ProviderCatalogQuery> queries;
        for (const QString &libraryId : libraryIds) {
            for (qsizetype start = 0; start < itemIds.size();
                 start += HomeRecommendations::MembershipBatchSize) {
                ProviderCatalogQuery query = baseCatalogQuery();
                query.parentId = libraryId;
                query.itemIds = itemIds.mid(start, HomeRecommendations::MembershipBatchSize);
                queries.append(query);
            }
        }
        if (queries.isEmpty()) {
            store();
            return;
        }
        read->pending = static_cast<int>(queries.size());
        const auto answered = [read, store]() {
            if (--read->pending == 0) {
                store();
            }
        };
        for (const ProviderCatalogQuery &query : std::as_const(queries)) {
            sendCatalogRequest(
                QStringLiteral("getHomeRecommendationLibraries"),
                ProviderCatalogOperation::LibraryMembership,
                query,
                [this, generation, read, answered](const ProviderCatalogResponse &response) {
                    if (generation != m_homeSectionsGeneration) {
                        return;
                    }
                    for (const QJsonValue &value : response.rawItems) {
                        read->allowedItemIds.insert(
                            value.toObject().value(QStringLiteral("itemId")).toString());
                    }
                    answered();
                },
                [this, generation, read, answered](const NetworkError &) {
                    if (generation != m_homeSectionsGeneration) {
                        return;
                    }
                    read->complete = false;
                    answered();
                });
        }
    };
    const auto answered = [read, checkMembership]() {
        if (--read->pending == 0) {
            checkMembership();
        }
    };

    // The server version tells whether its "similar to" rows are real
    // (HomeRecommendations::similarRowsWork). It is read once per
    // connection; a failed read hides them and asks again next time.
    const bool readVersion = !m_serverVersions.contains(connectionId);
    read->pending = static_cast<int>(libraryIds.size()) + (readVersion ? 1 : 0);
    if (readVersion) {
        sendCatalogRequest(
            QStringLiteral("getServerInfo"),
            ProviderCatalogOperation::ServerInfo,
            baseCatalogQuery(),
            [this, connectionId, generation, read, answered](
                const ProviderCatalogResponse &response) {
                const QString version
                    = response.capabilityMetadata.value(QStringLiteral("version")).toString();
                if (!version.isEmpty()) {
                    m_serverVersions.insert(connectionId, version);
                }
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                if (version.isEmpty()) {
                    read->complete = false;
                }
                answered();
            },
            [this, generation, read, answered](const NetworkError &) {
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                read->complete = false;
                answered();
            });
    }

    // Each movie library Home shows. A failed library shows the
    // categories it answered last.
    for (const QString &libraryId : libraryIds) {
        ProviderCatalogQuery recommendationsQuery = baseCatalogQuery();
        recommendationsQuery.parentId = libraryId;
        recommendationsQuery.limit = HomeRecommendations::CategoriesPerLibrary;
        recommendationsQuery.recommendationItemLimit = HomeRecommendations::MaxItems;
        sendCatalogRequest(
            QStringLiteral("getHomeRecommendations"),
            ProviderCatalogOperation::MovieRecommendations,
            recommendationsQuery,
            [this, connectionId, generation, read, answered, libraryId](
                const ProviderCatalogResponse &response) {
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                read->anySucceeded = true;
                QList<HomeRecommendations::Category> categories;
                for (const QJsonValue &value : response.rawItems) {
                    const QJsonObject row = value.toObject();
                    categories.append(HomeRecommendations::Category{
                        row.value(QStringLiteral("recommendationType")).toString(),
                        row.value(QStringLiteral("baselineItemName")).toString(),
                        m_authService->mapMediaItems(
                            row.value(QStringLiteral("items")).toArray(), connectionId)});
                }
                read->categoriesByLibrary.insert(libraryId, categories);
                answered();
            },
            [this, generation, read, answered, cacheKey, libraryId](const NetworkError &error) {
                if (generation != m_homeSectionsGeneration) {
                    return;
                }
                read->complete = false;
                read->error = error.userMessage;
                const auto cached = m_homeRecommendations.constFind(cacheKey);
                if (cached != m_homeRecommendations.cend()) {
                    const auto previous = cached->categoriesByLibrary.constFind(libraryId);
                    if (previous != cached->categoriesByLibrary.cend()) {
                        read->categoriesByLibrary.insert(libraryId, *previous);
                    }
                }
                answered();
            });
    }
}

bool LibraryService::supportsLibrarySections() const
{
    return m_authService && m_authService->isAuthenticated()
        && m_authService->supportsCapability(ProviderCapability::LibrarySections);
}

void LibraryService::getLibrarySections(const QString &libraryId)
{
    const QString connectionId = activeConnectionId(m_authService);
    const quint64 generation = ++m_librarySectionsGeneration;
    if (!supportsLibrarySections() || libraryId.trimmed().isEmpty()) {
        // Providers without curated library rows (Jellyfin) have none.
        emit canonicalLibrarySectionsLoaded(connectionId, libraryId, {});
        return;
    }
    withSectionItemLimit(
        connectionId,
        [this, connectionId, generation] {
            return generation == m_librarySectionsGeneration
                && activeConnectionId(m_authService) == connectionId;
        },
        [this, connectionId, libraryId, generation](int itemLimitMax) {
            readLibrarySections(connectionId, libraryId, generation, itemLimitMax);
        });
}

void LibraryService::readLibrarySections(const QString &connectionId,
                                         const QString &libraryId,
                                         quint64 generation,
                                         int itemLimitMax)
{
    ProviderCatalogQuery query = baseCatalogQuery();
    query.parentId = libraryId;
    if (itemLimitMax > 0) {
        query.limit = qMin(kHomeRowCardLimit, itemLimitMax);
    }
    sendCatalogRequest(
        QStringLiteral("getLibrarySections"),
        ProviderCatalogOperation::LibrarySections,
        query,
        [this, connectionId, libraryId, generation, itemLimit = query.limit](
            const ProviderCatalogResponse &response) {
            if (generation != m_librarySectionsGeneration) {
                return;
            }
            QVariantList rows;
            for (const QJsonValue &value : response.rawItems) {
                const QJsonObject row = value.toObject();
                // A row has more cards only when Bloom's item_limit cut it:
                // it came back full at that cap and the section holds more.
                // Without item_limit each section answers its own limit,
                // which getLibrarySectionItems does not exceed.
                const int itemCount = row.value(QStringLiteral("item_count")).toInt();
                const int totalCount = row.value(QStringLiteral("total_count")).toInt();
                const bool hasMore = itemLimit > 0 && itemCount >= itemLimit
                    && (totalCount > itemCount || row.value(QStringLiteral("has_more")).toBool());
                const QVariantList items = m_authService->mapMediaItems(
                    row.value(QStringLiteral("items")).toArray(), connectionId);
                if (items.isEmpty()) {
                    continue;
                }
                rows.append(QVariantMap{
                    {QStringLiteral("sectionId"), row.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("title"), row.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("sectionType"),
                     row.value(QStringLiteral("section_type")).toString()},
                    {QStringLiteral("featured"), row.value(QStringLiteral("featured")).toBool()},
                    {QStringLiteral("totalCount"), totalCount},
                    {QStringLiteral("hasMore"), hasMore},
                    {QStringLiteral("items"), items}});
            }
            emit canonicalLibrarySectionsLoaded(connectionId, libraryId, rows);
        },
        [this, connectionId, libraryId, generation, itemLimitMax](const NetworkError &error) {
            if (generation != m_librarySectionsGeneration) {
                return;
            }
            // A remembered cap the server now rejects is dropped, and the
            // rows are read once more without it (as on Home).
            if (itemLimitMax > 0 && (error.httpStatus == 400 || error.httpStatus == 422)) {
                m_homeSectionItemLimitMax.insert(connectionId, 0);
                if (activeConnectionId(m_authService) == connectionId) {
                    readLibrarySections(connectionId, libraryId, generation, 0);
                    return;
                }
            }
            emit canonicalLibrarySectionsFailed(connectionId, libraryId, error.userMessage);
        });
}

void LibraryService::getLibrarySectionItems(const QString &libraryId, const QString &sectionId)
{
    const QString connectionId = activeConnectionId(m_authService);
    if (!supportsLibrarySections()) {
        emit canonicalLibrarySectionItemsFailed(
            connectionId, libraryId, sectionId,
            tr("The active provider has no library rows."));
        return;
    }
    ProviderCatalogQuery query = baseCatalogQuery();
    query.parentId = libraryId;
    query.itemId = sectionId;
    sendCatalogRequest(
        QStringLiteral("getLibrarySectionItems"),
        ProviderCatalogOperation::LibrarySectionItems,
        query,
        [this, connectionId, libraryId, sectionId](const ProviderCatalogResponse &response) {
            emit canonicalLibrarySectionItemsLoaded(
                connectionId, libraryId, sectionId,
                m_authService->mapMediaItems(response.rawItems, connectionId));
        },
        [this, connectionId, libraryId, sectionId](const NetworkError &error) {
            emit canonicalLibrarySectionItemsFailed(
                connectionId, libraryId, sectionId, error.userMessage);
        });
}

void LibraryService::getLibraryLetters(const LibraryItemQuery &query)
{
    const QString connectionId = activeConnectionId(m_authService);
    const QString requestKey = query.requestKey;
    const ICatalogProvider *provider = m_authService && m_authService->isAuthenticated()
        ? m_authService->catalogProvider() : nullptr;
    ProviderCatalogQuery providerQuery = providerItemsQuery(query);
    providerQuery.startIndex = 0;
    providerQuery.limit = 0;
    providerQuery.useCacheValidation = false;
    if (!provider) {
        emit libraryLettersFailed(connectionId, requestKey, tr("Not authenticated"));
        return;
    }
    const ProviderCatalogRequest lettersRequest
        = provider->createRequest(ProviderCatalogOperation::LibraryLetters, providerQuery);
    if (!lettersRequest.supported) {
        emit libraryLettersFailed(connectionId, requestKey, lettersRequest.unsupportedReason);
        return;
    }
    // Providers that need no capability read (Jellyfin) build the index
    // from the list itself.
    if (!provider->createRequest(ProviderCatalogOperation::CatalogSearchCapabilities,
                                 baseCatalogQuery()).supported) {
        readLibraryLetters(connectionId, providerQuery, requestKey);
        return;
    }
    withSearchCapabilities(
        [this, connectionId, providerQuery, requestKey](const SearchCapabilities &capabilities,
                                                        const NetworkError *error) {
            if (activeConnectionId(m_authService) != connectionId) {
                return;
            }
            if (capabilities.known && capabilities.letterIndex) {
                readLibraryLetters(connectionId, providerQuery, requestKey);
            } else if (error) {
                emit libraryLettersFailed(connectionId, requestKey, error->userMessage);
            } else {
                emit libraryLettersFailed(connectionId, requestKey,
                                          capabilities.known
                                              ? tr("The server has no letter index.")
                                              : tr("The server's letter index is unavailable."));
            }
        });
}

QString LibraryService::searchCapabilitiesKey() const
{
    return activeConnectionId(m_authService) + QLatin1Char('\n') + activeProfileId(m_authService);
}

void LibraryService::withSearchCapabilities(SearchCapabilitiesHandler handler)
{
    const QString key = searchCapabilitiesKey();
    SearchCapabilities &entry = m_searchCapabilities[key];
    if (entry.known || (entry.failed && !entry.retryAfter.hasExpired())) {
        // A copy: the handler may read other entries and grow the hash.
        const SearchCapabilities answered = entry;
        handler(answered, nullptr);
        return;
    }
    // One read serves every caller; a read from an older session was
    // cancelled with it and no longer counts.
    entry.waiters.append(std::move(handler));
    if (entry.reading && entry.readGeneration == m_requestGeneration) {
        return;
    }
    const quint64 generation = m_requestGeneration;
    entry.reading = true;
    entry.readGeneration = generation;
    entry.failed = false;
    const auto finish = [this, key, generation](auto &&update, const NetworkError *error) {
        const auto found = m_searchCapabilities.find(key);
        if (found == m_searchCapabilities.end() || !found->reading
            || found->readGeneration != generation) {
            return;
        }
        update(*found);
        found->reading = false;
        const auto waiters = std::exchange(found->waiters, {});
        const SearchCapabilities answered = *found;
        for (const SearchCapabilitiesHandler &waiter : waiters) {
            waiter(answered, error);
        }
    };
    sendCatalogRequest(
        QStringLiteral("getCatalogSearchCapabilities"),
        ProviderCatalogOperation::CatalogSearchCapabilities,
        baseCatalogQuery(),
        [finish](const ProviderCatalogResponse &response) {
            finish([&response](SearchCapabilities &capabilities) {
                capabilities.known = true;
                capabilities.peopleMediaScope =
                    response.capabilityMetadata.value(QStringLiteral("peopleMediaScope")).toBool();
                capabilities.letterIndex =
                    response.capabilityMetadata.value(QStringLiteral("letterIndex")).toBool();
            }, nullptr);
        },
        [finish](const NetworkError &error) {
            finish([&error](SearchCapabilities &capabilities) {
                // A server without the route has neither feature; that is
                // remembered. Other failures are not repeated for a while.
                if (error.httpStatus == 404 || error.httpStatus == 405
                    || error.httpStatus == 501) {
                    capabilities.known = true;
                    capabilities.peopleMediaScope = false;
                    capabilities.letterIndex = false;
                } else {
                    capabilities.failed = true;
                    capabilities.retryAfter = QDeadlineTimer(SearchCapabilityRetryMs);
                }
            }, &error);
        });
}

void LibraryService::readLibraryLetters(const QString &connectionId,
                                        const ProviderCatalogQuery &providerQuery,
                                        const QString &requestKey)
{
    const ICatalogProvider *provider = m_authService ? m_authService->catalogProvider() : nullptr;
    const bool wholeList = provider
        && provider->createRequest(ProviderCatalogOperation::LibraryLetterCount, providerQuery)
               .supported;
    const auto readLetters = [this, connectionId, providerQuery, requestKey, wholeList]() {
        // The whole list's sort names (Jellyfin) are one large answer: a
        // retry would download it again, so one attempt with a longer
        // deadline. Parsing runs off the UI thread (sendCatalogRequest).
        std::optional<RetryPolicy> retryPolicy;
        if (wholeList) {
            RetryPolicy once = m_retryPolicy;
            once.maxAttempts = 1;
            retryPolicy = once;
        }
        sendCatalogRequest(
            QStringLiteral("getLibraryLetters"),
            ProviderCatalogOperation::LibraryLetters,
            providerQuery,
            [this, connectionId, requestKey](const ProviderCatalogResponse &response) {
                emit libraryLettersLoaded(connectionId, requestKey, response.capabilityMetadata);
            },
            [this, connectionId, requestKey](const NetworkError &error) {
                emit libraryLettersFailed(connectionId, requestKey, error.userMessage);
            },
            CatalogNotModifiedHandler(),
            retryPolicy,
            wholeList ? LetterIndexAttemptTimeoutMs : 0);
    };
    if (!wholeList) {
        readLetters();
        return;
    }
    // Count first, so a large library is never downloaded for its index.
    sendCatalogRequest(
        QStringLiteral("getLibraryLetterCount"),
        ProviderCatalogOperation::LibraryLetterCount,
        providerQuery,
        [this, connectionId, requestKey, readLetters](const ProviderCatalogResponse &response) {
            if (response.total > LetterIndexMaxTitles) {
                emit libraryLettersFailed(
                    connectionId, requestKey,
                    tr("The library is too large for a letter index."));
                return;
            }
            readLetters();
        },
        [this, connectionId, requestKey](const NetworkError &error) {
            emit libraryLettersFailed(connectionId, requestKey, error.userMessage);
        });
}

QVariantList LibraryService::homeRecommendationRows(const QString &cacheKey,
                                                    const QStringList &libraryIds) const
{
    const auto cached = m_homeRecommendations.constFind(cacheKey);
    if (libraryIds.isEmpty() || cached == m_homeRecommendations.cend()) {
        return {};
    }
    // Rows follow the order Home shows the libraries in.
    QList<QList<HomeRecommendations::Category>> categories;
    for (const QString &libraryId : libraryIds) {
        categories.append(cached->categoriesByLibrary.value(libraryId));
    }
    HomeRecommendations::Filter filter;
    filter.similarRowsWork = cached->similarRowsWork;
    filter.allowedItemIds = cached->allowedItemIds;
    return HomeRecommendations::rows(categories, filter);
}

void LibraryService::getThemeSongs(const QString &seriesId)
{
    const quint64 generation = ++m_themeRequestGeneration;
    if (m_authService && m_authService->activeProviderKind() == ProviderKind::Silo) {
        const auto finish = [this, seriesId, generation](const QStringList &urls) {
            if (generation != m_themeRequestGeneration) return;
            emit themeSongPlaybackLoaded(seriesId, urls, true);
        };
        ProviderCatalogQuery capabilityQuery = baseCatalogQuery();
        capabilityQuery.useCacheValidation = true;
        sendCatalogRequest(QStringLiteral("getThemeSongsCapability"), ProviderCatalogOperation::ThemeSongsCapability,
            capabilityQuery, [this, seriesId, generation, finish](const ProviderCatalogResponse &capability) {
                if (generation != m_themeRequestGeneration) return;
                if (!capability.capabilityMetadata.value(QStringLiteral("allowed")).toBool()) { finish({}); return; }
                ProviderCatalogQuery detailQuery = baseCatalogQuery(); detailQuery.itemId = seriesId;
                sendCatalogRequest(QStringLiteral("getThemeSongs"), ProviderCatalogOperation::ThemeSongs, detailQuery,
                    [this, generation, finish](const ProviderCatalogResponse &themes) {
                        if (generation != m_themeRequestGeneration) return;
                        if (themes.rawItems.isEmpty()) { finish({}); return; }
                        ProviderCatalogQuery grantQuery = baseCatalogQuery();
                        grantQuery.itemId = themes.capabilityMetadata.value(QStringLiteral("ownerId")).toString();
                        grantQuery.themeId = themes.rawItems.first().toObject().value(QStringLiteral("id")).toString();
                        sendCatalogRequest(QStringLiteral("getThemePlayback"), ProviderCatalogOperation::ThemeSongPlayback,
                            grantQuery, [this, finish](const ProviderCatalogResponse &grant) {
                                QUrl origin(m_authService->getServerUrl()); origin.setPath(QStringLiteral("/"));
                                origin.setQuery(QString()); origin.setFragment(QString());
                                const QUrl url = origin.resolved(QUrl(grant.capabilityMetadata.value(QStringLiteral("url")).toString()));
                                if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
                                    || url.host().isEmpty() || !url.userInfo().isEmpty()) { finish({}); return; }
                                finish({url.toString(QUrl::FullyEncoded)});
                            }, [finish](const NetworkError &) { finish({}); });
                    }, [finish](const NetworkError &) { finish({}); });
            }, [finish](const NetworkError &) { finish({}); });
        return;
    }
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
            emit themeSongPlaybackLoaded(seriesId, urls, false);
        });
}

void LibraryService::search(const QString &searchTerm, int limit,
                            const QStringList &excludedLibraryIds)
{
    const quint64 searchGeneration = ++m_searchGeneration;
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
    const auto emitResults = [this, connectionId, normalizedSearchTerm, searchGeneration](
                                 const QVariantList &items) {
        if (searchGeneration != m_searchGeneration) {
            return;
        }
        QVariantList movies;
        QVariantList series;
        for (const QVariant &value : items) {
            const QString mediaType =
                value.toMap().value(QStringLiteral("mediaType")).toString();
            if (mediaType == QStringLiteral("Movie")) {
                movies.append(value);
            } else if (mediaType == QStringLiteral("Series")) {
                series.append(value);
            }
        }
        emit canonicalSearchResultsLoaded(
            connectionId, normalizedSearchTerm, movies, series);
    };
    const auto emitFailure = [this, connectionId, normalizedSearchTerm, searchGeneration](
                                 const NetworkError &error) {
        if (searchGeneration != m_searchGeneration) {
            return;
        }
        emit canonicalSearchResultsFailed(
            connectionId, normalizedSearchTerm, error.userMessage);
    };

    const auto searchEverything = [this, connectionId, query, emitResults, emitFailure]() {
        sendCatalogRequest(
            QStringLiteral("search"),
            ProviderCatalogOperation::Search,
            query,
            [this, connectionId, emitResults](const ProviderCatalogResponse &response) {
                emitResults(m_authService->mapMediaItems(response.rawItems, connectionId));
            },
            emitFailure);
    };

    // Libraries of unsupported types are searched around like hidden ones.
    // Until this connection's libraries are known, list them first rather
    // than assume none are unsupported.
    if (excludedLibraryIds.isEmpty()
        && m_unsupportedLibraryIds.contains(connectionId)
        && m_unsupportedLibraryIds.value(connectionId).isEmpty()) {
        searchEverything();
        return;
    }

    // Some libraries are left out of search: list the libraries, then search
    // each remaining one.
    sendCatalogRequest(
        QStringLiteral("search"),
        ProviderCatalogOperation::Views,
        baseCatalogQuery(),
        [this, connectionId, query, limit, excludedLibraryIds, searchEverything, emitResults,
         emitFailure](const ProviderCatalogResponse &response) {
            const QVariantList allViews =
                m_authService->mapMediaItems(response.rawItems, connectionId);
            const QStringList unsupportedIds = LibraryKinds::unsupportedLibraryIds(allViews);
            m_unsupportedLibraryIds.insert(connectionId, unsupportedIds);
            if (excludedLibraryIds.isEmpty() && unsupportedIds.isEmpty()) {
                searchEverything();
                return;
            }
            QStringList excludedIds = excludedLibraryIds;
            for (const QString &id : unsupportedIds) {
                if (!excludedIds.contains(id)) {
                    excludedIds.append(id);
                }
            }
            const QStringList libraryIds = LibrarySearchScope::searchableLibraryIds(
                LibraryKinds::supportedLibraries(allViews), excludedIds);
            if (libraryIds.isEmpty()) {
                emitResults({});
                return;
            }
            struct Aggregate {
                QList<QVariantList> results;
                qsizetype pending = 0;
                qsizetype succeeded = 0;
                NetworkError lastError;
            };
            auto aggregate = std::make_shared<Aggregate>();
            aggregate->results.resize(libraryIds.size());
            aggregate->pending = libraryIds.size();
            const auto finishOne = [aggregate, limit, emitResults, emitFailure]() {
                if (--aggregate->pending > 0) {
                    return;
                }
                if (aggregate->succeeded == 0) {
                    emitFailure(aggregate->lastError);
                    return;
                }
                emitResults(LibrarySearchScope::interleave(aggregate->results, limit));
            };
            for (qsizetype index = 0; index < libraryIds.size(); ++index) {
                ProviderCatalogQuery scoped = query;
                scoped.parentId = libraryIds.at(index);
                sendCatalogRequest(
                    QStringLiteral("search"),
                    ProviderCatalogOperation::Search,
                    scoped,
                    [this, connectionId, aggregate, index, finishOne](
                        const ProviderCatalogResponse &libraryResponse) {
                        aggregate->results[index] =
                            m_authService->mapMediaItems(libraryResponse.rawItems, connectionId);
                        ++aggregate->succeeded;
                        finishOne();
                    },
                    [aggregate, finishOne](const NetworkError &error) {
                        aggregate->lastError = error;
                        finishOne();
                    });
            }
        },
        emitFailure);
}

void LibraryService::getPerson(const QString &personId)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = personId;
    sendCatalogRequest(
        QStringLiteral("getPerson"),
        ProviderCatalogOperation::Person,
        query,
        [this, connectionId, personId](const ProviderCatalogResponse &response) {
            const QVariantMap person = m_authService->mapPerson(response.rawItem, connectionId);
            if (person.isEmpty()) {
                emit canonicalPersonFailed(connectionId, personId,
                                           tr("The server sent an unreadable person."));
                return;
            }
            emit canonicalPersonLoaded(connectionId, personId, person);
        },
        [this, connectionId, personId](const NetworkError &error) {
            emit canonicalPersonFailed(connectionId, personId, error.userMessage);
        });
}

void LibraryService::getPersonItems(const QString &personId, int startIndex,
                                    const QString &cursor, int limit,
                                    const QString &requestKey)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.itemId = personId;
    query.startIndex = qMax(0, startIndex);
    query.cursor = cursor;
    query.limit = qMax(1, limit);
    sendCatalogRequest(
        QStringLiteral("getPersonItems"),
        ProviderCatalogOperation::PersonItems,
        query,
        [this, connectionId, personId, requestKey](const ProviderCatalogResponse &response) {
            emit canonicalPersonItemsLoaded(
                connectionId, personId, requestKey,
                m_authService->mapMediaItems(response.rawItems, connectionId),
                response.total, response.hasMore, response.nextCursor);
        },
        [this, connectionId, personId, requestKey](const NetworkError &error) {
            emit canonicalPersonItemsFailed(connectionId, personId, requestKey,
                                            error.userMessage);
        });
}

void LibraryService::searchPeople(const QString &searchTerm, int limit)
{
    const quint64 generation = ++m_peopleSearchGeneration;
    const QString connectionId = activeConnectionId(m_authService);
    const QString term = searchTerm.trimmed();
    const auto emitFailure = [this, connectionId, term, generation](const NetworkError &error) {
        if (generation == m_peopleSearchGeneration) {
            emit canonicalPeopleSearchFailed(connectionId, term, error.userMessage);
        }
    };
    if (!m_authService || !m_authService->isAuthenticated()) {
        emitCatalogError(QStringLiteral("searchPeople"), tr("Not authenticated"), emitFailure);
        return;
    }
    if (term.isEmpty()) {
        emit canonicalPeopleSearchLoaded(connectionId, term, {});
        return;
    }
    ProviderCatalogQuery query = baseCatalogQuery();
    query.searchTerm = term;
    query.limit = limit;
    const auto runSearch = [this, query, connectionId, term, generation, emitFailure]() {
        sendCatalogRequest(
            QStringLiteral("searchPeople"),
            ProviderCatalogOperation::PersonSearch,
            query,
            [this, connectionId, term, generation](const ProviderCatalogResponse &response) {
                if (generation == m_peopleSearchGeneration) {
                    emit canonicalPeopleSearchLoaded(
                        connectionId, term,
                        m_authService->mapPeople(response.rawItems, connectionId));
                }
            },
            emitFailure);
    };

    // Silo limits people search to movie and series credits the viewer can
    // see only when its search capabilities say so; otherwise people from
    // books, music and other libraries would show, so none are shown.
    const ICatalogProvider *provider = m_authService->catalogProvider();
    if (!provider
        || !provider->createRequest(ProviderCatalogOperation::CatalogSearchCapabilities,
                                    baseCatalogQuery()).supported) {
        runSearch();
        return;
    }
    withSearchCapabilities(
        [this, connectionId, term, generation, runSearch, emitFailure](
            const SearchCapabilities &capabilities, const NetworkError *error) {
            if (generation != m_peopleSearchGeneration) {
                return;
            }
            if (capabilities.known && capabilities.peopleMediaScope) {
                runSearch();
            } else if (error) {
                emitFailure(*error);
            } else {
                emit canonicalPeopleSearchLoaded(connectionId, term, {});
            }
        });
}

void LibraryService::getRandomItems(int limit)
{
    const QString connectionId = activeConnectionId(m_authService);
    ProviderCatalogQuery query = baseCatalogQuery();
    query.limit = limit;
    sendRandomSampleRequest(
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
        sendRandomSampleRequest(
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
        sendRandomSampleRequest(
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

ProviderLibraryQueryCapabilities LibraryService::libraryQueryCapabilities() const
{
    const ICatalogProvider *provider =
        m_authService ? m_authService->catalogProvider() : nullptr;
    return provider ? provider->libraryQueryCapabilities()
                    : ProviderLibraryQueryCapabilities{};
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

HttpRequestHandle *LibraryService::pingServer(QObject *context,
                                              std::function<void(ServerReachability)> done)
{
    const QString endpoint =
        m_authService ? m_authService->sessionValidationEndpoint() : QString();
    if (endpoint.isEmpty() || !m_transport || !m_authService->networkManager()) {
        return nullptr;
    }
    // Public health routes (Silo /health, Jellyfin /System/Info/Public) never
    // answer 401, so they cannot tell an expired session from a live one.
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Never;
    options.unauthorizedPolicy = UnauthorizedPolicy::DeferSessionExpiry;
    options.attemptTimeoutMs = kServerPingTimeoutMs;
    const auto shared =
        std::make_shared<std::function<void(ServerReachability)>>(std::move(done));
    return m_transport->sendWithRetry(
        context,
        endpoint,
        [this, endpoint]() {
            return m_authService->networkManager()->get(m_authService->createRequest(endpoint));
        },
        [shared](QNetworkReply *) { (*shared)(ServerReachability::Reachable); },
        [shared](const NetworkError &error) {
            // A renewal that could not reach the server reports status 0.
            (*shared)(error.httpStatus == 401 ? ServerReachability::SessionExpired
                                              : ServerReachability::Unreachable);
        },
        options);
}
