#include <QtTest/QtTest>
#include "../src/network/LibraryService.h"

class LibraryItemQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void cacheKeySeparatesSearchFilterAndSort();
    void cacheKeySeparatesPaginationAndHeavyFields();
};

void LibraryItemQueryTest::cacheKeySeparatesSearchFilterAndSort()
{
    LibraryItemQuery base;
    base.parentId = "library";
    base.includeItemTypes = {"Movie"};

    LibraryItemQuery searched = base;
    searched.searchTerm = "matrix";

    LibraryItemQuery filtered = base;
    filtered.genres = {"Action"};

    LibraryItemQuery sorted = base;
    sorted.sortBy = "PremiereDate";
    sorted.sortOrder = "Descending";

    QVERIFY(base.cacheKey() != searched.cacheKey());
    QVERIFY(base.cacheKey() != filtered.cacheKey());
    QVERIFY(base.cacheKey() != sorted.cacheKey());
    QVERIFY(searched.cacheKey() != filtered.cacheKey());
    QVERIFY(filtered.cacheKey() != sorted.cacheKey());
}

void LibraryItemQueryTest::cacheKeySeparatesPaginationAndHeavyFields()
{
    LibraryItemQuery firstPage;
    firstPage.parentId = "library";
    firstPage.limit = 50;
    firstPage.searchTerm = "show";

    LibraryItemQuery nextPage = firstPage;
    nextPage.startIndex = 50;

    LibraryItemQuery unpaged = firstPage;
    unpaged.limit = 0;

    LibraryItemQuery differentLimit = firstPage;
    differentLimit.limit = 100;

    LibraryItemQuery lightFields = firstPage;
    lightFields.includeHeavyFields = false;

    QVERIFY(firstPage.cacheKey() != nextPage.cacheKey());
    QVERIFY(firstPage.cacheKey() != unpaged.cacheKey());
    QVERIFY(firstPage.cacheKey() != differentLimit.cacheKey());
    QVERIFY(firstPage.cacheKey() != lightFields.cacheKey());
}

QTEST_MAIN(LibraryItemQueryTest)
#include "LibraryItemQueryTest.moc"
