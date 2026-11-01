#include <QtTest/QtTest>

#include "models/HomeRecommendations.h"

namespace {

using HomeRecommendations::Category;

QVariantMap item(const QString &itemId, const QString &mediaType = QStringLiteral("Movie"))
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("mediaType"), mediaType}
    };
}

QVariantList movies(const QStringList &itemIds)
{
    QVariantList result;
    for (const QString &itemId : itemIds) {
        result.append(item(itemId));
    }
    return result;
}

QStringList itemIds(const QVariant &row)
{
    QStringList result;
    for (const QVariant &value : row.toMap().value(QStringLiteral("items")).toList()) {
        result.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    return result;
}

QStringList titles(const QVariantList &rows)
{
    QStringList result;
    for (const QVariant &row : rows) {
        result.append(row.toMap().value(QStringLiteral("title")).toString());
    }
    return result;
}

} // namespace

class HomeRecommendationsTest : public QObject
{
    Q_OBJECT

private slots:
    void titlesFollowTheReasonAndBaseline();
    void rowsKeepServerOrderAndShape();
    void onlyMovieCardsAreKept();
    void emptyAndUntitledRowsAreDropped();
    void sameRowFromTwoLibrariesIsMerged();
    void rowsAndCardsAreCapped();
    void similarRowsFollowTheServerVersion();
    void identicalSimilarRowsAreDropped();
    void onlyAllowedItemsAreKept();
    void readsAreReusedForTheirLifetime();
    void itemIdsAreListedOnce();
};

void HomeRecommendationsTest::titlesFollowTheReasonAndBaseline()
{
    using HomeRecommendations::title;
    QCOMPARE(title(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat")),
             QStringLiteral("Because you watched Heat"));
    QCOMPARE(title(QStringLiteral("SimilarToLikedItem"), QStringLiteral("Heat")),
             QStringLiteral("More like Heat"));
    QCOMPARE(title(QStringLiteral("HasDirectorFromRecentlyPlayed"), QStringLiteral("Michael Mann")),
             QStringLiteral("Directed by Michael Mann"));
    QCOMPARE(title(QStringLiteral("HasLikedDirector"), QStringLiteral("Michael Mann")),
             QStringLiteral("Directed by Michael Mann"));
    QCOMPARE(title(QStringLiteral("HasActorFromRecentlyPlayed"), QStringLiteral("Al Pacino")),
             QStringLiteral("Starring Al Pacino"));
    QCOMPARE(title(QStringLiteral("HasLikedActor"), QStringLiteral(" Al Pacino ")),
             QStringLiteral("Starring Al Pacino"));
    // A blank baseline or a type Bloom does not know has no title.
    QVERIFY(title(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("  ")).isEmpty());
    QVERIFY(title(QStringLiteral("SomethingNew"), QStringLiteral("Heat")).isEmpty());
}

void HomeRecommendationsTest::rowsKeepServerOrderAndShape()
{
    const QVariantList rows = HomeRecommendations::rows({
        {Category{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                  movies({QStringLiteral("m1"), QStringLiteral("m2")})},
         Category{QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
                  movies({QStringLiteral("m3")})}},
        {Category{QStringLiteral("SimilarToLikedItem"), QStringLiteral("Alien"),
                  movies({QStringLiteral("m4")})}},
    });
    QCOMPARE(titles(rows), (QStringList{QStringLiteral("Because you watched Heat"),
                                        QStringLiteral("Starring Al Pacino"),
                                        QStringLiteral("More like Alien")}));
    const QVariantMap first = rows.first().toMap();
    QCOMPARE(first.value(QStringLiteral("sectionId")).toString(),
             QStringLiteral("bloom:recommendations:SimilarToRecentlyPlayed:heat"));
    QCOMPARE(itemIds(first), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    // Section IDs stay the same across reads, so a refresh updates in place.
    QCOMPARE(HomeRecommendations::rows({{Category{QStringLiteral("SimilarToRecentlyPlayed"),
                                                  QStringLiteral("Heat"),
                                                  movies({QStringLiteral("m9")})}}})
                 .first().toMap().value(QStringLiteral("sectionId")),
             first.value(QStringLiteral("sectionId")));
}

void HomeRecommendationsTest::onlyMovieCardsAreKept()
{
    QVariantList items = movies({QStringLiteral("m1")});
    items.append(item(QStringLiteral("s1"), QStringLiteral("Series")));
    items.append(item(QStringLiteral("e1"), QStringLiteral("Episode")));
    items.append(item(QStringLiteral("b1"), QStringLiteral("BoxSet")));
    items.append(item(QString()));
    items.append(item(QStringLiteral("m1")));
    items.append(item(QStringLiteral("m2")));
    const QVariantList rows = HomeRecommendations::rows({
        {Category{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"), items}}});
    QCOMPARE(rows.size(), 1);
    // Each movie once.
    QCOMPARE(itemIds(rows.first()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
}

void HomeRecommendationsTest::emptyAndUntitledRowsAreDropped()
{
    const QVariantList rows = HomeRecommendations::rows({
        {Category{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"), {}},
         Category{QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
                  QVariantList{item(QStringLiteral("s1"), QStringLiteral("Series"))}},
         Category{QStringLiteral("SimilarToLikedItem"), QString(),
                  movies({QStringLiteral("m1")})},
         Category{QStringLiteral("Unknown"), QStringLiteral("Heat"),
                  movies({QStringLiteral("m2")})},
         Category{QStringLiteral("HasLikedDirector"), QStringLiteral("Ridley Scott"),
                  movies({QStringLiteral("m3")})}},
        {},
    });
    QCOMPARE(titles(rows), QStringList{QStringLiteral("Directed by Ridley Scott")});
    QVERIFY(HomeRecommendations::rows({}).isEmpty());
}

void HomeRecommendationsTest::sameRowFromTwoLibrariesIsMerged()
{
    // The same reason and baseline (case aside) from a second library adds
    // its new cards to the first row; another reason is another row.
    const QVariantList rows = HomeRecommendations::rows({
        {Category{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                  movies({QStringLiteral("m1"), QStringLiteral("m2")})}},
        {Category{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("HEAT "),
                  movies({QStringLiteral("m2"), QStringLiteral("m3")})},
         Category{QStringLiteral("SimilarToLikedItem"), QStringLiteral("Heat"),
                  movies({QStringLiteral("m4")})}},
    });
    QCOMPARE(titles(rows), (QStringList{QStringLiteral("Because you watched Heat"),
                                        QStringLiteral("More like Heat")}));
    QCOMPARE(itemIds(rows.first()),
             (QStringList{QStringLiteral("m1"), QStringLiteral("m2"), QStringLiteral("m3")}));
}

void HomeRecommendationsTest::rowsAndCardsAreCapped()
{
    QStringList many;
    for (int i = 0; i < HomeRecommendations::MaxItems + 5; ++i) {
        many.append(QStringLiteral("m%1").arg(i));
    }
    // Person rows: identical "similar to" rows would be dropped.
    QList<Category> categories;
    for (int i = 0; i < HomeRecommendations::MaxRows + 3; ++i) {
        categories.append(Category{QStringLiteral("HasLikedActor"),
                                   QStringLiteral("Movie %1").arg(i), movies(many)});
    }
    // A merged row stays within the card cap too.
    categories.append(Category{QStringLiteral("HasLikedActor"),
                               QStringLiteral("Movie 0"), movies({QStringLiteral("extra")})});
    const QVariantList rows = HomeRecommendations::rows({categories});
    QCOMPARE(rows.size(), HomeRecommendations::MaxRows);
    QCOMPARE(itemIds(rows.first()).size(), HomeRecommendations::MaxItems);
    QVERIFY(!itemIds(rows.first()).contains(QStringLiteral("extra")));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Starring Movie %1").arg(HomeRecommendations::MaxRows - 1));
}

void HomeRecommendationsTest::similarRowsFollowTheServerVersion()
{
    using HomeRecommendations::similarRowsWork;
    // Jellyfin 10.11 queries "similar to" rows without their baseline.
    QVERIFY(!similarRowsWork(QStringLiteral("10.11.0")));
    QVERIFY(!similarRowsWork(QStringLiteral("10.11.10")));
    QVERIFY(!similarRowsWork(QStringLiteral("10.11.11")));
    // 10.10 and older, and 12.0 and newer, compare with it.
    QVERIFY(similarRowsWork(QStringLiteral("10.10.7")));
    QVERIFY(similarRowsWork(QStringLiteral("10.9.11")));
    QVERIFY(similarRowsWork(QStringLiteral("12.0.0")));
    QVERIFY(similarRowsWork(QStringLiteral("12.4")));
    QVERIFY(similarRowsWork(QStringLiteral("13.1.2")));
    // Unknown: not trusted.
    QVERIFY(!similarRowsWork(QString()));
    QVERIFY(!similarRowsWork(QStringLiteral("unknown")));

    const QList<QList<Category>> categories{{
        {QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
         movies({QStringLiteral("m1")})},
        {QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
         movies({QStringLiteral("m2")})},
        {QStringLiteral("SimilarToLikedItem"), QStringLiteral("Alien"),
         movies({QStringLiteral("m3")})},
        {QStringLiteral("HasDirectorFromRecentlyPlayed"), QStringLiteral("Michael Mann"),
         movies({QStringLiteral("m4")})}}};
    HomeRecommendations::Filter filter;
    filter.similarRowsWork = false;
    // People rows stay.
    QCOMPARE(titles(HomeRecommendations::rows(categories, filter)),
             (QStringList{QStringLiteral("Starring Al Pacino"),
                          QStringLiteral("Directed by Michael Mann")}));
    filter.similarRowsWork = true;
    QCOMPARE(HomeRecommendations::rows(categories, filter).size(), 4);
}

void HomeRecommendationsTest::identicalSimilarRowsAreDropped()
{
    // A server that ignores the baseline answers the same items for each
    // "similar to" row, in any order.
    const QList<QList<Category>> categories{
        {{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
          movies({QStringLiteral("m1"), QStringLiteral("m2")})},
         {QStringLiteral("SimilarToLikedItem"), QStringLiteral("Alien"),
          movies({QStringLiteral("m2"), QStringLiteral("m1")})},
         {QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Ronin"),
          movies({QStringLiteral("m5")})}},
        {{QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
          movies({QStringLiteral("m5")})},
         {QStringLiteral("HasLikedDirector"), QStringLiteral("Ridley Scott"),
          movies({QStringLiteral("m5")})}}};
    // Ronin matches a person row: dropped too. People rows stay even when
    // they match each other.
    QCOMPARE(titles(HomeRecommendations::rows(categories)),
             (QStringList{QStringLiteral("Starring Al Pacino"),
                          QStringLiteral("Directed by Ridley Scott")}));

    // The same title read from two libraries is one row, not a duplicate.
    const QList<QList<Category>> twoLibraries{
        {{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
          movies({QStringLiteral("m1"), QStringLiteral("m2")})}},
        {{QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
          movies({QStringLiteral("m1"), QStringLiteral("m2")})}}};
    const QVariantList merged = HomeRecommendations::rows(twoLibraries);
    QCOMPARE(titles(merged), QStringList{QStringLiteral("Because you watched Heat")});
    QCOMPARE(itemIds(merged.first()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
}

void HomeRecommendationsTest::onlyAllowedItemsAreKept()
{
    const QList<QList<Category>> categories{{
        {QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
         movies({QStringLiteral("m1"), QStringLiteral("hidden-1"), QStringLiteral("m2")})},
        {QStringLiteral("HasLikedDirector"), QStringLiteral("Michael Mann"),
         movies({QStringLiteral("hidden-2")})}}};
    HomeRecommendations::Filter filter;
    filter.allowedItemIds = QSet<QString>{QStringLiteral("m1"), QStringLiteral("m2")};
    const QVariantList rows = HomeRecommendations::rows(categories, filter);
    // Items of hidden libraries go; a row left empty goes with them.
    QCOMPARE(titles(rows), QStringList{QStringLiteral("Starring Al Pacino")});
    QCOMPARE(itemIds(rows.first()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));

    // An empty list keeps nothing; no list keeps everything.
    filter.allowedItemIds = QSet<QString>();
    QVERIFY(HomeRecommendations::rows(categories, filter).isEmpty());
    QCOMPARE(HomeRecommendations::rows(categories).size(), 2);
}

void HomeRecommendationsTest::readsAreReusedForTheirLifetime()
{
    using HomeRecommendations::cacheFresh;
    using HomeRecommendations::CacheLifetimeMs;
    const qint64 fetched = 1'000'000;
    QVERIFY(cacheFresh(fetched, fetched));
    QVERIFY(cacheFresh(fetched, fetched + CacheLifetimeMs - 1));
    QVERIFY(!cacheFresh(fetched, fetched + CacheLifetimeMs));
    // Never read, or read "in the future" (the clock moved back).
    QVERIFY(!cacheFresh(0, fetched));
    QVERIFY(!cacheFresh(fetched, fetched - 1));
    QCOMPARE(CacheLifetimeMs, 45LL * 60 * 1000);
}

void HomeRecommendationsTest::itemIdsAreListedOnce()
{
    const QList<QList<Category>> categories{
        {{QStringLiteral("HasLikedActor"), QStringLiteral("A"),
          movies({QStringLiteral("m1"), QStringLiteral("m2")})}},
        {{QStringLiteral("HasLikedActor"), QStringLiteral("B"),
          {item(QStringLiteral("m2")), item(QStringLiteral("s1"), QStringLiteral("Series")),
           item(QString())}}}};
    QCOMPARE(HomeRecommendations::itemIds(categories),
             (QStringList{QStringLiteral("m1"), QStringLiteral("m2"), QStringLiteral("s1")}));
}

QTEST_GUILESS_MAIN(HomeRecommendationsTest)
#include "HomeRecommendationsTest.moc"
