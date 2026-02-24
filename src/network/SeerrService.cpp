#include "SeerrService.h"

#include "AuthenticationService.h"
#include "../utils/ConfigManager.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

SeerrService::SeerrService(AuthenticationService *authService, ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_configManager(configManager)
{
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

void SeerrService::validateConnection()
{
    static const QString endpoint = QStringLiteral("auth/me");

    if (!m_authService || !m_authService->networkManager()) {
        emit errorOccurred(endpoint, tr("Network manager unavailable"));
        emit connectionValidated(false, tr("Network manager unavailable"));
        return;
    }

    if (!ensureConfigured(endpoint)) {
        emit connectionValidated(false, tr("Seerr URL or API key is not configured"));
        return;
    }

    QNetworkReply *reply = m_authService->networkManager()->get(createRequest(endpoint));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QString error = tr("Connection failed: %1").arg(reply->errorString());
            emit errorOccurred("auth/me", error);
            emit connectionValidated(false, error);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            const QString error = tr("Invalid Seerr auth response");
            emit errorOccurred("auth/me", error);
            emit connectionValidated(false, error);
            return;
        }

        emit connectionValidated(true, tr("Connection successful"));
    });
}

/**
 * @brief Converts a raw Seerr API result object into the normalised item map used by UI delegates.
 *
 * Poster paths that are relative (not already a full URL) are expanded to the TMDB
 * image CDN at w342 resolution.  A synthetic "seerr:{type}:{tmdbId}" Id is generated so
 * delegates can key on items from both Jellyfin and Seerr sources consistently.
 */
QJsonObject SeerrService::mapSearchResultItem(const QJsonObject &item) const
{
    const QString mediaType = item.value("mediaType").toString().toLower();
    const QString posterPath = item.value("posterPath").toString();
    QString imageUrl;
    if (!posterPath.isEmpty()) {
        if (posterPath.startsWith("http://") || posterPath.startsWith("https://")) {
            imageUrl = posterPath;
        } else {
            imageUrl = QStringLiteral("https://image.tmdb.org/t/p/w342%1").arg(posterPath);
        }
    }

    QJsonObject mapped;
    mapped["Source"] = "Seerr";
    mapped["SeerrMediaType"] = mediaType;
    mapped["SeerrTmdbId"] = item.value("id").toInt();
    mapped["PosterPath"] = posterPath;
    mapped["imageUrl"] = imageUrl;
    mapped["BackdropPath"] = item.value("backdropPath").toString();
    mapped["Overview"] = item.value("overview").toString();

    if (mediaType == "movie") {
        mapped["Type"] = "Movie";
        mapped["Name"] = item.value("title").toString();

        const QString releaseDate = item.value("releaseDate").toString();
        const QString yearToken = releaseDate.section('-', 0, 0);
        bool ok = false;
        const int year = yearToken.toInt(&ok);
        if (ok) mapped["ProductionYear"] = year;
    } else {
        mapped["Type"] = "Series";
        mapped["Name"] = item.value("name").toString();

        const QString firstAirDate = item.value("firstAirDate").toString();
        const QString yearToken = firstAirDate.section('-', 0, 0);
        bool ok = false;
        const int year = yearToken.toInt(&ok);
        if (ok) mapped["ProductionYear"] = year;
    }

    // Synthetic ID so delegates can treat Seerr entries similarly to Jellyfin entries.
    mapped["Id"] = QString("seerr:%1:%2")
        .arg(mediaType)
        .arg(mapped.value("SeerrTmdbId").toInt());

    const QJsonObject mediaInfo = item.value("mediaInfo").toObject();
    if (!mediaInfo.isEmpty()) {
        mapped["SeerrMediaInfo"] = mediaInfo;
    }

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
    if (tmdbId <= 0 || (normalizedMediaType != "movie" && normalizedMediaType != "tv")) {
        emit errorOccurred("similar", tr("Invalid media target for similar titles"));
        emit similarResultsLoaded(normalizedMediaType, tmdbId, QJsonArray());
        return;
    }

    if (!ensureConfigured("similar")) {
        emit similarResultsLoaded(normalizedMediaType, tmdbId, QJsonArray());
        return;
    }

    const QString endpoint = normalizedMediaType == "movie"
        ? QStringLiteral("movie/%1/similar").arg(tmdbId)
        : QStringLiteral("tv/%1/similar").arg(tmdbId);

    QUrlQuery query;
    query.addQueryItem("page", QString::number(qMax(1, page)));

    QNetworkReply *reply = m_authService->networkManager()->get(createRequest(endpoint, query));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalizedMediaType, tmdbId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("similar", tr("Failed loading similar titles: %1").arg(reply->errorString()));
            emit similarResultsLoaded(normalizedMediaType, tmdbId, QJsonArray());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit errorOccurred("similar", tr("Invalid similar titles response"));
            emit similarResultsLoaded(normalizedMediaType, tmdbId, QJsonArray());
            return;
        }

        const QJsonArray rawResults = doc.object().value("results").toArray();
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
