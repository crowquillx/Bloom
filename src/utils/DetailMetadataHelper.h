#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>

namespace DetailMetadataHelper {

struct CommonDetailMetadata {
    QString title;
    QString overview;
    int productionYear = 0;
    QString officialRating;
    bool isWatched = false;
    qint64 playbackPositionTicks = 0;
    QVariantList people;
    QStringList genres;
    QString logoUrl;
    QString posterUrl;
    QString backdropUrl;
};

inline QVariantList mapPeople(const QJsonArray &people)
{
    QVariantList mappedPeople;
    mappedPeople.reserve(people.size());

    for (const auto &value : people) {
        const QJsonObject person = value.toObject();
        const QString name = person.value("Name").toString();
        if (name.isEmpty()) {
            continue;
        }

        QVariantMap mapped = person.toVariantMap();
        QString subtitle = person.value("Role").toString();
        if (subtitle.isEmpty()) {
            subtitle = person.value("Type").toString();
        }
        mapped.insert("Subtitle", subtitle);
        mappedPeople.append(mapped);

        if (mappedPeople.size() >= 18) {
            break;
        }
    }

    return mappedPeople;
}

inline QStringList mapGenres(const QJsonArray &genresArray)
{
    QStringList genres;
    genres.reserve(genresArray.size());

    for (const auto &value : genresArray) {
        const QString genre = value.toString();
        if (!genre.isEmpty()) {
            genres.append(genre);
        }
    }

    return genres;
}

inline CommonDetailMetadata extractCommonMetadata(const QJsonObject &data,
                                                  const QString &itemId,
                                                  const std::function<QString(const QString &, const QString &, int)> &imageUrlBuilder,
                                                  const QString &emptyOverviewFallback = QString(),
                                                  bool fallbackBackdropToPoster = false)
{
    CommonDetailMetadata metadata;
    metadata.title = data.value("Name").toString();
    metadata.overview = data.value("Overview").toString();
    if (metadata.overview.isEmpty()) {
        metadata.overview = emptyOverviewFallback;
    }

    metadata.productionYear = data.value("ProductionYear").toInt();
    metadata.officialRating = data.value("OfficialRating").toString();

    const QJsonObject userData = data.value("UserData").toObject();
    metadata.isWatched = userData.value("Played").toBool();
    metadata.playbackPositionTicks = userData.value("PlaybackPositionTicks").toVariant().toLongLong();

    metadata.people = mapPeople(data.value("People").toArray());
    metadata.genres = mapGenres(data.value("Genres").toArray());

    if (imageUrlBuilder && !itemId.isEmpty()) {
        metadata.logoUrl = imageUrlBuilder(itemId, "Logo", 2000);
        metadata.posterUrl = imageUrlBuilder(itemId, "Primary", 400);
        metadata.backdropUrl = imageUrlBuilder(itemId, "Backdrop", 1920);

        if (fallbackBackdropToPoster && metadata.backdropUrl.isEmpty()) {
            metadata.backdropUrl = metadata.posterUrl;
        }
    }

    return metadata;
}

}
