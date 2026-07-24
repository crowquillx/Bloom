#include "NextEpisodeResolver.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <algorithm>

namespace {

struct EpisodeEntry {
    QVariantMap m_episode;
    QString m_id;
    QString m_sortName;
    QDateTime m_premiereDate;
    QDateTime m_lastPlayedDate;
    qint64 m_positionMs = 0;
    int m_seasonNumber = 0;
    int m_episodeNumber = 0;
    int m_airsBeforeSeason = -1;
    int m_airsAfterSeason = -1;
    int m_airsBeforeEpisode = -1;
    bool m_hasPlacementMetadata = false;
    bool m_isPlayed = false;
    bool m_isSpecial = false;
    int m_canonicalIndex = -1;
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

QString episodeSortName(const QVariantMap &episode)
{
    const QString sortName = episode.value(QStringLiteral("sortName")).toString();
    if (!sortName.isEmpty()) {
        return sortName;
    }
    return episode.value(QStringLiteral("name")).toString();
}

EpisodeEntry toEntry(const QVariantMap &episode)
{
    const QVariantMap userState = episode.value(QStringLiteral("userState")).toMap();

    EpisodeEntry entry;
    entry.m_episode = episode;
    entry.m_id = episode.value(QStringLiteral("itemId")).toString();
    entry.m_sortName = episodeSortName(episode);
    entry.m_premiereDate = parseIsoDate(episode.value(QStringLiteral("premiereDate")).toString());
    entry.m_lastPlayedDate = parseIsoDate(userState.value(QStringLiteral("lastPlayedAt")).toString());
    entry.m_positionMs = userState.value(QStringLiteral("positionMs")).toLongLong();
    entry.m_seasonNumber = episode.value(QStringLiteral("parentIndexNumber")).toInt();
    entry.m_episodeNumber = episode.value(QStringLiteral("indexNumber")).toInt();
    entry.m_airsBeforeSeason = episode.value(QStringLiteral("airsBeforeSeasonNumber"), -1).toInt();
    entry.m_airsAfterSeason = episode.value(QStringLiteral("airsAfterSeasonNumber"), -1).toInt();
    entry.m_airsBeforeEpisode = episode.value(QStringLiteral("airsBeforeEpisodeNumber"), -1).toInt();
    entry.m_hasPlacementMetadata = entry.m_airsBeforeSeason > 0
                                || entry.m_airsAfterSeason > 0
                                || entry.m_airsBeforeEpisode > 0;
    entry.m_isPlayed = userState.value(QStringLiteral("watched")).toBool();
    entry.m_isSpecial = entry.m_seasonNumber == 0 || entry.m_hasPlacementMetadata;
    return entry;
}

bool regularEpisodeLessThan(const EpisodeEntry &lhs, const EpisodeEntry &rhs)
{
    if (lhs.m_seasonNumber != rhs.m_seasonNumber) {
        return lhs.m_seasonNumber < rhs.m_seasonNumber;
    }
    if (lhs.m_episodeNumber != rhs.m_episodeNumber) {
        return lhs.m_episodeNumber < rhs.m_episodeNumber;
    }
    const int sortNameCompare = QString::compare(lhs.m_sortName, rhs.m_sortName, Qt::CaseInsensitive);
    if (sortNameCompare != 0) {
        return sortNameCompare < 0;
    }
    return lhs.m_id < rhs.m_id;
}

bool specialBucketLessThan(const EpisodeEntry &lhs, const EpisodeEntry &rhs)
{
    if (lhs.m_premiereDate.isValid() != rhs.m_premiereDate.isValid()) {
        return lhs.m_premiereDate.isValid();
    }
    if (lhs.m_premiereDate != rhs.m_premiereDate) {
        return lhs.m_premiereDate < rhs.m_premiereDate;
    }
    const int sortNameCompare = QString::compare(lhs.m_sortName, rhs.m_sortName, Qt::CaseInsensitive);
    if (sortNameCompare != 0) {
        return sortNameCompare < 0;
    }
    if (lhs.m_episodeNumber != rhs.m_episodeNumber) {
        return lhs.m_episodeNumber < rhs.m_episodeNumber;
    }
    return lhs.m_id < rhs.m_id;
}

bool isEligibleNextCandidate(const EpisodeEntry &entry, const QString &excludeItemId)
{
    return !entry.m_id.isEmpty() && entry.m_id != excludeItemId && !entry.m_isPlayed;
}

void appendSorted(QVector<EpisodeEntry> &ordered, QVector<EpisodeEntry> bucket)
{
    std::sort(bucket.begin(), bucket.end(), specialBucketLessThan);
    ordered += bucket;
}

QVector<EpisodeEntry> buildCanonicalTimeline(const QVariantList &episodes)
{
    QVector<EpisodeEntry> regularEpisodes;
    QMap<int, QVector<EpisodeEntry>> specialsBeforeSeason;
    QMap<int, QVector<EpisodeEntry>> specialsAfterSeason;
    QMap<int, QMap<int, QVector<EpisodeEntry>>> specialsBeforeEpisode;
    QVector<EpisodeEntry> unresolvedSpecials;
    QSet<int> referencedSeasons;

    for (const QVariant &value : episodes) {
        if (!value.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap episode = value.toMap();
        const QString type = episode.value(QStringLiteral("mediaType")).toString();
        if (!type.isEmpty() && type != QStringLiteral("Episode")) {
            continue;
        }
        if (episode.value(QStringLiteral("locationType")).toString() == QStringLiteral("Virtual")) {
            continue;
        }

        EpisodeEntry entry = toEntry(episode);
        if (entry.m_id.isEmpty()) {
            continue;
        }

        if (!entry.m_isSpecial) {
            regularEpisodes.append(entry);
            referencedSeasons.insert(entry.m_seasonNumber);
            continue;
        }

        if (entry.m_airsBeforeSeason > 0) {
            referencedSeasons.insert(entry.m_airsBeforeSeason);
            if (entry.m_airsBeforeEpisode > 0) {
                specialsBeforeEpisode[entry.m_airsBeforeSeason][entry.m_airsBeforeEpisode].append(entry);
            } else {
                specialsBeforeSeason[entry.m_airsBeforeSeason].append(entry);
            }
            continue;
        }

        if (entry.m_airsAfterSeason > 0) {
            referencedSeasons.insert(entry.m_airsAfterSeason);
            specialsAfterSeason[entry.m_airsAfterSeason].append(entry);
            continue;
        }

        unresolvedSpecials.append(entry);
    }

    std::sort(regularEpisodes.begin(), regularEpisodes.end(), regularEpisodeLessThan);

    QMap<int, QVector<EpisodeEntry>> regularEpisodesBySeason;
    for (const EpisodeEntry &entry : regularEpisodes) {
        regularEpisodesBySeason[entry.m_seasonNumber].append(entry);
    }

    QList<int> seasonNumbers = referencedSeasons.values();
    std::sort(seasonNumbers.begin(), seasonNumbers.end());

    QVector<EpisodeEntry> ordered;
    for (int seasonNumber : seasonNumbers) {
        appendSorted(ordered, specialsBeforeSeason.take(seasonNumber));

        QVector<EpisodeEntry> seasonEpisodes = regularEpisodesBySeason.value(seasonNumber);
        for (const EpisodeEntry &entry : seasonEpisodes) {
            appendSorted(ordered, specialsBeforeEpisode[seasonNumber].take(entry.m_episodeNumber));
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
        ordered[index].m_canonicalIndex = index;
    }

    return ordered;
}

bool anchorIsMoreRecent(const EpisodeEntry &candidate, const EpisodeEntry &best)
{
    if (candidate.m_lastPlayedDate.isValid() != best.m_lastPlayedDate.isValid()) {
        return candidate.m_lastPlayedDate.isValid();
    }
    if (candidate.m_lastPlayedDate != best.m_lastPlayedDate) {
        return candidate.m_lastPlayedDate > best.m_lastPlayedDate;
    }
    if (candidate.m_canonicalIndex != best.m_canonicalIndex) {
        return candidate.m_canonicalIndex > best.m_canonicalIndex;
    }
    if (candidate.m_positionMs != best.m_positionMs) {
        return candidate.m_positionMs > best.m_positionMs;
    }
    return candidate.m_id > best.m_id;
}

QVariantMap mergePreferredEpisode(const QVariantMap &selectedEpisode, const QVariantMap &preferredEpisode)
{
    if (selectedEpisode.isEmpty()) {
        return preferredEpisode;
    }
    if (preferredEpisode.isEmpty()) {
        return selectedEpisode;
    }
    if (selectedEpisode.value(QStringLiteral("itemId")).toString()
        != preferredEpisode.value(QStringLiteral("itemId")).toString()) {
        return selectedEpisode;
    }

    QVariantMap merged = selectedEpisode;
    for (auto it = preferredEpisode.constBegin(); it != preferredEpisode.constEnd(); ++it) {
        if (it.key() == QStringLiteral("userState")) {
            continue;
        }
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QVariantMap resolveFromAnchor(const QVector<EpisodeEntry> &ordered,
                              int anchorIndex,
                              const QString &excludeItemId,
                              const QVariantMap &preferredEpisode)
{
    if (anchorIndex < 0 || anchorIndex >= ordered.size()) {
        return {};
    }

    for (int index = anchorIndex + 1; index < ordered.size(); ++index) {
        if (!isEligibleNextCandidate(ordered[index], excludeItemId)) {
            continue;
        }
        return mergePreferredEpisode(ordered[index].m_episode, preferredEpisode);
    }

    return {};
}

}  // namespace

namespace NextEpisodeResolver {

QVariantMap resolveBestNextEpisode(const QVariantList &episodes,
                                   const QString &excludeItemId,
                                   const QVariantMap &preferredEpisode)
{
    const QVector<EpisodeEntry> ordered = buildCanonicalTimeline(episodes);
    if (ordered.isEmpty()) {
        return {};
    }

    if (!excludeItemId.isEmpty()) {
        for (const EpisodeEntry &entry : ordered) {
            if (entry.m_id == excludeItemId) {
                return resolveFromAnchor(ordered, entry.m_canonicalIndex, excludeItemId, preferredEpisode);
            }
        }
    }

    bool haveInProgress = false;
    EpisodeEntry bestInProgress;
    for (const EpisodeEntry &entry : ordered) {
        if (entry.m_isPlayed || entry.m_positionMs <= 0) {
            continue;
        }
        if (!haveInProgress || anchorIsMoreRecent(entry, bestInProgress)) {
            bestInProgress = entry;
            haveInProgress = true;
        }
    }
    if (haveInProgress) {
        return mergePreferredEpisode(bestInProgress.m_episode, preferredEpisode);
    }

    bool haveAnchor = false;
    EpisodeEntry bestAnchor;
    for (const EpisodeEntry &entry : ordered) {
        if (!entry.m_isPlayed) {
            continue;
        }
        if (!haveAnchor || anchorIsMoreRecent(entry, bestAnchor)) {
            bestAnchor = entry;
            haveAnchor = true;
        }
    }
    if (haveAnchor) {
        return resolveFromAnchor(ordered, bestAnchor.m_canonicalIndex, excludeItemId, preferredEpisode);
    }

    for (const EpisodeEntry &entry : ordered) {
        if (!entry.m_isSpecial && isEligibleNextCandidate(entry, excludeItemId)) {
            return mergePreferredEpisode(entry.m_episode, preferredEpisode);
        }
    }

    for (const EpisodeEntry &entry : ordered) {
        if (entry.m_isSpecial && isEligibleNextCandidate(entry, excludeItemId)) {
            return mergePreferredEpisode(entry.m_episode, preferredEpisode);
        }
    }

    return {};
}

}  // namespace NextEpisodeResolver
