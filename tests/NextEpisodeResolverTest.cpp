#include <QtTest/QtTest>

#include "network/NextEpisodeResolver.h"

namespace {

QVariantMap buildEpisode(const QString &id,
                         int seasonNumber,
                         int episodeNumber,
                         bool watched = false,
                         qint64 positionMs = 0,
                         const QString &lastPlayedAt = QString(),
                         const QVariantMap &extra = QVariantMap())
{
    QVariantMap userState{
        {QStringLiteral("watched"), watched},
        {QStringLiteral("positionMs"), positionMs}
    };
    if (!lastPlayedAt.isEmpty()) {
        userState.insert(QStringLiteral("lastPlayedAt"), lastPlayedAt);
    }

    QVariantMap episode{
        {QStringLiteral("itemId"), id},
        {QStringLiteral("mediaType"), QStringLiteral("Episode")},
        {QStringLiteral("name"), id},
        {QStringLiteral("sortName"), id},
        {QStringLiteral("seriesId"), QStringLiteral("series-1")},
        {QStringLiteral("seriesName"), QStringLiteral("Series One")},
        {QStringLiteral("parentIndexNumber"), seasonNumber},
        {QStringLiteral("indexNumber"), episodeNumber},
        {QStringLiteral("userState"), userState}
    };

    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        episode.insert(it.key(), it.value());
    }
    return episode;
}

QString resolvedId(const QVariantList &episodes,
                   const QString &excludeItemId = QString(),
                   const QVariantMap &preferredEpisode = QVariantMap())
{
    return NextEpisodeResolver::resolveBestNextEpisode(episodes, excludeItemId, preferredEpisode)
        .value(QStringLiteral("itemId")).toString();
}

} // namespace

class NextEpisodeResolverTest : public QObject
{
    Q_OBJECT

private slots:
    void resolvesEpisodeAfterMostRecentPlayedAnchor();
    void prefersMostRecentInProgressEpisode();
    void skipsExplicitExcludedAnchorToFollowingEpisode();
    void placesSpecialsUsingPlacementFields();
    void resolvesSpecialAfterSeasonFinaleWhenExcludedAnchorIsSeasonEpisode();
    void resolvesSpecialAfterSeasonFinaleWhenSpecialAndNextSeasonAreBothUnplayed();
    void fallsThroughToNextSeasonOnlyAfterAfterSeasonSpecialIsPlayed();
    void supportsPlacementDrivenSpecialEvenIfPayloadShapeIsSparse();
    void ignoresVirtualEpisodes();
    void fallsBackToFirstRegularEpisodeWithoutHistory();
    void fallsBackToSpecialOnlyWhenNoRegularEpisodesRemain();
    void prefersPreferredPayloadWhenIdsMatch();
    void preservesCanonicalUserStateWhenMergingPreferredPayload();
};

void NextEpisodeResolverTest::resolvesEpisodeAfterMostRecentPlayedAnchor()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("special-movie"), 0, 1),
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true, 0, QStringLiteral("2026-03-01T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e2"), 1, 2),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, true, 0, QStringLiteral("2026-03-05T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e4"), 1, 4)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e4"));
}

void NextEpisodeResolverTest::prefersMostRecentInProgressEpisode()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true, 0, QStringLiteral("2026-03-01T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false, 9'000, QStringLiteral("2026-03-04T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e3"), 1, 3, false, 3'000, QStringLiteral("2026-03-03T18:00:00Z"))
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e2"));
}

void NextEpisodeResolverTest::skipsExplicitExcludedAnchorToFollowingEpisode()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false, 4'000),
        buildEpisode(QStringLiteral("s1e3"), 1, 3)
    };

    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e2")), QStringLiteral("s1e3"));
}

void NextEpisodeResolverTest::placesSpecialsUsingPlacementFields()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("special-before-e2"), 0, 1, false, 0, QString(), QVariantMap{
            {QStringLiteral("airsBeforeSeasonNumber"), 1},
            {QStringLiteral("airsBeforeEpisodeNumber"), 2}
        }),
        buildEpisode(QStringLiteral("s1e2"), 1, 2),
        buildEpisode(QStringLiteral("special-after-s1"), 0, 2, false, 0, QString(), QVariantMap{
            {QStringLiteral("airsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("special-before-e2"));
    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e2")), QStringLiteral("special-after-s1"));
}

void NextEpisodeResolverTest::resolvesSpecialAfterSeasonFinaleWhenExcludedAnchorIsSeasonEpisode()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e22"), 1, 22, true),
        buildEpisode(QStringLiteral("special-after-s1"), 0, 1, false, 0, QString(), QVariantMap{
            {QStringLiteral("airsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1)
    };

    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e22")), QStringLiteral("special-after-s1"));
}

void NextEpisodeResolverTest::resolvesSpecialAfterSeasonFinaleWhenSpecialAndNextSeasonAreBothUnplayed()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e21"), 1, 21, true, 0, QStringLiteral("2026-03-01T18:00:00Z")),
        buildEpisode(QStringLiteral("s1e22"), 1, 22, true, 0, QStringLiteral("2026-03-05T18:00:00Z")),
        buildEpisode(QStringLiteral("special-after-s1"), 0, 1, false, 0, QString(), QVariantMap{
            {QStringLiteral("airsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("special-after-s1"));
}

void NextEpisodeResolverTest::fallsThroughToNextSeasonOnlyAfterAfterSeasonSpecialIsPlayed()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e22"), 1, 22, true, 0, QStringLiteral("2026-03-05T18:00:00Z")),
        buildEpisode(QStringLiteral("special-after-s1"), 0, 1, true, 0, QStringLiteral("2026-03-06T18:00:00Z"), QVariantMap{
            {QStringLiteral("airsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s2e1"));
}

void NextEpisodeResolverTest::supportsPlacementDrivenSpecialEvenIfPayloadShapeIsSparse()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e22"), 1, 22, true),
        buildEpisode(QStringLiteral("special-sparse"), 3, 1, false, 0, QString(), QVariantMap{
            {QStringLiteral("airsAfterSeasonNumber"), 1}
        }),
        buildEpisode(QStringLiteral("s2e1"), 2, 1)
    };

    QCOMPARE(resolvedId(episodes, QStringLiteral("s1e22")), QStringLiteral("special-sparse"));
}

void NextEpisodeResolverTest::ignoresVirtualEpisodes()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("virtual-s1e2"), 1, 2, false, 0, QString(), QVariantMap{
            {QStringLiteral("locationType"), QStringLiteral("Virtual")}
        }),
        buildEpisode(QStringLiteral("s1e3"), 1, 3)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e3"));
}

void NextEpisodeResolverTest::fallsBackToFirstRegularEpisodeWithoutHistory()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("late-special"), 0, 1),
        buildEpisode(QStringLiteral("s1e1"), 1, 1),
        buildEpisode(QStringLiteral("s1e2"), 1, 2)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("s1e1"));
}

void NextEpisodeResolverTest::fallsBackToSpecialOnlyWhenNoRegularEpisodesRemain()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, true),
        buildEpisode(QStringLiteral("special-1"), 0, 1)
    };

    QCOMPARE(resolvedId(episodes), QStringLiteral("special-1"));
}

void NextEpisodeResolverTest::prefersPreferredPayloadWhenIdsMatch()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2)
    };
    const QVariantMap preferred{
        {QStringLiteral("itemId"), QStringLiteral("s1e2")},
        {QStringLiteral("path"), QStringLiteral("/preferred/path.mkv")}
    };

    const QVariantMap resolved =
        NextEpisodeResolver::resolveBestNextEpisode(episodes, QString(), preferred);
    QCOMPARE(resolved.value(QStringLiteral("itemId")).toString(), QStringLiteral("s1e2"));
    QCOMPARE(resolved.value(QStringLiteral("path")).toString(), QStringLiteral("/preferred/path.mkv"));
}

void NextEpisodeResolverTest::preservesCanonicalUserStateWhenMergingPreferredPayload()
{
    const QVariantList episodes{
        buildEpisode(QStringLiteral("s1e1"), 1, 1, true),
        buildEpisode(QStringLiteral("s1e2"), 1, 2, false, 5'000,
                     QStringLiteral("2026-03-05T18:00:00Z"))
    };
    const QVariantMap preferred{
        {QStringLiteral("itemId"), QStringLiteral("s1e2")},
        {QStringLiteral("path"), QStringLiteral("/preferred/path.mkv")},
        {QStringLiteral("userState"), QVariantMap{
            {QStringLiteral("watched"), true},
            {QStringLiteral("positionMs"), 0},
            {QStringLiteral("lastPlayedAt"), QStringLiteral("2026-03-01T18:00:00Z")}
        }}
    };

    const QVariantMap resolved =
        NextEpisodeResolver::resolveBestNextEpisode(episodes, QString(), preferred);
    QCOMPARE(resolved.value(QStringLiteral("path")).toString(), QStringLiteral("/preferred/path.mkv"));

    const QVariantMap userState = resolved.value(QStringLiteral("userState")).toMap();
    QCOMPARE(userState.value(QStringLiteral("watched")).toBool(), false);
    QCOMPARE(userState.value(QStringLiteral("positionMs")).toLongLong(), 5'000LL);
    QCOMPARE(userState.value(QStringLiteral("lastPlayedAt")).toString(),
             QStringLiteral("2026-03-05T18:00:00Z"));
}

QTEST_MAIN(NextEpisodeResolverTest)

#include "NextEpisodeResolverTest.moc"
