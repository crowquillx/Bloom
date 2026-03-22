#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>
#include <functional>

namespace ExternalRatingsHelper {

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
        qWarning() << "No external IDs found for MDBList lookup";
        return;
    }

    qDebug() << "Fetching MDBList ratings for IMDb:" << imdbId << "TMDB:" << tmdbId;

    QUrl url;
    QUrlQuery query;
    query.addQueryItem("apikey", apiKey);

    if (!tmdbId.isEmpty()) {
        url = QUrl("https://api.mdblist.com/tmdb/" + type + "/" + tmdbId);
    } else if (!imdbId.isEmpty()) {
        url = QUrl("https://api.mdblist.com/imdb/" + imdbId);
    } else {
        qWarning() << "No IDs for MDBList request";
        return;
    }

    url.setQuery(query);

    QNetworkRequest request(url);
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
            qWarning() << "MDBList API error:" << reply->errorString();
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
            qWarning() << "Wikidata query failed:" << reply->errorString();
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
            qWarning() << "AniList API error:" << reply->errorString();
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