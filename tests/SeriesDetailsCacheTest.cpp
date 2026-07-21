#include <QtTest/QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDateTime>
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
    void wireShapedCachesAreRejected();
    void stalePrefetchCompletionDoesNotBlockNewConnection();
};

void SeriesDetailsCacheTest::seriesCacheStoresAndLoadsFresh()
{
    SeriesDetailsViewModel vm;
    const QString seriesId = "test-series";
    QJsonObject payload{{"connectionId", "connection-1"},
                        {"itemId", seriesId},
                        {"name", "Test"}};

    vm.clearCacheForTest(seriesId);
    vm.storeSeriesCache(seriesId, payload);

    QJsonObject loaded;
    QVERIFY(vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/true));
    QCOMPARE(loaded.value("name").toString(), QStringLiteral("Test"));
}

void SeriesDetailsCacheTest::seriesCacheRespectsFreshness()
{
    SeriesDetailsViewModel vm;
    const QString seriesId = "stale-series";
    QJsonObject payload{{"connectionId", "connection-1"},
                        {"itemId", seriesId},
                        {"name", "Old"}};

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
    vm.clearMemoryCacheForTest(seriesId);

    QJsonObject loaded;
    // Fresh load should fail due to stale timestamp
    QVERIFY(!vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/true));
    // Allowing stale should succeed
    QVERIFY(vm.loadSeriesFromCache(seriesId, loaded, /*requireFresh*/false));
    QCOMPARE(loaded.value("name").toString(), QStringLiteral("Old"));
}

void SeriesDetailsCacheTest::itemsCacheStoresAndLoadsFresh()
{
    SeriesDetailsViewModel vm;
    const QString parentId = "parent-1";
    QJsonArray items;
    items.append(QJsonObject{{"connectionId", "connection-1"},
                             {"itemId", "child"},
                             {"name", "Child"}});

    vm.clearCacheForTest(parentId);
    vm.storeItemsCache(parentId, items);

    QJsonArray loaded;
    QVERIFY(vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/true));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().toObject().value("name").toString(), QStringLiteral("Child"));
}

void SeriesDetailsCacheTest::itemsCacheRespectsFreshness()
{
    SeriesDetailsViewModel vm;
    const QString parentId = "parent-stale";
    QJsonArray items;
    items.append(QJsonObject{{"connectionId", "connection-1"},
                             {"itemId", "child"},
                             {"name", "Child"}});

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
    vm.clearMemoryCacheForTest(parentId);

    QJsonArray loaded;
    QVERIFY(!vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/true));
    QVERIFY(vm.loadItemsFromCache(parentId, loaded, /*requireFresh*/false));
    QCOMPARE(loaded.size(), 1);
}

void SeriesDetailsCacheTest::stalePrefetchCompletionDoesNotBlockNewConnection()
{
    SeriesDetailsViewModel vm;
    const QString seasonId = QStringLiteral("shared-season");
    const QString oldKey = QStringLiteral("old-connection\n") + seasonId;
    const QString newKey = QStringLiteral("new-connection\n") + seasonId;
    vm.m_connectionId = QStringLiteral("new-connection");
    vm.m_prefetchSeasonIds = {oldKey, newKey};

    vm.onItemsLoaded(QStringLiteral("old-connection"), seasonId, QString(), {}, 0);
    QVERIFY(!vm.m_prefetchSeasonIds.contains(oldKey));
    QVERIFY(vm.m_prefetchSeasonIds.contains(newKey));

    vm.onItemsLoaded(QStringLiteral("new-connection"), seasonId, QString(), {}, 0);
    QVERIFY(!vm.m_prefetchSeasonIds.contains(newKey));
    vm.clearCacheForTest(seasonId);
}

void SeriesDetailsCacheTest::wireShapedCachesAreRejected()
{
    SeriesDetailsViewModel vm;
    const QString seriesId = QStringLiteral("wire-series");
    const QJsonObject wireSeries{{QStringLiteral("Id"), seriesId},
                                 {QStringLiteral("Name"), QStringLiteral("Wire")},
                                 {QStringLiteral("UserData"), QJsonObject{}}};
    const QJsonArray wireItems{QJsonObject{{QStringLiteral("Id"), QStringLiteral("wire-season")},
                                           {QStringLiteral("Name"), QStringLiteral("Wire season")}}};

    vm.clearCacheForTest(seriesId);

    QFile seriesFile(vm.seriesCachePath(seriesId));
    QVERIFY(seriesFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    seriesFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("data"), wireSeries}
    }).toJson(QJsonDocument::Compact));
    seriesFile.close();

    QFile itemsFile(vm.itemsCachePath(seriesId));
    QVERIFY(itemsFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    itemsFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("items"), wireItems}
    }).toJson(QJsonDocument::Compact));
    itemsFile.close();
    vm.clearMemoryCacheForTest(seriesId);

    QJsonObject loadedSeries;
    QJsonArray loadedItems;
    QVERIFY(!vm.loadSeriesFromCache(seriesId, loadedSeries, false));
    QVERIFY(!vm.loadItemsFromCache(seriesId, loadedItems, false));
    QVERIFY(!QFile::exists(vm.seriesCachePath(seriesId)));
    QVERIFY(!QFile::exists(vm.itemsCachePath(seriesId)));
}

QTEST_MAIN(SeriesDetailsCacheTest)
#include "SeriesDetailsCacheTest.moc"
