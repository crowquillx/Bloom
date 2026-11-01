#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/ContinueWatchingViewModel.h"

#include "TestConfigIsolation.h"

namespace {

// Records Home card writes instead of sending them; tests answer through
// the service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct StateWrite {
        QString requestKey;
        QString field;
        QString itemId;
        bool value = false;
    };

    struct Dismissal {
        QString requestKey;
        QString surface;
        QString itemId;
        QString seriesId;
        QString progressUpdatedAt;
        bool dismissed = false;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return liveConnectionId; }
    HomeCardCapabilities homeCardCapabilities(const QString &connectionId) const override
    {
        return capabilities.value(connectionId);
    }

    void setHomeItemDismissed(const QString &requestKey, const QString &surface,
                              const QString &itemId, const QString &seriesId,
                              const QString &progressUpdatedAt, bool dismissed) override
    {
        dismissals.append({requestKey, surface, itemId, seriesId, progressUpdatedAt, dismissed});
    }
    void clearResumePosition(const QString &requestKey, const QString &itemId) override
    {
        resumeResets.append({requestKey, itemId});
    }
    void setHomeItemState(const QString &requestKey, const QString &field,
                          const QString &itemId, bool value) override
    {
        states.append({requestKey, field, itemId, value});
    }

    void answer(const QString &requestKey, bool succeeded)
    {
        answerFor(liveConnectionId, requestKey, succeeded);
    }
    void answerFor(const QString &connectionId, const QString &requestKey, bool succeeded)
    {
        emit homeCardWriteFinished(connectionId, requestKey, succeeded,
                                   succeeded ? QString() : QStringLiteral("Not Found"));
    }
    // The last state write for field.
    StateWrite lastState(const QString &field) const
    {
        for (qsizetype i = states.size() - 1; i >= 0; --i) {
            if (states.at(i).field == field) {
                return states.at(i);
            }
        }
        return {};
    }

    QString liveConnectionId = QStringLiteral("silo");
    QHash<QString, HomeCardCapabilities> capabilities{
        {QStringLiteral("silo"), HomeCardCapabilities{true, false, true, false}},
        {QStringLiteral("jellyfin"), HomeCardCapabilities{false, true, false, false}}};
    QList<Dismissal> dismissals;
    QList<QPair<QString, QString>> resumeResets;
    QList<StateWrite> states;
};

QVariantMap card(const QString &connectionId, const QString &itemId,
                 const QString &mediaType, qint64 positionMs,
                 const QString &seriesId = QString())
{
    QVariantMap item{
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType},
        {QStringLiteral("positionMs"), positionMs},
        {QStringLiteral("watched"), false},
        {QStringLiteral("favorite"), false},
        {QStringLiteral("inWatchlist"), false},
        {QStringLiteral("userState"),
         QVariantMap{{QStringLiteral("favorite"), false},
                     {QStringLiteral("positionMs"), positionMs}}}
    };
    if (!seriesId.isEmpty()) {
        item.insert(QStringLiteral("seriesId"), seriesId);
        item.insert(QStringLiteral("seriesName"), seriesId);
    }
    if (positionMs > 0 && connectionId == QStringLiteral("silo")) {
        item.insert(QStringLiteral("progressUpdatedAt"),
                    QStringLiteral("2026-10-30T20:15:00.000Z"));
    }
    return item;
}

QStringList ids(const QVariantList &items)
{
    QStringList result;
    for (const QVariant &value : items) {
        result.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    return result;
}

QVariantMap action(const QVariantList &actions, const QString &id)
{
    for (const QVariant &value : actions) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) {
            return value.toMap();
        }
    }
    return {};
}

QStringList actionIds(const QVariantList &actions)
{
    QStringList result;
    for (const QVariant &value : actions) {
        result.append(value.toMap().value(QStringLiteral("id")).toString());
    }
    return result;
}

QVariantMap itemById(const QVariantList &items, const QString &itemId)
{
    for (const QVariant &value : items) {
        if (value.toMap().value(QStringLiteral("itemId")).toString() == itemId) {
            return value.toMap();
        }
    }
    return {};
}

} // namespace

class ContinueWatchingViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void splitsRowsByProgressAndMergesOnRequest();
    void actionsFollowProviderAndCardFields();
    void siloRemovalIsOptimisticAndDropsTheShow();
    void confirmedRemovalWaitsForTheNextList();
    void failedRemovalRestoresTheCard();
    void hideFromUpNextSendsTheSeries();
    void undoShowsCardsAgainAndUndismisses();
    void failedUndoHidesCardsAgain();
    void jellyfinRemovalClearsResumePositionWithoutUndo();
    void markWatchedHidesAndRollsBack();
    void favoriteAndWatchlistToggleInPlaceAndRollBack();
    void refusesCardsOfAnotherConnectionUntilRouted();
    void oneWritePerCardAtATime();
    void failedWriteIgnoresALateReply();
    void repliesMatchTheirOwnAccount();
    void pendingChangesSurviveARefresh();
    void undoKeepsCardsOverListsReadBeforeIt();
    void undoGoneOnceItsToastCloses();
    void episodeDismissalsAskBeforeDroppingTheShow();
    void notInterestedOnMoviesWhenAllowed();
    void shelvesChangeByRowsNotResets();

private:
    QTemporaryDir m_configDirectory;
    std::unique_ptr<ScopedConfigIsolation> m_configIsolation;
    ScriptedLibraryService *m_service = nullptr;
    ContinueWatchingViewModel *m_model = nullptr;
};

void ContinueWatchingViewModelTest::initTestCase()
{
    QVERIFY(m_configDirectory.isValid());
    m_configIsolation = std::make_unique<ScopedConfigIsolation>(m_configDirectory.path());
}

void ContinueWatchingViewModelTest::init()
{
    m_service = new ScriptedLibraryService(this);
    m_model = new ContinueWatchingViewModel(m_service, this);
}

void ContinueWatchingViewModelTest::cleanup()
{
    delete m_model;
    m_model = nullptr;
    delete m_service;
    m_service = nullptr;
}

void ContinueWatchingViewModelTest::splitsRowsByProgressAndMergesOnRequest()
{
    QSignalSpy changed(m_model, &ContinueWatchingViewModel::itemsChanged);
    m_model->setItems({card("silo", "movie:a", "Movie", 60000),
                       card("silo", "ep:b1", "Episode", 0, "series:b"),
                       card("silo", "ep:c1", "Episode", 1000, "series:c")});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:a", "ep:c1"}));
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1"}));
    QCOMPARE(ids(m_model->items()), QStringList({"movie:a", "ep:b1", "ep:c1"}));

    // One shelf with every card, in the list's order.
    m_model->setMergeRows(true);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"movie:a", "ep:b1", "ep:c1"}));
    QCOMPARE(changed.count(), 2);
}

void ContinueWatchingViewModelTest::actionsFollowProviderAndCardFields()
{
    // A Silo movie in progress: details, remove, watched, favorite, watchlist.
    const QVariantList movie = m_model->actionsFor(card("silo", "movie:a", "Movie", 60000));
    QCOMPARE(actionIds(movie), QStringList({"openDetails", "remove", "markWatched",
                                            "favorite", "watchlistAdd"}));
    QVERIFY(action(movie, "openDetails").value("navigation").toBool());
    QVERIFY(action(movie, "remove").value("enabled").toBool());

    // Without the card's progress stamp Silo cannot anchor the dismissal.
    QVariantMap unstamped = card("silo", "movie:a", "Movie", 60000);
    unstamped.remove(QStringLiteral("progressUpdatedAt"));
    QVERIFY(!action(m_model->actionsFor(unstamped), "remove").value("enabled").toBool());

    // An Up Next episode: series, hide, watched; no favorite or watchlist.
    const QVariantList episode =
        m_model->actionsFor(card("silo", "ep:b1", "Episode", 0, "series:b"));
    QCOMPARE(actionIds(episode), QStringList({"openSeries", "hide", "markWatched"}));
    QVERIFY(action(episode, "hide").value("enabled").toBool());
    QVERIFY(!action(m_model->actionsFor(card("silo", "ep:x", "Episode", 0)), "hide")
                 .value("enabled").toBool());

    // Jellyfin clears the resume position and has no Up Next hide or
    // watchlist; Mark as watched stands in for hiding.
    QCOMPARE(actionIds(m_model->actionsFor(card("jellyfin", "m1", "Movie", 60000))),
             QStringList({"openDetails", "remove", "markWatched", "favorite"}));
    QCOMPARE(actionIds(m_model->actionsFor(card("jellyfin", "e1", "Episode", 0, "s1"))),
             QStringList({"openSeries", "markWatched"}));

    // Current state picks the toggle direction.
    QVariantMap watched = card("silo", "movie:w", "Movie", 0);
    watched.insert(QStringLiteral("watched"), true);
    watched.insert(QStringLiteral("favorite"), true);
    watched.insert(QStringLiteral("inWatchlist"), true);
    const QVariantList toggles = m_model->actionsFor(watched);
    QVERIFY(actionIds(toggles).contains("markUnwatched"));
    QVERIFY(actionIds(toggles).contains("unfavorite"));
    QVERIFY(actionIds(toggles).contains("watchlistRemove"));
}

void ContinueWatchingViewModelTest::siloRemovalIsOptimisticAndDropsTheShow()
{
    m_model->setItems({card("silo", "ep:a2", "Episode", 30000, "series:a"),
                       card("silo", "movie:m", "Movie", 60000),
                       card("silo", "ep:a3", "Episode", 0, "series:a"),
                       card("silo", "ep:b1", "Episode", 0, "series:b")});
    QSignalSpy offered(m_model, &ContinueWatchingViewModel::undoOffered);
    QSignalSpy refresh(m_model, &ContinueWatchingViewModel::refreshRequested);

    QVERIFY(m_model->trigger("remove", card("silo", "ep:a2", "Episode", 30000, "series:a")));
    // Every card of the show leaves both rows at once.
    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:m"}));
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1"}));
    QCOMPARE(m_service->dismissals.size(), 1);
    const auto sent = m_service->dismissals.first();
    QCOMPARE(sent.surface, QStringLiteral("continue_watching"));
    QCOMPARE(sent.itemId, QStringLiteral("ep:a2"));
    QCOMPARE(sent.progressUpdatedAt, QStringLiteral("2026-10-30T20:15:00.000Z"));
    QVERIFY(sent.dismissed);
    QVERIFY(!m_model->undoAvailable());

    m_service->answer(sent.requestKey, true);
    QCOMPARE(offered.count(), 1);
    QVERIFY(offered.first().first().toString().contains(QStringLiteral("Stopped watching")));
    QVERIFY(m_model->undoAvailable());
    QCOMPARE(m_model->undoConnectionId(), QStringLiteral("silo"));
    QCOMPARE(refresh.count(), 1);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m", "ep:b1"}));
}

void ContinueWatchingViewModelTest::confirmedRemovalWaitsForTheNextList()
{
    const QVariantList items{card("silo", "movie:m", "Movie", 60000),
                             card("silo", "movie:n", "Movie", 1000)};
    m_model->setItems(items);
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));

    // A list read before the write still lists the card: it stays hidden.
    m_model->setItems(items);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:n"}));

    m_service->answer(m_service->dismissals.first().requestKey, true);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:n"}));

    // The list read after the confirmation is the server's word.
    m_model->setItems({card("silo", "movie:n", "Movie", 1000)});
    QCOMPARE(ids(m_model->items()), QStringList({"movie:n"}));
    m_model->setItems(items);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m", "movie:n"}));
}

void ContinueWatchingViewModelTest::failedRemovalRestoresTheCard()
{
    m_model->setItems({card("silo", "movie:m", "Movie", 60000),
                       card("silo", "movie:n", "Movie", 1000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);
    QSignalSpy offered(m_model, &ContinueWatchingViewModel::undoOffered);

    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:n"}));
    m_service->answer(m_service->dismissals.first().requestKey, false);

    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:m", "movie:n"}));
    QCOMPARE(failed.count(), 1);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("Not Found")));
    QCOMPARE(offered.count(), 0);
    QVERIFY(!m_model->undoAvailable());
}

void ContinueWatchingViewModelTest::hideFromUpNextSendsTheSeries()
{
    m_model->setItems({card("silo", "ep:b1", "Episode", 0, "series:b"),
                       card("silo", "ep:c1", "Episode", 0, "series:c")});
    QVERIFY(m_model->trigger("hide", card("silo", "ep:b1", "Episode", 0, "series:b")));
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:c1"}));
    const auto sent = m_service->dismissals.first();
    QCOMPARE(sent.surface, QStringLiteral("next_up"));
    QCOMPARE(sent.itemId, QStringLiteral("ep:b1"));
    QCOMPARE(sent.seriesId, QStringLiteral("series:b"));
    QVERIFY(sent.progressUpdatedAt.isEmpty());

    // Hide is not offered on a Continue Watching card, nor remove on Up Next.
    QVERIFY(!m_model->trigger("hide", card("silo", "ep:c1", "Episode", 5000, "series:c")));
    QVERIFY(!m_model->trigger("remove", card("silo", "ep:c1", "Episode", 0, "series:c")));
    QCOMPARE(m_service->dismissals.size(), 1);
}

void ContinueWatchingViewModelTest::undoShowsCardsAgainAndUndismisses()
{
    const QVariantList items{card("silo", "movie:x", "Movie", 1000),
                             card("silo", "ep:a2", "Episode", 30000, "series:a"),
                             card("silo", "ep:a3", "Episode", 0, "series:a")};
    m_model->setItems(items);
    QVERIFY(m_model->trigger("remove", card("silo", "ep:a2", "Episode", 30000, "series:a")));
    m_service->answer(m_service->dismissals.first().requestKey, true);
    // The refresh leaves the show out.
    m_model->setItems({card("silo", "movie:x", "Movie", 1000)});
    QVERIFY(m_model->undoAvailable());

    QSignalSpy refresh(m_model, &ContinueWatchingViewModel::refreshRequested);
    QVERIFY(m_model->undo());
    QVERIFY(!m_model->undoAvailable());
    // Both cards come back where they were.
    QCOMPARE(ids(m_model->items()), QStringList({"movie:x", "ep:a2", "ep:a3"}));
    QCOMPARE(m_service->dismissals.size(), 2);
    const auto sent = m_service->dismissals.last();
    QVERIFY(!sent.dismissed);
    QCOMPARE(sent.surface, QStringLiteral("continue_watching"));
    QCOMPARE(sent.itemId, QStringLiteral("ep:a2"));

    m_service->answer(sent.requestKey, true);
    QCOMPARE(refresh.count(), 1);
    QVERIFY(!m_model->undo());
}

void ContinueWatchingViewModelTest::failedUndoHidesCardsAgain()
{
    m_model->setItems({card("silo", "movie:m", "Movie", 60000),
                       card("silo", "movie:n", "Movie", 1000)});
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    m_service->answer(m_service->dismissals.first().requestKey, true);
    QVERIFY(m_model->undo());
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m", "movie:n"}));

    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);
    m_service->answer(m_service->dismissals.last().requestKey, false);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:n"}));

    // Closing the toast forgets an undo.
    QVERIFY(m_model->trigger("remove", card("silo", "movie:n", "Movie", 1000)));
    m_service->answer(m_service->dismissals.last().requestKey, true);
    QVERIFY(m_model->undoAvailable());
    m_model->dismissUndo();
    QVERIFY(!m_model->undo());
}

void ContinueWatchingViewModelTest::jellyfinRemovalClearsResumePositionWithoutUndo()
{
    m_service->liveConnectionId = QStringLiteral("jellyfin");
    m_model->setItems({card("jellyfin", "e1", "Episode", 60000, "s1"),
                       card("jellyfin", "e7", "Episode", 0, "s2")});
    QSignalSpy offered(m_model, &ContinueWatchingViewModel::undoOffered);
    QSignalSpy refresh(m_model, &ContinueWatchingViewModel::refreshRequested);

    QVERIFY(m_model->trigger("remove", card("jellyfin", "e1", "Episode", 60000, "s1")));
    QCOMPARE(m_service->resumeResets.size(), 1);
    QCOMPARE(m_service->resumeResets.first().second, QStringLiteral("e1"));
    QVERIFY(m_service->dismissals.isEmpty());
    QVERIFY(m_model->continueWatching().isEmpty());
    QCOMPARE(ids(m_model->nextUp()), QStringList({"e7"}));

    // With its position cleared the episode may come back as an Up Next
    // card: only its Continue Watching card is hidden.
    m_model->setItems({card("jellyfin", "e1", "Episode", 0, "s1"),
                       card("jellyfin", "e7", "Episode", 0, "s2")});
    QVERIFY(m_model->continueWatching().isEmpty());
    QCOMPARE(ids(m_model->nextUp()), QStringList({"e1", "e7"}));

    m_service->answer(m_service->resumeResets.first().first, true);
    QCOMPARE(offered.count(), 0);
    QVERIFY(!m_model->undoAvailable());
    QCOMPARE(refresh.count(), 1);
}

void ContinueWatchingViewModelTest::markWatchedHidesAndRollsBack()
{
    m_model->setItems({card("silo", "ep:b1", "Episode", 0, "series:b"),
                       card("silo", "ep:c1", "Episode", 0, "series:c")});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);
    QSignalSpy refresh(m_model, &ContinueWatchingViewModel::refreshRequested);

    QVERIFY(m_model->trigger("markWatched", card("silo", "ep:b1", "Episode", 0, "series:b")));
    const auto first = m_service->lastState("watched");
    QCOMPARE(first.itemId, QStringLiteral("ep:b1"));
    QVERIFY(first.value);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:c1"}));
    m_service->answer(first.requestKey, false);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1", "ep:c1"}));
    QCOMPARE(failed.count(), 1);

    // Only its own request key settles a card.
    QVERIFY(m_model->trigger("markWatched", card("silo", "ep:c1", "Episode", 0, "series:c")));
    const auto second = m_service->lastState("watched");
    m_service->answer(first.requestKey, true);
    QCOMPARE(refresh.count(), 0);
    m_service->answer(second.requestKey, true);
    QCOMPARE(refresh.count(), 1);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1"}));
    QVERIFY(!m_model->undoAvailable());
}

void ContinueWatchingViewModelTest::favoriteAndWatchlistToggleInPlaceAndRollBack()
{
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);

    QVERIFY(m_model->trigger("favorite", card("silo", "movie:m", "Movie", 60000)));
    auto sent = m_service->lastState("favorite");
    QCOMPARE(sent.itemId, QStringLiteral("movie:m"));
    QVERIFY(sent.value);
    QVariantMap shown = itemById(m_model->items(), "movie:m");
    QVERIFY(shown.value("favorite").toBool());
    QVERIFY(shown.value("userState").toMap().value("favorite").toBool());
    m_service->answer(sent.requestKey, true);
    QVERIFY(itemById(m_model->items(), "movie:m").value("favorite").toBool());

    QVERIFY(m_model->trigger("unfavorite", itemById(m_model->items(), "movie:m")));
    sent = m_service->lastState("favorite");
    QVERIFY(!sent.value);
    m_service->answer(sent.requestKey, false);
    QVERIFY(itemById(m_model->items(), "movie:m").value("favorite").toBool());
    QCOMPARE(failed.count(), 1);

    QVERIFY(m_model->trigger("watchlistAdd", itemById(m_model->items(), "movie:m")));
    sent = m_service->lastState("inWatchlist");
    QVERIFY(sent.value);
    QVERIFY(itemById(m_model->items(), "movie:m").value("inWatchlist").toBool());
    m_service->answer(sent.requestKey, false);
    QVERIFY(!itemById(m_model->items(), "movie:m").value("inWatchlist").toBool());
    QCOMPARE(failed.count(), 2);
    // Toggles never hide the card.
    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:m"}));
}

void ContinueWatchingViewModelTest::refusesCardsOfAnotherConnectionUntilRouted()
{
    // A merged Home: a Jellyfin card while Silo is live.
    m_model->setItems({card("silo", "movie:m", "Movie", 60000),
                       card("jellyfin", "m1", "Movie", 60000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);

    // Capabilities come from the card's own provider.
    QCOMPARE(actionIds(m_model->actionsFor(card("jellyfin", "m1", "Movie", 60000))),
             QStringList({"openDetails", "remove", "markWatched", "favorite"}));
    QVERIFY(!m_model->trigger("remove", card("jellyfin", "m1", "Movie", 60000)));
    QCOMPARE(failed.count(), 1);
    QVERIFY(m_service->resumeResets.isEmpty());
    QCOMPARE(m_model->continueWatching().size(), 2);

    // Routed: the live session is the card's owner.
    m_service->liveConnectionId = QStringLiteral("jellyfin");
    QVERIFY(m_model->trigger("remove", card("jellyfin", "m1", "Movie", 60000)));
    QCOMPARE(m_service->resumeResets.size(), 1);
    QCOMPARE(ids(m_model->continueWatching()), QStringList({"movie:m"}));

    // An undo waits for its own connection too.
    m_service->liveConnectionId = QStringLiteral("silo");
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    m_service->answer(m_service->dismissals.first().requestKey, true);
    m_service->liveConnectionId = QStringLiteral("jellyfin");
    QVERIFY(!m_model->undo());
    QVERIFY(m_model->undoAvailable());
    m_service->liveConnectionId = QStringLiteral("silo");
    QVERIFY(m_model->undo());
}

void ContinueWatchingViewModelTest::oneWritePerCardAtATime()
{
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    QVERIFY(m_model->trigger("favorite", card("silo", "movie:m", "Movie", 60000)));
    QVERIFY(!m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    QVERIFY(!m_model->trigger("openDetails", card("silo", "movie:m", "Movie", 60000)));
    QVERIFY(!m_model->trigger("unknown", card("silo", "movie:x", "Movie", 60000)));
    QVERIFY(m_service->dismissals.isEmpty());
}

void ContinueWatchingViewModelTest::failedWriteIgnoresALateReply()
{
    // LibraryService answers a write whose session went away as a failure.
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    QVERIFY(m_model->items().isEmpty());
    const QString key = m_service->dismissals.first().requestKey;
    m_service->answer(key, false);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m"}));

    // A reply after that changes nothing, and the card takes a new write.
    m_service->answer(key, true);
    QVERIFY(!m_model->undoAvailable());
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m"}));
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
}

void ContinueWatchingViewModelTest::repliesMatchTheirOwnAccount()
{
    // A merged Home: the same item ID on two Silo accounts.
    m_service->capabilities.insert(QStringLiteral("silo-b"),
                                   HomeCardCapabilities{true, false, true, false});
    m_model->setItems({card("silo", "movie:m", "Movie", 60000),
                       card("silo-b", "movie:m", "Movie", 60000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);

    QVERIFY(m_model->trigger("favorite", card("silo", "movie:m", "Movie", 60000)));
    const QString keyA = m_service->lastState("favorite").requestKey;
    m_service->liveConnectionId = QStringLiteral("silo-b");
    QVERIFY(m_model->trigger("favorite", card("silo-b", "movie:m", "Movie", 60000)));
    const QString keyB = m_service->lastState("favorite").requestKey;
    QVERIFY(keyA != keyB);

    // A refusal reported for B under A's key settles nothing.
    m_service->answerFor(QStringLiteral("silo-b"), keyA, false);
    QCOMPARE(failed.count(), 0);
    // B's refusal rolls back B's card only.
    m_service->answerFor(QStringLiteral("silo-b"), keyB, false);
    QCOMPARE(failed.count(), 1);
    bool favoriteA = false;
    bool favoriteB = true;
    for (const QVariant &value : m_model->items()) {
        const QVariantMap item = value.toMap();
        if (item.value("connectionId").toString() == QStringLiteral("silo")) {
            favoriteA = item.value("favorite").toBool();
        } else {
            favoriteB = item.value("favorite").toBool();
        }
    }
    QVERIFY(favoriteA);
    QVERIFY(!favoriteB);
    m_service->answerFor(QStringLiteral("silo"), keyA, true);
    QCOMPARE(failed.count(), 1);
}

void ContinueWatchingViewModelTest::pendingChangesSurviveARefresh()
{
    const QVariantList items{card("silo", "movie:m", "Movie", 60000),
                             card("silo", "movie:n", "Movie", 1000)};
    m_model->setItems(items);
    QVERIFY(m_model->trigger("favorite", card("silo", "movie:m", "Movie", 60000)));
    const QString key = m_service->lastState("favorite").requestKey;

    // Another card's removal asks for a list read before the favorite landed.
    m_model->setItems(items);
    QVariantMap shown = itemById(m_model->items(), "movie:m");
    QVERIFY(shown.value("favorite").toBool());
    QVERIFY(shown.value("userState").toMap().value("favorite").toBool());
    QCOMPARE(action(m_model->actionsFor(shown), "unfavorite").value("id").toString(),
             QStringLiteral("unfavorite"));

    m_service->answer(key, true);
    QVERIFY(itemById(m_model->items(), "movie:m").value("favorite").toBool());
    // Settled: the server's word wins again.
    m_model->setItems(items);
    QVERIFY(!itemById(m_model->items(), "movie:m").value("favorite").toBool());
}

void ContinueWatchingViewModelTest::undoKeepsCardsOverListsReadBeforeIt()
{
    m_model->setItems({card("silo", "movie:x", "Movie", 1000),
                       card("silo", "movie:m", "Movie", 60000)});
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    m_service->answer(m_service->dismissals.first().requestKey, true);
    QVERIFY(m_model->undo());
    QCOMPARE(ids(m_model->items()), QStringList({"movie:x", "movie:m"}));

    // The refresh the dismissal asked for still leaves the card out.
    QSignalSpy changed(m_model->continueWatchingModel(), &QAbstractItemModel::rowsRemoved);
    m_model->setItems({card("silo", "movie:x", "Movie", 1000)});
    QCOMPARE(ids(m_model->items()), QStringList({"movie:x", "movie:m"}));
    QCOMPARE(changed.count(), 0);

    // Once the undo lands, lists are the server's word.
    m_service->answer(m_service->dismissals.last().requestKey, true);
    m_model->setItems({card("silo", "movie:x", "Movie", 1000)});
    QCOMPARE(ids(m_model->items()), QStringList({"movie:x"}));
}

void ContinueWatchingViewModelTest::undoGoneOnceItsToastCloses()
{
    // The toast closing while the menu offered Undo: picking it then finds
    // nothing to undo, and a newer removal replaces an older undo.
    m_model->setItems({card("silo", "movie:m", "Movie", 60000),
                       card("silo", "movie:n", "Movie", 1000)});
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    m_service->answer(m_service->dismissals.last().requestKey, true);
    QVERIFY(m_model->trigger("remove", card("silo", "movie:n", "Movie", 1000)));
    m_service->answer(m_service->dismissals.last().requestKey, true);
    QVERIFY(m_model->undo());
    QCOMPARE(m_service->dismissals.last().itemId, QStringLiteral("movie:n"));
    QVERIFY(!m_model->undoAvailable());
    m_service->answer(m_service->dismissals.last().requestKey, true);

    QVERIFY(m_model->trigger("remove", card("silo", "movie:n", "Movie", 1000)));
    m_service->answer(m_service->dismissals.last().requestKey, true);
    QSignalSpy undoChanged(m_model, &ContinueWatchingViewModel::undoChanged);
    m_model->dismissUndo();
    QCOMPARE(undoChanged.count(), 1);
    QVERIFY(!m_model->undoAvailable());
    QVERIFY(!m_model->undo());
}

void ContinueWatchingViewModelTest::episodeDismissalsAskBeforeDroppingTheShow()
{
    // A Silo episode on either row drops the whole show, also on the
    // profile's watch services, so the action names the show and asks.
    for (const QVariantMap &episode : {card("silo", "ep:a2", "Episode", 30000, "series:a"),
                                       card("silo", "ep:a3", "Episode", 0, "series:a")}) {
        const QVariantList actions = m_model->actionsFor(episode);
        const QVariantMap dismissal =
            action(actions, ContinueWatchingViewModel::surfaceOf(episode)
                                    == QStringLiteral("continue_watching")
                                ? QStringLiteral("remove")
                                : QStringLiteral("hide"));
        QCOMPARE(dismissal.value("text").toString(), QStringLiteral("Stop watching series:a"));
        QVERIFY(dismissal.value("confirmTitle").toString().contains(QStringLiteral("series:a")));
        QVERIFY(dismissal.value("confirmMessage").toString().contains(QStringLiteral("Trakt")));
        QVERIFY(!dismissal.value("confirmText").toString().isEmpty());
    }
    // A movie keeps a plain removal with undo, and Jellyfin never asks.
    QVERIFY(!action(m_model->actionsFor(card("silo", "movie:m", "Movie", 60000)), "remove")
                 .contains("confirmTitle"));
    QVERIFY(!action(m_model->actionsFor(card("jellyfin", "e1", "Episode", 60000, "s1")),
                    "remove")
                 .contains("confirmTitle"));

    // Confirmed, the toast says the show was dropped beyond Bloom too.
    m_model->setItems({card("silo", "ep:a2", "Episode", 30000, "series:a")});
    QSignalSpy offered(m_model, &ContinueWatchingViewModel::undoOffered);
    QVERIFY(m_model->trigger("remove", card("silo", "ep:a2", "Episode", 30000, "series:a")));
    m_service->answer(m_service->dismissals.last().requestKey, true);
    QCOMPARE(offered.count(), 1);
    QVERIFY(offered.first().first().toString().contains(QStringLiteral("watch services")));
}

void ContinueWatchingViewModelTest::notInterestedOnMoviesWhenAllowed()
{
    m_service->capabilities[QStringLiteral("silo")].notInterested = true;
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    const QVariantList actions = m_model->actionsFor(card("silo", "movie:m", "Movie", 60000));
    QCOMPARE(actionIds(actions).last(), QStringLiteral("notInterested"));
    QVERIFY(!actionIds(m_model->actionsFor(card("silo", "ep:a3", "Episode", 0, "series:a")))
                 .contains(QStringLiteral("notInterested")));

    QVERIFY(m_model->trigger("notInterested", card("silo", "movie:m", "Movie", 60000)));
    const auto sent = m_service->lastState("notInterested");
    QVERIFY(sent.value);
    const QVariantMap shown = itemById(m_model->items(), "movie:m");
    QVERIFY(shown.value("notInterested").toBool());
    QCOMPARE(actionIds(m_model->actionsFor(shown)).last(), QStringLiteral("interested"));
}

void ContinueWatchingViewModelTest::shelvesChangeByRowsNotResets()
{
    HomeCardListModel *row = m_model->continueWatchingModel();
    m_model->setItems({card("silo", "movie:a", "Movie", 1000),
                       card("silo", "movie:b", "Movie", 1000),
                       card("silo", "movie:c", "Movie", 1000)});
    QCOMPARE(row->count(), 3);
    QSignalSpy reset(row, &QAbstractItemModel::modelReset);
    QSignalSpy removed(row, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(row, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(row, &QAbstractItemModel::dataChanged);

    QVERIFY(m_model->trigger("remove", card("silo", "movie:b", "Movie", 1000)));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(1).toInt(), 1);
    QCOMPARE(row->get(1).value("itemId").toString(), QStringLiteral("movie:c"));

    // A refresh with a new card first and changed data keeps the others.
    QVariantMap updated = card("silo", "movie:c", "Movie", 2000);
    m_model->setItems({card("silo", "movie:z", "Movie", 1000),
                       card("silo", "movie:a", "Movie", 1000), updated});
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(ids(row->items()), QStringList({"movie:z", "movie:a", "movie:c"}));
    QCOMPARE(row->data(row->index(2), HomeCardListModel::ModelDataRole).toMap()
                 .value("positionMs").toLongLong(), 2000);
}

QTEST_GUILESS_MAIN(ContinueWatchingViewModelTest)
#include "ContinueWatchingViewModelTest.moc"
