#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "../src/network/LibraryService.h"
#include "../src/utils/ConfigManager.h"
#include "../src/viewmodels/TasteSeedViewModels.h"

#include "TestConfigIsolation.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct PageRequest {
        QString cursor;
        int limit = 0;
        QString requestKey;
    };
    struct SubmitRequest {
        QStringList itemIds;
        QString requestKey;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool supportsTasteSeed() const override { return supported && !connectionId.isEmpty(); }
    QString tasteSeedScope() const override
    {
        return supportsTasteSeed() ? connectionId + QLatin1Char('\n') + profileId : QString();
    }
    void getTasteSeedState(const QString &requestKey) override
    {
        stateRequests.append(requestKey);
    }
    void getTasteSeedItems(const QString &cursor, int limit, const QString &requestKey) override
    {
        pageRequests.append({cursor, limit, requestKey});
    }
    void submitTasteSeed(const QStringList &itemIds, const QString &requestKey) override
    {
        submitRequests.append({itemIds, requestKey});
    }

    void answerState(bool cold, int request = -1, const QString &forConnection = QString())
    {
        const QString key = request < 0 ? stateRequests.last() : stateRequests.at(request);
        emit tasteSeedStateLoaded(forConnection.isEmpty() ? connectionId : forConnection, key,
                                     cold);
    }
    void failState()
    {
        emit tasteSeedStateFailed(connectionId, stateRequests.last(),
                                     QStringLiteral("Server error"));
    }
    void answerPage(const QVariantList &items, bool hasMore, const QString &nextCursor = QString(),
                    int request = -1, const QString &forConnection = QString())
    {
        const PageRequest sent = request < 0 ? pageRequests.last() : pageRequests.at(request);
        emit canonicalTasteSeedItemsLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                           sent.requestKey, items, hasMore, nextCursor);
    }
    void failPage(int httpStatus = 500)
    {
        emit canonicalTasteSeedItemsFailed(connectionId, pageRequests.last().requestKey,
                                           QStringLiteral("Server error"), httpStatus);
    }
    void finishSubmit(bool success, const QString &error = QString())
    {
        emit homeCardWriteFinished(connectionId, submitRequests.last().requestKey, success, error);
    }

    QString connectionId = QStringLiteral("connection-1");
    QString profileId = QStringLiteral("profile-1");
    bool supported = true;
    QStringList stateRequests;
    QList<PageRequest> pageRequests;
    QList<SubmitRequest> submitRequests;
};

QVariantMap title(const QString &itemId, const QString &mediaType = QStringLiteral("Movie"),
                  bool favorite = false)
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType},
        {QStringLiteral("userState"), QVariantMap{{QStringLiteral("favorite"), favorite}}}
    };
}

QStringList rowIds(const QAbstractItemModel &model)
{
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        ids.append(model.data(model.index(row, 0), TasteSeedViewModel::ItemIdRole).toString());
    }
    return ids;
}

QString scopeOf(const ScriptedLibraryService &service)
{
    return service.connectionId + QLatin1Char('\n') + service.profileId;
}

} // namespace

class TasteSeedViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // TasteSeedPromptViewModel
    void promptShowsForColdProfilesOnly();
    void promptHiddenWithoutSupportInMergedSpacesAndWhenDone();
    void promptReadsOncePerScopeUnlessForced();
    void promptIgnoresStaleAndFailedReads();
    void promptDismissRemembersTheScope();
    void promptHidesOnAFavoriteAndASeedMarksTheScopeDone();

    // TasteSeedViewModel
    void pickerUnavailableReadsNothing();
    void pickerLoadsTitlesAndPreselectsFavorites();
    void pickerReadsOnPastEmptyPagesAndPagesByCursor();
    void pickerPageFailures();
    void pickerIgnoresStaleAnswers();
    void pickerSelectionNeedsTheMinimumAndStopsAtTheMaximum();
    void pickerSubmitsNewPicksAndMarksTheScopeDone();
    void pickerFavoritesAloneFinishWithoutARequest();
    void pickerSubmitFailureAndSessionChangeKeepTheSelection();
    void pickerSkipMarksTheScopeDone();
    void pickerSubmitAfterASessionChangeStartsOver();

private:
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<ScriptedLibraryService> m_service;
};

void TasteSeedViewModelTest::init()
{
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
    m_isolation = std::make_unique<ScopedConfigIsolation>(m_directory->path());
    m_config = std::make_unique<ConfigManager>();
    m_service = std::make_unique<ScriptedLibraryService>();
}

void TasteSeedViewModelTest::cleanup()
{
    m_service.reset();
    m_config.reset();
    m_isolation.reset();
    m_directory.reset();
}

// ============================================================================
// TasteSeedPromptViewModel
// ============================================================================

void TasteSeedViewModelTest::promptShowsForColdProfilesOnly()
{
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    QSignalSpy shown(&prompt, &TasteSeedPromptViewModel::shownChanged);
    QVERIFY(!prompt.shown());

    prompt.check();
    QCOMPARE(m_service->stateRequests.size(), 1);
    // Nothing shows until the server answers.
    QVERIFY(!prompt.shown());
    m_service->answerState(true);
    QVERIFY(prompt.shown());
    QCOMPARE(shown.count(), 1);

    // A warm profile hides it, and stays warm: no read in later sessions.
    prompt.check(true);
    m_service->answerState(false);
    QVERIFY(!prompt.shown());
    QCOMPARE(shown.count(), 2);
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));
    const qsizetype reads = m_service->stateRequests.size();
    TasteSeedPromptViewModel next(m_service.get(), m_config.get());
    next.check();
    next.check(true);
    QCOMPARE(m_service->stateRequests.size(), reads);
    QVERIFY(!next.shown());
}

void TasteSeedViewModelTest::promptHiddenWithoutSupportInMergedSpacesAndWhenDone()
{
    // Jellyfin (no taste seeding): no read at all.
    m_service->supported = false;
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    prompt.check();
    QVERIFY(m_service->stateRequests.isEmpty());
    QVERIFY(!prompt.shown());

    // A merged space: no read, nothing shown.
    m_service->supported = true;
    prompt.setMerged(true);
    prompt.check();
    QVERIFY(m_service->stateRequests.isEmpty());

    // Back to a single space: a cold profile shows, merging hides it again.
    prompt.setMerged(false);
    prompt.check();
    m_service->answerState(true);
    QVERIFY(prompt.shown());
    prompt.setMerged(true);
    QVERIFY(!prompt.shown());
    prompt.setMerged(false);
    QVERIFY(prompt.shown());

    // Done for this connection and profile: hidden, and never read again.
    m_config->setTasteSeedDone(scopeOf(*m_service), true);
    QVERIFY(!prompt.shown());
    const qsizetype reads = m_service->stateRequests.size();
    prompt.check(true);
    QCOMPARE(m_service->stateRequests.size(), reads);

    // Another profile of the same account is not done.
    m_service->profileId = QStringLiteral("profile-2");
    prompt.check();
    QCOMPARE(m_service->stateRequests.size(), reads + 1);
    m_service->answerState(true);
    QVERIFY(prompt.shown());
}

void TasteSeedViewModelTest::promptReadsOncePerScopeUnlessForced()
{
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    prompt.check();
    // A second check while the first is read does not read again.
    prompt.check();
    QCOMPARE(m_service->stateRequests.size(), 1);
    m_service->answerState(true);
    prompt.check();
    QCOMPARE(m_service->stateRequests.size(), 1);
    // After playback Home forces a fresh read.
    prompt.check(true);
    QCOMPARE(m_service->stateRequests.size(), 2);
    // A new profile is a new scope: hidden until its own answer.
    m_service->profileId = QStringLiteral("profile-2");
    prompt.check();
    QVERIFY(!prompt.shown());
    QCOMPARE(m_service->stateRequests.size(), 3);
}

void TasteSeedViewModelTest::promptIgnoresStaleAndFailedReads()
{
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    prompt.check();
    // The profile changed while the summary was read: its answer is dropped.
    m_service->profileId = QStringLiteral("profile-2");
    m_service->answerState(true);
    QVERIFY(!prompt.shown());

    // An answer to an older read is dropped too.
    prompt.check();
    prompt.check(true);
    QCOMPARE(m_service->stateRequests.size(), 3);
    m_service->answerState(true, 1);
    QVERIFY(!prompt.shown());
    m_service->answerState(true, 2);
    QVERIFY(prompt.shown());

    // A failed read shows nothing new and keeps nothing stale.
    m_service->profileId = QStringLiteral("profile-3");
    prompt.check();
    m_service->failState();
    QVERIFY(!prompt.shown());
}

void TasteSeedViewModelTest::promptDismissRemembersTheScope()
{
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    prompt.check();
    m_service->answerState(true);
    QVERIFY(prompt.shown());
    prompt.dismiss();
    QVERIFY(!prompt.shown());
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));

    // A new Home on the same scope stays quiet without asking the server.
    TasteSeedPromptViewModel next(m_service.get(), m_config.get());
    const qsizetype reads = m_service->stateRequests.size();
    next.check();
    QCOMPARE(m_service->stateRequests.size(), reads);
    QVERIFY(!next.shown());

    // A picker finishing elsewhere hides a shown prompt at once.
    m_service->profileId = QStringLiteral("profile-2");
    next.check();
    m_service->answerState(true);
    QVERIFY(next.shown());
    m_config->setTasteSeedDone(scopeOf(*m_service), true);
    QVERIFY(!next.shown());
}

void TasteSeedViewModelTest::promptHidesOnAFavoriteAndASeedMarksTheScopeDone()
{
    TasteSeedPromptViewModel prompt(m_service.get(), m_config.get());
    prompt.check();
    m_service->answerState(true);
    QVERIFY(prompt.shown());

    // Unfavoriting tells nothing; a favorite hides the prompt at once,
    // without marking the scope (the next read finds the profile warm).
    emit m_service->favoriteStatusChanged(QStringLiteral("movie:a"), false);
    QVERIFY(prompt.shown());
    emit m_service->favoriteStatusChanged(QStringLiteral("movie:a"), true);
    QVERIFY(!prompt.shown());
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));

    // A seed recorded for another connection changes nothing.
    m_service->profileId = QStringLiteral("profile-2");
    prompt.check();
    m_service->answerState(true);
    QVERIFY(prompt.shown());
    emit m_service->tasteSeedSubmitted(QStringLiteral("connection-2"), {QStringLiteral("movie:a")});
    QVERIFY(prompt.shown());
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));

    // The live connection's seed marks the scope done, also when no picker
    // is left to do it (Back while saving).
    emit m_service->tasteSeedSubmitted(QStringLiteral("connection-1"), {QStringLiteral("movie:a")});
    QVERIFY(!prompt.shown());
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));
}

// ============================================================================
// TasteSeedViewModel
// ============================================================================

void TasteSeedViewModelTest::pickerUnavailableReadsNothing()
{
    m_service->supported = false;
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    picker.load();
    QVERIFY(!picker.available());
    QVERIFY(!picker.isLoading());
    QVERIFY(!picker.hasError());
    QVERIFY(m_service->pageRequests.isEmpty());
    QVERIFY(!picker.submit());
}

void TasteSeedViewModelTest::pickerLoadsTitlesAndPreselectsFavorites()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    const QHash<int, QByteArray> roles = picker.roleNames();
    QCOMPARE(roles.value(TasteSeedViewModel::ItemDataRole), QByteArrayLiteral("itemData"));
    QCOMPARE(roles.value(TasteSeedViewModel::SelectedRole), QByteArrayLiteral("selected"));

    picker.load();
    QVERIFY(picker.available());
    QVERIFY(picker.isLoading());
    QCOMPARE(picker.connectionId(), QStringLiteral("connection-1"));
    QCOMPARE(m_service->pageRequests.size(), 1);
    QCOMPARE(m_service->pageRequests.first().cursor, QString());
    QCOMPARE(m_service->pageRequests.first().limit, TasteSeedViewModel::PageSize);

    m_service->answerPage({title(QStringLiteral("movie:a")),
                           title(QStringLiteral("series:b"), QStringLiteral("Series"), true),
                           // Not a title Bloom shows, and a repeat.
                           title(QStringLiteral("episode:c"), QStringLiteral("Episode")),
                           title(QStringLiteral("movie:a"))},
                          false);
    QVERIFY(!picker.isLoading());
    QVERIFY(!picker.hasMore());
    QCOMPARE(rowIds(picker), (QStringList{QStringLiteral("movie:a"), QStringLiteral("series:b")}));
    // The profile's favorite starts selected and counts toward the minimum.
    QCOMPARE(picker.selectedCount(), 1);
    QVERIFY(!picker.data(picker.index(0), TasteSeedViewModel::SelectedRole).toBool());
    QVERIFY(picker.data(picker.index(1), TasteSeedViewModel::SelectedRole).toBool());
    QCOMPARE(picker.get(1).value(QStringLiteral("mediaType")).toString(), QStringLiteral("Series"));
    QVERIFY(picker.newPicks().isEmpty());
}

void TasteSeedViewModelTest::pickerReadsOnPastEmptyPagesAndPagesByCursor()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    picker.load();
    // A page with nothing Bloom shows reads the next one at once.
    m_service->answerPage({title(QStringLiteral("episode:x"), QStringLiteral("Episode"))}, true,
                          QStringLiteral("c1"));
    QCOMPARE(m_service->pageRequests.size(), 2);
    QCOMPARE(m_service->pageRequests.last().cursor, QStringLiteral("c1"));
    QVERIFY(picker.isLoading());
    m_service->answerPage({title(QStringLiteral("movie:a"))}, true, QStringLiteral("c2"));
    QVERIFY(!picker.isLoading());
    QVERIFY(picker.hasMore());
    QCOMPARE(picker.count(), 1);

    picker.loadMore();
    QVERIFY(picker.loadingMore());
    QCOMPARE(m_service->pageRequests.last().cursor, QStringLiteral("c2"));
    // Another loadMore while one is read does nothing.
    picker.loadMore();
    QCOMPARE(m_service->pageRequests.size(), 3);

    // Up to MaxEmptyPages empty pages in a row, then it waits.
    for (int i = 0; i < TasteSeedViewModel::MaxEmptyPages; ++i) {
        m_service->answerPage({}, true, QStringLiteral("e%1").arg(i));
    }
    QCOMPARE(m_service->pageRequests.size(), 3 + TasteSeedViewModel::MaxEmptyPages - 1);
    QVERIFY(!picker.loadingMore());
    QVERIFY(picker.hasMore());

    // A continuation without a cursor ends the list.
    picker.loadMore();
    m_service->answerPage({title(QStringLiteral("movie:b"))}, true, QString());
    QVERIFY(!picker.hasMore());
    QCOMPARE(picker.count(), 2);
}

void TasteSeedViewModelTest::pickerPageFailures()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    QSignalSpy pageFailed(&picker, &TasteSeedViewModel::pageFailed);
    picker.load();
    m_service->failPage();
    QVERIFY(!picker.isLoading());
    QVERIFY(picker.hasError());
    QCOMPARE(picker.errorMessage(), QStringLiteral("Server error"));
    QCOMPARE(pageFailed.count(), 0);

    picker.reload();
    QVERIFY(!picker.hasError());
    m_service->answerPage({title(QStringLiteral("movie:a"))}, true, QStringLiteral("c1"));
    picker.loadMore();
    m_service->failPage();
    // The cards stay and the same page can be read again.
    QVERIFY(!picker.hasError());
    QCOMPARE(pageFailed.count(), 1);
    QCOMPARE(picker.count(), 1);
    QVERIFY(picker.hasMore());
    picker.loadMore();
    QCOMPARE(m_service->pageRequests.last().cursor, QStringLiteral("c1"));
}

void TasteSeedViewModelTest::pickerIgnoresStaleAnswers()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    picker.load();
    // Another connection's answer is dropped.
    m_service->answerPage({title(QStringLiteral("movie:x"))}, false, QString(), -1,
                          QStringLiteral("connection-2"));
    QCOMPARE(picker.count(), 0);
    QVERIFY(picker.isLoading());

    // So is the answer to an older load.
    picker.load();
    m_service->answerPage({title(QStringLiteral("movie:old"))}, false, QString(), 0);
    QCOMPARE(picker.count(), 0);
    m_service->answerPage({title(QStringLiteral("movie:new"))}, false);
    QCOMPARE(rowIds(picker), QStringList{QStringLiteral("movie:new")});

    // A load after a session change starts over on the new connection.
    picker.toggle(0);
    m_service->connectionId = QStringLiteral("connection-2");
    picker.load();
    QCOMPARE(picker.connectionId(), QStringLiteral("connection-2"));
    QCOMPARE(picker.count(), 0);
    QCOMPARE(picker.selectedCount(), 0);
    m_service->answerPage({title(QStringLiteral("movie:late"))}, false, QString(), 1,
                          QStringLiteral("connection-1"));
    QCOMPARE(picker.count(), 0);
}

void TasteSeedViewModelTest::pickerSelectionNeedsTheMinimumAndStopsAtTheMaximum()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    picker.load();
    QVariantList page;
    for (int i = 0; i < TasteSeedViewModel::MaximumPicks + 2; ++i) {
        page.append(title(QStringLiteral("movie:%1").arg(i)));
    }
    page.append(title(QStringLiteral("series:fav"), QStringLiteral("Series"), true));
    m_service->answerPage(page, false);
    QCOMPARE(picker.selectedCount(), 1);
    QVERIFY(!picker.canSubmit());
    QVERIFY(!picker.submit());

    QSignalSpy selection(&picker, &TasteSeedViewModel::selectionChanged);
    QSignalSpy changed(&picker, &QAbstractItemModel::dataChanged);
    QVERIFY(picker.toggle(0));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 0);
    QVERIFY(picker.data(picker.index(0), TasteSeedViewModel::SelectedRole).toBool());
    QVERIFY(!picker.canSubmit());
    QVERIFY(picker.toggle(1));
    QCOMPARE(picker.selectedCount(), picker.minimumPicks());
    QVERIFY(picker.canSubmit());
    QCOMPARE(selection.count(), 2);
    // Unpicking goes back below the minimum.
    QVERIFY(picker.toggle(1));
    QVERIFY(!picker.canSubmit());
    QVERIFY(!picker.toggle(-1));
    QVERIFY(!picker.toggle(picker.count()));

    // New picks stop at the server's maximum; favorites do not count.
    for (int i = 1; i < TasteSeedViewModel::MaximumPicks; ++i) {
        QVERIFY(picker.toggle(i));
    }
    QCOMPARE(picker.newPicks().size(), TasteSeedViewModel::MaximumPicks);
    QVERIFY(!picker.toggle(TasteSeedViewModel::MaximumPicks));
    // A favorite stays picked: unpicking would not remove it on the server.
    const int favorite = picker.count() - 1;
    QVERIFY(picker.favoriteAt(favorite));
    QVERIFY(picker.data(picker.index(favorite), TasteSeedViewModel::FavoriteRole).toBool());
    QVERIFY(!picker.favoriteAt(0));
    QVERIFY(!picker.data(picker.index(0), TasteSeedViewModel::FavoriteRole).toBool());
    QVERIFY(!picker.toggle(favorite));
    QVERIFY(picker.data(picker.index(favorite), TasteSeedViewModel::SelectedRole).toBool());
}

void TasteSeedViewModelTest::pickerSubmitsNewPicksAndMarksTheScopeDone()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    QSignalSpy submitted(&picker, &TasteSeedViewModel::submitted);
    picker.load();
    m_service->answerPage({title(QStringLiteral("movie:a")),
                           title(QStringLiteral("series:fav"), QStringLiteral("Series"), true),
                           title(QStringLiteral("movie:b")),
                           title(QStringLiteral("movie:c"))},
                          false);
    picker.toggle(2);
    picker.toggle(0);
    QVERIFY(picker.canSubmit());
    QVERIFY(picker.submit());
    QVERIFY(picker.submitting());
    QVERIFY(!picker.canSubmit());
    // Busy: no second submission, no selection change.
    QVERIFY(!picker.submit());
    QVERIFY(!picker.toggle(3));
    QCOMPARE(m_service->submitRequests.size(), 1);
    // Only the new picks, in the order shown.
    QCOMPARE(m_service->submitRequests.first().itemIds,
             (QStringList{QStringLiteral("movie:a"), QStringLiteral("movie:b")}));
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));

    m_service->finishSubmit(true);
    QVERIFY(!picker.submitting());
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.first().at(0).toInt(), 2);
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));
    // The picks are favorites now: nothing new to send.
    QVERIFY(picker.newPicks().isEmpty());

    // Another view model's write is not this one's.
    emit m_service->homeCardWriteFinished(QStringLiteral("connection-1"),
                                          QStringLiteral("other-key"), true, QString());
    QCOMPARE(submitted.count(), 1);
}

void TasteSeedViewModelTest::pickerFavoritesAloneFinishWithoutARequest()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    QSignalSpy submitted(&picker, &TasteSeedViewModel::submitted);
    picker.load();
    m_service->answerPage({title(QStringLiteral("movie:a"), QStringLiteral("Movie"), true),
                           title(QStringLiteral("movie:b"), QStringLiteral("Movie"), true),
                           title(QStringLiteral("movie:c"), QStringLiteral("Movie"), true)},
                          false);
    QVERIFY(picker.canSubmit());
    QVERIFY(picker.submit());
    QVERIFY(m_service->submitRequests.isEmpty());
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.first().at(0).toInt(), 0);
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));
}

void TasteSeedViewModelTest::pickerSubmitFailureAndSessionChangeKeepTheSelection()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    QSignalSpy submitted(&picker, &TasteSeedViewModel::submitted);
    QSignalSpy failed(&picker, &TasteSeedViewModel::submitFailed);
    picker.load();
    m_service->answerPage({title(QStringLiteral("movie:a")), title(QStringLiteral("movie:b")),
                           title(QStringLiteral("movie:c"))},
                          false);
    picker.toggle(0);
    picker.toggle(1);
    picker.toggle(2);
    QVERIFY(picker.submit());
    m_service->finishSubmit(false, QStringLiteral("Unprocessable"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("Unprocessable"));
    QCOMPARE(submitted.count(), 0);
    QVERIFY(!picker.submitting());
    QCOMPARE(picker.selectedCount(), 3);
    QVERIFY(picker.canSubmit());
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));

    // A session change before the answer: LibraryService answers the write
    // as failed (homeCardWriteUnconfirmed, then homeCardWriteFinished).
    QVERIFY(picker.submit());
    QCOMPARE(m_service->submitRequests.size(), 2);
    emit m_service->homeCardWriteUnconfirmed(QStringLiteral("connection-1"),
                                             m_service->submitRequests.last().requestKey);
    m_service->finishSubmit(false, QStringLiteral("The connection changed before the server answered."));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(submitted.count(), 0);
    QVERIFY(!picker.submitting());
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));
}

void TasteSeedViewModelTest::pickerSkipMarksTheScopeDone()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    picker.load();
    picker.skip();
    QVERIFY(m_config->isTasteSeedDone(scopeOf(*m_service)));
    QVERIFY(m_service->submitRequests.isEmpty());
    // Done is per connection and profile.
    QVERIFY(!m_config->isTasteSeedDone(QStringLiteral("connection-1\nprofile-2")));
    QVERIFY(!m_config->isTasteSeedDone(QString()));
    m_config->setTasteSeedDone(scopeOf(*m_service), false);
    QVERIFY(!m_config->isTasteSeedDone(scopeOf(*m_service)));
}

void TasteSeedViewModelTest::pickerSubmitAfterASessionChangeStartsOver()
{
    TasteSeedViewModel picker(m_service.get(), m_config.get());
    QSignalSpy submitted(&picker, &TasteSeedViewModel::submitted);
    picker.load();
    m_service->answerPage({title(QStringLiteral("movie:a")), title(QStringLiteral("movie:b")),
                           title(QStringLiteral("movie:c"))},
                          false);
    picker.toggle(0);
    picker.toggle(1);
    picker.toggle(2);
    QVERIFY(picker.canSubmit());

    // Another profile of the same connection: the picks are not its own.
    m_service->profileId = QStringLiteral("profile-2");
    QVERIFY(!picker.submit());
    QVERIFY(m_service->submitRequests.isEmpty());
    QCOMPARE(submitted.count(), 0);
    QCOMPARE(picker.count(), 0);
    QCOMPARE(picker.selectedCount(), 0);
    QVERIFY(picker.isLoading());
    QCOMPARE(m_service->pageRequests.size(), 2);

    // Another connection too.
    m_service->answerPage({title(QStringLiteral("movie:a")), title(QStringLiteral("movie:b")),
                           title(QStringLiteral("movie:c"))},
                          false);
    picker.toggle(0);
    picker.toggle(1);
    picker.toggle(2);
    m_service->connectionId = QStringLiteral("connection-2");
    QVERIFY(!picker.submit());
    QVERIFY(m_service->submitRequests.isEmpty());
    QCOMPARE(picker.connectionId(), QStringLiteral("connection-2"));
    QCOMPARE(picker.selectedCount(), 0);
    QVERIFY(!m_config->isTasteSeedDone(QStringLiteral("connection-1\nprofile-2")));
}

QTEST_MAIN(TasteSeedViewModelTest)
#include "TasteSeedViewModelTest.moc"
