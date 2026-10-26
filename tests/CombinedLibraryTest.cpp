#include <QtTest/QtTest>

#include "models/CombinedLibraryCursor.h"
#include "models/LibraryKinds.h"

namespace {

QVariantMap item(const QString &id, const QString &title, int year = 0,
                 const QString &dateAdded = QString())
{
    return {{QStringLiteral("itemId"), id},
            {QStringLiteral("name"), title},
            {QStringLiteral("sortName"), title},
            {QStringLiteral("productionYear"), year},
            {QStringLiteral("dateAdded"), dateAdded}};
}

QVariantList items(std::initializer_list<QVariantMap> values)
{
    QVariantList list;
    for (const QVariantMap &value : values) {
        list.append(value);
    }
    return list;
}

QStringList ids(const QVariantList &values)
{
    QStringList result;
    for (const QVariant &value : values) {
        result.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    return result;
}

QVariantMap view(const QString &id, const QString &collectionType)
{
    return {{QStringLiteral("itemId"), id}, {QStringLiteral("collectionType"), collectionType}};
}

} // namespace

class CombinedLibraryTest : public QObject
{
    Q_OBJECT

private slots:
    void supportsOnlyMovieAndSeriesLibraries();
    void mergesByTitleAcrossLibraries();
    void waitsForEveryLibraryBeforeReleasing();
    void pagesUntilEveryLibraryIsDrained();
    void sortsByYearDescending();
    void sortsByDateAdded();
    void failedLibraryDoesNotBlockOthers();
    void randomInterleavesWithoutWaiting();
};

void CombinedLibraryTest::supportsOnlyMovieAndSeriesLibraries()
{
    // Jellyfin and Silo names for the two supported kinds; everything else drops.
    QCOMPARE(LibraryKinds::kindForCollectionType(QStringLiteral("movies")), LibraryKinds::Movies);
    QCOMPARE(LibraryKinds::kindForCollectionType(QStringLiteral("tvshows")), LibraryKinds::Series);
    QCOMPARE(LibraryKinds::kindForCollectionType(QStringLiteral("series")), LibraryKinds::Series);
    QCOMPARE(LibraryKinds::kindForCollectionType(QStringLiteral("TVShows")), LibraryKinds::Series);
    for (const QString &type : {QStringLiteral("music"), QStringLiteral("homevideos"),
                                QStringLiteral("boxsets"), QStringLiteral("mixed"),
                                QStringLiteral("audiobooks"), QString()}) {
        QVERIFY2(LibraryKinds::kindForCollectionType(type).isEmpty(), qPrintable(type));
    }

    const QVariantList views{view(QStringLiteral("m"), QStringLiteral("movies")),
                             view(QStringLiteral("music"), QStringLiteral("music")),
                             view(QStringLiteral("s"), QStringLiteral("series")),
                             view(QStringLiteral("box"), QStringLiteral("boxsets"))};
    const QVariantList supported = LibraryKinds::supportedLibraries(views);
    QCOMPARE(ids(supported), QStringList({QStringLiteral("m"), QStringLiteral("s")}));
    QCOMPARE(supported.at(0).toMap().value(QStringLiteral("libraryKind")).toString(),
             LibraryKinds::Movies);
    QCOMPARE(supported.at(1).toMap().value(QStringLiteral("libraryKind")).toString(),
             LibraryKinds::Series);
    QCOMPARE(LibraryKinds::unsupportedLibraryIds(views),
             QStringList({QStringLiteral("music"), QStringLiteral("box")}));
}

void CombinedLibraryTest::mergesByTitleAcrossLibraries()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QString(), QString());
    QCOMPARE(cursor.librariesNeedingPages(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    // Asked once until the page arrives.
    QVERIFY(cursor.librariesNeedingPages().isEmpty());

    cursor.addPage(QStringLiteral("a"), items({item("a1", "Alien"), item("a2", "Heat")}), 2);
    cursor.addPage(QStringLiteral("b"), items({item("b1", "alpha"), item("b2", "Brazil")}), 2);
    QCOMPARE(cursor.totalCount(), 4);
    // Titles compare case-folded: alien, alpha, brazil, heat.
    QCOMPARE(ids(cursor.take(10)), QStringList({"a1", "b1", "b2", "a2"}));
    QVERIFY(cursor.finished());
}

void CombinedLibraryTest::waitsForEveryLibraryBeforeReleasing()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("title"), QString());
    cursor.librariesNeedingPages();
    cursor.addPage(QStringLiteral("a"), items({item("a1", "Alien")}), 1);
    // b has not answered yet, so nothing can be released.
    QVERIFY(cursor.take(5).isEmpty());
    cursor.addPage(QStringLiteral("b"), items({item("b1", "Aardvark")}), 1);
    QCOMPARE(ids(cursor.take(5)), QStringList({"b1", "a1"}));
}

void CombinedLibraryTest::pagesUntilEveryLibraryIsDrained()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("title"), QString());
    cursor.librariesNeedingPages();
    cursor.addPage(QStringLiteral("a"), items({item("a1", "A"), item("a2", "C")}), 4);
    cursor.addPage(QStringLiteral("b"), items({item("b1", "B")}), 1);

    // a's buffer runs dry before its last items arrive.
    QCOMPARE(ids(cursor.take(10)), QStringList({"a1", "b1", "a2"}));
    QCOMPARE(cursor.librariesNeedingPages(), QStringList({QStringLiteral("a")}));
    QCOMPARE(cursor.nextOffset(QStringLiteral("a")), 2);
    cursor.addPage(QStringLiteral("a"), items({item("a3", "D"), item("a4", "E")}), 4);
    QCOMPARE(ids(cursor.take(10)), QStringList({"a3", "a4"}));
    QVERIFY(cursor.finished());
    QCOMPARE(cursor.totalCount(), 5);
}

void CombinedLibraryTest::sortsByYearDescending()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("year"),
                 QStringLiteral("descending"));
    cursor.librariesNeedingPages();
    cursor.addPage(QStringLiteral("a"), items({item("a1", "X", 2024), item("a2", "Y", 1990)}), 2);
    cursor.addPage(QStringLiteral("b"), items({item("b1", "Z", 2001)}), 1);
    QCOMPARE(ids(cursor.take(10)), QStringList({"a1", "b1", "a2"}));
}

void CombinedLibraryTest::sortsByDateAdded()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("dateAdded"),
                 QStringLiteral("descending"));
    cursor.librariesNeedingPages();
    // Jellyfin sends seven fractional digits; Silo sends whole seconds.
    cursor.addPage(QStringLiteral("a"),
                   items({item("a1", "X", 0, "2026-09-01T16:32:40.9691452Z"),
                          item("a2", "Y", 0, "2026-09-01T16:32:40.5943954Z")}), 2);
    cursor.addPage(QStringLiteral("b"), items({item("b1", "Z", 0, "2026-09-01T16:32:40.7Z")}), 1);
    QCOMPARE(ids(cursor.take(10)), QStringList({"a1", "b1", "a2"}));
}

void CombinedLibraryTest::failedLibraryDoesNotBlockOthers()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("title"), QString());
    cursor.librariesNeedingPages();
    cursor.addPage(QStringLiteral("a"), items({item("a1", "A")}), 1);
    QVERIFY(cursor.take(5).isEmpty());
    cursor.pageFailed(QStringLiteral("b"));
    QCOMPARE(ids(cursor.take(5)), QStringList({"a1"}));
    QVERIFY(cursor.finished());
}

void CombinedLibraryTest::randomInterleavesWithoutWaiting()
{
    CombinedLibraryCursor cursor;
    cursor.reset({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("random"), QString());
    cursor.librariesNeedingPages();
    cursor.addPage(QStringLiteral("a"), items({item("a1", "Q"), item("a2", "R")}), 2);
    // Random order has nothing to preserve, so a's items flow before b answers.
    QCOMPARE(cursor.take(1).size(), 1);
    cursor.addPage(QStringLiteral("b"), items({item("b1", "S")}), 1);
    QCOMPARE(cursor.take(10).size(), 2);
    QVERIFY(cursor.finished());
}

QTEST_GUILESS_MAIN(CombinedLibraryTest)
#include "CombinedLibraryTest.moc"
