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
    void markItemPlayed(const QString &itemId) override { played.append(itemId); }
    void markItemUnplayed(const QString &itemId) override { unplayed.append(itemId); }
    void markItemFavorite(const QString &itemId) override { favorites.append(itemId); }
    void markItemUnfavorite(const QString &itemId) override { unfavorites.append(itemId); }
    void setItemWatchlisted(const QString &itemId, bool inWatchlist) override
    {
        watchlist.append({itemId, inWatchlist});
    }

    void answer(const QString &requestKey, bool succeeded)
    {
        emit homeCardWriteFinished(liveConnectionId, requestKey, succeeded,
                                   succeeded ? QString() : QStringLiteral("Not Found"));
    }
    void confirmPlayed(const QString &itemId, bool isPlayed)
    {
        emit itemPlayedStatusChanged(itemId, isPlayed);
    }
    void refusePlayed(const QString &itemId, bool isPlayed)
    {
        emit itemPlayedStatusFailed(itemId, isPlayed, QStringLiteral("Forbidden"));
    }
    void confirmFavorite(const QString &itemId, bool isFavorite)
    {
        emit favoriteStatusChanged(itemId, isFavorite);
    }
    void refuseFavorite(const QString &itemId, bool isFavorite)
    {
        emit favoriteStatusFailed(itemId, isFavorite, QStringLiteral("Forbidden"));
    }
    void refuseWatchlist(const QString &itemId, bool inWatchlist)
    {
        emit watchlistStatusFailed(itemId, inWatchlist, QStringLiteral("Forbidden"));
    }

    QString liveConnectionId = QStringLiteral("silo");
    QHash<QString, HomeCardCapabilities> capabilities{
        {QStringLiteral("silo"), HomeCardCapabilities{true, false, true}},
        {QStringLiteral("jellyfin"), HomeCardCapabilities{false, true, false}}};
    QList<Dismissal> dismissals;
    QList<QPair<QString, QString>> resumeResets;
    QStringList played;
    QStringList unplayed;
    QStringList favorites;
    QStringList unfavorites;
    QList<QPair<QString, bool>> watchlist;
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
    void unansweredWriteReleasesItsCard();

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
    QVERIFY(offered.first().first().toString().contains(QStringLiteral("Up Next")));
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
    QCOMPARE(m_service->played, QStringList({"ep:b1"}));
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:c1"}));
    m_service->refusePlayed(QStringLiteral("ep:b1"), true);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1", "ep:c1"}));
    QCOMPARE(failed.count(), 1);

    // An answer for another item, or the other direction, is not ours.
    QVERIFY(m_model->trigger("markWatched", card("silo", "ep:c1", "Episode", 0, "series:c")));
    m_service->confirmPlayed(QStringLiteral("ep:b1"), true);
    m_service->confirmPlayed(QStringLiteral("ep:c1"), false);
    QCOMPARE(refresh.count(), 0);
    m_service->confirmPlayed(QStringLiteral("ep:c1"), true);
    QCOMPARE(refresh.count(), 1);
    QCOMPARE(ids(m_model->nextUp()), QStringList({"ep:b1"}));
    QVERIFY(!m_model->undoAvailable());
}

void ContinueWatchingViewModelTest::favoriteAndWatchlistToggleInPlaceAndRollBack()
{
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    QSignalSpy failed(m_model, &ContinueWatchingViewModel::actionFailed);

    QVERIFY(m_model->trigger("favorite", card("silo", "movie:m", "Movie", 60000)));
    QCOMPARE(m_service->favorites, QStringList({"movie:m"}));
    QVariantMap shown = itemById(m_model->items(), "movie:m");
    QVERIFY(shown.value("favorite").toBool());
    QVERIFY(shown.value("userState").toMap().value("favorite").toBool());
    m_service->confirmFavorite(QStringLiteral("movie:m"), true);
    QVERIFY(itemById(m_model->items(), "movie:m").value("favorite").toBool());

    QVERIFY(m_model->trigger("unfavorite", itemById(m_model->items(), "movie:m")));
    QCOMPARE(m_service->unfavorites, QStringList({"movie:m"}));
    m_service->refuseFavorite(QStringLiteral("movie:m"), false);
    QVERIFY(itemById(m_model->items(), "movie:m").value("favorite").toBool());
    QCOMPARE(failed.count(), 1);

    QVERIFY(m_model->trigger("watchlistAdd", itemById(m_model->items(), "movie:m")));
    QCOMPARE(m_service->watchlist.size(), 1);
    QVERIFY(m_service->watchlist.first().second);
    QVERIFY(itemById(m_model->items(), "movie:m").value("inWatchlist").toBool());
    m_service->refuseWatchlist(QStringLiteral("movie:m"), true);
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

void ContinueWatchingViewModelTest::unansweredWriteReleasesItsCard()
{
    m_model->setPendingTimeoutMs(20);
    m_model->setItems({card("silo", "movie:m", "Movie", 60000)});
    QSignalSpy refresh(m_model, &ContinueWatchingViewModel::refreshRequested);
    QVERIFY(m_model->trigger("remove", card("silo", "movie:m", "Movie", 60000)));
    QVERIFY(m_model->items().isEmpty());

    QTRY_COMPARE(refresh.count(), 1);
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m"}));
    // A late answer changes nothing.
    m_service->answer(m_service->dismissals.first().requestKey, true);
    QVERIFY(!m_model->undoAvailable());
    QCOMPARE(ids(m_model->items()), QStringList({"movie:m"}));
}

QTEST_GUILESS_MAIN(ContinueWatchingViewModelTest)
#include "ContinueWatchingViewModelTest.moc"
