#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/ServiceLocator.h"

#define private public
#include "viewmodels/LibraryViewModel.h"
#undef private

namespace {

QVariantMap canonicalItem(const QString &itemId)
{
    return {
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), QStringLiteral("Movie")},
    };
}

QVariantList canonicalItems(std::initializer_list<const char *> itemIds)
{
    QVariantList items;
    items.reserve(static_cast<qsizetype>(itemIds.size()));
    for (const char *itemId : itemIds) {
        items.append(canonicalItem(QString::fromLatin1(itemId)));
    }
    return items;
}

class ControlledLibraryService final : public LibraryService
{
public:
    struct FilterRequest {
        QString parentId;
        QString requestKey;
    };

    explicit ControlledLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override
    {
        return QStringLiteral("test-connection");
    }

    void getItems(const LibraryItemQuery &query) override
    {
        itemRequests.append(query);
    }

    void getViewsForRequest(const QString &requestKey) override
    {
        viewRequests.append(requestKey);
    }

    void getFilterOptionsForRequest(
        const QString &parentId,
        const QStringList &includeItemTypes,
        bool recursive,
        const QString &requestKey) override
    {
        Q_UNUSED(includeItemTypes)
        Q_UNUSED(recursive)
        filterRequests.append({parentId, requestKey});
    }

    void succeedItem(int requestIndex,
                     const QVariantList &items,
                     int totalRecordCount)
    {
        succeedItemForConnection(
            requestIndex, getActiveConnectionId(), items, totalRecordCount);
    }

    void succeedItemForConnection(int requestIndex,
                                  const QString &connectionId,
                                  const QVariantList &items,
                                  int totalRecordCount)
    {
        const LibraryItemQuery request = itemRequests.at(requestIndex);
        emit canonicalItemsLoadedForConnection(
            connectionId,
            request.parentId,
            request.requestKey,
            items,
            totalRecordCount);
    }

    void failItem(int requestIndex, const QString &error)
    {
        const LibraryItemQuery request = itemRequests.at(requestIndex);
        emit canonicalItemsFailedForConnection(
            getActiveConnectionId(),
            request.parentId,
            request.requestKey,
            error);
    }

    void failFilter(int requestIndex, const QString &error)
    {
        const FilterRequest request = filterRequests.at(requestIndex);
        emit filterOptionsFailedForRequest(
            getActiveConnectionId(),
            request.parentId,
            request.requestKey,
            error);
    }

    void succeedFilter(int requestIndex,
                       const QStringList &genres,
                       const QStringList &tags,
                       const QStringList &studios)
    {
        const FilterRequest request = filterRequests.at(requestIndex);
        emit filterOptionsLoadedForRequest(
            getActiveConnectionId(),
            request.parentId,
            request.requestKey,
            genres,
            tags,
            studios);
    }

    void failView(int requestIndex, const QString &error)
    {
        emit canonicalViewsFailedForRequest(
            getActiveConnectionId(),
            viewRequests.at(requestIndex),
            error);
    }

    QList<LibraryItemQuery> itemRequests;
    QList<QString> viewRequests;
    QList<FilterRequest> filterRequests;
};

}

class LibraryViewModelCanonicalTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void canonicalRolesAndContainerFiltering();
    void cachePayloadRejectsProviderWireShape();
    void swrIdentityUsesCanonicalItemId();
    void failedLoadMoreRecovers();
    void viewsFailureClearsLoadingState();
    void staleResultsCannotOwnNewDataset();
    void connectionMismatchCannotCompleteRequest();
    void supersededPaginationCannotAppendToNewQuery();
    void backgroundRefreshFailurePreservesCachedContent();
    void backgroundRefreshInvalidatesUnverifiedLaterPages();
    void filterFailureClearsLoadingState();
    void filterOptionsAreIndependentOfItemDataset();
    void unchangedCacheScopePreservesDisplayedState();
    void validEmptySnapshotIsReused();
    void paginationUsesOneDatasetCache();
    void isolatedOffsetPageIsNotCachedAsDatasetStart();
    void memoryCacheIsBounded();

private:
    std::unique_ptr<QTemporaryDir> m_cacheDir;
    ControlledLibraryService *m_libraryService = nullptr;
};

void LibraryViewModelCanonicalTest::init()
{
    ServiceLocator::clear();
    LibraryViewModel::clearAllCache();
    m_cacheDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_cacheDir->isValid());
    qputenv("XDG_CACHE_HOME", m_cacheDir->path().toUtf8());
    m_libraryService = new ControlledLibraryService(this);
    ServiceLocator::registerService<LibraryService>(m_libraryService);
}

void LibraryViewModelCanonicalTest::cleanup()
{
    ServiceLocator::clear();
    LibraryViewModel::clearAllCache();
    delete m_libraryService;
    m_libraryService = nullptr;
    m_cacheDir.reset();
}

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

void LibraryViewModelCanonicalTest::failedLoadMoreRecovers()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one", "two"}), 4);
    QCOMPARE(viewModel.rowCount(), 2);
    QVERIFY(viewModel.canLoadMore());

    viewModel.loadMore(2);
    QVERIFY(viewModel.isLoadingMore());
    m_libraryService->failItem(1, QStringLiteral("temporary failure"));
    QVERIFY(!viewModel.isLoadingMore());
    QVERIFY(viewModel.canLoadMore());
    QCOMPARE(viewModel.rowCount(), 2);

    viewModel.loadMore(2);
    QCOMPARE(m_libraryService->itemRequests.size(), 3);
    m_libraryService->succeedItem(2, canonicalItems({"three", "four"}), 4);
    QVERIFY(!viewModel.isLoadingMore());
    QCOMPARE(viewModel.rowCount(), 4);
    QVERIFY(!viewModel.canLoadMore());
}

void LibraryViewModelCanonicalTest::viewsFailureClearsLoadingState()
{
    LibraryViewModel viewModel;
    viewModel.loadViews();
    QVERIFY(viewModel.isLoading());
    QCOMPARE(m_libraryService->viewRequests.size(), 1);

    m_libraryService->failView(0, QStringLiteral("views failed"));
    QVERIFY(!viewModel.isLoading());
    QVERIFY(viewModel.hasError());
    QVERIFY(!viewModel.m_loadingViews);
}

void LibraryViewModelCanonicalTest::staleResultsCannotOwnNewDataset()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    viewModel.setSearchTerm(QStringLiteral("new query"));
    viewModel.reloadWithCurrentQuery();
    QCOMPARE(m_libraryService->itemRequests.size(), 2);

    m_libraryService->failItem(0, QStringLiteral("stale failure"));
    QVERIFY(viewModel.isLoading());
    QVERIFY(!viewModel.hasError());

    m_libraryService->succeedItem(0, canonicalItems({"stale"}), 1);
    QVERIFY(viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 0);

    m_libraryService->succeedItem(1, canonicalItems({"current"}), 1);
    QVERIFY(!viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 1);
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("current"));
    QCOMPARE(viewModel.m_staleRequestRejections, quint64(2));
}

void LibraryViewModelCanonicalTest::connectionMismatchCannotCompleteRequest()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);

    m_libraryService->succeedItemForConnection(
        0,
        QStringLiteral("other-connection"),
        canonicalItems({"wrong"}),
        1);
    QVERIFY(viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 0);

    m_libraryService->succeedItem(0, canonicalItems({"right"}), 1);
    QVERIFY(!viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 1);
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("right"));
}

void LibraryViewModelCanonicalTest::supersededPaginationCannotAppendToNewQuery()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one", "two"}), 4);

    viewModel.loadMore(2);
    QVERIFY(viewModel.isLoadingMore());
    viewModel.setSearchTerm(QStringLiteral("filtered"));
    viewModel.reloadWithCurrentQuery();
    QVERIFY(!viewModel.isLoadingMore());

    m_libraryService->succeedItem(1, canonicalItems({"three", "four"}), 4);
    QCOMPARE(viewModel.rowCount(), 2);
    QVERIFY(viewModel.isLoading());

    m_libraryService->succeedItem(2, canonicalItems({"filtered"}), 1);
    QCOMPARE(viewModel.rowCount(), 1);
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("filtered"));
}

void LibraryViewModelCanonicalTest::backgroundRefreshFailurePreservesCachedContent()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"cached"}), 1);
    const QString datasetKey =
        m_libraryService->itemRequests.at(0).datasetKey();
    const QString memoryKey = viewModel.scopedCacheKey(datasetKey);
    viewModel.s_libraryCache[memoryKey].timestamp = 1;

    viewModel.loadLibrary("library", "movies", 0, 2);
    QCOMPARE(m_libraryService->itemRequests.size(), 2);
    QVERIFY(viewModel.m_isBackgroundRefresh);
    QVERIFY(!viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 1);

    m_libraryService->failItem(1, QStringLiteral("refresh failed"));
    QVERIFY(!viewModel.m_isBackgroundRefresh);
    QVERIFY(!viewModel.hasError());
    QCOMPARE(viewModel.rowCount(), 1);
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("cached"));
}

void LibraryViewModelCanonicalTest::backgroundRefreshInvalidatesUnverifiedLaterPages()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one", "two"}), 4);
    viewModel.loadMore(2);
    m_libraryService->succeedItem(1, canonicalItems({"three", "four"}), 4);

    const QString datasetKey =
        m_libraryService->itemRequests.at(0).datasetKey();
    viewModel.s_libraryCache[
        viewModel.scopedCacheKey(datasetKey)].timestamp = 1;

    viewModel.loadLibrary("library", "movies", 0, 2);
    QVERIFY(viewModel.m_isBackgroundRefresh);
    QCOMPARE(viewModel.rowCount(), 4);

    m_libraryService->succeedItem(
        2, canonicalItems({"new-one", "new-two"}), 4);
    QVERIFY(!viewModel.m_isBackgroundRefresh);
    QCOMPARE(viewModel.rowCount(), 2);
    QVERIFY(viewModel.canLoadMore());
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("new-one"));

    const LibraryCacheEntry cached = viewModel.getCachedData(datasetKey);
    QCOMPARE(cached.items.size(), 2);
    QCOMPARE(cached.items.at(1).toObject()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("new-two"));
}

void LibraryViewModelCanonicalTest::filterFailureClearsLoadingState()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one"}), 1);

    viewModel.loadFilterOptions("library", "movies");
    QVERIFY(viewModel.filterOptionsLoading());
    QCOMPARE(m_libraryService->filterRequests.size(), 1);

    m_libraryService->failFilter(0, QStringLiteral("filter failure"));
    QVERIFY(!viewModel.filterOptionsLoading());
    QCOMPARE(viewModel.rowCount(), 1);
}

void LibraryViewModelCanonicalTest::filterOptionsAreIndependentOfItemDataset()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one"}), 1);

    viewModel.loadFilterOptions("library", "movies");
    QVERIFY(viewModel.filterOptionsLoading());
    viewModel.m_activeDatasetKey = QStringLiteral("changed-item-dataset");

    m_libraryService->succeedFilter(
        0,
        {QStringLiteral("Drama")},
        {QStringLiteral("Favorite")},
        {QStringLiteral("Studio")});
    QVERIFY(!viewModel.filterOptionsLoading());
    QCOMPARE(viewModel.availableGenres(), QStringList{QStringLiteral("Drama")});
    QCOMPARE(viewModel.availableTags(), QStringList{QStringLiteral("Favorite")});
    QCOMPARE(viewModel.availableStudios(), QStringList{QStringLiteral("Studio")});
}

void LibraryViewModelCanonicalTest::unchangedCacheScopePreservesDisplayedState()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one"}), 1);
    QCOMPARE(viewModel.rowCount(), 1);

    const quint64 generation = viewModel.m_requestGeneration;
    viewModel.reopenCacheStore();

    QCOMPARE(viewModel.m_requestGeneration, generation);
    QCOMPARE(viewModel.currentParentId(), QStringLiteral("library"));
    QCOMPARE(viewModel.rowCount(), 1);
    QCOMPARE(viewModel.getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("one"));
}

void LibraryViewModelCanonicalTest::validEmptySnapshotIsReused()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("empty", "movies", 0, 20);
    m_libraryService->succeedItem(0, QVariantList(), 0);
    QVERIFY(!viewModel.isLoading());
    QCOMPARE(viewModel.rowCount(), 0);

    viewModel.loadLibrary("empty", "movies", 0, 20);
    QCOMPARE(m_libraryService->itemRequests.size(), 1);
    QVERIFY(!viewModel.isLoading());
    QCOMPARE(viewModel.totalRecordCount(), 0);
}

void LibraryViewModelCanonicalTest::paginationUsesOneDatasetCache()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one", "two"}), 4);
    viewModel.loadMore(2);
    m_libraryService->succeedItem(1, canonicalItems({"three", "four"}), 4);

    const LibraryItemQuery first = m_libraryService->itemRequests.at(0);
    const LibraryItemQuery second = m_libraryService->itemRequests.at(1);
    QCOMPARE(first.datasetKey(), second.datasetKey());
    QVERIFY(first.requestKey != second.requestKey);

    const LibraryCacheEntry cached =
        viewModel.getCachedData(first.datasetKey());
    QVERIFY(cached.hasSnapshot());
    QCOMPARE(cached.items.size(), 4);
    QCOMPARE(cached.totalRecordCount, 4);

    const auto persisted = viewModel.m_cacheStore->read(first.datasetKey());
    QVERIFY(persisted.hasSnapshot());
    QCOMPARE(persisted.items.size(), 4);
    QCOMPARE(persisted.totalCount, 4);
}

void LibraryViewModelCanonicalTest::isolatedOffsetPageIsNotCachedAsDatasetStart()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 100, 2);
    m_libraryService->succeedItem(
        0, canonicalItems({"middle-one", "middle-two"}), 200);
    QCOMPARE(viewModel.rowCount(), 2);

    const QString datasetKey =
        m_libraryService->itemRequests.constFirst().datasetKey();
    QVERIFY(!viewModel.getCachedData(datasetKey).hasSnapshot());
    QVERIFY(!viewModel.m_cacheStore->read(datasetKey).hasSnapshot());

    viewModel.loadLibrary("library", "movies", 0, 2);
    QCOMPARE(m_libraryService->itemRequests.size(), 2);
    QVERIFY(viewModel.isLoading());
}

void LibraryViewModelCanonicalTest::memoryCacheIsBounded()
{
    LibraryViewModel viewModel;
    viewModel.m_cacheStore.reset();
    for (int i = 0; i < 80; ++i) {
        viewModel.updateCache(
            QStringLiteral("dataset-%1").arg(i),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("itemId"),
                     QStringLiteral("item-%1").arg(i)},
                },
            },
            1);
    }

    QVERIFY(viewModel.s_libraryCache.size()
            <= LibraryViewModel::kMemoryCacheMaxEntries);
    QVERIFY(viewModel.s_libraryCacheBytes
            <= LibraryViewModel::kMemoryCacheMaxBytes);
    QCOMPARE(viewModel.s_libraryCache.size(),
             viewModel.s_libraryCacheLru.size());
}

QTEST_MAIN(LibraryViewModelCanonicalTest)
#include "LibraryViewModelCanonicalTest.moc"
