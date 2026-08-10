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
    void emptySnapshotPersists();
    void canonicalRowsPersistAndUpsert();
    void upsertWithOffsets();
    void rejectsNonContiguousOffset();
    void upsertReplacesPageRange();
    void movingExistingItemKeepsPositionsContiguous();
    void upsertWithPrune();
    void malformedRowsInvalidateSnapshot();
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
    items.append(QJsonObject{{"itemId", "one"}, {"name", "One"}});

    QVERIFY(store.replaceAll("parent", items, 1));

    auto slice = store.read("parent");
    QVERIFY(slice.hasData());
    QCOMPARE(slice.items.size(), 1);
    QCOMPARE(slice.totalCount, 1);
    QVERIFY(slice.isFresh(600000));
    QCOMPARE(slice.items.first().toObject().value("itemId").toString(), QStringLiteral("one"));
}

void LibraryCacheStoreTest::emptySnapshotPersists()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QVERIFY(store.replaceAll("empty-parent", QJsonArray(), 0));

    const auto slice = store.read("empty-parent");
    QVERIFY(slice.hasSnapshot());
    QVERIFY(slice.hasData());
    QVERIFY(slice.items.isEmpty());
    QCOMPARE(slice.totalCount, 0);
    QVERIFY(slice.isFresh(600000));
}

void LibraryCacheStoreTest::canonicalRowsPersistAndUpsert()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    const QJsonArray initial{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("one")},
                    {QStringLiteral("name"), QStringLiteral("One")}}
    };
    QVERIFY(store.replaceAll(QStringLiteral("parent"), initial, 1));

    const QJsonArray next{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("two")},
                    {QStringLiteral("name"), QStringLiteral("Two")}}
    };
    QVERIFY(store.upsertItems(QStringLiteral("parent"), next, 2, false, 1));

    const auto slice = store.read(QStringLiteral("parent"));
    QCOMPARE(slice.items.size(), 2);
    QCOMPARE(slice.items.at(0).toObject().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("one"));
    QCOMPARE(slice.items.at(1).toObject().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("two"));
    QCOMPARE(slice.totalCount, 2);
}

void LibraryCacheStoreTest::upsertWithOffsets()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QJsonArray initial;
    initial.append(QJsonObject{{"itemId", "one"}, {"name", "One"}});
    QVERIFY(store.replaceAll("parent", initial, 1));

    QJsonArray next;
    next.append(QJsonObject{{"itemId", "two"}, {"name", "Two"}});
    QVERIFY(store.upsertItems("parent", next, 2, false, 1));

    auto slice = store.read("parent");
    QCOMPARE(slice.items.size(), 2);
    QCOMPARE(slice.items.at(0).toObject().value("itemId").toString(), QStringLiteral("one"));
    QCOMPARE(slice.items.at(1).toObject().value("itemId").toString(), QStringLiteral("two"));
    QCOMPARE(slice.totalCount, 2);
}

void LibraryCacheStoreTest::rejectsNonContiguousOffset()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    const QJsonArray middlePage{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("middle")}}
    };
    QVERIFY(!store.upsertItems(
        QStringLiteral("parent"), middlePage, 101, false, 100));
    QVERIFY(!store.read(QStringLiteral("parent")).hasSnapshot());
}

void LibraryCacheStoreTest::upsertReplacesPageRange()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    const QJsonArray initial{
        QJsonObject{{"itemId", "one"}},
        QJsonObject{{"itemId", "two"}},
        QJsonObject{{"itemId", "three"}},
        QJsonObject{{"itemId", "four"}},
    };
    QVERIFY(store.replaceAll("parent", initial, 4));

    const QJsonArray replacement{
        QJsonObject{{"itemId", "new-two"}},
        QJsonObject{{"itemId", "new-three"}},
    };
    QVERIFY(store.upsertItems("parent", replacement, 4, false, 1));

    const auto slice = store.read("parent");
    QCOMPARE(slice.items.size(), 4);
    QCOMPARE(slice.items.at(0).toObject().value("itemId").toString(),
             QStringLiteral("one"));
    QCOMPARE(slice.items.at(1).toObject().value("itemId").toString(),
             QStringLiteral("new-two"));
    QCOMPARE(slice.items.at(2).toObject().value("itemId").toString(),
             QStringLiteral("new-three"));
    QCOMPARE(slice.items.at(3).toObject().value("itemId").toString(),
             QStringLiteral("four"));
}

void LibraryCacheStoreTest::movingExistingItemKeepsPositionsContiguous()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    const QJsonArray initial{
        QJsonObject{{"itemId", "one"}},
        QJsonObject{{"itemId", "two"}},
        QJsonObject{{"itemId", "three"}},
        QJsonObject{{"itemId", "four"}},
        QJsonObject{{"itemId", "five"}},
    };
    QVERIFY(store.replaceAll("parent", initial, 5));

    const QJsonArray replacement{
        QJsonObject{{"itemId", "four"}},
        QJsonObject{{"itemId", "new-three"}},
    };
    QVERIFY(store.upsertItems("parent", replacement, 5, false, 1));

    const auto moved = store.read("parent");
    QCOMPARE(moved.items.size(), 4);
    QCOMPARE(moved.items.at(0).toObject().value("itemId").toString(),
             QStringLiteral("one"));
    QCOMPARE(moved.items.at(1).toObject().value("itemId").toString(),
             QStringLiteral("four"));
    QCOMPARE(moved.items.at(2).toObject().value("itemId").toString(),
             QStringLiteral("new-three"));
    QCOMPARE(moved.items.at(3).toObject().value("itemId").toString(),
             QStringLiteral("five"));

    // A later page can extend the compacted prefix; the moved row did not
    // leave a hidden gap that poisons subsequent pagination.
    const QJsonArray finalPage{
        QJsonObject{{"itemId", "new-five"}},
    };
    QVERIFY(store.upsertItems("parent", finalPage, 5, false, 4));
    const auto completed = store.read("parent");
    QCOMPARE(completed.items.size(), 5);
    QCOMPARE(completed.items.at(4).toObject().value("itemId").toString(),
             QStringLiteral("new-five"));
}

void LibraryCacheStoreTest::upsertWithPrune()
{
    QTemporaryDir dir;
    LibraryCacheStore store(tempDbPath(dir), 600000);
    QVERIFY(store.open());

    QJsonArray initial;
    initial.append(QJsonObject{{"itemId", "one"}, {"name", "One"}});
    initial.append(QJsonObject{{"itemId", "two"}, {"name", "Two"}});
    QVERIFY(store.replaceAll("parent", initial, 2));

    QJsonArray latest;
    latest.append(QJsonObject{{"itemId", "two"}, {"name", "Two"}});
    QVERIFY(store.upsertItems("parent", latest, 1, true, 0));

    auto slice = store.read("parent");
    QCOMPARE(slice.items.size(), 1);
    QCOMPARE(slice.items.first().toObject().value("itemId").toString(), QStringLiteral("two"));
    QCOMPARE(slice.totalCount, 1);
}

void LibraryCacheStoreTest::malformedRowsInvalidateSnapshot()
{
    QTemporaryDir dir;
    const QString dbPath = tempDbPath(dir);
    LibraryCacheStore store(dbPath, 600000);
    QVERIFY(store.open());

    const QJsonArray items{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("one")}}
    };
    QVERIFY(store.replaceAll(QStringLiteral("parent"), items, 1));

    const QString connectionName = QStringLiteral("library_cache_corruption_test");
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(dbPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "UPDATE library_cache SET json = '{broken' "
            "WHERE parent_id = 'parent'")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    const auto slice = store.read(QStringLiteral("parent"));
    QVERIFY(slice.hasSnapshot());
    QVERIFY(slice.decodeError);
    QVERIFY(!slice.hasData());
    QVERIFY(!slice.isFresh(600000));
    QVERIFY(slice.items.isEmpty());
}

void LibraryCacheStoreTest::freshnessDetection()
{
    QTemporaryDir dir;
    QString db = tempDbPath(dir);
    LibraryCacheStore store(db, 100);
    QVERIFY(store.open());

    QJsonArray items;
    items.append(QJsonObject{{"itemId", "one"}, {"name", "One"}});
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


