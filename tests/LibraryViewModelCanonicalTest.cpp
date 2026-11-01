#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/ServiceLocator.h"
#include "viewmodels/LibraryViewModel.h"

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

QVariantList numberedItems(int first, int count)
{
    QVariantList items;
    for (int index = first; index < first + count; ++index) {
        items.append(canonicalItem(QStringLiteral("item-%1").arg(index)));
    }
    return items;
}

// A to Z over a 1,000-title list: A at 0, M at 600, Z at 990.
QVariantMap thousandTitleIndex()
{
    const auto letter = [](const char *name, int count, int position) {
        return QVariantMap{{QStringLiteral("letter"), QString::fromLatin1(name)},
                           {QStringLiteral("count"), count},
                           {QStringLiteral("position"), position}};
    };
    const auto run = [](const char *name, int position) {
        return QVariantMap{{QStringLiteral("letter"), QString::fromLatin1(name)},
                           {QStringLiteral("position"), position}};
    };
    return {{QStringLiteral("total"), 1000},
            {QStringLiteral("letters"), QVariantList{letter("A", 600, 0), letter("M", 390, 600),
                                                     letter("Z", 10, 990)}},
            {QStringLiteral("runs"), QVariantList{run("A", 0), run("M", 600), run("Z", 990)}}};
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

    ProviderLibraryQueryCapabilities libraryQueryCapabilities() const override
    {
        return queryCapabilities;
    }

    void getItems(const LibraryItemQuery &query) override
    {
        itemRequests.append(query);
    }

    void getViewsForRequest(const QString &requestKey) override
    {
        viewRequests.append(requestKey);
    }

    void getLibraryLetters(const LibraryItemQuery &query) override
    {
        letterRequests.append(query);
    }

    void succeedLetters(int requestIndex, const QVariantMap &index)
    {
        emit libraryLettersLoaded(
            getActiveConnectionId(), letterRequests.at(requestIndex).requestKey, index);
    }

    void failLetters(int requestIndex, const QString &error)
    {
        emit libraryLettersFailed(
            getActiveConnectionId(), letterRequests.at(requestIndex).requestKey, error);
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

    void notModifiedItem(int requestIndex)
    {
        const LibraryItemQuery request = itemRequests.at(requestIndex);
        emit canonicalItemsNotModifiedForConnection(
            getActiveConnectionId(),
            request.parentId,
            request.requestKey);
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
    QList<LibraryItemQuery> letterRequests;
    QList<QString> viewRequests;
    QList<FilterRequest> filterRequests;
    ProviderLibraryQueryCapabilities queryCapabilities;
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
    void sortStateUsesCanonicalKeys();
    void unsupportedSavedQueryFallsBack();
    void swrIdentityUsesCanonicalItemId();
    void swrMetadataRefreshUpdatesRowsWithoutReset();
    void failedLoadMoreRecovers();
    void viewsFailureClearsLoadingState();
    void staleResultsCannotOwnNewDataset();
    void connectionMismatchCannotCompleteRequest();
    void supersededPaginationCannotAppendToNewQuery();
    void backgroundRefreshFailurePreservesCachedContent();
    void backgroundRefreshInvalidatesUnverifiedLaterPages();
    void notModifiedCompletesEveryLoadKind();
    void filterFailureClearsLoadingState();
    void filterOptionsAreIndependentOfItemDataset();
    void unchangedCacheScopePreservesDisplayedState();
    void validEmptySnapshotIsReused();
    void paginationUsesOneDatasetCache();
    void prefixOverlappingPageInvalidatesDatasetCache();
    void suffixOverlapInvalidatesUnverifiedCachePositions();
    void duplicatePageDoesNotEnterMemoryCache();
    void isolatedOffsetPageIsNotCachedAsDatasetStart();
    void memoryCacheIsBounded();
    void letterIndexFollowsTitleOrder();
    void letterJumpSeeksAndFillsPlaceholders();
    void seekAnswersAroundPagesInFlight();
    void failedWindowLeavesPlaceholdersToRetry();

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
    qputenv("APPDATA", m_cacheDir->path().toUtf8());
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

void LibraryViewModelCanonicalTest::sortStateUsesCanonicalKeys()
{
    LibraryViewModel viewModel;
    viewModel.setSortBy(QStringLiteral("releaseDate"));
    viewModel.setSortOrder(QStringLiteral("descending"));
    QCOMPARE(viewModel.sortBy(), QStringLiteral("releaseDate"));
    QCOMPARE(viewModel.sortOrder(), QStringLiteral("descending"));

    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 20);
    QCOMPARE(m_libraryService->itemRequests.size(), 1);
    QCOMPARE(m_libraryService->itemRequests.first().sortBy,
             QStringLiteral("releaseDate"));
    QCOMPARE(m_libraryService->itemRequests.first().sortOrder,
             QStringLiteral("descending"));

    viewModel.setSortBy(QStringLiteral("PremiereDate"));
    viewModel.setSortOrder(QStringLiteral("Descending"));
    QVERIFY(viewModel.sortBy().isEmpty());
    QVERIFY(viewModel.sortOrder().isEmpty());
}

void LibraryViewModelCanonicalTest::unsupportedSavedQueryFallsBack()
{
    LibraryViewModel viewModel;
    QVERIFY(viewModel.supportedSorts().contains(QStringLiteral("random")));
    QVERIFY(viewModel.supportedFilters().contains(QStringLiteral("tags")));
    viewModel.setSortBy(QStringLiteral("random"));
    viewModel.setSortOrder(QStringLiteral("descending"));
    viewModel.toggleTag(QStringLiteral("award-winner"));
    viewModel.toggleGenre(QStringLiteral("Drama"));
    viewModel.setAddedSinceFilter(QStringLiteral("30d"));
    QCOMPARE(viewModel.sortBy(), QStringLiteral("random"));
    QCOMPARE(viewModel.activeFilterCount(), 3);

    // The next library belongs to a connection without random order or
    // tags, as on Silo.
    ProviderLibraryQueryCapabilities silo;
    silo.sorts.removeAll(QStringLiteral("random"));
    silo.filters.removeAll(QStringLiteral("tags"));
    m_libraryService->queryCapabilities = silo;

    QSignalSpy capabilitiesSpy(&viewModel, &LibraryViewModel::queryCapabilitiesChanged);
    QSignalSpy sortSpy(&viewModel, &LibraryViewModel::sortByChanged);
    QSignalSpy tagsSpy(&viewModel, &LibraryViewModel::selectedTagsChanged);
    QSignalSpy countSpy(&viewModel, &LibraryViewModel::activeFilterCountChanged);
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 20);

    QCOMPARE(capabilitiesSpy.count(), 1);
    QVERIFY(!viewModel.supportedSorts().contains(QStringLiteral("random")));
    QVERIFY(!viewModel.supportedFilters().contains(QStringLiteral("tags")));
    QCOMPARE(sortSpy.count(), 1);
    QCOMPARE(tagsSpy.count(), 1);
    QVERIFY(countSpy.count() >= 1);
    QVERIFY(viewModel.sortBy().isEmpty());
    QCOMPARE(viewModel.sortOrder(), QStringLiteral("descending"));
    QVERIFY(viewModel.selectedTags().isEmpty());
    QCOMPARE(viewModel.selectedGenres(), QStringList{QStringLiteral("Drama")});
    QCOMPARE(viewModel.addedSinceFilter(), QStringLiteral("30d"));
    QCOMPARE(viewModel.activeFilterCount(), 2);

    QCOMPARE(m_libraryService->itemRequests.size(), 1);
    const LibraryItemQuery request = m_libraryService->itemRequests.first();
    QVERIFY(request.sortBy.isEmpty());
    QVERIFY(request.tags.isEmpty());
    QCOMPARE(request.genres, QStringList{QStringLiteral("Drama")});
    QCOMPARE(request.minDateLastSaved, QDate::currentDate().addDays(-30));

    // Unsupported choices cannot be made again on this connection.
    viewModel.setSortBy(QStringLiteral("random"));
    QVERIFY(viewModel.sortBy().isEmpty());
    viewModel.toggleTag(QStringLiteral("award-winner"));
    QVERIFY(viewModel.selectedTags().isEmpty());
    viewModel.setSortBy(QStringLiteral("title"));
    QCOMPARE(viewModel.sortBy(), QStringLiteral("title"));

    // Back on a connection that serves everything, random is offered again.
    m_libraryService->queryCapabilities = {};
    viewModel.loadViews();
    QCOMPARE(capabilitiesSpy.count(), 2);
    QVERIFY(viewModel.supportedSorts().contains(QStringLiteral("random")));
    QCOMPARE(viewModel.sortBy(), QStringLiteral("title"));
    viewModel.setSortBy(QStringLiteral("random"));
    QCOMPARE(viewModel.sortBy(), QStringLiteral("random"));
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

    QJsonArray metadataChanged = cached.items;
    QJsonObject changed = metadataChanged.at(0).toObject();
    changed.insert(QStringLiteral("watched"), true);
    metadataChanged.replace(0, changed);
    QVERIFY(viewModel.hasDataChanged(metadataChanged, 2, cached));
}

void LibraryViewModelCanonicalTest::swrMetadataRefreshUpdatesRowsWithoutReset()
{
    LibraryViewModel viewModel;
    QVariantMap userStateItem = canonicalItem(QStringLiteral("movie-1"));
    userStateItem.insert(QStringLiteral("watched"), false);
    userStateItem.insert(QStringLiteral("favorite"), false);
    userStateItem.insert(QStringLiteral("playbackProgress"), 0.1);

    QVariantMap visibleItem = canonicalItem(QStringLiteral("movie-2"));
    visibleItem.insert(QStringLiteral("name"), QStringLiteral("Old title"));
    visibleItem.insert(QStringLiteral("productionYear"), 2020);
    visibleItem.insert(QStringLiteral("overview"), QStringLiteral("Old overview"));
    visibleItem.insert(QStringLiteral("communityRating"), 6.5);
    visibleItem.insert(QStringLiteral("primaryArtwork"), QVariantMap{
        {QStringLiteral("connectionId"), QStringLiteral("test-connection")},
        {QStringLiteral("itemId"), QStringLiteral("movie-2")},
        {QStringLiteral("kind"), QStringLiteral("primary")},
        {QStringLiteral("ownerKind"), QStringLiteral("mediaItem")},
        {QStringLiteral("tag"), QStringLiteral("old-artwork")},
    });

    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 2);
    m_libraryService->succeedItem(0, {userStateItem, visibleItem}, 2);
    const QString oldImageUrl = viewModel.data(
        viewModel.index(1), LibraryViewModel::ImageUrlRole).toString();
    QVERIFY(!oldImageUrl.isEmpty());

    const QString datasetKey = m_libraryService->itemRequests.at(0).datasetKey();
    viewModel.s_libraryCache[
        viewModel.scopedCacheKey(datasetKey)].timestamp = 1;

    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 2);
    QVERIFY(viewModel.m_isBackgroundRefresh);

    QSignalSpy changedSpy(&viewModel, &QAbstractItemModel::dataChanged);
    QSignalSpy resetSpy(&viewModel, &QAbstractItemModel::modelReset);

    userStateItem.insert(QStringLiteral("watched"), true);
    userStateItem.insert(QStringLiteral("favorite"), true);
    userStateItem.insert(QStringLiteral("playbackProgress"), 0.75);
    visibleItem.insert(QStringLiteral("name"), QStringLiteral("New title"));
    visibleItem.insert(QStringLiteral("productionYear"), 2025);
    visibleItem.insert(QStringLiteral("overview"), QStringLiteral("New overview"));
    visibleItem.insert(QStringLiteral("communityRating"), 8.4);
    QVariantMap artwork = visibleItem.value(QStringLiteral("primaryArtwork")).toMap();
    artwork.insert(QStringLiteral("tag"), QStringLiteral("new-artwork"));
    visibleItem.insert(QStringLiteral("primaryArtwork"), artwork);

    m_libraryService->succeedItem(1, {userStateItem, visibleItem}, 2);

    QVERIFY(!viewModel.m_isBackgroundRefresh);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 2);

    const QVariantMap refreshedState = viewModel.getItem(0);
    QCOMPARE(refreshedState.value(QStringLiteral("watched")).toBool(), true);
    QCOMPARE(refreshedState.value(QStringLiteral("favorite")).toBool(), true);
    QCOMPARE(refreshedState.value(QStringLiteral("playbackProgress")).toDouble(), 0.75);

    const QModelIndex visibleIndex = viewModel.index(1);
    QCOMPARE(viewModel.data(visibleIndex, LibraryViewModel::NameRole).toString(),
             QStringLiteral("New title"));
    QCOMPARE(viewModel.data(visibleIndex, LibraryViewModel::ProductionYearRole).toInt(),
             2025);
    QCOMPARE(viewModel.data(visibleIndex, LibraryViewModel::OverviewRole).toString(),
             QStringLiteral("New overview"));
    const QVariantMap refreshedVisible = viewModel.getItem(1);
    QCOMPARE(refreshedVisible.value(QStringLiteral("communityRating")).toDouble(), 8.4);
    QCOMPARE(refreshedVisible.value(QStringLiteral("primaryArtwork")).toMap()
                 .value(QStringLiteral("tag")).toString(),
             QStringLiteral("new-artwork"));
    const QString refreshedImageUrl = viewModel.data(
        visibleIndex, LibraryViewModel::ImageUrlRole).toString();
    QVERIFY(!refreshedImageUrl.isEmpty());
    QVERIFY(refreshedImageUrl != oldImageUrl);

    bool sawModelDataRole = false;
    bool sawImageRole = false;
    for (const QList<QVariant> &arguments : changedSpy) {
        const QList<int> roles = arguments.at(2).value<QList<int>>();
        sawModelDataRole = sawModelDataRole
            || roles.contains(LibraryViewModel::ModelDataRole);
        sawImageRole = sawImageRole
            || roles.contains(LibraryViewModel::ImageUrlRole);
    }
    QVERIFY(sawModelDataRole);
    QVERIFY(sawImageRole);
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

void LibraryViewModelCanonicalTest::notModifiedCompletesEveryLoadKind()
{
    {
        LibraryViewModel viewModel;
        QSignalSpy completedSpy(&viewModel, &LibraryViewModel::loadComplete);
        viewModel.loadLibrary("initial", "movies", 0, 2);
        QVERIFY(viewModel.isLoading());
        m_libraryService->notModifiedItem(0);
        QVERIFY(!viewModel.isLoading());
        QCOMPARE(completedSpy.count(), 1);
    }

    {
        LibraryViewModel viewModel;
        viewModel.loadLibrary("refresh", "movies", 0, 2);
        m_libraryService->succeedItem(1, canonicalItems({"cached"}), 1);
        const QString datasetKey =
            m_libraryService->itemRequests.at(1).datasetKey();
        const QString memoryKey = viewModel.scopedCacheKey(datasetKey);
        viewModel.s_libraryCache[memoryKey].timestamp = 1;

        viewModel.loadLibrary("refresh", "movies", 0, 2);
        QVERIFY(viewModel.m_isBackgroundRefresh);
        const qint64 staleTimestamp =
            viewModel.s_libraryCache.value(memoryKey).timestamp;
        m_libraryService->notModifiedItem(2);

        QVERIFY(!viewModel.m_isBackgroundRefresh);
        QCOMPARE(viewModel.rowCount(), 1);
        QVERIFY(viewModel.s_libraryCache.value(memoryKey).timestamp
                > staleTimestamp);
    }

    {
        LibraryViewModel viewModel;
        QSignalSpy completedSpy(
            &viewModel, &LibraryViewModel::loadMoreComplete);
        viewModel.loadLibrary("pagination", "movies", 0, 2);
        m_libraryService->succeedItem(
            3, canonicalItems({"one", "two"}), 4);
        viewModel.loadMore(2);
        QVERIFY(viewModel.isLoadingMore());
        m_libraryService->notModifiedItem(4);

        QVERIFY(!viewModel.isLoadingMore());
        QVERIFY(viewModel.canLoadMore());
        QCOMPARE(completedSpy.count(), 1);
    }
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

void LibraryViewModelCanonicalTest::prefixOverlappingPageInvalidatesDatasetCache()
{
    LibraryViewModel viewModel;
    viewModel.loadLibrary("library", "movies", 0, 2);
    m_libraryService->succeedItem(0, canonicalItems({"one", "two"}), 4);
    const QString datasetKey =
        m_libraryService->itemRequests.constFirst().datasetKey();
    QVERIFY(viewModel.getCachedData(datasetKey).hasSnapshot());

    viewModel.loadMore(2);
    m_libraryService->succeedItem(
        1, canonicalItems({"one", "four"}), 4);

    QVERIFY(!viewModel.getCachedData(datasetKey).hasSnapshot());
    QVERIFY(!viewModel.m_cacheStore->read(datasetKey).hasSnapshot());
}

void LibraryViewModelCanonicalTest::suffixOverlapInvalidatesUnverifiedCachePositions()
{
    LibraryViewModel viewModel;
    const QString datasetKey = QStringLiteral("suffix-reorder");
    viewModel.updateCache(
        datasetKey,
        QJsonArray::fromVariantList(
            canonicalItems({"one", "two", "three", "four", "five"})),
        5);

    viewModel.updateCachePage(
        datasetKey,
        QJsonArray::fromVariantList(canonicalItems({"one", "four"})),
        5,
        0);

    const LibraryCacheEntry cached = viewModel.getCachedData(datasetKey);
    QCOMPARE(cached.items.size(), 2);
    QCOMPARE(cached.items.at(0).toObject()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("one"));
    QCOMPARE(cached.items.at(1).toObject()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("four"));

    const auto persisted = viewModel.m_cacheStore->read(datasetKey);
    QCOMPARE(persisted.items, cached.items);
    QCOMPARE(persisted.totalCount, 5);
}

void LibraryViewModelCanonicalTest::duplicatePageDoesNotEnterMemoryCache()
{
    LibraryViewModel viewModel;
    const QString datasetKey = QStringLiteral("duplicate-page");
    const QJsonArray initial =
        QJsonArray::fromVariantList(canonicalItems({"one", "two"}));
    viewModel.updateCache(datasetKey, initial, 4);

    viewModel.updateCachePage(
        datasetKey,
        QJsonArray::fromVariantList(canonicalItems({"three", "three"})),
        4,
        2);

    const LibraryCacheEntry cached = viewModel.getCachedData(datasetKey);
    QCOMPARE(cached.items, initial);
    QCOMPARE(cached.totalRecordCount, 4);

    const auto persisted = viewModel.m_cacheStore->read(datasetKey);
    QCOMPARE(persisted.items, initial);
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

void LibraryViewModelCanonicalTest::letterIndexFollowsTitleOrder()
{
    LibraryViewModel viewModel;
    QSignalSpy indexSpy(&viewModel, &LibraryViewModel::letterIndexChanged);

    // Library order: no index, no request.
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    QVERIFY(m_libraryService->letterRequests.isEmpty());
    QVERIFY(viewModel.letterIndex().isEmpty());

    // Title order asks for the index of the same query.
    viewModel.setSortBy(QStringLiteral("title"));
    viewModel.setFavoriteFilter(QStringLiteral("favorite"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    QCOMPARE(m_libraryService->letterRequests.size(), 1);
    const LibraryItemQuery request = m_libraryService->letterRequests.first();
    QCOMPARE(request.sortBy, QStringLiteral("title"));
    QCOMPARE(request.parentId, QStringLiteral("library"));
    QCOMPARE(request.favorite, LibraryItemQuery::TriState::Yes);
    QCOMPARE(request.datasetKey(), m_libraryService->itemRequests.last().datasetKey());
    m_libraryService->succeedLetters(0, thousandTitleIndex());
    QCOMPARE(viewModel.letterIndex().size(), 3);
    QCOMPARE(indexSpy.count(), 1);
    QCOMPARE(viewModel.letterAtPosition(0), QStringLiteral("A"));
    QCOMPARE(viewModel.letterAtPosition(750), QStringLiteral("M"));
    QCOMPARE(viewModel.letterAtPosition(995), QStringLiteral("Z"));

    // A late answer for a replaced query is dropped; the new query's index
    // is shown once it arrives, and a failure falls back to none.
    viewModel.setFavoriteFilter(QStringLiteral("any"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    QCOMPARE(m_libraryService->letterRequests.size(), 2);
    QVERIFY(viewModel.letterIndex().isEmpty());
    m_libraryService->succeedLetters(0, thousandTitleIndex());
    QVERIFY(viewModel.letterIndex().isEmpty());
    m_libraryService->failLetters(1, QStringLiteral("no index"));
    QVERIFY(viewModel.letterIndex().isEmpty());

    // The index is kept per query: coming back to it shows it at once.
    viewModel.setFavoriteFilter(QStringLiteral("favorite"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    QCOMPARE(viewModel.letterIndex().size(), 3);
    QCOMPARE(m_libraryService->letterRequests.size(), 2);

    // Series children (no paging) and combined pages have no index.
    viewModel.loadLibrary(QStringLiteral("series-1"), QString(), 0, 0);
    QVERIFY(viewModel.letterIndex().isEmpty());
    viewModel.loadCombinedLibraries({QStringLiteral("a"), QStringLiteral("b")},
                                    QStringLiteral("movies"), 200);
    QVERIFY(viewModel.letterIndex().isEmpty());
    QCOMPARE(m_libraryService->letterRequests.size(), 2);
}

void LibraryViewModelCanonicalTest::letterJumpSeeksAndFillsPlaceholders()
{
    LibraryViewModel viewModel;
    viewModel.setSortBy(QStringLiteral("title"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    m_libraryService->succeedItem(0, numberedItems(0, 200), 1000);
    m_libraryService->succeedLetters(0, thousandTitleIndex());
    QCOMPARE(viewModel.rowCount(), 200);

    QSignalSpy ready(&viewModel, &LibraryViewModel::positionReady);
    QVERIFY(!viewModel.jumpToLetter(QStringLiteral("Q")));

    // A loaded letter answers at once.
    QVERIFY(viewModel.jumpToLetter(QStringLiteral("A")));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.takeFirst().at(0).toInt(), 0);
    QCOMPARE(m_libraryService->itemRequests.size(), 1);

    // M is past the loaded rows: the rows up to its window become
    // placeholders and the window is read at M's position.
    QVERIFY(viewModel.jumpToLetter(QStringLiteral("M")));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(viewModel.rowCount(), 700);
    QVERIFY(viewModel.getItem(200).value(QStringLiteral("bloomPlaceholder")).toBool());
    QVERIFY(viewModel.data(viewModel.index(650), LibraryViewModel::NameRole).toString().isEmpty());
    QCOMPARE(m_libraryService->itemRequests.size(), 2);
    LibraryItemQuery window = m_libraryService->itemRequests.last();
    QCOMPARE(window.startIndex, 600);
    QCOMPARE(window.limit, 100);
    QCOMPARE(window.sortBy, QStringLiteral("title"));
    // The same position again waits for the same window.
    viewModel.seekToPosition(600);
    QCOMPARE(m_libraryService->itemRequests.size(), 2);
    m_libraryService->succeedItem(1, numberedItems(600, 100), 1000);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.takeFirst().at(0).toInt(), 600);
    QCOMPARE(viewModel.getItem(600).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("item-600"));
    QCOMPARE(viewModel.rowCount(), 700);

    // Scrolling up from M reads the placeholder rows above it, upward.
    viewModel.ensureLoaded(599);
    QCOMPARE(m_libraryService->itemRequests.size(), 3);
    window = m_libraryService->itemRequests.last();
    QCOMPARE(window.startIndex, 500);
    QCOMPARE(window.limit, 100);
    viewModel.ensureLoaded(550);
    QCOMPARE(m_libraryService->itemRequests.size(), 3);
    // A loaded row needs nothing.
    viewModel.ensureLoaded(650);
    QCOMPARE(m_libraryService->itemRequests.size(), 3);
    m_libraryService->succeedItem(2, numberedItems(500, 100), 1000);
    QCOMPARE(viewModel.getItem(500).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("item-500"));
    QCOMPARE(viewModel.getItem(599).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("item-599"));
    QVERIFY(viewModel.getItem(499).value(QStringLiteral("bloomPlaceholder")).toBool());

    // Paging on continues after M's window.
    viewModel.loadMore(50);
    QCOMPARE(m_libraryService->itemRequests.last().startIndex, 700);
    m_libraryService->succeedItem(3, numberedItems(700, 50), 1000);
    QCOMPARE(viewModel.rowCount(), 750);

    // Z at 990: the window reaches the end of the list.
    QVERIFY(viewModel.jumpToLetter(QStringLiteral("Z")));
    QCOMPARE(viewModel.rowCount(), 1000);
    QCOMPARE(m_libraryService->itemRequests.last().startIndex, 990);
    QCOMPARE(m_libraryService->itemRequests.last().limit, 10);
}

void LibraryViewModelCanonicalTest::seekAnswersAroundPagesInFlight()
{
    LibraryViewModel viewModel;
    viewModel.setSortBy(QStringLiteral("title"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    m_libraryService->succeedItem(0, numberedItems(0, 200), 1000);
    m_libraryService->succeedLetters(0, thousandTitleIndex());

    // A page in flight when the jump adds placeholders fills its own rows.
    viewModel.loadMore(50);
    QCOMPARE(m_libraryService->itemRequests.last().startIndex, 200);
    QSignalSpy ready(&viewModel, &LibraryViewModel::positionReady);
    QVERIFY(viewModel.jumpToLetter(QStringLiteral("M")));
    QCOMPARE(viewModel.rowCount(), 700);
    m_libraryService->succeedItem(1, numberedItems(200, 50), 1000);
    QCOMPARE(viewModel.rowCount(), 700);
    QCOMPARE(viewModel.getItem(249).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("item-249"));
    QVERIFY(viewModel.getItem(250).value(QStringLiteral("bloomPlaceholder")).toBool());

    // The list shrank since the index was read: the window answers fewer
    // rows and the placeholders past the new end go.
    m_libraryService->succeedItem(2, numberedItems(600, 40), 640);
    QCOMPARE(viewModel.rowCount(), 640);
    QCOMPARE(viewModel.totalRecordCount(), 640);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toInt(), 600);

    // A new query drops the jump and its placeholders.
    viewModel.setSortOrder(QStringLiteral("descending"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    const int initial = static_cast<int>(m_libraryService->itemRequests.size()) - 1;
    m_libraryService->succeedItem(initial, numberedItems(0, 200), 1000);
    QCOMPARE(viewModel.rowCount(), 200);
    QVERIFY(!viewModel.getItem(199).value(QStringLiteral("bloomPlaceholder")).toBool());
}

void LibraryViewModelCanonicalTest::failedWindowLeavesPlaceholdersToRetry()
{
    LibraryViewModel viewModel;
    viewModel.setSortBy(QStringLiteral("title"));
    viewModel.loadLibrary(QStringLiteral("library"), QStringLiteral("movies"), 0, 200);
    m_libraryService->succeedItem(0, numberedItems(0, 200), 1000);
    m_libraryService->succeedLetters(0, thousandTitleIndex());

    QSignalSpy ready(&viewModel, &LibraryViewModel::positionReady);
    QVERIFY(viewModel.jumpToLetter(QStringLiteral("M")));
    m_libraryService->failItem(1, QStringLiteral("offline"));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(viewModel.rowCount(), 700);
    QVERIFY(viewModel.getItem(600).value(QStringLiteral("bloomPlaceholder")).toBool());
    QVERIFY(!viewModel.hasError());
    // Showing the rows again reads them again.
    viewModel.ensureLoaded(600);
    QCOMPARE(m_libraryService->itemRequests.size(), 3);
    QCOMPARE(m_libraryService->itemRequests.last().startIndex, 501);
    QCOMPARE(m_libraryService->itemRequests.last().limit, 100);
    m_libraryService->succeedItem(2, numberedItems(501, 100), 1000);
    QCOMPARE(viewModel.getItem(600).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("item-600"));
    QCOMPARE(ready.count(), 0);
}

QTEST_MAIN(LibraryViewModelCanonicalTest)
#include "LibraryViewModelCanonicalTest.moc"
