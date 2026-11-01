#include <QtTest/QtTest>

#include "models/ContinueWatching.h"

namespace {

QVariantMap item(const QString &itemId, qint64 positionMs, const QString &lastPlayedAt = QString(),
                 const QString &connectionId = QStringLiteral("connection-1"))
{
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("positionMs"), positionMs},
        {QStringLiteral("userState"), QVariantMap{
             {QStringLiteral("positionMs"), positionMs},
             {QStringLiteral("lastPlayedAt"), lastPlayedAt}}}
    };
}

QStringList refs(const QVariantList &items)
{
    QStringList result;
    for (const QVariant &value : items) {
        const QVariantMap map = value.toMap();
        result.append(map.value(QStringLiteral("connectionId")).toString() + QLatin1Char('/')
                      + map.value(QStringLiteral("itemId")).toString());
    }
    return result;
}

} // namespace

class ContinueWatchingTest : public QObject
{
    Q_OBJECT

private slots:
    void resumeItemsLeadByLastPlayed();
    void duplicatesKeepTheirFirstOccurrence();
    void sameItemIdOnTwoConnectionsStaysTwoItems();
    void undatedInProgressItemsFollowDatedOnes();
};

void ContinueWatchingTest::resumeItemsLeadByLastPlayed()
{
    // Jellyfin's resume list (a movie and an episode) and its Next Up.
    const QVariantList resume{
        item(QStringLiteral("movie-old"), 1000, QStringLiteral("2026-10-20T08:00:00.1234567Z")),
        item(QStringLiteral("episode-new"), 2000, QStringLiteral("2026-10-31T21:30:00Z")),
    };
    const QVariantList nextUp{
        item(QStringLiteral("episode-next-b"), 0),
        item(QStringLiteral("episode-mid"), 500, QStringLiteral("2026-10-25T12:00:00Z")),
        item(QStringLiteral("episode-next-a"), 0, QStringLiteral("2026-10-31T22:00:00Z")),
    };
    QCOMPARE(refs(ContinueWatching::combine(resume, nextUp)),
             (QStringList{QStringLiteral("connection-1/episode-new"),
                          QStringLiteral("connection-1/episode-mid"),
                          QStringLiteral("connection-1/movie-old"),
                          // Episodes to start keep Next Up's order, whatever
                          // their play date.
                          QStringLiteral("connection-1/episode-next-b"),
                          QStringLiteral("connection-1/episode-next-a")}));
}

void ContinueWatchingTest::duplicatesKeepTheirFirstOccurrence()
{
    QVariantMap fromResume = item(QStringLiteral("episode-1"), 3000,
                                  QStringLiteral("2026-10-31T10:00:00Z"));
    fromResume.insert(QStringLiteral("name"), QStringLiteral("resume copy"));
    QVariantMap fromNextUp = item(QStringLiteral("episode-1"), 0);
    fromNextUp.insert(QStringLiteral("name"), QStringLiteral("next up copy"));

    const QVariantList combined = ContinueWatching::combine(
        {fromResume}, {fromNextUp, item(QStringLiteral("episode-2"), 0)});
    QCOMPARE(refs(combined), (QStringList{QStringLiteral("connection-1/episode-1"),
                                          QStringLiteral("connection-1/episode-2")}));
    QCOMPARE(combined.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("resume copy"));

    // Items without an ID are dropped.
    QVERIFY(ContinueWatching::order({item(QString(), 100)}).isEmpty());
}

void ContinueWatchingTest::sameItemIdOnTwoConnectionsStaysTwoItems()
{
    const QVariantList combined = ContinueWatching::order({
        item(QStringLiteral("movie-1"), 100, QStringLiteral("2026-10-01T00:00:00Z"),
             QStringLiteral("alice")),
        item(QStringLiteral("movie-1"), 100, QStringLiteral("2026-10-02T00:00:00Z"),
             QStringLiteral("bob")),
    });
    QCOMPARE(refs(combined), (QStringList{QStringLiteral("bob/movie-1"),
                                          QStringLiteral("alice/movie-1")}));
}

void ContinueWatchingTest::undatedInProgressItemsFollowDatedOnes()
{
    const QVariantList combined = ContinueWatching::order({
        item(QStringLiteral("undated-a"), 100),
        item(QStringLiteral("dated"), 100, QStringLiteral("2026-10-01T00:00:00Z")),
        item(QStringLiteral("unparsable"), 100, QStringLiteral("yesterday")),
        item(QStringLiteral("up-next"), 0),
    });
    QCOMPARE(refs(combined), (QStringList{QStringLiteral("connection-1/dated"),
                                          QStringLiteral("connection-1/undated-a"),
                                          QStringLiteral("connection-1/unparsable"),
                                          QStringLiteral("connection-1/up-next")}));
}

QTEST_MAIN(ContinueWatchingTest)
#include "ContinueWatchingTest.moc"
