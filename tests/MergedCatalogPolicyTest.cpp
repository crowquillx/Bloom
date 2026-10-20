#include <QtTest/QtTest>

#include "profiles/MergedCatalogPolicy.h"

namespace {

QVariantMap item(const QString &connectionId,
                 const QString &itemId,
                 const QString &lastPlayedAt = QString())
{
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("userState"),
         QVariantMap{{QStringLiteral("lastPlayedAt"), lastPlayedAt}}},
    };
}

MergedSourceResult source(const QString &connectionId,
                          int priority,
                          const QVariantList &items,
                          bool succeeded = true)
{
    MergedSourceResult result;
    result.memberId = QStringLiteral("member-") + connectionId;
    result.connectionId = connectionId;
    result.label = QStringLiteral("Label ") + connectionId;
    result.priority = priority;
    result.succeeded = succeeded;
    result.items = items;
    if (!succeeded) {
        result.error = QStringLiteral("offline");
    }
    return result;
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

class MergedCatalogPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void viewsFollowMemberPriorityAndKeepProviderOrder();
    void failedSourcesDoNotHideHealthyResults();
    void searchInterleavesByRankWithoutDeduplication();
    void nextUpSortsByRecencyThenPriorityThenMediaRef();
    void sameServerUsersStayDistinct();
    void sourceLabelsNeverIncludeMemberIdentity();
};

void MergedCatalogPolicyTest::viewsFollowMemberPriorityAndKeepProviderOrder()
{
    DefaultMergedCatalogPolicy policy;
    // Arrival order differs from priority order.
    const QVariantList merged = policy.merge(MergedCatalogOperation::Views, {
        source(QStringLiteral("b"), 1, {item("b", "movies"), item("b", "shows")}),
        source(QStringLiteral("a"), 0, {item("a", "z-last"), item("a", "a-first")}),
    });
    QCOMPARE(refs(merged), QStringList({QStringLiteral("a/z-last"), QStringLiteral("a/a-first"),
                                        QStringLiteral("b/movies"), QStringLiteral("b/shows")}));
}

void MergedCatalogPolicyTest::failedSourcesDoNotHideHealthyResults()
{
    DefaultMergedCatalogPolicy policy;
    const QVariantList merged = policy.merge(MergedCatalogOperation::Search, {
        source(QStringLiteral("down"), 0, {item("down", "ghost")}, false),
        source(QStringLiteral("up"), 1, {item("up", "one")}),
    });
    QCOMPARE(refs(merged), QStringList({QStringLiteral("up/one")}));
}

void MergedCatalogPolicyTest::searchInterleavesByRankWithoutDeduplication()
{
    DefaultMergedCatalogPolicy policy;
    const QVariantList merged = policy.merge(MergedCatalogOperation::Search, {
        source(QStringLiteral("b"), 1, {item("b", "dune"), item("b", "arrival")}),
        source(QStringLiteral("a"), 0, {item("a", "dune"), item("a", "heat"), item("a", "up")}),
    });
    // The same title on two servers stays twice; each keeps its own MediaRef.
    QCOMPARE(refs(merged), QStringList({QStringLiteral("a/dune"), QStringLiteral("b/dune"),
                                        QStringLiteral("a/heat"), QStringLiteral("b/arrival"),
                                        QStringLiteral("a/up")}));
}

void MergedCatalogPolicyTest::nextUpSortsByRecencyThenPriorityThenMediaRef()
{
    DefaultMergedCatalogPolicy policy;
    const QVariantList merged = policy.merge(MergedCatalogOperation::NextUp, {
        source(QStringLiteral("b"), 1, {
            // Jellyfin emits seven fractional digits.
            item("b", "newest", QStringLiteral("2026-10-20T21:00:00.1234567Z")),
            item("b", "tie", QStringLiteral("2026-10-19T08:00:00Z")),
            item("b", "never"),
        }),
        source(QStringLiteral("a"), 0, {
            item("a", "older", QStringLiteral("2026-10-01T08:00:00Z")),
            item("a", "tie", QStringLiteral("2026-10-19T08:00:00Z")),
            item("a", "never"),
        }),
    });
    QCOMPARE(refs(merged), QStringList({
        QStringLiteral("b/newest"),
        QStringLiteral("a/tie"),     // same time: member priority wins
        QStringLiteral("b/tie"),
        QStringLiteral("a/older"),
        QStringLiteral("a/never"),   // unplayed last, still by priority
        QStringLiteral("b/never"),
    }));
}

void MergedCatalogPolicyTest::sameServerUsersStayDistinct()
{
    DefaultMergedCatalogPolicy policy;
    // Two accounts on one physical server return the same library IDs.
    MergedSourceResult alice = source(QStringLiteral("alice-conn"), 0, {item("alice-conn", "lib-1")});
    alice.label = QStringLiteral("Alice · Media");
    MergedSourceResult bob = source(QStringLiteral("bob-conn"), 1, {item("bob-conn", "lib-1")});
    bob.label = QStringLiteral("Bob · Media");
    const QVariantList merged = policy.merge(MergedCatalogOperation::Views, {alice, bob});
    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("Alice · Media"));
    QCOMPARE(merged.at(1).toMap().value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("Bob · Media"));
    QCOMPARE(merged.at(0).toMap().value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("alice-conn"));
    QCOMPARE(merged.at(1).toMap().value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("bob-conn"));
}

void MergedCatalogPolicyTest::sourceLabelsNeverIncludeMemberIdentity()
{
    DefaultMergedCatalogPolicy policy;
    const QVariantList merged = policy.merge(MergedCatalogOperation::Views, {
        source(QStringLiteral("a"), 0, {item("a", "lib")}),
    });
    const QVariantMap map = merged.first().toMap();
    QVERIFY(map.contains(QStringLiteral("sourceLabel")));
    QVERIFY(!map.contains(QStringLiteral("memberId")));
}

QTEST_GUILESS_MAIN(MergedCatalogPolicyTest)
#include "MergedCatalogPolicyTest.moc"
