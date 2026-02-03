#include <QtTest/QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
// Expose cache helpers for white-box tests
#define private public
#define protected public
#include "../src/viewmodels/SeriesDetailsViewModel.h"
#undef private
#undef protected

class SeriesDetailsCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void seriesCacheStoresAndLoadsFresh();
    void seriesCacheRespectsFreshness();
    void itemsCacheStoresAndLoadsFresh();
    void itemsCacheRespectsFreshness();
};

void SeriesDetailsCacheTest::seriesCacheStoresAndLoadsFresh()
{
    SeriesDetailsViewModel vm;
    const QString seriesId = "test-series";
    QJsonObject payload{{"Name", "Test"}, {"Id", seriesId}};

    vm.clearCacheForTest(seriesId);
    vm.storeSeriesCache(seriesId, payload);

    QJsonObject loaded;
    QVERIFY(vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/true));
    QCOMPARE(loaded.value("Name").toString(), QStringLiteral("Test"));
}

void SeriesDetailsCacheTest::seriesCacheRespectsFreshness()
{
    SeriesDetailsViewModel vm;
    const QString seriesId = "stale-series";
    QJsonObject payload{{"Name", "Old"}, {"Id", seriesId}};

    vm.clearCacheForTest(seriesId);
    vm.storeSeriesCache(seriesId, payload);

    // Overwrite on disk with stale timestamp
    QString path = vm.seriesCachePath(seriesId);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QJsonObject root;
    root.insert("timestamp", 0);  // clearly stale
    root.insert("data", payload);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    QJsonObject loaded;
    // Fresh load should fail due to stale timestamp
    QVERIFY(!vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/true));
    // Allowing stale should succeed
    QVERIFY(vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/false));
    QCOMPARE(loaded.value("Name").toString(), QStringLiteral("Old"));
}

void SeriesDetailsCacheTest::itemsCacheStoresAndLoadsFresh()
{
    SeriesDetailsViewModel vm;
    const QString parentId = "parent-1";
    QJsonArray items;
    items.append(QJsonObject{{"Id", "child"}, {"Name", "Child"}});

    vm.clearCacheForTest(parentId);
    vm.storeItemsCache(parentId, items);

    QJsonArray loaded;
    QVERIFY(vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/true));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().toObject().value("Name").toString(), QStringLiteral("Child"));
}

void SeriesDetailsCacheTest::itemsCacheRespectsFreshness()
{
    SeriesDetailsViewModel vm;
    const QString parentId = "parent-stale";
    QJsonArray items;
    items.append(QJsonObject{{"Id", "child"}, {"Name", "Child"}});

    vm.clearCacheForTest(parentId);
    vm.storeItemsCache(parentId, items);

    QString path = vm.itemsCachePath(parentId);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QJsonObject root;
    root.insert("timestamp", 0);
    root.insert("items", items);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    QJsonArray loaded;
    QVERIFY(!vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/true));
    QVERIFY(vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/false));
    QCOMPARE(loaded.size(), 1);
}

QTEST_MAIN(SeriesDetailsCacheTest)
#include "SeriesDetailsCacheTest.moc"
