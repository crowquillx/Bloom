#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include "../src/utils/LibraryCacheStore.h"

class LibraryCacheStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void replaceAllAndRead();
    void upsertWithOffsets();
    void upsertWithPrune();
    void freshnessDetection();
};

static QString tempDbPath(QTemporaryDir &dir)
{
    if (!dir.isValid()) {
        QTest::qFail("Temporary directory is not valid", __FILE__, __LINE__);
        return QString();
    }
    return dir.filePath("library_cache.db");
}

void LibraryCacheStoreTest::replaceAllAndRead()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QJsonArray items;
    items.append(QJsonObject{{"Id", "one"}, {"Name", "One"}});

    QVERIFY(store.replaceAll("parent", items, 1));

    auto slice = store.read("parent");
    QVERIFY(slice.hasData());
    QCOMPARE(slice.items.size(), 1);
    QCOMPARE(slice.totalCount, 1);
    QVERIFY(slice.isFresh(600000));
    QCOMPARE(slice.items.first().toObject().value("Id").toString(), QStringLiteral("one"));
}

void LibraryCacheStoreTest::upsertWithOffsets()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QJsonArray initial;
    initial.append(QJsonObject{{"Id", "one"}, {"Name", "One"}});
    QVERIFY(store.replaceAll("parent", initial, 1));

    QJsonArray next;
    next.append(QJsonObject{{"Id", "two"}, {"Name", "Two"}});
    QVERIFY(store.upsertItems("parent", next, 2, false, 1));

    auto slice = store.read("parent");
    QCOMPARE(slice.items.size(), 2);
    QCOMPARE(slice.items.at(0).toObject().value("Id").toString(), QStringLiteral("one"));
    QCOMPARE(slice.items.at(1).toObject().value("Id").toString(), QStringLiteral("two"));
    QCOMPARE(slice.totalCount, 2);
}

void LibraryCacheStoreTest::upsertWithPrune()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QJsonArray initial;
    initial.append(QJsonObject{{"Id", "one"}, {"Name", "One"}});
    initial.append(QJsonObject{{"Id", "two"}, {"Name", "Two"}});
    QVERIFY(store.replaceAll("parent", initial, 2));

    QJsonArray latest;
    latest.append(QJsonObject{{"Id", "two"}, {"Name", "Two"}});
    QVERIFY(store.upsertItems("parent", latest, 1, true, 0));

    auto slice = store.read("parent");
    QCOMPARE(slice.items.size(), 1);
    QCOMPARE(slice.items.first().toObject().value("Id").toString(), QStringLiteral("two"));
    QCOMPARE(slice.totalCount, 1);
}

void LibraryCacheStoreTest::freshnessDetection()
{
    QTemporaryDir dir;
    QString db = tempDbPath(dir);
    LibraryCacheStore store(db, 100);
    QVERIFY(store.open());

    QJsonArray items;
    items.append(QJsonObject{{"Id", "one"}, {"Name", "One"}});
    QVERIFY(store.replaceAll("parent", items, 1));

    // Force stale timestamp
    QSqlDatabase dbConn = QSqlDatabase::addDatabase("QSQLITE", "stale_test");
    dbConn.setDatabaseName(db);
    QVERIFY(dbConn.open());
    QSqlQuery q(dbConn);
    QVERIFY(q.exec("UPDATE library_meta SET updated_at = 0"));
    dbConn.close();
    QSqlDatabase::removeDatabase("stale_test");

    auto slice = store.read("parent");
    QVERIFY(!slice.isFresh(50));
}

QTEST_MAIN(LibraryCacheStoreTest)
#include "LibraryCacheStoreTest.moc"






