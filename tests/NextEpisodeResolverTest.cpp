#include <QtTest/QtTest>

#include "network/NextEpisodeResolver.h"

namespace {

QJsonObject buildEpisode(const QString &id,
                         int seasonNumber,
                         int episodeNumber,
                         bool played = false,
                         qint64 playbackPositionTicks = 0,
                         const QString &lastPlayedDate = QString(),
                         const QJsonObject &extra = QJsonObject())
{
    QJsonObject episode{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Type"), QStringLiteral("Episode")},
        {QStringLiteral("Name"), id},
        {QStringLiteral("SortName"), id},
        {QStringLiteral("SeriesId"), QStringLiteral("series-1")},
        {QStringLiteral("SeriesName"), QStringLiteral("Series One")},
        {QStringLiteral("ParentIndexNumber"), seasonNumber},
        {QStringLiteral("IndexNumber"), episodeNumber},
        {QStringLiteral("UserData"), QJsonObject{
            {QStringLiteral("Played"), played},
            {QStringLiteral("PlaybackPositionTicks"), static_cast<double>(playbackPositionTicks)}
        }}
    };

    if (!lastPlayedDate.isEmpty()) {
        QJsonObject userData = episode.value(QStringLiteral("UserData")).toObject();
        userData.insert(QStringLiteral("LastPlayedDate"), lastPlayedDate);
        episode.insert(QStringLiteral("UserData"), userData);
    }

    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        episode.insert(it.key(), it.value());
    }

    return episode;
}

QString resolvedId(const QJsonArray &episodes,
                   const QString &excludeItemId = QString(),
                   const QJsonObject &preferredEpisode = QJsonObject())
{
    return NextEpisodeResolver::resolveBestNextEpisode(episodes, excludeItemId, preferredEpisode)
        .value(QStringLiteral("Id")).toString();
}

}  // namespace

class NextEpisodeResolverTest : public QObject
{
    Q_OBJECT

private slots:
    void resolvesEpisodeAfterMostRecentPlayedAnchor();
    void prefersMostRecentInProgressEpisode();
    void skipsExplicitExcludedAnchorToFollowingEpisode();
    void placesSpecialsUsingPlacementFields();
    void ignoresVirtualEpisodes();
    void fallsBackToFirstRegularEpisodeWithoutHistory();
    void fallsBackToSpecialOnlyWhenNoRegularEpisodesRemain();
    void prefersPreferredPayloadWhenIdsMatch();
    void preservesCanonicalUserDataWhenMergingPreferredPayload();
};

void NextEpisodeResolverTest::resolvesEpisodeAfterMostRecentPlayedAnchor()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("special-movie"), 0, 1, false),
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true, 0, QStringLiteral("2026-03-01T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, true, 0, QStringLiteral("2026-03-05T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e4"), 1, 4, false)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e4"));
}

void NextEpisodeResolverTest::prefersMostRecentInProgressEpisode()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true, 0, QStringLiteral("2026-03-01T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false, 9000, QStringLiteral("2026-03-04T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, false, 3000, QStringLiteral("2026-03-03T18:00:00Z"))
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e2"));
}

void NextEpisodeResolverTest::skipsExplicitExcludedAnchorToFollowingEpisode()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false, 4000),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, false)
    };

    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e2")), QStringLiteral("s1e3"));
}

void NextEpisodeResolverTest::placesSpecialsUsingPlacementFields()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("special-before-e2"), 0, 1, false, 0, QString(), QJsonObject{
            {QStringLiteral("AirsBeforeSeasonNumber"), 1},
            {QStringLiteral("AirsBeforeEpisodeNumber"), 2}
        }),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false),
        buildEpisode(QStringLiteral("special-after-s1"), 0, 2, false, 0, QString(), QJsonObject{
            {QStringLiteral("AirsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1, false)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("special-before-e2"));
    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e2")), QStringLiteral("special-after-s1"));
}

void NextEpisodeResolverTest::ignoresVirtualEpisodes()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("virtual-s1e2"), 1, 2, false, 0, QString(), QJsonObject{
            {QStringLiteral("LocationType"), QStringLiteral("Virtual")}
        }),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, false)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e3"));
}

void NextEpisodeResolverTest::fallsBackToFirstRegularEpisodeWithoutHistory()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("late-special"), 0, 1, false),
        buildEpisode(QStringLiteral("s1e1"), 1, 1, false),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e1"));
}

void NextEpisodeResolverTest::fallsBackToSpecialOnlyWhenNoRegularEpisodesRemain()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, true),
        buildEpisode(QStringLiteral("special-1"), 0, 1, false)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("special-1"));
}

void NextEpisodeResolverTest::prefersPreferredPayloadWhenIdsMatch()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false)
    };

    const QJsonObject preferred{
        {QStringLiteral("Id"), QStringLiteral("s1e2")},
        {QStringLiteral("Path"), QStringLiteral("/preferred/path.mkv")}
    };

    const QJsonObject resolved =
        NextEpisodeResolver::resolveBestNextEpisode(episodes, QString(), preferred);
    QCOMPARE(resolved.value(QStringLiteral("Id")).toString(), QStringLiteral("s1e2"));
    QCOMPARE(resolved.value(QStringLiteral("Path")).toString(), QStringLiteral("/preferred/path.mkv"));
}

void NextEpisodeResolverTest::preservesCanonicalUserDataWhenMergingPreferredPayload()
{
    const QJsonArray episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"),
                     1,
                     2,
                     true,
                     5000,
                     QStringLiteral("2026-03-05T18:00:00Z"))
    };

    const QJsonObject preferred{
        {QStringLiteral("Id"), QStringLiteral("s1e2")},
        {QStringLiteral("Path"), QStringLiteral("/preferred/path.mkv")},
        {QStringLiteral("UserData"), QJsonObject{
            {QStringLiteral("Played"), false},
            {QStringLiteral("PlaybackPositionTicks"), 0.0},
            {QStringLiteral("LastPlayedDate"), QStringLiteral("2026-03-01T18:00:00Z")}
        }}
    };

    const QJsonObject resolved =
        NextEpisodeResolver::resolveBestNextEpisode(episodes, QString(), preferred);
    QCOMPARE(resolved.value(QStringLiteral("Path")).toString(), QStringLiteral("/preferred/path.mkv"));

    const QJsonObject userData = resolved.value(QStringLiteral("UserData")).toObject();
    QCOMPARE(userData.value(QStringLiteral("Played")).toBool(), true);
    QCOMPARE(userData.value(QStringLiteral("PlaybackPositionTicks")).toInteger(), 5000LL);
    QCOMPARE(userData.value(QStringLiteral("LastPlayedDate")).toString(),
             QStringLiteral("2026-03-05T18:00:00Z"));
}

QTEST_MAIN(NextEpisodeResolverTest)

#include "NextEpisodeResolverTest.moc"
