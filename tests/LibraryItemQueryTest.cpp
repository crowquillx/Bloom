#include <QtTest/QtTest>
#include "../src/models/MediaModels.h"
#include "../src/network/LibraryService.h"

class LibraryItemQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void cacheKeySeparatesSearchFilterAndSort();
    void cacheKeySeparatesPaginationAndHeavyFields();
    void artworkSourcePreservesConnectionIdentity();
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

void LibraryItemQueryTest::artworkSourcePreservesConnectionIdentity()
{
    LibraryService service(nullptr);
    const QString source = service.getCachedArtworkUrlForConnection(
        QStringLiteral("connection-2"),
        QStringLiteral("movie-1"),
        QStringLiteral("primary"),
        0,
        QStringLiteral("tag-1"),
        640);
    QVERIFY(source.startsWith(QStringLiteral("image://cached/")));

    const QString encodedKey = source.sliced(QStringLiteral("image://cached/").size());
    const QByteArray cacheKey = QByteArray::fromPercentEncoding(encodedKey.toUtf8());
    const Bloom::ArtworkRef artwork =
        Bloom::ArtworkRef::fromCacheKey(QString::fromUtf8(cacheKey));
    QVERIFY(artwork.isValid());
    QCOMPARE(artwork.connectionId, QStringLiteral("connection-2"));
    QCOMPARE(artwork.itemId, QStringLiteral("movie-1"));
    QCOMPARE(artwork.requestedWidth, 640);
}

QTEST_MAIN(LibraryItemQueryTest)
#include "LibraryItemQueryTest.moc"
