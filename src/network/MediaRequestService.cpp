#include "MediaRequestService.h"

#include "AuthenticationService.h"
#include "LibraryService.h"
#include "SeerrService.h"
#include "providers/ICatalogProvider.h"
#include "utils/ConfigManager.h"

#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <utility>

namespace {

constexpr int kMaxPreparedSiloMedia = 16;

QString normalizedMediaType(const QString &mediaType)
{
    const QString normalized = mediaType.trimmed().toLower();
    return normalized == QStringLiteral("movie") || normalized == QStringLiteral("tv")
        ? normalized : QString();
}

QString mediaKey(const QString &mediaType, int tmdbId)
{
    return QStringLiteral("%1:%2").arg(mediaType).arg(tmdbId);
}

// Titles the library already has show on the library shelves instead.
QVariantList withoutLibraryTitles(const QVariantList &items)
{
    QVariantList filtered;
    for (const QVariant &value : items) {
        if (value.toMap().value(QStringLiteral("libraryItemId")).toString().isEmpty()) {
            filtered.append(value);
        }
    }
    return filtered;
}

// Seerr reports its failing endpoint; callers only distinguish the step.
QString seerrOperation(const QString &endpoint)
{
    if (endpoint == QStringLiteral("search") || endpoint == QStringLiteral("request")
        || endpoint == QStringLiteral("cancel")) {
        return endpoint;
    }
    return QStringLiteral("prepareRequest");
}

// Seerr's Discover rows, in page order.
const QStringList kSeerrDiscoverKeys{
    QStringLiteral("trending"),
    QStringLiteral("popular_movies"),
    QStringLiteral("upcoming_movies"),
    QStringLiteral("popular_series"),
    QStringLiteral("upcoming_series"),
};

QString seerrDiscoverTitle(const QString &key)
{
    if (key == QStringLiteral("trending")) return MediaRequestService::tr("Trending");
    if (key == QStringLiteral("popular_movies")) return MediaRequestService::tr("Popular Movies");
    if (key == QStringLiteral("upcoming_movies")) return MediaRequestService::tr("Upcoming Movies");
    if (key == QStringLiteral("popular_series")) return MediaRequestService::tr("Popular Shows");
    if (key == QStringLiteral("upcoming_series")) return MediaRequestService::tr("Upcoming Shows");
    return {};
}

} // namespace

MediaRequestService::MediaRequestService(AuthenticationService *authService,
                                         LibraryService *libraryService,
                                         SeerrService *seerrService,
                                         ConfigManager *configManager,
                                         QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_libraryService(libraryService)
    , m_seerrService(seerrService)
    , m_configManager(configManager)
{
    if (m_seerrService) {
        connect(m_seerrService, &SeerrService::configuredChanged, this, [this]() {
            updateBackend(false);
        });
        // Seerr answers only while it is the backend: a reply to work started
        // before Silo took over must not reach the screens.
        connect(m_seerrService, &SeerrService::searchResultsLoaded, this,
                [this](const QString &term, const QJsonArray &results) {
            if (!seerrActive()) {
                return;
            }
            emit searchResultsLoaded(term, results.toVariantList());
        });
        connect(m_seerrService, &SeerrService::similarResultsLoaded, this,
                [this](const QString &mediaType, int tmdbId, const QJsonArray &results) {
            if (!seerrActive()) {
                return;
            }
            emit similarResultsLoaded(mediaType, tmdbId, results.toVariantList());
        });
        connect(m_seerrService, &SeerrService::similarResultsFailed, this,
                [this](const QString &mediaType, int tmdbId, const QString &error) {
            if (seerrActive()) {
                emit similarResultsFailed(mediaType, tmdbId, error);
            }
        });
        connect(m_seerrService, &SeerrService::requestPreparationLoaded, this,
                [this](const QString &mediaType, int tmdbId, const QJsonObject &data) {
            if (!seerrActive()) {
                return;
            }
            QVariantMap payload = data.toVariantMap();
            payload.insert(QStringLiteral("backend"), QStringLiteral("seerr"));
            emit requestPreparationLoaded(mediaType, tmdbId, payload);
        });
        connect(m_seerrService, &SeerrService::requestCreated, this,
                [this](const QString &mediaType, int tmdbId, const QJsonObject &requestData) {
            if (!seerrActive()) {
                return;
            }
            // Seerr's MediaRequestStatus: 1 pending approval, 2 approved,
            // 3 declined.
            const int status = requestData.value(QStringLiteral("status")).toInt();
            const QString state = status == 1 ? QStringLiteral("pending")
                : status == 2                 ? QStringLiteral("approved")
                                              : QString();
            emit requestCreated(mediaType, tmdbId, QVariantMap{
                {QStringLiteral("backend"), QStringLiteral("seerr")},
                {QStringLiteral("requestState"), state},
            });
        });
        connect(m_seerrService, &SeerrService::errorOccurred, this,
                [this](const QString &endpoint, const QString &error) {
            if (!seerrActive()) {
                return;
            }
            emit errorOccurred(seerrOperation(endpoint), error);
        });
        const auto finishSeerrDiscoverRow = [this](const QString &key, const QVariantMap &row) {
            m_seerrDiscoverPending.removeOne(key);
            if (!row.value(QStringLiteral("items")).toList().isEmpty()) {
                m_seerrDiscoverRows.insert(key, row);
            }
            if (!m_seerrDiscoverPending.isEmpty()) {
                return;
            }
            QVariantList rows;
            for (const QString &rowKey : kSeerrDiscoverKeys) {
                if (m_seerrDiscoverRows.contains(rowKey)) {
                    rows.append(m_seerrDiscoverRows.value(rowKey));
                }
            }
            m_seerrDiscoverRows.clear();
            if (rows.isEmpty()) {
                emit discoverFailed(tr("Seerr returned no discovery rows"));
            } else {
                emit discoverLoaded(rows);
            }
        };
        connect(m_seerrService, &SeerrService::discoverRowLoaded, this,
                [this, finishSeerrDiscoverRow](const QString &key, int page, const QJsonObject &row) {
            if (!seerrActive()) {
                return;
            }
            QVariantMap mapped = row.toVariantMap();
            mapped.insert(QStringLiteral("title"), seerrDiscoverTitle(key));
            if (page == 1 && m_seerrDiscoverPending.contains(key)) {
                finishSeerrDiscoverRow(key, mapped);
            } else {
                emit discoverRowLoaded(key, page, mapped);
            }
        });
        connect(m_seerrService, &SeerrService::discoverRowFailed, this,
                [this, finishSeerrDiscoverRow](const QString &key, int page, const QString &error) {
            if (!seerrActive()) {
                return;
            }
            if (page == 1 && m_seerrDiscoverPending.contains(key)) {
                finishSeerrDiscoverRow(key, {});
            } else {
                emit discoverRowFailed(key, page, error);
            }
        });
        connect(m_seerrService, &SeerrService::genresLoaded, this, [this](const QJsonArray &genres) {
            if (!seerrActive()) {
                return;
            }
            QVariantList items;
            for (const QJsonValue &value : genres) {
                const QJsonObject genre = value.toObject();
                const int movieId = genre.value(QStringLiteral("movieId")).toInt(-1);
                const int tvId = genre.value(QStringLiteral("tvId")).toInt(-1);
                items.append(QVariantMap{
                    {QStringLiteral("kind"), QStringLiteral("genre")},
                    {QStringLiteral("id"), genre.value(QStringLiteral("name")).toString()},
                    {QStringLiteral("name"), genre.value(QStringLiteral("name")).toString()},
                    {QStringLiteral("logoUrl"), QString()},
                    {QStringLiteral("movieId"), movieId >= 0 ? QString::number(movieId) : QString()},
                    {QStringLiteral("tvId"), tvId >= 0 ? QString::number(tvId) : QString()},
                });
            }
            emit browseCategoriesLoaded(items.isEmpty() ? QVariantList{} : QVariantList{QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("genre")},
                {QStringLiteral("title"), tr("Genres")},
                {QStringLiteral("items"), items},
            }});
        });
        connect(m_seerrService, &SeerrService::genresFailed, this,
                [this](const QString &error) {
            if (seerrActive()) {
                emit browseCategoriesFailed(error);
            }
        });
        connect(m_seerrService, &SeerrService::browseLoaded, this,
                [this](const QString &key, int page, const QJsonObject &row) {
            if (!seerrActive()) {
                return;
            }
            emit browseLoaded(key, page, row.toVariantMap());
        });
        connect(m_seerrService, &SeerrService::browseFailed, this,
                [this](const QString &key, int page, const QString &error) {
            if (seerrActive()) {
                emit browseFailed(key, page, error);
            }
        });
        connect(m_seerrService, &SeerrService::requestsLoaded, this, [this](const QJsonArray &requests) {
            if (!seerrActive()) {
                return;
            }
            emit myRequestsLoaded(requests.toVariantList());
        });
        connect(m_seerrService, &SeerrService::requestsFailed, this,
                [this](const QString &error) {
            if (seerrActive()) {
                emit myRequestsFailed(error);
            }
        });
        connect(m_seerrService, &SeerrService::requestCancelled, this, [this](int requestId) {
            if (!seerrActive()) {
                return;
            }
            const QPair<QString, int> title = m_seerrCancels.take(requestId);
            emit requestCancelled(QString::number(requestId), title.first, title.second);
        });
    }

    if (m_authService) {
        connect(m_authService, &AuthenticationService::authenticatedChanged,
                this, &MediaRequestService::syncViewer);
        connect(m_authService, &AuthenticationService::connectionActivated,
                this, &MediaRequestService::syncViewer);
        connect(m_authService, &AuthenticationService::userIdChanged,
                this, &MediaRequestService::syncViewer);
    }
    // A household profile switch updates the active connection; request
    // permissions and limits are per profile.
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged,
                this, &MediaRequestService::syncViewer);
    }

    m_lastBackend = backend();
    syncViewer();
}

QString MediaRequestService::backend() const
{
    if (m_seerrService && m_seerrService->isConfigured()) {
        return QStringLiteral("seerr");
    }
    return useSilo() ? QStringLiteral("silo") : QString();
}

QString MediaRequestService::backendName() const
{
    const QString current = backend();
    if (current == QStringLiteral("seerr")) {
        return QStringLiteral("Seerr");
    }
    if (current == QStringLiteral("silo")) {
        const auto connection = m_configManager ? m_configManager->getActiveConnection() : std::nullopt;
        if (connection.has_value() && !connection->serverName.trimmed().isEmpty()) {
            return connection->serverName.trimmed();
        }
        return QStringLiteral("Silo");
    }
    return {};
}

QString MediaRequestService::statusLabel(const QString &status) const
{
    if (status == QStringLiteral("pending")) return tr("Pending");
    if (status == QStringLiteral("approved")) return tr("Approved");
    if (status == QStringLiteral("processing")) return tr("Processing");
    if (status == QStringLiteral("partial")) return tr("Partial");
    if (status == QStringLiteral("available")) return tr("Available");
    if (status == QStringLiteral("deleted")) return tr("Deleted");
    if (status == QStringLiteral("declined")) return tr("Declined");
    if (status == QStringLiteral("cancelled")) return tr("Cancelled");
    if (status == QStringLiteral("failed")) return tr("Failed");
    return {};
}

bool MediaRequestService::useSilo() const
{
    return m_siloAvailable && m_libraryService && m_libraryService->supportsMediaRequests();
}

QString MediaRequestService::viewerKey() const
{
    if (!m_libraryService || !m_libraryService->supportsMediaRequests()) {
        return {};
    }
    const auto connection = m_configManager ? m_configManager->getActiveConnection() : std::nullopt;
    return QStringLiteral("%1|%2|%3").arg(
        connection.has_value() ? connection->connectionId : QString(),
        m_authService ? m_authService->getUserId() : QString(),
        connection.has_value() ? connection->profileId : QString());
}

void MediaRequestService::syncViewer()
{
    const QString key = viewerKey();
    if (key == m_viewerKey) {
        return;
    }
    m_viewerKey = key;
    m_preparedSiloMedia.clear();
    abandonPending();
    setSiloAvailable(false, {});
    fetchSiloStatus();
}

void MediaRequestService::refreshAvailability()
{
    syncViewer();
    if (!m_statusToken) {
        fetchSiloStatus();
    }
}

void MediaRequestService::setSiloAvailable(bool available, const QVariantMap &features)
{
    const QVariantMap effective = available ? features : QVariantMap{};
    const bool featuresChanged = effective != m_siloFeatures;
    m_siloAvailable = available;
    m_siloFeatures = effective;
    updateBackend(featuresChanged);
}

void MediaRequestService::updateBackend(bool siloFeaturesChanged)
{
    const QString current = backend();
    if (current == m_lastBackend) {
        if (siloFeaturesChanged && current == QStringLiteral("silo")) {
            emit availabilityChanged();
        }
        return;
    }
    m_lastBackend = current;
    // Work started on the old backend must not answer for the new one. The
    // Silo capability read stays: it decides whether Silo can take over.
    abandonPending(m_statusToken);
    m_seerrDiscoverPending.clear();
    m_seerrDiscoverRows.clear();
    m_seerrCancels.clear();
    emit availabilityChanged();
}

bool MediaRequestService::siloFeature(const char *name) const
{
    return m_siloFeatures.value(QLatin1String(name)).toBool();
}

bool MediaRequestService::followSupported() const
{
    return backend() == QStringLiteral("silo") && siloFeature("followSupported");
}

bool MediaRequestService::watchlistTitlesSupported() const
{
    return backend() == QStringLiteral("silo") && siloFeature("watchlistTitlesSupported");
}

bool MediaRequestService::watchlistRequests() const
{
    return watchlistTitlesSupported() && siloFeature("watchlistRequests");
}

void MediaRequestService::fetchSiloStatus()
{
    if (m_viewerKey.isEmpty() || !m_libraryService) {
        return;
    }
    const quint64 token = track([this]() { m_statusToken = 0; });
    m_statusToken = token;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestStatus, {},
        [this, token](const QVariantMap &features) {
            if (!finish(token)) {
                return;
            }
            m_statusToken = 0;
            setSiloAvailable(features.value(QStringLiteral("available")).toBool(), features);
        },
        [this, token](const QString &) {
            // Keep the last answer: a failed read says nothing new.
            if (finish(token)) {
                m_statusToken = 0;
            }
        });
}

quint64 MediaRequestService::track(Abandon onAbandoned)
{
    const quint64 token = ++m_nextToken;
    m_pending.insert(token, std::move(onAbandoned));
    return token;
}

bool MediaRequestService::finish(quint64 token)
{
    return m_pending.remove(token) > 0;
}

void MediaRequestService::abandonPending(quint64 keep)
{
    QHash<quint64, Abandon> pending = std::exchange(m_pending, {});
    if (keep != 0 && pending.contains(keep)) {
        m_pending.insert(keep, pending.take(keep));
    }
    for (const Abandon &onAbandoned : std::as_const(pending)) {
        if (onAbandoned) {
            onAbandoned();
        }
    }
}

void MediaRequestService::search(const QString &searchTerm)
{
    const QString term = searchTerm.trimmed();
    if (term.isEmpty() || backend().isEmpty()) {
        emit searchResultsLoaded(searchTerm, {});
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->search(searchTerm);
        return;
    }

    const auto emptyResults = [this, searchTerm]() { emit searchResultsLoaded(searchTerm, {}); };
    const quint64 token = track(emptyResults);
    ProviderCatalogQuery query;
    query.searchTerm = term;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestSearch, query,
        [this, token, searchTerm](const QVariantMap &result) {
            if (finish(token)) {
                emit searchResultsLoaded(
                    searchTerm, withoutLibraryTitles(result.value(QStringLiteral("items")).toList()));
            }
        },
        [this, token, searchTerm](const QString &error) {
            if (finish(token)) {
                emit errorOccurred(QStringLiteral("search"), error);
                emit searchResultsLoaded(searchTerm, {});
            }
        });
}

void MediaRequestService::getSimilar(const QString &mediaType, int tmdbId)
{
    const QString type = normalizedMediaType(mediaType);
    if (type.isEmpty() || tmdbId <= 0) {
        emit similarResultsFailed(mediaType.trimmed().toLower(), tmdbId,
                                  tr("Invalid media target for similar titles"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->getSimilar(type, tmdbId, 1);
        return;
    }
    if (backend().isEmpty()) {
        emit similarResultsFailed(type, tmdbId, tr("Media requests are not available"));
        return;
    }

    // Silo's title detail carries its recommendations.
    const quint64 token = track([this, type, tmdbId]() {
        emit similarResultsFailed(type, tmdbId, tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.requestMediaType = type;
    query.tmdbId = tmdbId;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestDetail, query,
        [this, token, type, tmdbId](const QVariantMap &result) {
            if (finish(token)) {
                const QVariantMap detail = result.value(QStringLiteral("detail")).toMap();
                emit similarResultsLoaded(
                    type, tmdbId,
                    withoutLibraryTitles(detail.value(QStringLiteral("recommendations")).toList()));
            }
        },
        [this, token, type, tmdbId](const QString &error) {
            if (finish(token)) {
                emit similarResultsFailed(type, tmdbId, error);
            }
        });
}

void MediaRequestService::prepareRequest(const QString &mediaType, int tmdbId, const QString &title)
{
    const QString type = normalizedMediaType(mediaType);
    if (type.isEmpty() || tmdbId <= 0) {
        emit errorOccurred(QStringLiteral("prepareRequest"), tr("Invalid request target"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->prepareRequest(type, tmdbId, title);
        return;
    }
    if (backend().isEmpty()) {
        emit errorOccurred(QStringLiteral("prepareRequest"), tr("Media requests are not available"));
        return;
    }

    const quint64 token = track([this]() {
        emit errorOccurred(QStringLiteral("prepareRequest"), tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.requestMediaType = type;
    query.tmdbId = tmdbId;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestDetail, query,
        [this, token, type, tmdbId, title](const QVariantMap &result) {
            if (!finish(token)) {
                return;
            }
            QVariantMap detail = result.value(QStringLiteral("detail")).toMap();
            if (m_preparedSiloMedia.size() >= kMaxPreparedSiloMedia) {
                m_preparedSiloMedia.clear();
            }
            m_preparedSiloMedia.insert(mediaKey(type, tmdbId), detail);

            detail.remove(QStringLiteral("recommendations"));
            detail.insert(QStringLiteral("backend"), QStringLiteral("silo"));
            detail.insert(QStringLiteral("mediaType"), type);
            detail.insert(QStringLiteral("tmdbId"), tmdbId);
            if (detail.value(QStringLiteral("name")).toString().isEmpty()) {
                detail.insert(QStringLiteral("name"), title);
            }
            detail.insert(QStringLiteral("seasonRequestsSupported"),
                          siloFeature("seasonRequestsSupported"));
            detail.insert(QStringLiteral("followSupported"), followSupported());
            detail.insert(QStringLiteral("watchlistTitlesSupported"), watchlistTitlesSupported());
            detail.insert(QStringLiteral("watchlistRequests"), watchlistRequests());
            emit requestPreparationLoaded(type, tmdbId, detail);
        },
        [this, token](const QString &error) {
            if (finish(token)) {
                emit errorOccurred(QStringLiteral("prepareRequest"), error);
            }
        });
}

void MediaRequestService::createRequest(const QString &mediaType,
                                        int tmdbId,
                                        bool requestAllSeasons,
                                        const QVariantList &seasonNumbers,
                                        int serverId,
                                        int profileId,
                                        const QString &rootFolderPath)
{
    const QString type = normalizedMediaType(mediaType);
    if (type.isEmpty() || tmdbId <= 0) {
        emit errorOccurred(QStringLiteral("request"), tr("Invalid request payload"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->createRequest(type, tmdbId, requestAllSeasons, seasonNumbers,
                                      serverId, profileId, rootFolderPath);
        return;
    }
    const QVariantMap media = m_preparedSiloMedia.value(mediaKey(type, tmdbId));
    if (backend().isEmpty() || media.isEmpty()) {
        emit errorOccurred(QStringLiteral("request"), tr("Media requests are not available"));
        return;
    }

    ProviderCatalogQuery query;
    query.requestMediaType = type;
    query.tmdbId = tmdbId;
    query.requestMedia = media;
    if (type == QStringLiteral("tv") && !requestAllSeasons && siloFeature("seasonRequestsSupported")) {
        for (const QVariant &value : seasonNumbers) {
            bool ok = false;
            const int season = value.toInt(&ok);
            if (ok && season > 0) {
                query.requestSeasons.append(season);
            }
        }
        if (query.requestSeasons.isEmpty()) {
            emit errorOccurred(QStringLiteral("request"), tr("Select at least one season"));
            return;
        }
    }

    const quint64 token = track([this]() {
        emit errorOccurred(QStringLiteral("request"),
                           tr("The active account changed before the request finished"));
    });
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::CreateMediaRequest, query,
        [this, token, type, tmdbId](const QVariantMap &result) {
            if (!finish(token)) {
                return;
            }
            // The title's request state changed; read it fresh next time.
            m_preparedSiloMedia.remove(mediaKey(type, tmdbId));
            const QVariantMap request = result.value(QStringLiteral("request")).toMap();
            emit requestCreated(type, tmdbId, QVariantMap{
                {QStringLiteral("backend"), QStringLiteral("silo")},
                {QStringLiteral("requestState"), request.value(QStringLiteral("requestState"))},
            });
        },
        [this, token](const QString &error) {
            if (finish(token)) {
                emit errorOccurred(QStringLiteral("request"), error);
            }
        });
}

void MediaRequestService::loadDiscover()
{
    if (!isDiscoverAvailable()) {
        emit discoverFailed(tr("Discover is not available"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        // A newer load supersedes rows still on their way.
        m_seerrDiscoverPending = kSeerrDiscoverKeys;
        m_seerrDiscoverRows.clear();
        for (const QString &key : kSeerrDiscoverKeys) {
            m_seerrService->getDiscoverRow(key, 1);
        }
        return;
    }
    const quint64 token = track([this]() { emit discoverFailed(tr("The active account changed")); });
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestDiscover, {},
        [this, token](const QVariantMap &result) {
            if (finish(token)) {
                emit discoverLoaded(result.value(QStringLiteral("rows")).toList());
            }
        },
        [this, token](const QString &error) {
            if (finish(token)) {
                emit discoverFailed(error);
            }
        });
}

void MediaRequestService::loadDiscoverRow(const QString &key, int page)
{
    if (!isDiscoverAvailable() || page < 1) {
        emit discoverRowFailed(key, page, tr("Discover is not available"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->getDiscoverRow(key, page);
        return;
    }
    const quint64 token = track([this, key, page]() {
        emit discoverRowFailed(key, page, tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.sectionKey = key;
    query.page = page;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestDiscoverSection, query,
        [this, token, key, page](const QVariantMap &result) {
            if (finish(token)) {
                emit discoverRowLoaded(key, page, result.value(QStringLiteral("row")).toMap());
            }
        },
        [this, token, key, page](const QString &error) {
            if (finish(token)) {
                emit discoverRowFailed(key, page, error);
            }
        });
}

void MediaRequestService::loadBrowseCategories()
{
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->getGenres();
    } else if (backend() == QStringLiteral("silo")) {
        loadSiloBrowseCategories();
    } else {
        emit browseCategoriesFailed(tr("Media requests are not available"));
    }
}

void MediaRequestService::loadSiloBrowseCategories()
{
    struct State {
        QHash<QString, QVariantList> items;
        int pending = 3;
        QString error;
    };
    auto state = std::make_shared<State>();
    const QStringList kinds{QStringLiteral("genre"), QStringLiteral("studio"), QStringLiteral("network")};
    const quint64 token = track([this]() {
        emit browseCategoriesFailed(tr("The active account changed"));
    });
    const auto finishKind = [this, state, token, kinds]() {
        if (--state->pending > 0 || !finish(token)) {
            return;
        }
        QVariantList groups;
        for (const QString &kind : kinds) {
            const QVariantList items = state->items.value(kind);
            if (items.isEmpty()) {
                continue;
            }
            groups.append(QVariantMap{
                {QStringLiteral("kind"), kind},
                {QStringLiteral("title"), kind == QStringLiteral("genre") ? tr("Genres")
                                          : kind == QStringLiteral("studio") ? tr("Studios")
                                                                             : tr("Networks")},
                {QStringLiteral("items"), items},
            });
        }
        if (groups.isEmpty() && !state->error.isEmpty()) {
            emit browseCategoriesFailed(state->error);
        } else {
            emit browseCategoriesLoaded(groups);
        }
    };
    for (const QString &kind : kinds) {
        ProviderCatalogQuery query;
        query.browseKind = kind;
        m_libraryService->sendMediaRequestOperation(
            ProviderCatalogOperation::MediaRequestBrowseCategories, query,
            [state, kind, finishKind](const QVariantMap &result) {
                // Studios make movies and networks air series; genres browse
                // series only when the server says so.
                QVariantList items;
                for (const QVariant &value : result.value(QStringLiteral("items")).toList()) {
                    QVariantMap category = value.toMap();
                    const QString id = category.value(QStringLiteral("id")).toString();
                    const bool series = kind == QStringLiteral("network")
                        || (kind == QStringLiteral("genre")
                            && category.value(QStringLiteral("seriesSupported")).toBool());
                    category.remove(QStringLiteral("seriesSupported"));
                    category.insert(QStringLiteral("kind"), kind);
                    category.insert(QStringLiteral("movieId"),
                                    kind == QStringLiteral("network") ? QString() : id);
                    category.insert(QStringLiteral("tvId"), series ? id : QString());
                    items.append(category);
                }
                state->items.insert(kind, items);
                finishKind();
            },
            [state, finishKind](const QString &error) {
                state->error = error;
                finishKind();
            });
    }
}

QString MediaRequestService::browseKey(const QString &kind, const QString &id,
                                       const QString &mediaType, const QString &sort) const
{
    return QStringLiteral("%1|%2|%3|%4").arg(kind, id, mediaType, sort);
}

void MediaRequestService::browse(const QString &kind, const QString &id, const QString &mediaType,
                                 const QString &sort, int page)
{
    const QString key = browseKey(kind, id, mediaType, sort);
    const QString type = normalizedMediaType(mediaType);
    if (type.isEmpty() || id.trimmed().isEmpty() || page < 1) {
        emit browseFailed(key, page, tr("Nothing to browse"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        bool ok = false;
        const int genreId = id.toInt(&ok);
        if (kind != QStringLiteral("genre") || !ok) {
            emit browseFailed(key, page, tr("Seerr browses genres only"));
            return;
        }
        m_seerrService->browseGenre(key, type, genreId, sort, page);
        return;
    }
    if (backend().isEmpty()) {
        emit browseFailed(key, page, tr("Media requests are not available"));
        return;
    }

    const quint64 token = track([this, key, page]() {
        emit browseFailed(key, page, tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.browseKind = kind;
    query.browseId = id;
    query.browseSort = sort;
    query.requestMediaType = type;
    query.page = page;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestBrowse, query,
        [this, token, key, page](const QVariantMap &result) {
            if (finish(token)) {
                emit browseLoaded(key, page, result.value(QStringLiteral("row")).toMap());
            }
        },
        [this, token, key, page](const QString &error) {
            if (finish(token)) {
                emit browseFailed(key, page, error);
            }
        });
}

void MediaRequestService::loadMyRequests()
{
    if (backend() == QStringLiteral("seerr")) {
        m_seerrService->getRequests(20);
        return;
    }
    if (backend().isEmpty()) {
        emit myRequestsFailed(tr("Media requests are not available"));
        return;
    }
    const quint64 token = track([this]() { emit myRequestsFailed(tr("The active account changed")); });
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::MediaRequestList, {},
        [this, token](const QVariantMap &result) {
            if (finish(token)) {
                emit myRequestsLoaded(result.value(QStringLiteral("items")).toList());
            }
        },
        [this, token](const QString &error) {
            if (finish(token)) {
                emit myRequestsFailed(error);
            }
        });
}

void MediaRequestService::cancelRequest(const QString &requestId, const QString &mediaType, int tmdbId)
{
    const QString id = requestId.trimmed();
    const QString type = normalizedMediaType(mediaType);
    if (id.isEmpty()) {
        emit errorOccurred(QStringLiteral("cancel"), tr("Invalid request"));
        return;
    }
    if (backend() == QStringLiteral("seerr")) {
        bool ok = false;
        const int seerrId = id.toInt(&ok);
        if (!ok) {
            emit errorOccurred(QStringLiteral("cancel"), tr("Invalid request"));
            return;
        }
        m_seerrCancels.insert(seerrId, {type, tmdbId});
        m_seerrService->cancelRequest(seerrId);
        return;
    }
    if (backend().isEmpty()) {
        emit errorOccurred(QStringLiteral("cancel"), tr("Media requests are not available"));
        return;
    }
    const quint64 token = track([this]() {
        emit errorOccurred(QStringLiteral("cancel"), tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.itemId = id;
    m_libraryService->sendMediaRequestOperation(
        ProviderCatalogOperation::CancelMediaRequest, query,
        [this, token, id, type, tmdbId](const QVariantMap &) {
            if (!finish(token)) {
                return;
            }
            m_preparedSiloMedia.remove(mediaKey(type, tmdbId));
            emit requestCancelled(id, type, tmdbId);
        },
        [this, token](const QString &error) {
            if (finish(token)) {
                emit errorOccurred(QStringLiteral("cancel"), error);
            }
        });
}

void MediaRequestService::setFollowing(const QString &mediaType, int tmdbId, bool following)
{
    if (!followSupported()) {
        emit errorOccurred(QStringLiteral("follow"), tr("This server can't notify you about requests"));
        return;
    }
    setSiloTitleState(ProviderCatalogOperation::SetMediaRequestFollow, QStringLiteral("follow"),
                      mediaType, tmdbId, following);
}

void MediaRequestService::setWatchlisted(const QString &mediaType, int tmdbId, bool inWatchlist)
{
    if (!watchlistTitlesSupported()) {
        emit errorOccurred(QStringLiteral("watchlist"), tr("This server can't keep this title on a watchlist"));
        return;
    }
    setSiloTitleState(ProviderCatalogOperation::SetWatchlistTitle, QStringLiteral("watchlist"),
                      mediaType, tmdbId, inWatchlist);
}

void MediaRequestService::setSiloTitleState(ProviderCatalogOperation operation,
                                            const QString &errorOperation,
                                            const QString &mediaType,
                                            int tmdbId,
                                            bool on)
{
    const QString type = normalizedMediaType(mediaType);
    if (type.isEmpty() || tmdbId <= 0) {
        emit errorOccurred(errorOperation, tr("Invalid title"));
        return;
    }
    const quint64 token = track([this, errorOperation]() {
        emit errorOccurred(errorOperation, tr("The active account changed"));
    });
    ProviderCatalogQuery query;
    query.requestMediaType = type;
    query.tmdbId = tmdbId;
    query.stateValue = on;
    m_libraryService->sendMediaRequestOperation(
        operation, query,
        [this, token, operation, type, tmdbId, on](const QVariantMap &) {
            if (!finish(token)) {
                return;
            }
            if (operation == ProviderCatalogOperation::SetMediaRequestFollow) {
                emit followingChanged(type, tmdbId, on);
            } else {
                emit watchlistChanged(type, tmdbId, on);
            }
        },
        [this, token, errorOperation](const QString &error) {
            if (finish(token)) {
                emit errorOccurred(errorOperation, error);
            }
        });
}
