#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>
#include <functional>
#include "BloomLogging.h"

namespace ExternalRatingsHelper {

// Ratings are optional decoration; never let a stalled request linger.
inline constexpr int kTransferTimeoutMs = 15000;

inline QVariantMap mergeRatings(const QVariantMap &rawRatings, const QVariantMap &aniListRating)
{
    QVariantMap combined = rawRatings;
    QVariantList ratings = combined.value("ratings").toList();

    if (!aniListRating.isEmpty()) {
        bool found = false;
        auto getScore = [](const QVariantMap &rating) {
            return rating.value("score", rating.value("value")).toInt();
        };

        for (QVariant &rating : ratings) {
            if (rating.toMap().value("source").toString().compare("AniList", Qt::CaseInsensitive) == 0) {
                if (getScore(aniListRating) > getScore(rating.toMap())) {
                    rating = aniListRating;
                }
                found = true;
                break;
            }
        }

        if (!found) {
            ratings.append(aniListRating);
        }
    }

    combined["ratings"] = ratings;
    return combined;
}

// The one table of rating sources: every spelling the media servers,
// MDBList and AniList use for a source, mapped to the key ratings are
// deduplicated by and RatingMetadataChip picks its logo and format by.
// Unknown sources keep their own (lowercased) name.
inline QString ratingSourceKey(const QString &source)
{
    QString normalized = source.trimmed().toLower();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    // Ordered: the first matching spelling decides.
    struct Alias {
        const char *spelling;
        const char *key;
    };
    static constexpr Alias aliases[] = {
        {"tomatoesaudience", "audience"},  // rottenTomatoesAudience
        {"popcorn", "audience"},           // MDBList popcorn, Popcornmeter
        {"tomatoes", "tomatoes"},          // rottenTomatoesCritic, MDBList tomatoes
        {"imdb", "imdb"},
        {"metacriticuser", "metacriticuser"},
        {"metacritic", "metacritic"},
        {"tmdb", "tmdb"},
        {"trakt", "trakt"},
        {"letterboxd", "letterboxd"},
        {"myanimelist", "mal"},
        {"anilist", "anilist"},
        {"rogerebert", "rogerebert"},
        {"kinopoisk", "kinopoisk"},
        {"douban", "douban"},
    };
    for (const Alias &alias : aliases) {
        if (normalized.contains(QLatin1String(alias.spelling))) {
            return QLatin1String(alias.key);
        }
    }
    if (normalized == QStringLiteral("mal")) {
        return QStringLiteral("mal");
    }
    return normalized;
}

// Every rating carries its `sourceKey` for the rating chip.
inline QVariantList withSourceKeys(const QVariantList &ratings)
{
    QVariantList result;
    result.reserve(ratings.size());
    for (const QVariant &value : ratings) {
        QVariantMap rating = value.toMap();
        rating.insert(QStringLiteral("sourceKey"),
                      ratingSourceKey(rating.value(QStringLiteral("source")).toString()));
        result.append(rating);
    }
    return result;
}

// The media server's own ratings come first, in the server's order and
// marked `fromServer`; the external ratings follow for sources the server
// does not show. Every rating carries its `sourceKey`.
inline QVariantMap withServerRatings(const QVariantList &serverRatings, const QVariantMap &combined)
{
    QVariantMap result = combined;
    QVariantList ratings;
    QSet<QString> shown;
    for (const QVariant &value : withSourceKeys(serverRatings)) {
        QVariantMap rating = value.toMap();
        rating.insert(QStringLiteral("fromServer"), true);
        shown.insert(rating.value(QStringLiteral("sourceKey")).toString());
        ratings.append(rating);
    }
    for (const QVariant &value : withSourceKeys(combined.value(QStringLiteral("ratings")).toList())) {
        if (!shown.contains(value.toMap().value(QStringLiteral("sourceKey")).toString())) {
            ratings.append(value);
        }
    }
    result[QStringLiteral("ratings")] = ratings;
    return result;
}

inline void fetchMdbListRatings(QNetworkAccessManager *networkManager,
                                QObject *context,
                                const QString &apiKey,
                                const QString &imdbId,
                                const QString &tmdbId,
                                const QString &type,
                                std::function<void(const QVariantMap &)> onSuccess)
{
    if (!networkManager || !context || apiKey.isEmpty()) {
        return;
    }

    if (imdbId.isEmpty() && tmdbId.isEmpty()) {
        qCWarning(lcViewModels) << "No external IDs found for MDBList lookup";
        return;
    }

    qCDebug(lcViewModels) << "Fetching MDBList ratings for IMDb:" << imdbId << "TMDB:" << tmdbId;

    QUrl url;
    QUrlQuery query;
    query.addQueryItem("apikey", apiKey);

    if (!tmdbId.isEmpty()) {
        url = QUrl("https://api.mdblist.com/tmdb/" + type + "/" + tmdbId);
    } else if (!imdbId.isEmpty()) {
        url = QUrl("https://api.mdblist.com/imdb/" + imdbId);
    } else {
        qCWarning(lcViewModels) << "No IDs for MDBList request";
        return;
    }

    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = networkManager->get(request);
    QPointer<QObject> guard(context);

    QObject::connect(reply, &QNetworkReply::finished, context, [guard, reply, onSuccess]() {
        reply->deleteLater();
        if (!guard) {
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject() && onSuccess) {
                onSuccess(doc.object().toVariantMap());
            }
        } else {
            // The error text includes the request URL, which carries the API key.
            qCWarning(lcViewModels) << "MDBList API error:" << redactNetworkLogText(reply->errorString());
        }
    });
}

inline void fetchAniListIdFromWikidata(QNetworkAccessManager *networkManager,
                                       QObject *context,
                                       const QString &imdbId,
                                       std::function<void(const QString &)> onFinished)
{
    if (!networkManager || !context || imdbId.isEmpty()) {
        if (onFinished) {
            onFinished(QString());
        }
        return;
    }

    const QString sparql = QString(
        "SELECT ?anilistId WHERE { ?item wdt:P345 \"%1\" . ?item wdt:P8729 ?anilistId . } LIMIT 1"
    ).arg(imdbId);

    QUrl url("https://query.wikidata.org/sparql");
    QUrlQuery query;
    query.addQueryItem("query", sparql);
    query.addQueryItem("format", "json");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Bloom/1.0 (Qt 6)");
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = networkManager->get(request);
    QPointer<QObject> guard(context);

    QObject::connect(reply, &QNetworkReply::finished, context, [guard, reply, onFinished]() {
        reply->deleteLater();
        if (!guard) {
            return;
        }

        QString anilistId;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QJsonArray bindings = doc.object()["results"].toObject()["bindings"].toArray();
            if (!bindings.isEmpty()) {
                const QJsonObject binding = bindings.first().toObject();
                anilistId = binding.value("anilistId").toObject().value("value").toString();
            }
        } else {
            qCWarning(lcViewModels) << "Wikidata query failed:" << redactNetworkLogText(reply->errorString());
        }

        if (onFinished) {
            onFinished(anilistId);
        }
    });
}

inline void queryAniListById(QNetworkAccessManager *networkManager,
                             QObject *context,
                             const QString &anilistId,
                             std::function<void(const QJsonObject &)> onSuccess)
{
    if (!networkManager || !context || anilistId.isEmpty()) {
        return;
    }

    QUrl url("https://graphql.anilist.co");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(kTransferTimeoutMs);

    QJsonObject variables;
    variables["id"] = anilistId.toInt();

    QJsonObject queryObj;
    queryObj["query"] = "query($id:Int){ Media(id:$id,type:ANIME){ id averageScore meanScore siteUrl } }";
    queryObj["variables"] = variables;

    QNetworkReply *reply = networkManager->post(request, QJsonDocument(queryObj).toJson());
    QPointer<QObject> guard(context);

    QObject::connect(reply, &QNetworkReply::finished, context, [guard, reply, onSuccess]() {
        reply->deleteLater();
        if (!guard) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcViewModels) << "AniList API error:" << redactNetworkLogText(reply->errorString());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject media = doc.object()["data"].toObject()["Media"].toObject();
        if (!media.isEmpty() && onSuccess) {
            onSuccess(media);
        }
    });
}

}
