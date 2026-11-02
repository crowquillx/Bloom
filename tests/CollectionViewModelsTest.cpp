#include <QtTest/QtTest>

#include <QSignalSpy>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/CollectionViewModels.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct LibraryRequest {
        QString libraryId;
        int startIndex = 0;
        int limit = 0;
        QString requestKey;
    };
    struct ItemsRequest {
        QString collectionId;
        QString collectionKind;
        QString sortBy;
        int startIndex = 0;
        QString cursor;
        int limit = 0;
        QString requestKey;
    };
    struct PartOfRequest {
        QString itemId;
        QString requestKey;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool supportsCollections() const override { return collectionsSupported; }
    void getLibraryCollections(const QString &libraryId, int startIndex, int limit,
                               const QString &requestKey) override
    {
        libraryRequests.append({libraryId, startIndex, limit, requestKey});
    }
    void getCollectionItems(const QString &collectionId, const QString &collectionKind,
                            const QString &sortBy, int startIndex, const QString &cursor,
                            int limit, const QString &requestKey) override
    {
        itemsRequests.append({collectionId, collectionKind, sortBy, startIndex, cursor, limit,
                              requestKey});
    }
    void getItemCollections(const QString &itemId, const QString &requestKey) override
    {
        partOfRequests.append({itemId, requestKey});
    }

    void answerLibrary(const QVariantList &collections, bool hasMore,
                       const QString &forConnection = QString(), int request = -1)
    {
        const LibraryRequest sent = request < 0 ? libraryRequests.last() : libraryRequests.at(request);
        emit canonicalLibraryCollectionsLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                               sent.libraryId, sent.requestKey, collections,
                                               int(collections.size()), hasMore);
    }
    void failLibrary()
    {
        const LibraryRequest sent = libraryRequests.last();
        emit canonicalLibraryCollectionsFailed(connectionId, sent.libraryId, sent.requestKey,
                                               QStringLiteral("Server error"));
    }
    void answerItems(const QVariantList &items, bool hasMore, const QString &nextCursor = QString(),
                     int total = -1, int request = -1, const QString &forConnection = QString())
    {
        const ItemsRequest sent = request < 0 ? itemsRequests.last() : itemsRequests.at(request);
        emit canonicalCollectionItemsLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                            sent.collectionId, sent.requestKey, items,
                                            total < 0 ? int(items.size()) : total, hasMore,
                                            nextCursor);
    }
    void failItems()
    {
        const ItemsRequest sent = itemsRequests.last();
        emit canonicalCollectionItemsFailed(connectionId, sent.collectionId, sent.requestKey,
                                            QStringLiteral("Server error"));
    }
    void answerPartOf(const QVariantList &collections, int request = -1,
                      const QString &forConnection = QString())
    {
        const PartOfRequest sent = request < 0 ? partOfRequests.last() : partOfRequests.at(request);
        emit canonicalItemCollectionsLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                            sent.itemId, sent.requestKey, collections);
    }
    void failPartOf()
    {
        const PartOfRequest sent = partOfRequests.last();
        emit canonicalItemCollectionsFailed(connectionId, sent.itemId, sent.requestKey,
                                            QStringLiteral("Server error"));
    }

    QString connectionId = QStringLiteral("connection-1");
    bool collectionsSupported = true;
    QList<LibraryRequest> libraryRequests;
    QList<ItemsRequest> itemsRequests;
    QList<PartOfRequest> partOfRequests;
};

QVariantMap collection(const QString &collectionId,
                       const QString &kind = QStringLiteral("library"),
                       const QString &connectionId = QStringLiteral("connection-1"))
{
    return {
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), collectionId},
        {QStringLiteral("collectionId"), collectionId},
        {QStringLiteral("collectionKind"), kind},
        {QStringLiteral("mediaType"), QStringLiteral("Collection")},
        {QStringLiteral("name"), collectionId},
        {QStringLiteral("itemCount"), 3}
    };
}

QVariantMap title(const QString &itemId, const QString &mediaType)
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType}
    };
}

QStringList rowIds(const QAbstractItemModel &model, int role)
{
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        ids.append(model.data(model.index(row, 0), role).toString());
    }
    return ids;
}

QStringList collectionIds(const QVariantList &collections)
{
    QStringList ids;
    for (const QVariant &value : collections) {
        ids.append(value.toMap().value(QStringLiteral("collectionId")).toString());
    }
    return ids;
}

} // namespace

class CollectionViewModelsTest : public QObject
{
    Q_OBJECT

private slots:
    // LibraryCollectionsViewModel
    void libraryCollectionsNeedTheCapability();
    void libraryCollectionsLoadOnceAndKeepValidCollections();
    void libraryCollectionsPageByServerCountWithInserts();
    void libraryCollectionsErrorsAndRetries();
    void libraryCollectionsIgnoreStaleAnswers();

    // CollectionViewModel
    void collectionLoadsItsTitlesOnly();
    void collectionPagesByCursorAndCount();
    void collectionSortReadsAgainAndDropsOldAnswers();
    void collectionErrorsAndRetries();
    void collectionOfAnotherSourceIsRefused();

    // ItemCollectionsViewModel
    void partOfLoadsTheTitlesCollections();
    void partOfKeepsCollectionsWhileReloadingTheSameTitle();
    void partOfFailureShowsNothing();
    void partOfIgnoresStaleAnswersAndUnsupportedProviders();
};

// ============================================================================
// LibraryCollectionsViewModel
// ============================================================================

void CollectionViewModelsTest::libraryCollectionsNeedTheCapability()
{
    ScriptedLibraryService service;
    service.collectionsSupported = false;
    LibraryCollectionsViewModel model(&service);

    model.load(QStringLiteral("library-1"));
    QVERIFY(!model.available());
    QVERIFY(service.libraryRequests.isEmpty());
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasError());
    QCOMPARE(model.count(), 0);
}

void CollectionViewModelsTest::libraryCollectionsLoadOnceAndKeepValidCollections()
{
    ScriptedLibraryService service;
    LibraryCollectionsViewModel model(&service);

    model.load(QStringLiteral("library-1"));
    QVERIFY(model.available());
    QCOMPARE(service.libraryRequests.size(), 1);
    QCOMPARE(service.libraryRequests.first().libraryId, QStringLiteral("library-1"));
    QCOMPARE(service.libraryRequests.first().startIndex, 0);
    QCOMPARE(service.libraryRequests.first().limit, LibraryCollectionsViewModel::PageSize);
    QVERIFY(model.isLoading());
    QVERIFY(!model.loaded());

    // Asking again while it loads sends nothing new.
    model.load(QStringLiteral("library-1"));
    QCOMPARE(service.libraryRequests.size(), 1);

    QVariantMap noKind = collection(QStringLiteral("no-kind"));
    noKind.remove(QStringLiteral("collectionKind"));
    service.answerLibrary({collection(QStringLiteral("c1")), collection(QStringLiteral("c1")),
                           noKind, collection(QStringLiteral("c2"), QStringLiteral("user"))},
                          false);
    QVERIFY(!model.isLoading());
    QVERIFY(model.loaded());
    QCOMPARE(rowIds(model, LibraryCollectionsViewModel::CollectionIdRole),
             (QStringList{QStringLiteral("c1"), QStringLiteral("c2")}));
    QCOMPARE(model.indexOfCollection(QStringLiteral("c2")), 1);
    QCOMPARE(model.indexOfCollection(QStringLiteral("missing")), -1);
    QCOMPARE(model.get(1).value(QStringLiteral("collectionKind")).toString(), QStringLiteral("user"));
    QVERIFY(model.get(5).isEmpty());

    // A loaded library is kept; another one starts over.
    model.load(QStringLiteral("library-1"));
    QCOMPARE(service.libraryRequests.size(), 1);
    model.load(QStringLiteral("library-2"));
    QCOMPARE(service.libraryRequests.size(), 2);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.libraryId(), QStringLiteral("library-2"));

    // So does another connection for the same library.
    service.answerLibrary({}, false);
    service.connectionId = QStringLiteral("connection-2");
    model.load(QStringLiteral("library-2"));
    QCOMPARE(service.libraryRequests.size(), 3);

    model.clear();
    QVERIFY(model.libraryId().isEmpty());
    QVERIFY(!model.loaded());
}

void CollectionViewModelsTest::libraryCollectionsPageByServerCountWithInserts()
{
    ScriptedLibraryService service;
    LibraryCollectionsViewModel model(&service);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);

    model.load(QStringLiteral("library-1"));
    // The page counts what the server sent, an invalid entry too.
    QVariantMap unnamed = collection(QStringLiteral(""));
    service.answerLibrary({collection(QStringLiteral("c1")), unnamed}, true);
    QVERIFY(model.hasMore());
    model.loadMore();
    QVERIFY(model.loadingMore());
    QCOMPARE(service.libraryRequests.last().startIndex, 2);
    // One page at a time.
    model.loadMore();
    QCOMPARE(service.libraryRequests.size(), 2);

    service.answerLibrary({collection(QStringLiteral("c2"))}, false);
    QVERIFY(!model.loadingMore());
    QVERIFY(!model.hasMore());
    QCOMPARE(rowIds(model, LibraryCollectionsViewModel::CollectionIdRole),
             (QStringList{QStringLiteral("c1"), QStringLiteral("c2")}));
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserts.size(), 2);
    model.loadMore();
    QCOMPARE(service.libraryRequests.size(), 2);

    // An empty page ends paging even when the server says more.
    model.reload();
    service.answerLibrary({}, true);
    QVERIFY(!model.hasMore());
}

void CollectionViewModelsTest::libraryCollectionsErrorsAndRetries()
{
    ScriptedLibraryService service;
    LibraryCollectionsViewModel model(&service);

    model.load(QStringLiteral("library-1"));
    service.failLibrary();
    QVERIFY(model.hasError());
    QVERIFY(!model.isLoading());
    QVERIFY(!model.loaded());

    model.reload();
    QVERIFY(!model.hasError());
    QCOMPARE(service.libraryRequests.size(), 2);
    service.answerLibrary({collection(QStringLiteral("c1"))}, true);

    // A failed later page keeps the collections and tries again.
    model.loadMore();
    service.failLibrary();
    QVERIFY(!model.hasError());
    QVERIFY(model.hasMore());
    QVERIFY(!model.loadingMore());
    QCOMPARE(model.count(), 1);
    model.loadMore();
    QCOMPARE(service.libraryRequests.size(), 4);
    QCOMPARE(service.libraryRequests.last().startIndex, 1);
}

void CollectionViewModelsTest::libraryCollectionsIgnoreStaleAnswers()
{
    ScriptedLibraryService service;
    LibraryCollectionsViewModel model(&service);

    model.load(QStringLiteral("library-1"));
    model.load(QStringLiteral("library-2"));
    // The first library answers late.
    service.answerLibrary({collection(QStringLiteral("old"))}, false, QString(), 0);
    QCOMPARE(model.count(), 0);
    QVERIFY(model.isLoading());
    // Another connection's answer for the right request is not this one's.
    service.answerLibrary({collection(QStringLiteral("other"))}, false,
                          QStringLiteral("connection-2"));
    QCOMPARE(model.count(), 0);
    service.answerLibrary({collection(QStringLiteral("c1"))}, false);
    QCOMPARE(model.count(), 1);
    // The same answer twice is applied once.
    service.answerLibrary({collection(QStringLiteral("c9"))}, false);
    QCOMPARE(model.count(), 1);
}

// ============================================================================
// CollectionViewModel
// ============================================================================

void CollectionViewModelsTest::collectionLoadsItsTitlesOnly()
{
    ScriptedLibraryService service;
    CollectionViewModel model(&service);

    model.load(collection(QStringLiteral("c1")));
    QCOMPARE(model.collectionId(), QStringLiteral("c1"));
    QCOMPARE(model.connectionId(), QStringLiteral("connection-1"));
    QCOMPARE(model.collection().value(QStringLiteral("name")).toString(), QStringLiteral("c1"));
    QCOMPARE(model.sorts(), (QStringList{QString(), QStringLiteral("releaseDate"), QStringLiteral("title")}));
    QCOMPARE(service.itemsRequests.size(), 1);
    const auto request = service.itemsRequests.first();
    QCOMPARE(request.collectionId, QStringLiteral("c1"));
    QCOMPARE(request.collectionKind, QStringLiteral("library"));
    QVERIFY(request.sortBy.isEmpty());
    QCOMPARE(request.startIndex, 0);
    QVERIFY(request.cursor.isEmpty());
    QCOMPARE(request.limit, CollectionViewModel::PageSize);
    QVERIFY(model.isLoading());

    QVariantMap noId = title(QString(), QStringLiteral("Movie"));
    service.answerItems({title(QStringLiteral("m1"), QStringLiteral("Movie")),
                         title(QStringLiteral("e1"), QStringLiteral("Episode")),
                         title(QStringLiteral("m1"), QStringLiteral("Movie")),
                         noId,
                         title(QStringLiteral("s1"), QStringLiteral("Series"))},
                        false);
    QVERIFY(!model.isLoading());
    QCOMPARE(rowIds(model, CollectionViewModel::ItemIdRole),
             (QStringList{QStringLiteral("m1"), QStringLiteral("s1")}));
    QCOMPARE(model.data(model.index(1, 0), CollectionViewModel::MediaTypeRole).toString(),
             QStringLiteral("Series"));
    QCOMPARE(model.indexOfItem(QStringLiteral("s1")), 1);
    QCOMPARE(model.indexOfItem(QStringLiteral("e1")), -1);
    QCOMPARE(model.get(0).value(QStringLiteral("itemId")).toString(), QStringLiteral("m1"));
    // Every title is in; the count is what the page shows.
    QCOMPARE(model.totalCount(), 2);
}

void CollectionViewModelsTest::collectionPagesByCursorAndCount()
{
    ScriptedLibraryService service;
    CollectionViewModel model(&service);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);

    model.load(collection(QStringLiteral("c1")));
    service.answerItems({title(QStringLiteral("m1"), QStringLiteral("Movie")),
                         title(QStringLiteral("e1"), QStringLiteral("Episode"))},
                        true, QStringLiteral("cursor-2"), 5);
    QVERIFY(model.hasMore());
    // The server counts an episode Bloom does not show.
    QCOMPARE(model.totalCount(), 4);
    model.loadMore();
    QVERIFY(model.loadingMore());
    QCOMPARE(service.itemsRequests.last().startIndex, 2);
    QCOMPARE(service.itemsRequests.last().cursor, QStringLiteral("cursor-2"));
    model.loadMore();
    QCOMPARE(service.itemsRequests.size(), 2);

    service.answerItems({title(QStringLiteral("m2"), QStringLiteral("Movie"))}, false);
    QVERIFY(!model.hasMore());
    QVERIFY(!model.loadingMore());
    QCOMPARE(rowIds(model, CollectionViewModel::ItemIdRole),
             (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserts.size(), 2);
}

void CollectionViewModelsTest::collectionSortReadsAgainAndDropsOldAnswers()
{
    ScriptedLibraryService service;
    CollectionViewModel model(&service);
    QSignalSpy sortChanged(&model, &CollectionViewModel::sortByChanged);

    model.load(collection(QStringLiteral("c1")));
    service.answerItems({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, true,
                        QStringLiteral("cursor-2"));
    model.loadMore();

    model.setSortBy(QStringLiteral("releaseDate"));
    QCOMPARE(sortChanged.size(), 1);
    QCOMPARE(model.sortBy(), QStringLiteral("releaseDate"));
    QCOMPARE(service.itemsRequests.size(), 3);
    QCOMPARE(service.itemsRequests.last().sortBy, QStringLiteral("releaseDate"));
    // A new order starts from the first page.
    QCOMPARE(service.itemsRequests.last().startIndex, 0);
    QVERIFY(service.itemsRequests.last().cursor.isEmpty());
    QCOMPARE(model.count(), 0);
    QVERIFY(!model.loadingMore());

    // The page asked for in the old order is not this order's.
    service.answerItems({title(QStringLiteral("old"), QStringLiteral("Movie"))}, false, QString(), -1, 1);
    QCOMPARE(model.count(), 0);
    service.answerItems({title(QStringLiteral("m9"), QStringLiteral("Movie")),
                         title(QStringLiteral("m1"), QStringLiteral("Movie"))},
                        false);
    QCOMPARE(rowIds(model, CollectionViewModel::ItemIdRole),
             (QStringList{QStringLiteral("m9"), QStringLiteral("m1")}));

    // Unknown sorts and the same sort change nothing.
    model.setSortBy(QStringLiteral("random"));
    model.setSortBy(QStringLiteral("releaseDate"));
    QCOMPARE(model.sortBy(), QStringLiteral("releaseDate"));
    QCOMPARE(service.itemsRequests.size(), 3);
    QCOMPARE(sortChanged.size(), 1);

    // A sort chosen before the collection is kept for its first read.
    CollectionViewModel early(&service);
    early.setSortBy(QStringLiteral("title"));
    QCOMPARE(service.itemsRequests.size(), 3);
    early.load(collection(QStringLiteral("c2")));
    QCOMPARE(service.itemsRequests.last().sortBy, QStringLiteral("title"));
}

void CollectionViewModelsTest::collectionErrorsAndRetries()
{
    ScriptedLibraryService service;
    CollectionViewModel model(&service);

    model.load(collection(QStringLiteral("c1")));
    service.failItems();
    QVERIFY(model.hasError());
    QVERIFY(!model.isLoading());
    model.reload();
    QVERIFY(!model.hasError());
    QCOMPARE(service.itemsRequests.size(), 2);

    service.answerItems({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, true,
                        QStringLiteral("cursor-2"));
    model.loadMore();
    service.failItems();
    QVERIFY(!model.hasError());
    QVERIFY(model.hasMore());
    QCOMPARE(model.count(), 1);
    model.loadMore();
    QCOMPARE(service.itemsRequests.size(), 4);
    QCOMPARE(service.itemsRequests.last().cursor, QStringLiteral("cursor-2"));
}

void CollectionViewModelsTest::collectionOfAnotherSourceIsRefused()
{
    ScriptedLibraryService service;
    CollectionViewModel model(&service);

    model.load(collection(QStringLiteral("c1"), QStringLiteral("library"),
                          QStringLiteral("connection-2")));
    QVERIFY(model.hasError());
    QVERIFY(service.itemsRequests.isEmpty());

    // A map that is not a collection is refused too.
    model.load({{QStringLiteral("name"), QStringLiteral("Nothing")}});
    QVERIFY(model.hasError());
    QVERIFY(service.itemsRequests.isEmpty());

    // Answers for another collection are ignored.
    model.load(collection(QStringLiteral("c1")));
    QVERIFY(!model.hasError());
    emit service.canonicalCollectionItemsLoaded(
        QStringLiteral("connection-1"), QStringLiteral("c2"), service.itemsRequests.last().requestKey,
        {title(QStringLiteral("m1"), QStringLiteral("Movie"))}, 1, false, QString());
    QCOMPARE(model.count(), 0);
    QVERIFY(model.isLoading());
}

// ============================================================================
// ItemCollectionsViewModel
// ============================================================================

void CollectionViewModelsTest::partOfLoadsTheTitlesCollections()
{
    ScriptedLibraryService service;
    ItemCollectionsViewModel model(&service);
    QSignalSpy changed(&model, &ItemCollectionsViewModel::collectionsChanged);

    model.load(QStringLiteral("movie-1"));
    QCOMPARE(service.partOfRequests.size(), 1);
    QCOMPARE(service.partOfRequests.first().itemId, QStringLiteral("movie-1"));
    QVERIFY(model.loading());

    QVariantMap noKind = collection(QStringLiteral("x"));
    noKind.remove(QStringLiteral("collectionKind"));
    service.answerPartOf({collection(QStringLiteral("c1"), QStringLiteral("boxSet")),
                          collection(QStringLiteral("c1"), QStringLiteral("boxSet")), noKind});
    QVERIFY(!model.loading());
    QCOMPARE(collectionIds(model.collections()), QStringList{QStringLiteral("c1")});
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.itemId(), QStringLiteral("movie-1"));

    // Another title clears them at once.
    model.load(QStringLiteral("movie-2"));
    QCOMPARE(model.count(), 0);
    service.answerPartOf({});
    QCOMPARE(model.count(), 0);

    model.clear();
    QVERIFY(model.itemId().isEmpty());
    QVERIFY(!model.loading());
    QVERIFY(changed.size() >= 3);
}

void CollectionViewModelsTest::partOfKeepsCollectionsWhileReloadingTheSameTitle()
{
    ScriptedLibraryService service;
    ItemCollectionsViewModel model(&service);

    model.load(QStringLiteral("movie-1"));
    service.answerPartOf({collection(QStringLiteral("c1"))});
    QSignalSpy changed(&model, &ItemCollectionsViewModel::collectionsChanged);

    // A return to the page keeps the shelf until the answer.
    model.load(QStringLiteral("movie-1"));
    QCOMPARE(model.count(), 1);
    QVERIFY(model.loading());
    // The same answer changes nothing the shelf binds to.
    service.answerPartOf({collection(QStringLiteral("c1"))});
    QCOMPARE(changed.size(), 0);
    QVERIFY(!model.loading());
}

void CollectionViewModelsTest::partOfFailureShowsNothing()
{
    ScriptedLibraryService service;
    ItemCollectionsViewModel model(&service);

    model.load(QStringLiteral("movie-1"));
    service.answerPartOf({collection(QStringLiteral("c1"))});
    model.load(QStringLiteral("movie-1"));
    service.failPartOf();
    QCOMPARE(model.count(), 0);
    QVERIFY(!model.loading());
}

void CollectionViewModelsTest::partOfIgnoresStaleAnswersAndUnsupportedProviders()
{
    ScriptedLibraryService service;
    ItemCollectionsViewModel model(&service);

    model.load(QStringLiteral("movie-1"));
    model.load(QStringLiteral("movie-2"));
    // The first title answers late.
    service.answerPartOf({collection(QStringLiteral("old"))}, 0);
    QCOMPARE(model.count(), 0);
    QVERIFY(model.loading());
    service.answerPartOf({collection(QStringLiteral("other"))}, -1, QStringLiteral("connection-2"));
    QCOMPARE(model.count(), 0);
    service.answerPartOf({collection(QStringLiteral("c2"))});
    QCOMPARE(collectionIds(model.collections()), QStringList{QStringLiteral("c2")});

    ScriptedLibraryService unsupported;
    unsupported.collectionsSupported = false;
    ItemCollectionsViewModel none(&unsupported);
    none.load(QStringLiteral("movie-1"));
    QVERIFY(unsupported.partOfRequests.isEmpty());
    QVERIFY(!none.loading());
    QCOMPARE(none.count(), 0);
}

QTEST_GUILESS_MAIN(CollectionViewModelsTest)
#include "CollectionViewModelsTest.moc"
