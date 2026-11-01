#include <QtTest/QtTest>

#include <QSignalSpy>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/LibrarySectionsViewModel.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct ItemsRequest {
        QString libraryId;
        QString sectionId;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool supportsLibrarySections() const override { return sections; }
    void getLibrarySections(const QString &libraryId) override { sectionRequests.append(libraryId); }
    void getLibrarySectionItems(const QString &libraryId, const QString &sectionId) override
    {
        itemRequests.append({libraryId, sectionId});
    }

    void loadRows(const QString &forConnection, const QString &libraryId, const QVariantList &rows)
    {
        emit canonicalLibrarySectionsLoaded(forConnection, libraryId, rows);
    }
    void failRows(const QString &forConnection, const QString &libraryId)
    {
        emit canonicalLibrarySectionsFailed(forConnection, libraryId, QStringLiteral("Unavailable"));
    }
    void loadItems(const QString &libraryId, const QString &sectionId, const QVariantList &items)
    {
        emit canonicalLibrarySectionItemsLoaded(connectionId, libraryId, sectionId, items);
    }
    void failItems(const QString &libraryId, const QString &sectionId)
    {
        emit canonicalLibrarySectionItemsFailed(connectionId, libraryId, sectionId,
                                                QStringLiteral("Unavailable"));
    }

    QString connectionId = QStringLiteral("silo-1");
    bool sections = true;
    QStringList sectionRequests;
    QList<ItemsRequest> itemRequests;
};

QVariantList items(std::initializer_list<const char *> ids)
{
    QVariantList list;
    for (const char *id : ids) {
        list.append(QVariantMap{{QStringLiteral("itemId"), QString::fromLatin1(id)},
                                {QStringLiteral("name"), QString::fromLatin1(id)},
                                {QStringLiteral("mediaType"), QStringLiteral("Movie")}});
    }
    return list;
}

QVariantMap row(const char *id, const QVariantList &cards, bool hasMore = false)
{
    return {{QStringLiteral("sectionId"), QString::fromLatin1(id)},
            {QStringLiteral("title"), QStringLiteral("Row %1").arg(QString::fromLatin1(id))},
            {QStringLiteral("sectionType"), QStringLiteral("genre")},
            {QStringLiteral("featured"), false},
            {QStringLiteral("totalCount"), int(cards.size()) + (hasMore ? 10 : 0)},
            {QStringLiteral("hasMore"), hasMore},
            {QStringLiteral("items"), cards}};
}

QStringList sectionIds(const LibrarySectionsViewModel &model)
{
    QStringList ids;
    for (int i = 0; i < model.rowCount(); ++i) {
        ids.append(model.data(model.index(i), LibrarySectionsViewModel::SectionIdRole).toString());
    }
    return ids;
}

QStringList cardIds(const LibrarySectionsViewModel &model, int rowIndex)
{
    QStringList ids;
    const QVariantList cards
        = model.data(model.index(rowIndex), LibrarySectionsViewModel::ItemsRole).toList();
    for (const QVariant &card : cards) {
        ids.append(card.toMap().value(QStringLiteral("itemId")).toString());
    }
    return ids;
}

} // namespace

class LibrarySectionsViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void rowsFollowTheServerAndBelongToOneLibrary();
    void reloadUpdatesRowsInPlace();
    void rowsWithMoreCardsReadTheRest();
    void focusIsFoundByIdAfterAReload();
    void providersWithoutRowsShowNone();
};

void LibrarySectionsViewModelTest::rowsFollowTheServerAndBelongToOneLibrary()
{
    ScriptedLibraryService service;
    LibrarySectionsViewModel model(&service);
    QSignalSpy countSpy(&model, &LibrarySectionsViewModel::countChanged);

    model.load(QStringLiteral("lib-1"));
    QVERIFY(model.available());
    QVERIFY(model.isLoading());
    QCOMPARE(service.sectionRequests, QStringList{QStringLiteral("lib-1")});
    QCOMPARE(model.libraryId(), QStringLiteral("lib-1"));

    // Answers for another connection or library are not this model's.
    service.loadRows(QStringLiteral("other"), QStringLiteral("lib-1"), {row("x", items({"a"}))});
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-2"), {row("x", items({"a"}))});
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.isLoading());

    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"a", "b"}), true), row("crime", items({"c"}))});
    QVERIFY(!model.isLoading());
    QCOMPARE(sectionIds(model), (QStringList{QStringLiteral("recent"), QStringLiteral("crime")}));
    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 1);
    const QModelIndex first = model.index(0);
    QCOMPARE(model.data(first, LibrarySectionsViewModel::TitleRole).toString(),
             QStringLiteral("Row recent"));
    QCOMPARE(model.data(first, LibrarySectionsViewModel::SectionTypeRole).toString(),
             QStringLiteral("genre"));
    QVERIFY(model.data(first, LibrarySectionsViewModel::HasMoreRole).toBool());
    QVERIFY(!model.data(first, LibrarySectionsViewModel::LoadingMoreRole).toBool());
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY(model.roleNames().values().contains("items"));

    // A failed reload keeps the rows.
    model.load(QStringLiteral("lib-1"));
    service.failRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"));
    QVERIFY(model.hasError());
    QCOMPARE(model.rowCount(), 2);

    // Another library drops the rows before its own arrive.
    model.load(QStringLiteral("lib-2"));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.hasError());
    // A late answer for the first library is dropped.
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"), {row("late", items({"z"}))});
    QCOMPARE(model.rowCount(), 0);
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-2"), {row("drama", items({"d"}))});
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("drama")});

    // Another connection too.
    service.connectionId = QStringLiteral("silo-2");
    model.load(QStringLiteral("lib-2"));
    QCOMPARE(model.rowCount(), 0);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.libraryId().isEmpty());
    QVERIFY(!model.isLoading());
    service.loadRows(QStringLiteral("silo-2"), QStringLiteral("lib-2"), {row("drama", items({"d"}))});
    QCOMPARE(model.rowCount(), 0);
}

void LibrarySectionsViewModelTest::reloadUpdatesRowsInPlace()
{
    ScriptedLibraryService service;
    LibrarySectionsViewModel model(&service);
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"a", "b"})), row("crime", items({"c"}))});

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"b", "a"})), row("crime", items({"c"}))});
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.first().at(0).toModelIndex().row(), 0);
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("b"), QStringLiteral("a")}));

    // Other rows or another order reset the model.
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("crime", items({"c"})), row("recent", items({"a"}))});
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(sectionIds(model), (QStringList{QStringLiteral("crime"), QStringLiteral("recent")}));
}

void LibrarySectionsViewModelTest::rowsWithMoreCardsReadTheRest()
{
    ScriptedLibraryService service;
    LibrarySectionsViewModel model(&service);
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"a", "b"}), true), row("crime", items({"c"}))});

    // A row without more needs nothing; out-of-range rows are ignored.
    model.loadMore(1);
    model.loadMore(7);
    QVERIFY(service.itemRequests.isEmpty());

    model.loadMore(0);
    model.loadMore(0);
    QCOMPARE(service.itemRequests.size(), 1);
    QCOMPARE(service.itemRequests.first().libraryId, QStringLiteral("lib-1"));
    QCOMPARE(service.itemRequests.first().sectionId, QStringLiteral("recent"));
    QVERIFY(model.data(model.index(0), LibrarySectionsViewModel::LoadingMoreRole).toBool());

    // A failure keeps the cards and stops asking: focus moves near the
    // row's end do not read it again until the rows are read again.
    service.failItems(QStringLiteral("lib-1"), QStringLiteral("recent"));
    QVERIFY(!model.data(model.index(0), LibrarySectionsViewModel::LoadingMoreRole).toBool());
    QVERIFY(!model.data(model.index(0), LibrarySectionsViewModel::HasMoreRole).toBool());
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    model.loadMore(0);
    QCOMPARE(service.itemRequests.size(), 1);
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"a", "b"}), true), row("crime", items({"c"}))});
    QVERIFY(model.data(model.index(0), LibrarySectionsViewModel::HasMoreRole).toBool());
    model.loadMore(0);
    QCOMPARE(service.itemRequests.size(), 2);

    // The whole row replaces the first page; it has no more after that.
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    service.loadItems(QStringLiteral("lib-2"), QStringLiteral("recent"), items({"x"}));
    QCOMPARE(cardIds(model, 0).size(), 2);
    service.loadItems(QStringLiteral("lib-1"), QStringLiteral("recent"), items({"a", "b", "d", "e"}));
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                             QStringLiteral("d"), QStringLiteral("e")}));
    QVERIFY(!model.data(model.index(0), LibrarySectionsViewModel::HasMoreRole).toBool());
    model.loadMore(0);
    QCOMPARE(service.itemRequests.size(), 2);

    // A reload that brings the first page again keeps the cards past it,
    // so a focused card there stays (a return from its page), with the
    // first page's cards refreshed. The row keeps its more flag, so its
    // next read replaces the merge with the whole row.
    model.load(QStringLiteral("lib-1"));
    QVariantList refreshed = items({"a", "b"});
    QVariantMap watched = refreshed.first().toMap();
    watched.insert(QStringLiteral("watched"), true);
    refreshed[0] = watched;
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", refreshed, true), row("crime", items({"c"}))});
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                             QStringLiteral("d"), QStringLiteral("e")}));
    QVERIFY(model.data(model.index(0), LibrarySectionsViewModel::ItemsRole)
                .toList().first().toMap().value(QStringLiteral("watched")).toBool());
    QVERIFY(model.data(model.index(0), LibrarySectionsViewModel::HasMoreRole).toBool());

    // A new first card pushes "b" out of the fresh first page: it is kept
    // by identity, not dropped because it sat inside the old first page.
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"n", "a"}), true), row("crime", items({"c"}))});
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("n"), QStringLiteral("a"),
                                             QStringLiteral("b"), QStringLiteral("d"),
                                             QStringLiteral("e")}));
    // The next read near the row's end repairs it with the server's row,
    // even when that has fewer cards than shown (one was removed).
    model.loadMore(0);
    QCOMPARE(service.itemRequests.size(), 3);
    service.loadItems(QStringLiteral("lib-1"), QStringLiteral("recent"), items({"n", "a", "b", "e"}));
    QCOMPARE(cardIds(model, 0), (QStringList{QStringLiteral("n"), QStringLiteral("a"),
                                             QStringLiteral("b"), QStringLiteral("e")}));
    QVERIFY(!model.data(model.index(0), LibrarySectionsViewModel::HasMoreRole).toBool());
}

void LibrarySectionsViewModelTest::focusIsFoundByIdAfterAReload()
{
    ScriptedLibraryService service;
    LibrarySectionsViewModel model(&service);
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("recent", items({"a", "b", "c"})), row("crime", items({"d", "e"}))});
    // The view saves {sectionId: "crime", itemId: "e"} (row 1, card 1).
    QCOMPARE(model.rowOfSection(QStringLiteral("crime")), 1);
    QCOMPARE(model.indexOfItem(1, QStringLiteral("e")), 1);

    // The shelves come back reordered with the card moved: the saved IDs
    // find them, not the saved positions.
    model.load(QStringLiteral("lib-1"));
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"),
                     {row("new", items({"x"})), row("crime", items({"f", "d", "e"})),
                      row("recent", items({"a"}))});
    const int shelf = model.rowOfSection(QStringLiteral("crime"));
    QCOMPARE(shelf, 1);
    QCOMPARE(model.indexOfItem(shelf, QStringLiteral("e")), 2);
    // Gone: the view falls back to the saved positions.
    QCOMPARE(model.rowOfSection(QStringLiteral("gone")), -1);
    QCOMPARE(model.indexOfItem(shelf, QStringLiteral("gone")), -1);
    QCOMPARE(model.indexOfItem(9, QStringLiteral("e")), -1);
}

void LibrarySectionsViewModelTest::providersWithoutRowsShowNone()
{
    ScriptedLibraryService service;
    service.sections = false;
    LibrarySectionsViewModel model(&service);
    QSignalSpy availableSpy(&model, &LibrarySectionsViewModel::availableChanged);
    model.load(QStringLiteral("lib-1"));
    QVERIFY(!model.available());
    QVERIFY(service.sectionRequests.isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.isLoading());

    service.sections = true;
    model.load(QStringLiteral("lib-1"));
    QVERIFY(model.available());
    QCOMPARE(availableSpy.count(), 1);
    service.loadRows(QStringLiteral("silo-1"), QStringLiteral("lib-1"), {row("crime", items({"c"}))});
    QCOMPARE(model.rowCount(), 1);

    // A sign-in to a provider without rows drops them.
    service.sections = false;
    model.load(QStringLiteral("lib-1"));
    QVERIFY(!model.available());
    QCOMPARE(model.rowCount(), 0);
}

QTEST_MAIN(LibrarySectionsViewModelTest)
#include "LibrarySectionsViewModelTest.moc"
