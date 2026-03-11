#include "NextEpisodeResolver.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <algorithm>

namespace {

struct EpisodeEntry {
    QJsonObject episode;
    QString id;
    QString sortName;
    QDateTime premiereDate;
    QDateTime lastPlayedDate;
    qint64 playbackPositionTicks = 0;
    int seasonNumber = 0;
    int episodeNumber = 0;
    int airsBeforeSeason = -1;
    int airsAfterSeason = -1;
    int airsBeforeEpisode = -1;
    bool isPlayed = false;
    bool isSpecial = false;
    int canonicalIndex = -1;
};

QDateTime parseIsoDate(const QString &value)
{
    if (value.isEmpty()) {
        return {};
    }

    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }
    return parsed.toUTC();
}

QString episodeSortName(const QJsonObject &episode)
{
    const QString sortName = episode.value(QStringLiteral("SortName")).toString();
    if (!sortName.isEmpty()) {
        return sortName;
    }
    return episode.value(QStringLiteral("Name")).toString();
}

EpisodeEntry toEntry(const QJsonObject &episode)
{
    const QJsonObject userData = episode.value(QStringLiteral("UserData")).toObject();

    EpisodeEntry entry;
    entry.episode = episode;
    entry.id = episode.value(QStringLiteral("Id")).toString();
    entry.sortName = episodeSortName(episode);
    entry.premiereDate = parseIsoDate(episode.value(QStringLiteral("PremiereDate")).toString());
    entry.lastPlayedDate = parseIsoDate(userData.value(QStringLiteral("LastPlayedDate")).toString());
    entry.playbackPositionTicks = userData.value(QStringLiteral("PlaybackPositionTicks")).toVariant().toLongLong();
    entry.seasonNumber = episode.value(QStringLiteral("ParentIndexNumber")).toInt();
    entry.episodeNumber = episode.value(QStringLiteral("IndexNumber")).toInt();
    entry.airsBeforeSeason = episode.value(QStringLiteral("AirsBeforeSeasonNumber")).toInt(-1);
    entry.airsAfterSeason = episode.value(QStringLiteral("AirsAfterSeasonNumber")).toInt(-1);
    entry.airsBeforeEpisode = episode.value(QStringLiteral("AirsBeforeEpisodeNumber")).toInt(-1);
    entry.isPlayed = userData.value(QStringLiteral("Played")).toBool();
    entry.isSpecial = entry.seasonNumber == 0;
    return entry;
}

bool regularEpisodeLessThan(const EpisodeEntry &lhs, const EpisodeEntry &rhs)
{
    if (lhs.seasonNumber != rhs.seasonNumber) {
        return lhs.seasonNumber < rhs.seasonNumber;
    }
    if (lhs.episodeNumber != rhs.episodeNumber) {
        return lhs.episodeNumber < rhs.episodeNumber;
    }
    const int sortNameCompare = QString::compare(lhs.sortName, rhs.sortName, Qt::CaseInsensitive);
    if (sortNameCompare != 0) {
        return sortNameCompare < 0;
    }
    return lhs.id < rhs.id;
}

bool specialBucketLessThan(const EpisodeEntry &lhs, const EpisodeEntry &rhs)
{
    if (lhs.premiereDate.isValid() != rhs.premiereDate.isValid()) {
        return lhs.premiereDate.isValid();
    }
    if (lhs.premiereDate != rhs.premiereDate) {
        return lhs.premiereDate < rhs.premiereDate;
    }
    const int sortNameCompare = QString::compare(lhs.sortName, rhs.sortName, Qt::CaseInsensitive);
    if (sortNameCompare != 0) {
        return sortNameCompare < 0;
    }
    if (lhs.episodeNumber != rhs.episodeNumber) {
        return lhs.episodeNumber < rhs.episodeNumber;
    }
    return lhs.id < rhs.id;
}

bool isEligibleNextCandidate(const EpisodeEntry &entry, const QString &excludeItemId)
{
    return !entry.id.isEmpty() && entry.id != excludeItemId && !entry.isPlayed;
}

void appendSorted(QVector<EpisodeEntry> &ordered, QVector<EpisodeEntry> bucket)
{
    std::sort(bucket.begin(), bucket.end(), specialBucketLessThan);
    ordered += bucket;
}

QVector<EpisodeEntry> buildCanonicalTimeline(const QJsonArray &episodes)
{
    QVector<EpisodeEntry> regularEpisodes;
    QMap<int, QVector<EpisodeEntry>> specialsBeforeSeason;
    QMap<int, QVector<EpisodeEntry>> specialsAfterSeason;
    QMap<int, QMap<int, QVector<EpisodeEntry>>> specialsBeforeEpisode;
    QVector<EpisodeEntry> unresolvedSpecials;
    QSet<int> referencedSeasons;

    for (const QJsonValue &value : episodes) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject episode = value.toObject();
        const QString type = episode.value(QStringLiteral("Type")).toString();
        if (!type.isEmpty() && type != QStringLiteral("Episode")) {
            continue;
        }
        if (episode.value(QStringLiteral("LocationType")).toString() == QStringLiteral("Virtual")) {
            continue;
        }

        EpisodeEntry entry = toEntry(episode);
        if (entry.id.isEmpty()) {
            continue;
        }

        if (!entry.isSpecial) {
            regularEpisodes.append(entry);
            referencedSeasons.insert(entry.seasonNumber);
            continue;
        }

        if (entry.airsBeforeSeason > 0) {
            referencedSeasons.insert(entry.airsBeforeSeason);
            if (entry.airsBeforeEpisode > 0) {
                specialsBeforeEpisode[entry.airsBeforeSeason][entry.airsBeforeEpisode].append(entry);
            } else {
                specialsBeforeSeason[entry.airsBeforeSeason].append(entry);
            }
            continue;
        }

        if (entry.airsAfterSeason > 0) {
            referencedSeasons.insert(entry.airsAfterSeason);
            specialsAfterSeason[entry.airsAfterSeason].append(entry);
            continue;
        }

        unresolvedSpecials.append(entry);
    }

    std::sort(regularEpisodes.begin(), regularEpisodes.end(), regularEpisodeLessThan);

    QMap<int, QVector<EpisodeEntry>> regularEpisodesBySeason;
    for (const EpisodeEntry &entry : regularEpisodes) {
        regularEpisodesBySeason[entry.seasonNumber].append(entry);
    }

    QList<int> seasonNumbers = referencedSeasons.values();
    std::sort(seasonNumbers.begin(), seasonNumbers.end());

    QVector<EpisodeEntry> ordered;
    for (int seasonNumber : seasonNumbers) {
        appendSorted(ordered, specialsBeforeSeason.take(seasonNumber));

        QVector<EpisodeEntry> seasonEpisodes = regularEpisodesBySeason.value(seasonNumber);
        for (const EpisodeEntry &entry : seasonEpisodes) {
            appendSorted(ordered, specialsBeforeEpisode[seasonNumber].take(entry.episodeNumber));
            ordered.append(entry);
        }

        if (specialsBeforeEpisode.contains(seasonNumber)) {
            const auto leftovers = specialsBeforeEpisode.take(seasonNumber);
            for (auto it = leftovers.constBegin(); it != leftovers.constEnd(); ++it) {
                appendSorted(ordered, it.value());
            }
        }

        appendSorted(ordered, specialsAfterSeason.take(seasonNumber));
    }
    std::sort(unresolvedSpecials.begin(), unresolvedSpecials.end(), specialBucketLessThan);
    ordered += unresolvedSpecials;

    for (int index = 0; index < ordered.size(); ++index) {
        ordered[index].canonicalIndex = index;
    }

    return ordered;
}

bool anchorIsMoreRecent(const EpisodeEntry &candidate, const EpisodeEntry &best)
{
    if (candidate.lastPlayedDate.isValid() != best.lastPlayedDate.isValid()) {
        return candidate.lastPlayedDate.isValid();
    }
    if (candidate.lastPlayedDate != best.lastPlayedDate) {
        return candidate.lastPlayedDate > best.lastPlayedDate;
    }
    if (candidate.canonicalIndex != best.canonicalIndex) {
        return candidate.canonicalIndex > best.canonicalIndex;
    }
    if (candidate.playbackPositionTicks != best.playbackPositionTicks) {
        return candidate.playbackPositionTicks > best.playbackPositionTicks;
    }
    return candidate.id > best.id;
}

QJsonObject mergePreferredEpisode(const QJsonObject &selectedEpisode, const QJsonObject &preferredEpisode)
{
    if (selectedEpisode.isEmpty()) {
        return preferredEpisode;
    }
    if (preferredEpisode.isEmpty()) {
        return selectedEpisode;
    }
    if (selectedEpisode.value(QStringLiteral("Id")).toString() != preferredEpisode.value(QStringLiteral("Id")).toString()) {
        return selectedEpisode;
    }

    QJsonObject merged = selectedEpisode;
    for (auto it = preferredEpisode.constBegin(); it != preferredEpisode.constEnd(); ++it) {
        if (it.key() == QStringLiteral("UserData")
            && merged.value(it.key()).isObject()
            && it.value().isObject()) {
            QJsonObject mergedUserData = merged.value(it.key()).toObject();
            const QJsonObject preferredUserData = it.value().toObject();
            for (auto userIt = preferredUserData.constBegin(); userIt != preferredUserData.constEnd(); ++userIt) {
                mergedUserData.insert(userIt.key(), userIt.value());
            }
            merged.insert(it.key(), mergedUserData);
            continue;
        }
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QJsonObject resolveFromAnchor(const QVector<EpisodeEntry> &ordered,
                              int anchorIndex,
                              const QString &excludeItemId,
                              const QJsonObject &preferredEpisode)
{
    if (anchorIndex < 0 || anchorIndex >= ordered.size()) {
        return {};
    }

    for (int index = anchorIndex + 1; index < ordered.size(); ++index) {
        if (!isEligibleNextCandidate(ordered[index], excludeItemId)) {
            continue;
        }
        return mergePreferredEpisode(ordered[index].episode, preferredEpisode);
    }

    return {};
}

}  // namespace

namespace NextEpisodeResolver {

QJsonObject resolveBestNextEpisode(const QJsonArray &episodes,
                                   const QString &excludeItemId,
                                   const QJsonObject &preferredEpisode)
{
    const QVector<EpisodeEntry> ordered = buildCanonicalTimeline(episodes);
    if (ordered.isEmpty()) {
        return {};
    }

    if (!excludeItemId.isEmpty()) {
        for (const EpisodeEntry &entry : ordered) {
            if (entry.id == excludeItemId) {
                return resolveFromAnchor(ordered, entry.canonicalIndex, excludeItemId, preferredEpisode);
            }
        }
    }

    bool haveInProgress = false;
    EpisodeEntry bestInProgress;
    for (const EpisodeEntry &entry : ordered) {
        if (entry.isPlayed || entry.playbackPositionTicks <= 0) {
            continue;
        }
        if (!haveInProgress || anchorIsMoreRecent(entry, bestInProgress)) {
            bestInProgress = entry;
            haveInProgress = true;
        }
    }
    if (haveInProgress) {
        return mergePreferredEpisode(bestInProgress.episode, preferredEpisode);
    }

    bool haveAnchor = false;
    EpisodeEntry bestAnchor;
    for (const EpisodeEntry &entry : ordered) {
        if (!entry.isPlayed) {
            continue;
        }
        if (!haveAnchor || anchorIsMoreRecent(entry, bestAnchor)) {
            bestAnchor = entry;
            haveAnchor = true;
        }
    }
    if (haveAnchor) {
        return resolveFromAnchor(ordered, bestAnchor.canonicalIndex, excludeItemId, preferredEpisode);
    }

    for (const EpisodeEntry &entry : ordered) {
        if (!entry.isSpecial && isEligibleNextCandidate(entry, excludeItemId)) {
            return mergePreferredEpisode(entry.episode, preferredEpisode);
        }
    }

    for (const EpisodeEntry &entry : ordered) {
        if (entry.isSpecial && isEligibleNextCandidate(entry, excludeItemId)) {
            return mergePreferredEpisode(entry.episode, preferredEpisode);
        }
    }

    return {};
}

}  // namespace NextEpisodeResolver
