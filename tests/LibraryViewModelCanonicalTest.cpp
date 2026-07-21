#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#define private public
#include "viewmodels/LibraryViewModel.h"
#undef private

class LibraryViewModelCanonicalTest : public QObject
{
    Q_OBJECT

private slots:
    void canonicalRolesAndContainerFiltering();
    void cachePayloadRejectsProviderWireShape();
    void swrIdentityUsesCanonicalItemId();
};

void LibraryViewModelCanonicalTest::canonicalRolesAndContainerFiltering()
{
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    qputenv("XDG_CACHE_HOME", cacheDir.path().toUtf8());

    LibraryViewModel viewModel;
    const QJsonObject movie{
        {QStringLiteral("itemId"), QStringLiteral("movie-1")},
        {QStringLiteral("name"), QStringLiteral("Example")},
        {QStringLiteral("mediaType"), QStringLiteral("Movie")},
        {QStringLiteral("productionYear"), 2024},
        {QStringLiteral("indexNumber"), 3},
        {QStringLiteral("parentIndexNumber"), 2},
        {QStringLiteral("overview"), QStringLiteral("Overview")}
    };
    const QJsonObject emptySeries{
        {QStringLiteral("itemId"), QStringLiteral("series-empty")},
        {QStringLiteral("name"), QStringLiteral("Empty")},
        {QStringLiteral("mediaType"), QStringLiteral("Series")},
        {QStringLiteral("childCount"), 0}
    };

    viewModel.setItems(QJsonArray{movie, emptySeries});

    QCOMPARE(viewModel.rowCount(), 1);
    const QModelIndex index = viewModel.index(0);
    QCOMPARE(viewModel.data(index, LibraryViewModel::NameRole).toString(),
             QStringLiteral("Example"));
    QCOMPARE(viewModel.data(index, LibraryViewModel::IdRole).toString(),
             QStringLiteral("movie-1"));
    QCOMPARE(viewModel.data(index, LibraryViewModel::TypeRole).toString(),
             QStringLiteral("Movie"));
    QCOMPARE(viewModel.data(index, LibraryViewModel::ProductionYearRole).toInt(), 2024);
    QCOMPARE(viewModel.data(index, LibraryViewModel::OverviewRole).toString(),
             QStringLiteral("Overview"));

    const QVariantMap exposedItem = viewModel.getItem(0);
    QCOMPARE(exposedItem.value(QStringLiteral("itemId")).toString(),
             QStringLiteral("movie-1"));
    QVERIFY(!exposedItem.contains(QStringLiteral("Id")));
}

void LibraryViewModelCanonicalTest::cachePayloadRejectsProviderWireShape()
{
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    qputenv("XDG_CACHE_HOME", cacheDir.path().toUtf8());

    LibraryViewModel viewModel;
    QVERIFY(viewModel.isCanonicalCachePayload(QJsonArray{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-1")}}
    }));
    QVERIFY(!viewModel.isCanonicalCachePayload(QJsonArray{
        QJsonObject{{QStringLiteral("Id"), QStringLiteral("movie-1")}}
    }));
    QVERIFY(!viewModel.isCanonicalCachePayload(QJsonArray{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-1")},
                    {QStringLiteral("Id"), QStringLiteral("wire-id")}}
    }));
}

void LibraryViewModelCanonicalTest::swrIdentityUsesCanonicalItemId()
{
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    qputenv("XDG_CACHE_HOME", cacheDir.path().toUtf8());

    LibraryViewModel viewModel;
    LibraryCacheEntry cached;
    cached.items = QJsonArray{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-1")}},
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-2")}}
    };
    cached.totalRecordCount = 2;

    QVERIFY(!viewModel.hasDataChanged(cached.items, 2, cached));
    const QJsonArray reordered{
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-2")}},
        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("movie-1")}}
    };
    QVERIFY(viewModel.hasDataChanged(reordered, 2, cached));
}

QTEST_MAIN(LibraryViewModelCanonicalTest)
#include "LibraryViewModelCanonicalTest.moc"
