#include <QtTest/QtTest>

#include <QSignalSpy>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/WatchHistoryViewModel.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct PageRequest {
        QString libraryId;
        int startIndex = 0;
        QString cursor;
        int limit = 0;
        QString requestKey;
    };
    struct RemoveRequest {
        QString requestKey;
        QString itemId;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool watchHistoryByLibrary() const override { return byLibrary; }
    bool supportsWatchHistoryRemoval() const override { return removal; }
    void getViewsForRequest(const QString &requestKey) override
    {
        viewRequests.append(requestKey);
    }
    void getWatchHistory(const QString &libraryId, int startIndex, const QString &cursor,
                         int limit, const QString &requestKey) override
    {
        pageRequests.append({libraryId, startIndex, cursor, limit, requestKey});
    }
    void removeFromWatchHistory(const QString &requestKey, const QString &itemId) override
    {
        removeRequests.append({requestKey, itemId});
    }

    void answerViews(const QVariantList &views)
    {
        emit canonicalViewsLoadedForRequest(connectionId, viewRequests.last(), views);
    }
    void failViews()
    {
        emit canonicalViewsFailedForRequest(connectionId, viewRequests.last(),
                                            QStringLiteral("Server error"));
    }
    void answerPage(const PageRequest &request, const QVariantList &items, int total,
                    bool hasMore, const QString &nextCursor = QString(),
                    const QString &forConnection = QString())
    {
        emit canonicalWatchHistoryLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                         request.requestKey, items, total, hasMore, nextCursor);
    }
    void answerLast(const QVariantList &items, bool hasMore, const QString &nextCursor = QString())
    {
        answerPage(pageRequests.last(), items, int(items.size()), hasMore, nextCursor);
    }
    void failPage(const PageRequest &request)
    {
        emit canonicalWatchHistoryFailed(connectionId, request.requestKey,
                                         QStringLiteral("Server error"));
    }
    void answerRemove(bool succeeded, const QString &forConnection = QString())
    {
        emit homeCardWriteFinished(forConnection.isEmpty() ? connectionId : forConnection,
                                   removeRequests.last().requestKey, succeeded,
                                   succeeded ? QString() : QStringLiteral("Refused"));
    }
    PageRequest requestFor(const QString &libraryId) const
    {
        for (qsizetype i = pageRequests.size() - 1; i >= 0; --i) {
            if (pageRequests.at(i).libraryId == libraryId) {
                return pageRequests.at(i);
            }
        }
        return {};
    }

    QString connectionId = QStringLiteral("connection-1");
    bool byLibrary = false;
    bool removal = true;
    QStringList viewRequests;
    QList<PageRequest> pageRequests;
    QList<RemoveRequest> removeRequests;
};

QVariantMap card(const QString &itemId, const QString &mediaType,
                 const QString &lastPlayedAt = QString())
{
    QVariantMap item{
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType}
    };
    if (!lastPlayedAt.isEmpty()) {
        item.insert(QStringLiteral("userState"),
                    QVariantMap{{QStringLiteral("lastPlayedAt"), lastPlayedAt}});
    }
    return item;
}

QVariantMap library(const QString &itemId, const QString &collectionType)
{
    return {{QStringLiteral("connectionId"), QStringLiteral("connection-1")},
            {QStringLiteral("itemId"), itemId},
            {QStringLiteral("collectionType"), collectionType}};
}

QStringList shownIds(const WatchHistoryViewModel &model)
{
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        ids.append(model.get(row).value(QStringLiteral("itemId")).toString());
    }
    return ids;
}

QVariantMap actionById(const QVariantList &actions, const QString &id)
{
    for (const QVariant &action : actions) {
        if (action.toMap().value(QStringLiteral("id")).toString() == id) {
            return action.toMap();
        }
    }
    return {};
}

} // namespace

class WatchHistoryViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void streamPagesByCursorAndKeepsVideoCards();
    void streamReadsPastEmptyPages();
    void streamErrorsKeepShownCards();
    void librariesMergeByLastPlay();
    void librariesLeaveOutHiddenAndUnsupported();
    void failedLibrariesAndViews();
    void staleAnswersAreIgnored();
    void removalIsOptimisticAndRollsBack();
    void removalNeedsTheLiveConnection();
    void actionsFollowTheProvider();
};

void WatchHistoryViewModelTest::streamPagesByCursorAndKeepsVideoCards()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

    model.load();
    QVERIFY(model.isLoading());
    QCOMPARE(service.pageRequests.size(), 1);
    QCOMPARE(service.pageRequests.first().cursor, QString());
    QCOMPARE(service.pageRequests.first().limit, WatchHistoryViewModel::PageSize);
    QVERIFY(service.viewRequests.isEmpty());

    // Only movie, series and episode cards, each once.
    service.answerLast({card(QStringLiteral("series-a"), QStringLiteral("Series")),
                        card(QStringLiteral("book-b"), QStringLiteral("AudioBook")),
                        card(QStringLiteral("movie-c"), QStringLiteral("Movie")),
                        card(QStringLiteral("movie-c"), QStringLiteral("Movie")),
                        card(QStringLiteral("episode-d"), QStringLiteral("Episode"))},
                       true, QStringLiteral("cursor-1"));
    QVERIFY(!model.isLoading());
    QCOMPARE(shownIds(model), (QStringList{QStringLiteral("series-a"), QStringLiteral("movie-c"),
                                           QStringLiteral("episode-d")}));
    QVERIFY(model.hasMore());

    // The next page continues from the cursor and only adds rows.
    model.loadMore();
    QVERIFY(model.loadingMore());
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(service.pageRequests.last().cursor, QStringLiteral("cursor-1"));
    // One page at a time.
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 2);
    service.answerLast({card(QStringLiteral("movie-c"), QStringLiteral("Movie")),
                        card(QStringLiteral("movie-e"), QStringLiteral("Movie"))},
                       false);
    QCOMPARE(model.count(), 4);
    QVERIFY(!model.hasMore());
    QVERIFY(!model.loadingMore());
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(reset.count(), 0);
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(model.indexOfItem(QStringLiteral("movie-e")), 3);
    QCOMPARE(model.indexOfItem(QStringLiteral("missing")), -1);

    // A page that says more without a cursor ends paging.
    model.reload();
    service.answerLast({card(QStringLiteral("movie-x"), QStringLiteral("Movie"))}, true);
    QVERIFY(!model.hasMore());
}

void WatchHistoryViewModelTest::streamReadsPastEmptyPages()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);

    // Silo pages can hold only cards Bloom drops, or nothing, while more
    // remain: the next pages are read at once.
    model.load();
    service.answerLast({card(QStringLiteral("book-a"), QStringLiteral("AudioBook"))}, true,
                       QStringLiteral("c1"));
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(service.pageRequests.last().cursor, QStringLiteral("c1"));
    QVERIFY(model.isLoading());
    service.answerLast({}, true, QStringLiteral("c2"));
    QCOMPARE(service.pageRequests.size(), 3);
    service.answerLast({card(QStringLiteral("movie-b"), QStringLiteral("Movie"))}, true,
                       QStringLiteral("c3"));
    QCOMPARE(service.pageRequests.size(), 3);
    QVERIFY(!model.isLoading());
    QCOMPARE(model.count(), 1);

    // At most MaxEmptyPages in a row; then it waits for loadMore().
    model.reload();
    for (int page = 0; page < WatchHistoryViewModel::MaxEmptyPages; ++page) {
        service.answerLast({}, true, QStringLiteral("e%1").arg(page));
    }
    const qsizetype requests = service.pageRequests.size();
    QVERIFY(!model.isLoading());
    QCOMPARE(model.count(), 0);
    QVERIFY(model.hasMore());
    QVERIFY(!model.hasError());
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), requests + 1);
    QCOMPARE(service.pageRequests.last().cursor,
             QStringLiteral("e%1").arg(WatchHistoryViewModel::MaxEmptyPages - 1));
}

void WatchHistoryViewModelTest::streamErrorsKeepShownCards()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);

    model.load();
    service.failPage(service.pageRequests.last());
    QVERIFY(model.hasError());
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasMore());

    // Retry starts over.
    model.reload();
    QVERIFY(!model.hasError());
    service.answerLast({card(QStringLiteral("movie-a"), QStringLiteral("Movie"))}, true,
                       QStringLiteral("c1"));
    model.loadMore();
    service.failPage(service.pageRequests.last());
    QVERIFY(!model.hasError());
    QCOMPARE(model.count(), 1);
    QVERIFY(model.hasMore());
    QVERIFY(!model.loadingMore());
    // The next loadMore() tries the same page again.
    model.loadMore();
    QCOMPARE(service.pageRequests.last().cursor, QStringLiteral("c1"));

    // Without a live connection there is nothing to read.
    service.connectionId.clear();
    const qsizetype requests = service.pageRequests.size();
    model.reload();
    QVERIFY(model.hasError());
    QCOMPARE(service.pageRequests.size(), requests);
}

void WatchHistoryViewModelTest::librariesMergeByLastPlay()
{
    ScriptedLibraryService service;
    service.byLibrary = true;
    WatchHistoryViewModel model(&service);

    model.load();
    QCOMPARE(service.viewRequests.size(), 1);
    QVERIFY(service.pageRequests.isEmpty());
    service.answerViews({library(QStringLiteral("movies"), QStringLiteral("movies")),
                         library(QStringLiteral("shows"), QStringLiteral("tvshows"))});
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(service.pageRequests.at(0).libraryId, QStringLiteral("movies"));
    QCOMPARE(service.pageRequests.at(1).libraryId, QStringLiteral("shows"));
    QCOMPARE(service.pageRequests.at(0).startIndex, 0);

    // Nothing shows until every library has answered; then the newest play
    // comes first, whichever library holds it.
    service.answerPage(service.requestFor(QStringLiteral("movies")),
                       {card(QStringLiteral("m1"), QStringLiteral("Movie"), QStringLiteral("2026-10-31T20:00:00Z")),
                        card(QStringLiteral("m2"), QStringLiteral("Movie"), QStringLiteral("2026-10-01T20:00:00Z"))},
                       120, true);
    QCOMPARE(model.count(), 0);
    QVERIFY(model.isLoading());
    service.answerPage(service.requestFor(QStringLiteral("shows")),
                       {card(QStringLiteral("e1"), QStringLiteral("Episode"), QStringLiteral("2026-10-15T20:00:00Z"))},
                       1, false);
    // The shows are done, so the movies' own order decides the rest; their
    // next page is read for the cards still wanted.
    QCOMPARE(shownIds(model), (QStringList{QStringLiteral("m1"), QStringLiteral("e1"),
                                           QStringLiteral("m2")}));
    QVERIFY(model.isLoading());
    QCOMPARE(service.pageRequests.size(), 3);
    QCOMPARE(service.pageRequests.last().libraryId, QStringLiteral("movies"));
    QCOMPARE(service.pageRequests.last().startIndex, 2);
    service.answerPage(service.pageRequests.last(),
                       {card(QStringLiteral("m3"), QStringLiteral("Movie"), QStringLiteral("2026-09-01T20:00:00Z"))},
                       3, false);
    QCOMPARE(shownIds(model), (QStringList{QStringLiteral("m1"), QStringLiteral("e1"),
                                           QStringLiteral("m2"), QStringLiteral("m3")}));
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasMore());
}

void WatchHistoryViewModelTest::librariesLeaveOutHiddenAndUnsupported()
{
    ScriptedLibraryService service;
    service.byLibrary = true;
    WatchHistoryViewModel model(&service);

    model.load({QStringLiteral("hidden-movies")});
    service.answerViews({library(QStringLiteral("hidden-movies"), QStringLiteral("movies")),
                         library(QStringLiteral("music"), QStringLiteral("music")),
                         library(QStringLiteral("home"), QStringLiteral("homevideos")),
                         library(QStringLiteral("shows"), QStringLiteral("tvshows"))});
    QCOMPARE(service.pageRequests.size(), 1);
    QCOMPARE(service.pageRequests.first().libraryId, QStringLiteral("shows"));

    // Paging asks the library for its next offset once the page is used up.
    QVariantList page;
    for (int i = 0; i < WatchHistoryViewModel::PageSize; ++i) {
        page.append(card(QStringLiteral("e%1").arg(i), QStringLiteral("Episode"),
                         QStringLiteral("2026-10-01T20:00:00Z")));
    }
    service.answerPage(service.pageRequests.last(), page, 100, true);
    QCOMPARE(model.count(), WatchHistoryViewModel::PageSize);
    QVERIFY(model.hasMore());
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(service.pageRequests.last().startIndex, WatchHistoryViewModel::PageSize);

    // No supported library: an empty history, not an error.
    model.load();
    service.answerViews({library(QStringLiteral("music"), QStringLiteral("music"))});
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasError());
    QCOMPARE(model.count(), 0);
    QVERIFY(!model.hasMore());
}

void WatchHistoryViewModelTest::failedLibrariesAndViews()
{
    ScriptedLibraryService service;
    service.byLibrary = true;
    WatchHistoryViewModel model(&service);

    // One failed library leaves the other.
    model.load();
    service.answerViews({library(QStringLiteral("movies"), QStringLiteral("movies")),
                         library(QStringLiteral("shows"), QStringLiteral("tvshows"))});
    service.failPage(service.requestFor(QStringLiteral("movies")));
    QVERIFY(model.isLoading());
    service.answerPage(service.requestFor(QStringLiteral("shows")),
                       {card(QStringLiteral("e1"), QStringLiteral("Episode"), QStringLiteral("2026-10-15T20:00:00Z"))},
                       1, false);
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("e1")});
    QVERIFY(!model.hasError());
    QVERIFY(!model.isLoading());

    // Every library failing is an error.
    model.load();
    service.answerViews({library(QStringLiteral("movies"), QStringLiteral("movies")),
                         library(QStringLiteral("shows"), QStringLiteral("tvshows"))});
    service.failPage(service.requestFor(QStringLiteral("movies")));
    service.failPage(service.requestFor(QStringLiteral("shows")));
    QVERIFY(model.hasError());
    QVERIFY(!model.isLoading());

    // So is a failed library list.
    model.load();
    service.failViews();
    QVERIFY(model.hasError());
    QVERIFY(!model.isLoading());
}

void WatchHistoryViewModelTest::staleAnswersAreIgnored()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);

    model.load();
    const ScriptedLibraryService::PageRequest older = service.pageRequests.last();
    model.reload();
    // An older load's answer and another connection's answer do not land.
    service.answerPage(older, {card(QStringLiteral("old"), QStringLiteral("Movie"))}, 1, false);
    service.answerPage(service.pageRequests.last(),
                       {card(QStringLiteral("other"), QStringLiteral("Movie"))}, 1, false,
                       QString(), QStringLiteral("connection-2"));
    QCOMPARE(model.count(), 0);
    QVERIFY(model.isLoading());
    service.answerLast({card(QStringLiteral("new"), QStringLiteral("Movie"))}, false);
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("new")});

    // Two pages share the service; each answers only its own key.
    WatchHistoryViewModel second(&service);
    second.load();
    service.answerLast({card(QStringLiteral("second"), QStringLiteral("Movie"))}, false);
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("new")});
    QCOMPARE(shownIds(second), QStringList{QStringLiteral("second")});
}

void WatchHistoryViewModelTest::removalIsOptimisticAndRollsBack()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);
    model.load();
    service.answerLast({card(QStringLiteral("a"), QStringLiteral("Movie")),
                        card(QStringLiteral("b"), QStringLiteral("Series")),
                        card(QStringLiteral("c"), QStringLiteral("Movie"))},
                       false);
    QVERIFY(model.canRemove());
    QSignalSpy failed(&model, &WatchHistoryViewModel::actionFailed);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

    // The card leaves at once; the write goes out for it.
    QVERIFY(model.trigger(QStringLiteral("remove"), model.get(1)));
    QCOMPARE(shownIds(model), (QStringList{QStringLiteral("a"), QStringLiteral("c")}));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(service.removeRequests.size(), 1);
    QCOMPARE(service.removeRequests.last().itemId, QStringLiteral("b"));
    service.answerRemove(true);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(model.count(), 2);

    // A refusal puts it back in its place with a message.
    QVERIFY(model.trigger(QStringLiteral("remove"), model.get(0)));
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("c")});
    // A reply for another connection is not this write's.
    service.answerRemove(false, QStringLiteral("connection-2"));
    QCOMPARE(model.count(), 1);
    service.answerRemove(false);
    QCOMPARE(shownIds(model), (QStringList{QStringLiteral("a"), QStringLiteral("c")}));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(reset.count(), 0);

    // One write per card; a reload while it runs keeps the card out until
    // the server answers.
    QVERIFY(model.trigger(QStringLiteral("remove"), model.get(0)));
    QVERIFY(!model.trigger(QStringLiteral("remove"), card(QStringLiteral("a"), QStringLiteral("Movie"))));
    QVERIFY(!actionById(model.actionsFor(card(QStringLiteral("a"), QStringLiteral("Movie"))),
                        QStringLiteral("remove")).value(QStringLiteral("enabled")).toBool());
    model.reload();
    service.answerLast({card(QStringLiteral("a"), QStringLiteral("Movie")),
                        card(QStringLiteral("c"), QStringLiteral("Movie"))},
                       false);
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("c")});
    service.answerRemove(true);
    QCOMPARE(shownIds(model), QStringList{QStringLiteral("c")});

    // Unknown actions and cards are refused.
    QVERIFY(!model.trigger(QStringLiteral("openDetails"), model.get(0)));
    QVERIFY(!model.trigger(QStringLiteral("remove"), card(QStringLiteral("gone"), QStringLiteral("Movie"))));
}

void WatchHistoryViewModelTest::removalNeedsTheLiveConnection()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);
    model.load();
    service.answerLast({card(QStringLiteral("a"), QStringLiteral("Movie"))}, false);
    QSignalSpy failed(&model, &WatchHistoryViewModel::actionFailed);

    // A card of another connection is refused.
    QVariantMap foreign = model.get(0);
    foreign.insert(QStringLiteral("connectionId"), QStringLiteral("connection-2"));
    QVERIFY(!model.trigger(QStringLiteral("remove"), foreign));
    QCOMPARE(failed.count(), 1);
    // So is any card once the session moved on.
    service.connectionId = QStringLiteral("connection-2");
    QVERIFY(!model.trigger(QStringLiteral("remove"), model.get(0)));
    QCOMPARE(failed.count(), 2);
    QVERIFY(service.removeRequests.isEmpty());
    QCOMPARE(model.count(), 1);
}

void WatchHistoryViewModelTest::actionsFollowTheProvider()
{
    ScriptedLibraryService service;
    WatchHistoryViewModel model(&service);
    model.load();

    // Silo: open the title, and remove with a confirmation.
    QVariantList actions = model.actionsFor(card(QStringLiteral("s"), QStringLiteral("Series")));
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("openDetails"));
    QVERIFY(actions.first().toMap().value(QStringLiteral("navigation")).toBool());
    const QVariantMap remove = actionById(actions, QStringLiteral("remove"));
    QVERIFY(remove.value(QStringLiteral("enabled")).toBool());
    QVERIFY(!remove.value(QStringLiteral("confirmTitle")).toString().isEmpty());
    QVERIFY(remove.value(QStringLiteral("confirmMessage")).toString().contains(QStringLiteral("episode")));
    QVERIFY(!remove.value(QStringLiteral("confirmText")).toString().isEmpty());

    // Episodes go to their series, when they know it.
    QVariantMap episode = card(QStringLiteral("e"), QStringLiteral("Episode"));
    QVERIFY(!actionById(model.actionsFor(episode), QStringLiteral("openSeries"))
                 .value(QStringLiteral("enabled")).toBool());
    episode.insert(QStringLiteral("seriesId"), QStringLiteral("series-1"));
    QVERIFY(actionById(model.actionsFor(episode), QStringLiteral("openSeries"))
                .value(QStringLiteral("enabled")).toBool());

    // Jellyfin: no removal.
    service.removal = false;
    model.reload();
    QVERIFY(!model.canRemove());
    actions = model.actionsFor(card(QStringLiteral("m"), QStringLiteral("Movie")));
    QCOMPARE(actions.size(), 1);
    QVERIFY(actionById(actions, QStringLiteral("remove")).isEmpty());
    QVERIFY(!model.trigger(QStringLiteral("remove"), card(QStringLiteral("m"), QStringLiteral("Movie"))));
}

QTEST_MAIN(WatchHistoryViewModelTest)
#include "WatchHistoryViewModelTest.moc"
