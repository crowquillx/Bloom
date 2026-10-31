#include "SeerrService.h"

#include "AuthenticationService.h"
#include "../utils/ConfigManager.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonValue>
#include <memory>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

SeerrService::SeerrService(AuthenticationService *authService, ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_configManager(configManager)
    , m_lastConfigured(isConfigured())
{
    if (m_configManager) {
        const auto notifyConfiguredIfChanged = [this]() {
            const bool configuredNow = isConfigured();
            if (configuredNow == m_lastConfigured) {
                return;
            }

            m_lastConfigured = configuredNow;
            emit configuredChanged();
        };

        connect(m_configManager, &ConfigManager::seerrBaseUrlChanged,
                this, notifyConfiguredIfChanged);
        connect(m_configManager, &ConfigManager::seerrApiKeyChanged,
                this, notifyConfiguredIfChanged);
    }
}

bool SeerrService::isConfigured() const
{
    if (!m_configManager) return false;
    return !m_configManager->getSeerrBaseUrl().trimmed().isEmpty()
        && !m_configManager->getSeerrApiKey().trimmed().isEmpty();
}

QString SeerrService::normalizedBaseUrl() const
{
    if (!m_configManager) return QString();

    QString baseUrl = m_configManager->getSeerrBaseUrl().trimmed();
    while (baseUrl.endsWith('/')) {
        baseUrl.chop(1);
    }
    return baseUrl;
}

QNetworkRequest SeerrService::createRequest(const QString &endpoint, const QUrlQuery &query) const
{
    const QString baseUrl = normalizedBaseUrl();
    QString path = endpoint;
    while (path.startsWith('/')) {
        path.remove(0, 1);
    }

    QUrl url(baseUrl + "/api/v1/" + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Seerr calls bypass HttpTransport; bound each one here.
    request.setTransferTimeout(20000);

    if (m_configManager) {
        request.setRawHeader("X-Api-Key", m_configManager->getSeerrApiKey().toUtf8());
    }

    return request;
}

bool SeerrService::ensureConfigured(const QString &endpoint)
{
    if (!m_authService || !m_authService->networkManager()) {
        emit errorOccurred(endpoint, tr("Network service unavailable"));
        return false;
    }

    if (!isConfigured()) {
        emit errorOccurred(endpoint, tr("Seerr URL or API key is not configured"));
        return false;
    }

    return true;
}

namespace {
// Seerr's MediaStatus as Bloom's request status token.
QString seerrRequestStatus(const QJsonObject &mediaInfo)
{
    switch (mediaInfo.value("status").toInt()) {
    case 2: return QStringLiteral("pending");
    case 3: return QStringLiteral("processing");
    case 4: return QStringLiteral("partial");
    case 5: return QStringLiteral("available");
    case 6: return QStringLiteral("deleted");
    default: return QString();
    }
}

QString tmdbImageUrl(const QString &path, const char *size)
{
    if (path.isEmpty()) {
        return QString();
    }
    if (path.startsWith("http://") || path.startsWith("https://")) {
        return path;
    }
    return QStringLiteral("https://image.tmdb.org/t/p/%1%2").arg(QLatin1String(size), path);
}
} // namespace

/**
 * @brief Converts a raw Seerr API result object into the discovery map shared with Silo.
 *
 * Relative poster and backdrop paths are expanded to the TMDB image CDN. A
 * synthetic "request:{type}:{tmdbId}" itemId lets delegates key on
 * local-library and discovery items consistently.
 */
QJsonObject SeerrService::mapSearchResultItem(const QJsonObject &item) const
{
    const QString mediaType = item.value("mediaType").toString().toLower();
    const QString posterPath = item.value("posterPath").toString();
    const QString backdropPath = item.value("backdropPath").toString();

    QJsonObject mapped;
    mapped["source"] = "request";
    mapped["requestBackend"] = "seerr";
    mapped["requestMediaType"] = mediaType;
    mapped["requestTmdbId"] = item.value("id").toInt();
    mapped["posterPath"] = posterPath;
    mapped["imageUrl"] = tmdbImageUrl(posterPath, "w342");
    mapped["backdropPath"] = backdropPath;
    mapped["backdropUrl"] = tmdbImageUrl(backdropPath, "w1280");
    mapped["overview"] = item.value("overview").toString();
    mapped["requestStatus"] = seerrRequestStatus(item.value("mediaInfo").toObject());

    const bool isMovie = mediaType == "movie";
    mapped["mediaType"] = isMovie ? "Movie" : "Series";
    mapped["name"] = item.value(isMovie ? "title" : "name").toString();
    const QString date = item.value(isMovie ? "releaseDate" : "firstAirDate").toString();
    bool ok = false;
    const int year = date.section('-', 0, 0).toInt(&ok);
    if (ok) mapped["productionYear"] = year;

    mapped["itemId"] = QString("request:%1:%2")
        .arg(mediaType)
        .arg(mapped.value("requestTmdbId").toInt());

    return mapped;
}

void SeerrService::search(const QString &searchTerm, int page)
{
    static const QString endpoint = QStringLiteral("search");

    if (searchTerm.trimmed().isEmpty()) {
        emit searchResultsLoaded(searchTerm, QJsonArray());
        return;
    }

    if (!ensureConfigured(endpoint)) {
        emit searchResultsLoaded(searchTerm, QJsonArray());
        return;
    }

    QUrlQuery query;
    query.addQueryItem("query", searchTerm.trimmed());
    query.addQueryItem("page", QString::number(qMax(1, page)));

    QNetworkReply *reply = m_authService->networkManager()->get(createRequest(endpoint, query));
    connect(reply, &QNetworkReply::finished, this, [this, reply, searchTerm]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("search", tr("Seerr search failed: %1").arg(reply->errorString()));
            emit searchResultsLoaded(searchTerm, QJsonArray());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit errorOccurred("search", tr("Invalid Seerr search response"));
            emit searchResultsLoaded(searchTerm, QJsonArray());
            return;
        }

        const QJsonArray rawResults = doc.object().value("results").toArray();
        QJsonArray mappedResults;
        for (const QJsonValue &value : rawResults) {
            const QJsonObject item = value.toObject();
            const QString mediaType = item.value("mediaType").toString().toLower();
            if (mediaType == "movie" || mediaType == "tv") {
                mappedResults.append(mapSearchResultItem(item));
            }
        }

        emit searchResultsLoaded(searchTerm, mappedResults);
    });
}

void SeerrService::getSimilar(const QString &mediaType, int tmdbId, int page)
{
    const QString normalizedMediaType = mediaType.trimmed().toLower();
    const auto emitSimilarFailure = [this, normalizedMediaType, tmdbId](const QString &error) {
        emit similarResultsFailed(normalizedMediaType, tmdbId, error);
    };

    if (tmdbId <= 0 || (normalizedMediaType != "movie" && normalizedMediaType != "tv")) {
        emitSimilarFailure(tr("Invalid media target for similar titles"));
        return;
    }

    if (!m_authService || !m_authService->networkManager()) {
        emitSimilarFailure(tr("Network service unavailable"));
        return;
    }

    if (!isConfigured()) {
        emitSimilarFailure(tr("Seerr URL or API key is not configured"));
        return;
    }

    const QString endpoint = normalizedMediaType == "movie"
        ? QStringLiteral("movie/%1/similar").arg(tmdbId)
        : QStringLiteral("tv/%1/similar").arg(tmdbId);

    QUrlQuery query;
    query.addQueryItem("page", QString::number(qMax(1, page)));

    QNetworkReply *reply = m_authService->networkManager()->get(createRequest(endpoint, query));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalizedMediaType, tmdbId, emitSimilarFailure]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emitSimilarFailure(tr("Failed loading similar titles: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emitSimilarFailure(tr("Invalid similar titles response"));
            return;
        }

        const QJsonObject root = doc.object();
        if (!root.contains("results") || !root.value("results").isArray()) {
            emitSimilarFailure(tr("Invalid similar titles response"));
            return;
        }

        const QJsonArray rawResults = root.value("results").toArray();
        QJsonArray mappedResults;
        for (const QJsonValue &value : rawResults) {
            const QJsonObject item = value.toObject();
            QString resolvedMediaType = item.value("mediaType").toString().toLower();
            if (resolvedMediaType.isEmpty()) {
                resolvedMediaType = normalizedMediaType;
            }

            if (resolvedMediaType == "movie" || resolvedMediaType == "tv") {
                QJsonObject normalizedItem = item;
                normalizedItem["mediaType"] = resolvedMediaType;
                mappedResults.append(mapSearchResultItem(normalizedItem));
            }
        }

        emit similarResultsLoaded(normalizedMediaType, tmdbId, mappedResults);
    });
}

QJsonObject SeerrService::pickDefaultServer(const QJsonArray &servers) const
{
    for (const QJsonValue &value : servers) {
        const QJsonObject server = value.toObject();
        if (server.value("isDefault").toBool(false)) {
            return server;
        }
    }

    if (!servers.isEmpty()) {
        return servers.first().toObject();
    }

    return QJsonObject();
}

void SeerrService::prepareRequest(const QString &mediaType, int tmdbId, const QString &title)
{
    const QString normalizedMediaType = mediaType.trimmed().toLower();
    if (tmdbId <= 0 || (normalizedMediaType != "movie" && normalizedMediaType != "tv")) {
        emit errorOccurred("prepareRequest", tr("Invalid request target"));
        return;
    }

    if (!ensureConfigured("prepareRequest")) {
        return;
    }

    const QString servicePath = normalizedMediaType == "movie" ? "service/radarr" : "service/sonarr";

    QNetworkReply *serversReply = m_authService->networkManager()->get(createRequest(servicePath));
    connect(serversReply, &QNetworkReply::finished, this, [this, serversReply, normalizedMediaType, tmdbId, servicePath, title]() {
        serversReply->deleteLater();

        if (serversReply->error() != QNetworkReply::NoError) {
            emit errorOccurred(servicePath, tr("Failed loading Seerr service list: %1").arg(serversReply->errorString()));
            return;
        }

        const QJsonDocument serversDoc = QJsonDocument::fromJson(serversReply->readAll());
        if (!serversDoc.isArray()) {
            emit errorOccurred(servicePath, tr("Invalid Seerr service list response"));
            return;
        }

        const QJsonArray servers = serversDoc.array();
        const QJsonObject defaultServer = pickDefaultServer(servers);
        const int serverId = defaultServer.value("id").toInt(-1);
        if (serverId < 0) {
            emit errorOccurred(servicePath, tr("No Seerr service instances are available"));
            return;
        }

        const QString detailsPath = QStringLiteral("%1/%2").arg(servicePath).arg(serverId);
        QNetworkReply *detailsReply = m_authService->networkManager()->get(createRequest(detailsPath));

        connect(detailsReply, &QNetworkReply::finished, this, [this, detailsReply, normalizedMediaType, tmdbId, servers, serverId, title]() {
            detailsReply->deleteLater();

            if (detailsReply->error() != QNetworkReply::NoError) {
                emit errorOccurred("service/details", tr("Failed loading Seerr service details: %1").arg(detailsReply->errorString()));
                return;
            }

            const QJsonDocument detailsDoc = QJsonDocument::fromJson(detailsReply->readAll());
            if (!detailsDoc.isObject()) {
                emit errorOccurred("service/details", tr("Invalid Seerr service details response"));
                return;
            }

            const QJsonObject details = detailsDoc.object();
            const QJsonObject serverObj = details.value("server").toObject();
            const QJsonArray profiles = details.value("profiles").toArray();
            const QJsonArray rootFolders = details.value("rootFolders").toArray();

            QJsonObject payload;
            payload["mediaType"] = normalizedMediaType;
            payload["tmdbId"] = tmdbId;
            payload["title"] = title;
            payload["servers"] = servers;
            payload["selectedServerId"] = serverId;
            payload["profiles"] = profiles;
            payload["rootFolders"] = rootFolders;
            payload["selectedProfileId"] = serverObj.value("activeProfileId").toInt(-1);

            const QString activeDirectory = serverObj.value("activeDirectory").toString();
            QString defaultRootPath;
            int defaultRootId = -1;
            for (const QJsonValue &rfVal : rootFolders) {
                const QJsonObject rf = rfVal.toObject();
                if (!activeDirectory.isEmpty() && rf.value("path").toString() == activeDirectory) {
                    defaultRootPath = rf.value("path").toString();
                    defaultRootId = rf.value("id").toInt(-1);
                    break;
                }
            }
            if (defaultRootId < 0 && !rootFolders.isEmpty()) {
                const QJsonObject rf = rootFolders.first().toObject();
                defaultRootPath = rf.value("path").toString();
                defaultRootId = rf.value("id").toInt(-1);
            }
            payload["selectedRootFolderPath"] = defaultRootPath;
            payload["selectedRootFolderId"] = defaultRootId;

            if (normalizedMediaType == "movie") {
                payload["seasonCount"] = 0;
                emit requestPreparationLoaded(normalizedMediaType, tmdbId, payload);
                return;
            }

            const QString tvPath = QStringLiteral("tv/%1").arg(tmdbId);
            QNetworkReply *tvReply = m_authService->networkManager()->get(createRequest(tvPath));
            connect(tvReply, &QNetworkReply::finished, this, [this, tvReply, normalizedMediaType, tmdbId, payload]() mutable {
                tvReply->deleteLater();

                if (tvReply->error() != QNetworkReply::NoError) {
                    emit errorOccurred("tv/details", tr("Failed loading Seerr TV details: %1").arg(tvReply->errorString()));
                    return;
                }

                const QJsonDocument tvDoc = QJsonDocument::fromJson(tvReply->readAll());
                if (!tvDoc.isObject()) {
                    emit errorOccurred("tv/details", tr("Invalid Seerr TV details response"));
                    return;
                }

                const QJsonObject tv = tvDoc.object();
                int seasonCount = tv.value("numberOfSeasons").toInt(0);
                payload["seasonCount"] = seasonCount;

                emit requestPreparationLoaded(normalizedMediaType, tmdbId, payload);
            });
        });
    });
}

void SeerrService::createRequest(const QString &mediaType,
                                 int tmdbId,
                                 bool requestAllSeasons,
                                 const QVariantList &seasonNumbers,
                                 int serverId,
                                 int profileId,
                                 const QString &rootFolderPath)
{
    const QString normalizedMediaType = mediaType.trimmed().toLower();
    if (tmdbId <= 0 || (normalizedMediaType != "movie" && normalizedMediaType != "tv")) {
        emit errorOccurred("request", tr("Invalid request payload"));
        return;
    }

    if (!ensureConfigured("request")) {
        return;
    }

    QJsonObject payload;
    payload["mediaType"] = normalizedMediaType;
    payload["mediaId"] = tmdbId;

    if (normalizedMediaType == "tv") {
        if (requestAllSeasons) {
            payload["seasons"] = QStringLiteral("all");
        } else {
            QJsonArray seasons;
            for (const QVariant &value : seasonNumbers) {
                bool ok = false;
                const int season = value.toInt(&ok);
                if (ok && season > 0) {
                    seasons.append(season);
                }
            }
            if (!seasons.isEmpty()) {
                payload["seasons"] = seasons;
            } else {
                payload["seasons"] = QStringLiteral("all");
            }
        }
    }

    if (serverId >= 0) {
        payload["serverId"] = serverId;
    }
    if (profileId >= 0) {
        payload["profileId"] = profileId;
    }
    if (!rootFolderPath.trimmed().isEmpty()) {
        payload["rootFolder"] = rootFolderPath.trimmed();
    }

    QNetworkReply *reply = m_authService->networkManager()->post(
        createRequest("request"),
        QJsonDocument(payload).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, normalizedMediaType, tmdbId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("request", tr("Failed to create Seerr request: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit errorOccurred("request", tr("Invalid Seerr request response"));
            return;
        }

        emit requestCreated(normalizedMediaType, tmdbId, doc.object());
    });
}

// ============================================================================
// Discovery, genres, and the request list
// ============================================================================

namespace {
// Seerr's MediaRequest status (1 pending, 2 approved, 3 declined, 4 failed,
// 5 completed) and its media's availability, as Bloom's request state.
QString seerrRequestState(int requestStatus, int mediaStatus)
{
    if (mediaStatus == 5 || requestStatus == 5) return QStringLiteral("available");
    if (mediaStatus == 4) return QStringLiteral("partially_available");
    switch (requestStatus) {
    case 1: return QStringLiteral("pending");
    case 2: return mediaStatus == 3 ? QStringLiteral("processing") : QStringLiteral("approved");
    case 3: return QStringLiteral("declined");
    case 4: return QStringLiteral("failed");
    default: return QString();
    }
}

QString discoverEndpoint(const QString &key, QUrlQuery &query, QString *fallbackMediaType)
{
    if (key == QStringLiteral("trending")) {
        query.addQueryItem("mediaType", "all");
        query.addQueryItem("timeWindow", "week");
        return QStringLiteral("discover/trending");
    }
    if (key == QStringLiteral("popular_movies")) {
        *fallbackMediaType = QStringLiteral("movie");
        query.addQueryItem("sortBy", "popularity.desc");
        return QStringLiteral("discover/movies");
    }
    if (key == QStringLiteral("upcoming_movies")) {
        *fallbackMediaType = QStringLiteral("movie");
        return QStringLiteral("discover/movies/upcoming");
    }
    if (key == QStringLiteral("popular_series")) {
        *fallbackMediaType = QStringLiteral("tv");
        query.addQueryItem("sortBy", "popularity.desc");
        return QStringLiteral("discover/tv");
    }
    if (key == QStringLiteral("upcoming_series")) {
        *fallbackMediaType = QStringLiteral("tv");
        return QStringLiteral("discover/tv/upcoming");
    }
    return QString();
}
} // namespace

void SeerrService::getJson(const QString &endpoint, const QUrlQuery &query,
                           JsonHandler onLoaded, ErrorHandler onFailed)
{
    if (!m_authService || !m_authService->networkManager()) {
        onFailed(tr("Network service unavailable"));
        return;
    }
    if (!isConfigured()) {
        onFailed(tr("Seerr URL or API key is not configured"));
        return;
    }
    QNetworkReply *reply = m_authService->networkManager()->get(createRequest(endpoint, query));
    connect(reply, &QNetworkReply::finished, this,
            [reply, onLoaded = std::move(onLoaded), onFailed = std::move(onFailed)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            onFailed(reply->errorString());
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        if (document.isNull()) {
            onFailed(tr("Invalid Seerr response"));
            return;
        }
        onLoaded(document);
    });
}

QJsonObject SeerrService::mapResultPage(const QJsonObject &page, const QString &fallbackMediaType) const
{
    QJsonArray items;
    for (const QJsonValue &value : page.value("results").toArray()) {
        QJsonObject item = value.toObject();
        QString mediaType = item.value("mediaType").toString().toLower();
        if (mediaType.isEmpty()) {
            mediaType = fallbackMediaType;
        }
        if (mediaType != "movie" && mediaType != "tv") {
            continue;
        }
        item["mediaType"] = mediaType;
        items.append(mapSearchResultItem(item));
    }
    const int current = qMax(1, page.value("page").toInt(1));
    const int totalPages = page.value("totalPages").toInt();
    return QJsonObject{
        {"items", items},
        {"page", current},
        {"nextPage", current + 1},
        {"hasMore", current < totalPages},
    };
}

void SeerrService::getDiscoverRow(const QString &key, int page)
{
    QUrlQuery query;
    QString fallbackMediaType;
    const QString endpoint = discoverEndpoint(key, query, &fallbackMediaType);
    if (endpoint.isEmpty()) {
        emit discoverRowFailed(key, page, tr("Unknown Seerr discovery row"));
        return;
    }
    query.addQueryItem("page", QString::number(qMax(1, page)));
    getJson(endpoint, query,
        [this, key, page, fallbackMediaType](const QJsonDocument &document) {
            if (!document.isObject() || !document.object().value("results").isArray()) {
                emit discoverRowFailed(key, page, tr("Invalid Seerr discovery response"));
                return;
            }
            QJsonObject row = mapResultPage(document.object(), fallbackMediaType);
            row["key"] = key;
            emit discoverRowLoaded(key, page, row);
        },
        [this, key, page](const QString &error) {
            emit discoverRowFailed(key, page, tr("Seerr discovery failed: %1").arg(error));
        });
}

void SeerrService::getGenres()
{
    struct State {
        QJsonArray movie;
        QJsonArray tv;
        int pending = 2;
        bool failed = false;
    };
    auto state = std::make_shared<State>();
    const auto finish = [this, state]() {
        if (--state->pending > 0) {
            return;
        }
        if (state->failed && state->movie.isEmpty() && state->tv.isEmpty()) {
            emit genresFailed(tr("Failed loading Seerr genres"));
            return;
        }
        // One entry per name, keeping TMDB's order with movie genres first.
        QJsonArray merged;
        QHash<QString, qsizetype> byName;
        const auto add = [&merged, &byName](const QJsonArray &genres, const char *idKey) {
            for (const QJsonValue &value : genres) {
                const QJsonObject genre = value.toObject();
                const QString name = genre.value("name").toString();
                const int id = genre.value("id").toInt(-1);
                if (name.isEmpty() || id < 0) {
                    continue;
                }
                const auto existing = byName.constFind(name);
                if (existing == byName.cend()) {
                    byName.insert(name, merged.size());
                    QJsonObject entry{{"name", name}, {"movieId", -1}, {"tvId", -1}};
                    entry[QLatin1String(idKey)] = id;
                    merged.append(entry);
                } else {
                    QJsonObject entry = merged.at(*existing).toObject();
                    entry[QLatin1String(idKey)] = id;
                    merged[*existing] = entry;
                }
            }
        };
        add(state->movie, "movieId");
        add(state->tv, "tvId");
        emit genresLoaded(merged);
    };
    for (const bool movie : {true, false}) {
        getJson(movie ? QStringLiteral("genres/movie") : QStringLiteral("genres/tv"), QUrlQuery(),
            [state, movie, finish](const QJsonDocument &document) {
                (movie ? state->movie : state->tv) = document.array();
                finish();
            },
            [state, finish](const QString &) {
                state->failed = true;
                finish();
            });
    }
}

void SeerrService::browseGenre(const QString &browseKey,
                               const QString &mediaType,
                               int genreId,
                               const QString &sort,
                               int page)
{
    const QString type = mediaType.trimmed().toLower();
    if (genreId < 0 || (type != "movie" && type != "tv")) {
        emit browseFailed(browseKey, page, tr("Invalid genre"));
        return;
    }
    const bool movie = type == "movie";
    QUrlQuery query;
    query.addQueryItem("genre", QString::number(genreId));
    query.addQueryItem("page", QString::number(qMax(1, page)));
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (sort == QStringLiteral("vote_average")) {
        query.addQueryItem("sortBy", "vote_average.desc");
        // Without a floor, barely rated titles top the list.
        query.addQueryItem("voteCountGte", "200");
    } else if (sort == QStringLiteral("release_date")) {
        query.addQueryItem("sortBy", movie ? "primary_release_date.desc" : "first_air_date.desc");
        query.addQueryItem(movie ? "primaryReleaseDateLte" : "firstAirDateLte", today);
    } else {
        query.addQueryItem("sortBy", "popularity.desc");
    }
    getJson(movie ? QStringLiteral("discover/movies") : QStringLiteral("discover/tv"), query,
        [this, browseKey, page, type](const QJsonDocument &document) {
            if (!document.isObject() || !document.object().value("results").isArray()) {
                emit browseFailed(browseKey, page, tr("Invalid Seerr discovery response"));
                return;
            }
            emit browseLoaded(browseKey, page, mapResultPage(document.object(), type));
        },
        [this, browseKey, page](const QString &error) {
            emit browseFailed(browseKey, page, tr("Seerr discovery failed: %1").arg(error));
        });
}

void SeerrService::getRequests(int take)
{
    QUrlQuery query;
    query.addQueryItem("take", QString::number(qBound(1, take, 50)));
    query.addQueryItem("skip", "0");
    query.addQueryItem("filter", "all");
    query.addQueryItem("sort", "added");
    getJson(QStringLiteral("request"), query,
        [this](const QJsonDocument &document) {
            if (!document.isObject() || !document.object().value("results").isArray()) {
                emit requestsFailed(tr("Invalid Seerr request list"));
                return;
            }
            const QJsonArray requests = document.object().value("results").toArray();
            if (requests.isEmpty()) {
                emit requestsLoaded({});
                return;
            }
            // Request entries carry no title or artwork: read each title.
            struct State {
                QList<QJsonObject> items;
                int pending = 0;
            };
            auto state = std::make_shared<State>();
            state->items.resize(requests.size());
            state->pending = int(requests.size());
            const auto finish = [this, state]() {
                if (--state->pending > 0) {
                    return;
                }
                QJsonArray items;
                for (const QJsonObject &item : std::as_const(state->items)) {
                    if (!item.isEmpty()) {
                        items.append(item);
                    }
                }
                emit requestsLoaded(items);
            };
            for (qsizetype index = 0; index < requests.size(); ++index) {
                const QJsonObject request = requests.at(index).toObject();
                const QJsonObject media = request.value("media").toObject();
                const QString mediaType = media.value("mediaType").toString().toLower();
                const int tmdbId = media.value("tmdbId").toInt();
                if ((mediaType != "movie" && mediaType != "tv") || tmdbId <= 0) {
                    finish();
                    continue;
                }
                getJson(QStringLiteral("%1/%2").arg(mediaType).arg(tmdbId), QUrlQuery(),
                    [this, state, index, request, media, mediaType, finish](const QJsonDocument &document) {
                        QJsonObject details = document.object();
                        details["mediaType"] = mediaType;
                        QJsonObject item = mapSearchResultItem(details);
                        const int requestStatus = request.value("status").toInt();
                        const QString requestState = seerrRequestState(
                            requestStatus, media.value("status").toInt());
                        QJsonArray seasons;
                        for (const QJsonValue &season : request.value("seasons").toArray()) {
                            seasons.append(season.toObject().value("seasonNumber").toInt());
                        }
                        item["requestId"] = QString::number(request.value("id").toInt());
                        item["requestState"] = requestState;
                        item["requestStatus"] = requestState == "partially_available"
                            ? QStringLiteral("partial") : requestState;
                        item["requestedSeasons"] = seasons;
                        item["createdAt"] = request.value("createdAt").toString();
                        // Seerr lets requesters remove only pending requests.
                        item["canCancel"] = requestStatus == 1;
                        state->items[index] = item;
                        finish();
                    },
                    [finish](const QString &) { finish(); });
            }
        },
        [this](const QString &error) {
            emit requestsFailed(tr("Failed loading Seerr requests: %1").arg(error));
        });
}

void SeerrService::cancelRequest(int requestId)
{
    if (requestId <= 0) {
        emit errorOccurred("cancel", tr("Invalid request"));
        return;
    }
    if (!ensureConfigured("cancel")) {
        return;
    }
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(
        createRequest(QStringLiteral("request/%1").arg(requestId)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("cancel", tr("Failed to cancel the Seerr request: %1").arg(reply->errorString()));
            return;
        }
        emit requestCancelled(requestId);
    });
}
