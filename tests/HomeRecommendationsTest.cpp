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
    QList<Category> categories;
    for (int i = 0; i < HomeRecommendations::MaxRows + 3; ++i) {
        categories.append(Category{QStringLiteral("SimilarToRecentlyPlayed"),
                                   QStringLiteral("Movie %1").arg(i), movies(many)});
    }
    // A merged row stays within the card cap too.
    categories.append(Category{QStringLiteral("SimilarToRecentlyPlayed"),
                               QStringLiteral("Movie 0"), movies({QStringLiteral("extra")})});
    const QVariantList rows = HomeRecommendations::rows({categories});
    QCOMPARE(rows.size(), HomeRecommendations::MaxRows);
    QCOMPARE(itemIds(rows.first()).size(), HomeRecommendations::MaxItems);
    QVERIFY(!itemIds(rows.first()).contains(QStringLiteral("extra")));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Because you watched Movie %1").arg(HomeRecommendations::MaxRows - 1));
}

QTEST_GUILESS_MAIN(HomeRecommendationsTest)
#include "HomeRecommendationsTest.moc"
