#include <QtTest/QtTest>
#include <QSignalSpy>

#include "network/AuthenticationService.h"
#include "network/LibraryService.h"
#include "player/PlaybackPolicy.h"
#include "player/ShuffleSession.h"

namespace {

QVariantMap episode(const QString &id, const QString &seriesId = QStringLiteral("series-1"),
                    int seasonNumber = 1, const QString &seasonId = QString())
{
    return {{QStringLiteral("itemId"), id},
            {QStringLiteral("mediaType"), QStringLiteral("Episode")},
            {QStringLiteral("seriesId"), seriesId},
            {QStringLiteral("seasonId"), seasonId},
            {QStringLiteral("parentId"), seasonId.isEmpty() ? seriesId : seasonId},
            {QStringLiteral("parentIndexNumber"), seasonNumber},
            {QStringLiteral("indexNumber"), 1},
            {QStringLiteral("positionMs"), 600000}};
}

QVariantMap movie(const QString &id)
{
    return {{QStringLiteral("itemId"), id}, {QStringLiteral("mediaType"), QStringLiteral("Movie")}};
}

QVariantMap serverShuffle(const QVariantMap &current, const QVariantMap &next)
{
    return {{QStringLiteral("shuffleId"), QStringLiteral("shuffle-1")},
            {QStringLiteral("scopeTitle"), QStringLiteral("Dark")},
            {QStringLiteral("current"), current},
            {QStringLiteral("next"), next}};
}

QVariantMap scope(const QString &kind, const QString &id = QStringLiteral("series-1"))
{
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("id"), id},
            {QStringLiteral("title"), QStringLiteral("Dark")}};
}

QString itemIdOf(const QVariant &value)
{
    return value.toMap().value(QStringLiteral("itemId")).toString();
}

} // namespace

class FakeShuffleLibrary final : public LibraryService
{
    Q_OBJECT

public:
    explicit FakeShuffleLibrary(AuthenticationService *authService)
        : LibraryService(authService)
    {
    }

    bool supportsShuffle(const QString &kind, const QString &collectionKind = QString()) const override
    {
        Q_UNUSED(collectionKind)
        return supported && !unsupportedKinds.contains(kind);
    }
    bool hasServerShuffle() const override { return server; }
    void loadShuffleCapability() override {}
    QString getActiveConnectionId() const override { return connectionId; }

    void createShuffle(const QVariantMap &shuffleScope, const QString &requestKey) override
    {
        createdScopes.append(shuffleScope);
        keys.append(requestKey);
    }
    void getShuffle(const QString &shuffleId, const QString &requestKey) override
    {
        reads.append(shuffleId);
        keys.append(requestKey);
    }
    void advanceShuffle(const QString &shuffleId, const QString &fromContentId,
                        const QString &requestKey) override
    {
        Q_UNUSED(shuffleId)
        advancedFrom.append(fromContentId);
        keys.append(requestKey);
    }
    void skipShuffleItem(const QString &shuffleId, const QString &nextContentId,
                         const QString &requestKey) override
    {
        Q_UNUSED(shuffleId)
        skipped.append(nextContentId);
        keys.append(requestKey);
    }
    void deleteShuffle(const QString &shuffleId) override { deleted.append(shuffleId); }
    void getShuffleItems(const QVariantMap &shuffleScope, int limit, const QString &requestKey) override
    {
        Q_UNUSED(shuffleScope)
        pageLimits.append(limit);
        keys.append(requestKey);
        if (autoScope.isEmpty() || pageLimits.size() > kMaxAutoPages) {
            return;
        }
        // Answers at once, like a server that shuffles the whole scope into
        // every page, in a different order each time.
        QVariantList page;
        const qsizetype size = autoScope.size();
        for (qsizetype i = 0; i < size; ++i) {
            page.append(autoScope.at((i + pageLimits.size()) % size));
        }
        answerPage(page, static_cast<int>(size));
    }
    using LibraryService::getItems;
    void getItems(const LibraryItemQuery &query) override { itemQueries.append(query); }

    void answer(const QVariantMap &result, const QString &connection = QString())
    {
        emit shuffleLoaded(connection.isEmpty() ? connectionId : connection, keys.last(), result);
    }
    void fail(int httpStatus, const QString &message = QStringLiteral("Server error"))
    {
        emit shuffleFailed(connectionId, keys.last(), httpStatus, message);
    }
    void answerPage(const QVariantList &items, int total)
    {
        answer({{QStringLiteral("items"), items}, {QStringLiteral("total"), total}});
    }
    void answerSeasons(const QVariantList &seasons)
    {
        const LibraryItemQuery query = itemQueries.last();
        emit canonicalItemsLoadedForConnection(connectionId, query.parentId, query.requestKey,
                                               seasons, static_cast<int>(seasons.size()));
    }

    // At most this many pages are answered on their own, so a session that
    // keeps reading shows up as a count instead of endless recursion.
    static constexpr int kMaxAutoPages = 40;
    QVariantList autoScope;
    bool supported = true;
    bool server = true;
    QStringList unsupportedKinds;
    QString connectionId = QStringLiteral("connection-1");
    QVariantList createdScopes;
    QStringList keys;
    QStringList reads;
    QStringList advancedFrom;
    QStringList skipped;
    QStringList deleted;
    QList<int> pageLimits;
    QList<LibraryItemQuery> itemQueries;
};

class ShuffleSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void serverShuffleStartsAdvancesSkipsAndStops();
    void serverEpisodeResolvesItsOwnSeason();
    void serverShuffleEndsWhenGoneOrStuck();
    void advanceDuringASkipRunsAfterIt();
    void repliesForAnotherConnectionOrShuffleAreDropped();
    void startIsRefusedWithoutSupport();
    void localShuffleQueuesWithoutRepeatsAndRefills();
    void localSkipReturnsTheItemToThePool();
    void localShuffleWithNothingPlayableFails();
    void smallLocalScopesStopReadingAndAlwaysHaveANext_data();
    void smallLocalScopesStopReadingAndAlwaysHaveANext();
    void failedRefillWhileUpNextWaitsEndsTheShuffle();
    void droppedRequestsEndAStartingOrMovingShuffle();
    void seasonListingFailuresAreNotKept();
};

void ShuffleSessionTest::serverShuffleStartsAdvancesSkipsAndStops()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);
    QSignalSpy next(&session, &ShuffleSession::nextChanged);

    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    QCOMPARE(session.state(), ShuffleSession::State::Starting);
    QVERIFY(session.usesServer());
    QCOMPARE(library.createdScopes.size(), 1);
    QCOMPARE(library.createdScopes.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("7"));

    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    QCOMPARE(session.state(), ShuffleSession::State::Ready);
    QCOMPARE(session.shuffleId(), QStringLiteral("shuffle-1"));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(itemIdOf(ready.last().first()), QStringLiteral("m1"));
    QCOMPARE(itemIdOf(next.last().first()), QStringLiteral("m2"));

    // Moving on is anchored on the item that finished.
    session.advance();
    QCOMPARE(session.state(), ShuffleSession::State::Moving);
    QCOMPARE(library.advancedFrom, QStringList{QStringLiteral("m1")});
    library.answer(serverShuffle(movie(QStringLiteral("m2")), movie(QStringLiteral("m3"))));
    QCOMPARE(ready.size(), 2);
    QCOMPARE(itemIdOf(ready.last().first()), QStringLiteral("m2"));
    QCOMPARE(session.currentItemId(), QStringLiteral("m2"));

    // A skip replaces the announced next item, never the current one.
    session.skipNext();
    QCOMPARE(library.skipped, QStringList{QStringLiteral("m3")});
    library.answer(serverShuffle(movie(QStringLiteral("m2")), movie(QStringLiteral("m4"))));
    QCOMPARE(ready.size(), 2);
    QCOMPARE(itemIdOf(next.last().first()), QStringLiteral("m4"));
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("m4"));

    // A read that finds a new next item announces it.
    session.refresh();
    QCOMPARE(library.reads, QStringList{QStringLiteral("shuffle-1")});
    library.answer(serverShuffle(movie(QStringLiteral("m2")), movie(QStringLiteral("m5"))));
    QCOMPARE(itemIdOf(next.last().first()), QStringLiteral("m5"));

    session.stop();
    QCOMPARE(library.deleted, QStringList{QStringLiteral("shuffle-1")});
    QVERIFY(!session.isActive());
    // A late reply belongs to no shuffle.
    library.answer(serverShuffle(movie(QStringLiteral("m9")), movie(QStringLiteral("m8"))));
    QCOMPARE(ready.size(), 2);
}

void ShuffleSessionTest::serverEpisodeResolvesItsOwnSeason()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);

    // Silo cards name the series and the season number, not the season.
    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.answer(serverShuffle(episode(QStringLiteral("e1"), QStringLiteral("series-1"), 2),
                                 episode(QStringLiteral("e2"), QStringLiteral("series-1"), 1)));
    QCOMPARE(ready.size(), 0);
    QCOMPARE(library.itemQueries.size(), 1);
    QCOMPARE(library.itemQueries.first().parentId, QStringLiteral("series-1"));
    QCOMPARE(library.itemQueries.first().includeItemTypes, QStringList{QStringLiteral("Season")});
    library.answerSeasons({QVariantMap{{QStringLiteral("itemId"), QStringLiteral("season-a")},
                                       {QStringLiteral("indexNumber"), 1}},
                           QVariantMap{{QStringLiteral("itemId"), QStringLiteral("season-b")},
                                       {QStringLiteral("indexNumber"), 2}}});
    QCOMPARE(ready.size(), 1);
    QVariantMap played = ready.last().first().toMap();
    QCOMPARE(played.value(QStringLiteral("seasonId")).toString(), QStringLiteral("season-b"));
    QCOMPARE(played.value(QStringLiteral("parentId")).toString(), QStringLiteral("season-b"));

    // The series' seasons are read once.
    session.advance();
    library.answer(serverShuffle(episode(QStringLiteral("e2"), QStringLiteral("series-1"), 1),
                                 episode(QStringLiteral("e3"), QStringLiteral("series-1"), 2)));
    QCOMPARE(library.itemQueries.size(), 1);
    QCOMPARE(ready.size(), 2);
    QCOMPARE(ready.last().first().toMap().value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-a"));

    // A season scope names the season itself.
    QVERIFY(session.start(scope(QStringLiteral("season"), QStringLiteral("season-x"))));
    library.answer(serverShuffle(episode(QStringLiteral("e7"), QStringLiteral("series-2"), 4),
                                 episode(QStringLiteral("e8"), QStringLiteral("series-2"), 4)));
    QCOMPARE(library.itemQueries.size(), 1);
    QCOMPARE(ready.last().first().toMap().value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-x"));

    // A failed listing plays the episode without a season scope.
    QVERIFY(session.start(scope(QStringLiteral("series"), QStringLiteral("series-3"))));
    library.answer(serverShuffle(episode(QStringLiteral("e9"), QStringLiteral("series-3"), 1),
                                 episode(QStringLiteral("e10"), QStringLiteral("series-3"), 1)));
    const LibraryItemQuery seasons = library.itemQueries.last();
    QCOMPARE(seasons.parentId, QStringLiteral("series-3"));
    emit library.canonicalItemsFailedForConnection(library.connectionId, seasons.parentId,
                                                   seasons.requestKey, QStringLiteral("offline"));
    QCOMPARE(itemIdOf(ready.last().first()), QStringLiteral("e9"));
    QVERIFY(ready.last().first().toMap().value(QStringLiteral("seasonId")).toString().isEmpty());
}

void ShuffleSessionTest::serverShuffleEndsWhenGoneOrStuck()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy failed(&session, &ShuffleSession::failed);

    // Nothing in the scope can play: the server says why.
    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.fail(409, QStringLiteral("Nothing here can be played."));
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.last().first().toString(), QStringLiteral("Nothing here can be played."));
    QVERIFY(library.deleted.isEmpty());

    // Gone (expired, stopped elsewhere): nothing left to delete.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    session.advance();
    library.fail(404);
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 2);
    QVERIFY(library.deleted.isEmpty());

    // Any other failure to move on ends it and deletes it.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    session.advance();
    library.fail(503);
    QCOMPARE(failed.size(), 3);
    QCOMPARE(library.deleted, QStringList{QStringLiteral("shuffle-1")});

    // A failed skip keeps the announced next item.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    session.skipNext();
    library.fail(503);
    QCOMPARE(failed.size(), 3);
    QCOMPARE(session.state(), ShuffleSession::State::Ready);
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("m2"));
}

void ShuffleSessionTest::advanceDuringASkipRunsAfterIt()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);

    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    session.skipNext();
    session.advance();
    QVERIFY(library.advancedFrom.isEmpty());
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m3"))));
    QCOMPARE(library.advancedFrom, QStringList{QStringLiteral("m1")});
    library.answer(serverShuffle(movie(QStringLiteral("m3")), movie(QStringLiteral("m4"))));
    QCOMPARE(itemIdOf(ready.last().first()), QStringLiteral("m3"));
}

void ShuffleSessionTest::repliesForAnotherConnectionOrShuffleAreDropped()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);

    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))),
                   QStringLiteral("connection-2"));
    QCOMPARE(ready.size(), 0);
    QCOMPARE(session.state(), ShuffleSession::State::Starting);

    // An earlier shuffle's reply is not this one's.
    const QString earlierKey = library.keys.last();
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("8"))));
    emit library.shuffleLoaded(library.connectionId, earlierKey,
                               serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    QCOMPARE(ready.size(), 0);

    // Stopping after the session moved to another connection deletes
    // nothing there.
    library.answer(serverShuffle(movie(QStringLiteral("m5")), movie(QStringLiteral("m6"))));
    QCOMPARE(ready.size(), 1);
    library.connectionId = QStringLiteral("connection-2");
    session.stop();
    QVERIFY(library.deleted.isEmpty());
}

void ShuffleSessionTest::startIsRefusedWithoutSupport()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);

    library.unsupportedKinds = {QStringLiteral("season")};
    QVERIFY(!session.start(scope(QStringLiteral("season"))));
    QVERIFY(!session.start(scope(QStringLiteral("playlist"))));
    QVERIFY(!session.start(QVariantMap{{QStringLiteral("kind"), QStringLiteral("series")}}));
    // A scope that belongs to another connection (merged spaces).
    QVariantMap foreign = scope(QStringLiteral("series"));
    foreign.insert(QStringLiteral("connectionId"), QStringLiteral("connection-9"));
    QVERIFY(!session.start(foreign));
    QVERIFY(library.createdScopes.isEmpty());
    QVERIFY(!session.isActive());
}

void ShuffleSessionTest::localShuffleQueuesWithoutRepeatsAndRefills()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    library.server = false;
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);

    QVERIFY(session.start(scope(QStringLiteral("season"), QStringLiteral("season-1"))));
    QVERIFY(!session.usesServer());
    QCOMPARE(library.pageLimits, QList<int>{Bloom::PlaybackPolicy::kLocalShufflePageSize});
    const QVariantList page{episode(QStringLiteral("e1"), QStringLiteral("s"), 1, QStringLiteral("season-1")),
                            episode(QStringLiteral("e2"), QStringLiteral("s"), 1, QStringLiteral("season-1")),
                            episode(QStringLiteral("e3"), QStringLiteral("s"), 1, QStringLiteral("season-1")),
                            episode(QStringLiteral("e4"), QStringLiteral("s"), 1, QStringLiteral("season-1"))};
    library.answerPage(page, 4);
    QCOMPARE(ready.size(), 1);
    QStringList order{session.currentItemId()};
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("e2"));
    QCOMPARE(library.pageLimits.size(), 1);

    // Running low reads another random page; what has played this cycle
    // is skipped until every item has.
    session.advance();
    order.append(session.currentItemId());
    QCOMPARE(library.pageLimits.size(), 2);
    library.answerPage(page, 4);
    session.advance();
    order.append(session.currentItemId());
    session.advance();
    order.append(session.currentItemId());
    QCOMPARE(order, (QStringList{QStringLiteral("e1"), QStringLiteral("e2"), QStringLiteral("e3"),
                                 QStringLiteral("e4")}));
    // The new cycle never opens with the item playing now.
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("e1"));
    session.advance();
    QCOMPARE(session.currentItemId(), QStringLiteral("e1"));
    QCOMPARE(ready.size(), 5);
    // The episode already names its season.
    QVERIFY(library.itemQueries.isEmpty());
    QCOMPARE(ready.last().first().toMap().value(QStringLiteral("positionMs")).toInt(), 600000);

    // With nothing queued, an advance waits for the refill.
    QVERIFY(session.start(scope(QStringLiteral("season"), QStringLiteral("season-1"))));
    library.answerPage({episode(QStringLiteral("e1"), QStringLiteral("s"), 1, QStringLiteral("season-1"))}, 30);
    QVERIFY(session.next().isEmpty());
    session.advance();
    QCOMPARE(session.state(), ShuffleSession::State::Moving);
    library.answerPage({episode(QStringLiteral("e5"), QStringLiteral("s"), 1, QStringLiteral("season-1"))}, 30);
    QCOMPARE(session.state(), ShuffleSession::State::Ready);
    QCOMPARE(itemIdOf(ready.last().first()), QStringLiteral("e5"));
}

void ShuffleSessionTest::localSkipReturnsTheItemToThePool()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    library.server = false;
    ShuffleSession session(&library);
    QSignalSpy next(&session, &ShuffleSession::nextChanged);

    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("lib"))));
    library.answerPage({movie(QStringLiteral("m1")), movie(QStringLiteral("m2")),
                        movie(QStringLiteral("m3")), movie(QStringLiteral("m4"))}, 4);
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("m2"));
    session.skipNext();
    QCOMPARE(itemIdOf(next.last().first()), QStringLiteral("m3"));
    QVERIFY(library.skipped.isEmpty());
    // The skipped movie never played, so a later page may bring it back.
    library.answerPage({movie(QStringLiteral("m2")), movie(QStringLiteral("m1"))}, 4);
    session.advance();
    session.advance();
    session.advance();
    QCOMPARE(session.currentItemId(), QStringLiteral("m2"));
}

void ShuffleSessionTest::localShuffleWithNothingPlayableFails()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    library.server = false;
    ShuffleSession session(&library);
    QSignalSpy failed(&session, &ShuffleSession::failed);

    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.answerPage({QVariantMap{{QStringLiteral("itemId"), QStringLiteral("s1")},
                                    {QStringLiteral("mediaType"), QStringLiteral("Series")}}},
                       1);
    QCOMPARE(failed.size(), 1);
    QVERIFY(!session.isActive());

    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.fail(500);
    QCOMPARE(failed.size(), 2);
    QVERIFY(library.deleted.isEmpty());
}

void ShuffleSessionTest::smallLocalScopesStopReadingAndAlwaysHaveANext_data()
{
    QTest::addColumn<int>("total");
    QTest::newRow("one item") << 1;
    QTest::newRow("two items") << 2;
    QTest::newRow("three items") << 3;
}

void ShuffleSessionTest::smallLocalScopesStopReadingAndAlwaysHaveANext()
{
    QFETCH(int, total);
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    library.server = false;
    for (int i = 1; i <= total; ++i) {
        library.autoScope.append(movie(QStringLiteral("m%1").arg(i)));
    }
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);
    QSignalSpy failed(&session, &ShuffleSession::failed);

    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("lib"))));
    QCOMPARE(session.state(), ShuffleSession::State::Ready);
    QCOMPARE(ready.size(), 1);
    QStringList order{session.currentItemId()};
    // Reading stops once a page brings nothing new, with a next item for
    // Up Next (the item itself in a scope of one).
    QVERIFY(library.pageLimits.size() <= 3);
    for (int pick = 1; pick <= 3 * total; ++pick) {
        QVERIFY2(!session.next().isEmpty(), qPrintable(QStringLiteral("pick %1").arg(pick)));
        const qsizetype readsBefore = library.pageLimits.size();
        session.advance();
        QCOMPARE(session.state(), ShuffleSession::State::Ready);
        QCOMPARE(ready.size(), pick + 1);
        order.append(session.currentItemId());
        // Each advance reads at most twice, then waits for the next one.
        QVERIFY(library.pageLimits.size() - readsBefore <= 2);
    }
    QVERIFY(failed.isEmpty());
    // Every item plays before any repeats, and nothing plays twice in a row
    // unless it is the only item.
    for (qsizetype start = 0; start + total <= order.size(); start += total) {
        QStringList cycle = order.mid(start, total);
        cycle.sort();
        cycle.removeDuplicates();
        QCOMPARE(cycle.size(), total);
    }
    for (qsizetype i = 1; total > 1 && i < order.size(); ++i) {
        QVERIFY(order.at(i) != order.at(i - 1));
    }
    QVERIFY(library.pageLimits.size() < FakeShuffleLibrary::kMaxAutoPages);
}

void ShuffleSessionTest::failedRefillWhileUpNextWaitsEndsTheShuffle()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    library.server = false;
    ShuffleSession session(&library);
    QSignalSpy failed(&session, &ShuffleSession::failed);

    // A refill that fails while something is queued is tried again later.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("lib"))));
    library.answerPage({movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))}, 30);
    QCOMPARE(itemIdOf(session.next()), QStringLiteral("m2"));
    library.fail(503);
    QVERIFY(session.isActive());
    QVERIFY(failed.isEmpty());

    // Up Next waits on an empty queue: the read it starts failing ends the
    // shuffle, so Up Next closes and releases the display.
    session.advance();
    QCOMPARE(session.currentItemId(), QStringLiteral("m2"));
    QVERIFY(session.next().isEmpty());
    const qsizetype reads = library.pageLimits.size();
    session.refresh();
    // The refill from the advance is still out; refresh waits on it.
    QCOMPARE(library.pageLimits.size(), reads);
    library.fail(503);
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 1);

    // With no read out, refresh starts one.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("lib"))));
    library.answerPage({movie(QStringLiteral("m1"))}, 30);
    library.fail(503);
    QVERIFY(session.isActive());
    QVERIFY(session.next().isEmpty());
    session.refresh();
    QCOMPARE(library.pageLimits.size(), reads + 3);
    library.fail(503);
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 2);
}

void ShuffleSessionTest::droppedRequestsEndAStartingOrMovingShuffle()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy failed(&session, &ShuffleSession::failed);

    // LibraryService fails a request whose session changed with status 0.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.fail(0, QStringLiteral("The connection changed before the server answered."));
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 1);

    // A dropped advance ends it without deleting it from another session.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    session.advance();
    QCOMPARE(session.state(), ShuffleSession::State::Moving);
    library.fail(0);
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 2);
    QVERIFY(library.deleted.isEmpty());

    // Moving on after the connection changed ends it at once.
    QVERIFY(session.start(scope(QStringLiteral("library"), QStringLiteral("7"))));
    library.answer(serverShuffle(movie(QStringLiteral("m1")), movie(QStringLiteral("m2"))));
    library.connectionId = QStringLiteral("connection-2");
    session.advance();
    QVERIFY(!session.isActive());
    QCOMPARE(failed.size(), 3);
    QVERIFY(library.advancedFrom.size() == 1);
    QVERIFY(library.deleted.isEmpty());
}

void ShuffleSessionTest::seasonListingFailuresAreNotKept()
{
    AuthenticationService auth(nullptr);
    FakeShuffleLibrary library(&auth);
    ShuffleSession session(&library);
    QSignalSpy ready(&session, &ShuffleSession::currentReady);

    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.answer(serverShuffle(episode(QStringLiteral("e1"), QStringLiteral("series-1"), 2),
                                 episode(QStringLiteral("e2"), QStringLiteral("series-1"), 1)));
    LibraryItemQuery seasons = library.itemQueries.last();
    emit library.canonicalItemsFailedForConnection(library.connectionId, seasons.parentId,
                                                   seasons.requestKey, QStringLiteral("offline"));
    QCOMPARE(ready.size(), 1);
    QVERIFY(ready.last().first().toMap().value(QStringLiteral("seasonId")).toString().isEmpty());

    // The series' next episode asks again, and that answer is kept.
    session.advance();
    library.answer(serverShuffle(episode(QStringLiteral("e2"), QStringLiteral("series-1"), 1),
                                 episode(QStringLiteral("e3"), QStringLiteral("series-1"), 2)));
    QCOMPARE(library.itemQueries.size(), 2);
    QCOMPARE(ready.size(), 1);
    library.answerSeasons({QVariantMap{{QStringLiteral("itemId"), QStringLiteral("season-a")},
                                       {QStringLiteral("indexNumber"), 1}}});
    QCOMPARE(ready.size(), 2);
    QCOMPARE(ready.last().first().toMap().value(QStringLiteral("seasonId")).toString(),
             QStringLiteral("season-a"));

    // A new shuffle reads the listing afresh (it may have changed, or belong
    // to another connection now).
    QVERIFY(session.start(scope(QStringLiteral("series"))));
    library.answer(serverShuffle(episode(QStringLiteral("e2"), QStringLiteral("series-1"), 1),
                                 episode(QStringLiteral("e3"), QStringLiteral("series-1"), 2)));
    QCOMPARE(library.itemQueries.size(), 3);
}

QTEST_GUILESS_MAIN(ShuffleSessionTest)
#include "ShuffleSessionTest.moc"
