#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"
#include "../src/viewmodels/HomeSectionsViewModel.h"
#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"

#include "TestConfigIsolation.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct WatchlistWrite {
        QString itemId;
        bool inWatchlist = false;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool supportsWatchlist() const override { return watchlist; }
    bool supportsHomeFavoritesRow() const override { return !watchlist; }

    void getHomeSections() override { ++homeRequests; }
    void setItemWatchlisted(const QString &itemId, bool inWatchlist) override
    {
        writes.append({itemId, inWatchlist});
    }

    using LibraryService::getItem;
    void getItem(const QString &itemId) override { itemRequests.append(itemId); }
    void getChapters(const QString &) override {}
    void getSimilarItems(const QString &, int) override {}
    void getItemExtras(const QString &itemId) override { extrasRequests.append(itemId); }
    void getSeriesDetails(const QString &seriesId) override { itemRequests.append(seriesId); }
    void getNextUnplayedEpisode(const QString &, const QString &, const QString &) override {}

    void loadRows(const QString &forConnection, const QVariantList &rows)
    {
        emit canonicalHomeSectionsLoaded(forConnection, rows);
    }
    void failRows(const QString &forConnection, const QString &error)
    {
        emit canonicalHomeSectionsFailed(forConnection, error);
    }
    void confirmWatchlist(const QString &itemId, bool inWatchlist)
    {
        emit watchlistStatusChanged(itemId, inWatchlist);
    }
    void confirmFavorite(const QString &itemId, bool isFavorite)
    {
        emit favoriteStatusChanged(itemId, isFavorite);
    }
    void refuseWatchlist(const QString &itemId, bool inWatchlist)
    {
        emit watchlistStatusFailed(itemId, inWatchlist, QStringLiteral("Not Found"));
    }
    void failRead(const QString &endpoint, const QString &error)
    {
        emit errorOccurred(endpoint, error);
    }
    void loadMovie(const QString &itemId, const QVariantMap &data)
    {
        emit canonicalItemLoaded(itemId, data);
    }
    void loadSeries(const QString &seriesId, const QVariantMap &data)
    {
        emit canonicalSeriesDetailsLoaded(connectionId, seriesId, data);
    }
    void loadExtras(const QString &forConnection,
                    const QString &itemId,
                    const QVariantList &extras,
                    bool complete = true)
    {
        emit canonicalItemExtrasLoaded(forConnection, itemId, extras, complete);
    }
    void failExtras(const QString &forConnection, const QString &itemId)
    {
        emit canonicalItemExtrasFailed(forConnection, itemId, QStringLiteral("Not Found"));
    }

    QString connectionId = QStringLiteral("connection-1");
    bool watchlist = true;
    int homeRequests = 0;
    QList<WatchlistWrite> writes;
    QStringList itemRequests;
    QStringList extrasRequests;
};

QVariantMap extra(const QString &itemId, const QString &kind)
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("extraKind"), kind}
    };
}

QVariantMap card(const QString &itemId, const QString &mediaType)
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType}
    };
}

QVariantMap row(const QString &sectionId, const QString &title, const QVariantList &items)
{
    return {
        {QStringLiteral("sectionId"), sectionId},
        {QStringLiteral("title"), title},
        {QStringLiteral("items"), items}
    };
}

QStringList sectionIds(const HomeSectionsViewModel &model)
{
    QStringList ids;
    for (int i = 0; i < model.rowCount(); ++i) {
        ids.append(model.data(model.index(i), HomeSectionsViewModel::SectionIdRole).toString());
    }
    return ids;
}

QVariantMap detail(const QString &itemId, const QString &mediaType, bool inWatchlist)
{
    QVariantMap data = card(itemId, mediaType);
    data.insert(QStringLiteral("inWatchlist"), inWatchlist);
    data.insert(QStringLiteral("userState"),
                QVariantMap{{QStringLiteral("inWatchlist"), inWatchlist}});
    return data;
}

} // namespace

class HomeRowsAndWatchlistTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void homeRowsExposeServerOrderAndRoles();
    void homeRowsUpdateInPlaceWhenRowsAreUnchanged();
    void homeRowsIgnoreOtherConnectionsAndDropThemOnSwitch();
    void homeRowsFailureKeepsShownRows();
    void homeRowsRefreshAfterWatchlistChange();
    void homeRowsRefreshAfterFavoriteChangeOnlyWithFavoritesRow();
    void homeRowsRefreshAfterFavoriteChangeDuringFirstRead();
    void homeRowsClearForMergedSpaces();

    void movieWatchlistToggleIsOptimisticAndRollsBack();
    void movieWatchlistIsHiddenWithoutProviderSupport();
    void seriesWatchlistToggleIsOptimisticAndRollsBack();
    void movieLoadIgnoresOtherReadsErrors();

    void movieExtrasComeFromServiceWhenDetailHasNone();
    void movieExtrasInDetailNeedNoRequest();
    void seriesExtrasExposeTrailer();

private:
    QTemporaryDir m_configDirectory;
    std::unique_ptr<ScopedConfigIsolation> m_configIsolation;
    ScriptedLibraryService *m_service = nullptr;
};

void HomeRowsAndWatchlistTest::initTestCase()
{
    QVERIFY(m_configDirectory.isValid());
    m_configIsolation = std::make_unique<ScopedConfigIsolation>(m_configDirectory.path());
}

void HomeRowsAndWatchlistTest::init()
{
    ServiceLocator::clear();
    m_service = new ScriptedLibraryService(this);
    ServiceLocator::registerService<LibraryService>(m_service);
}

void HomeRowsAndWatchlistTest::cleanup()
{
    ServiceLocator::clear();
    delete m_service;
    m_service = nullptr;
}

void HomeRowsAndWatchlistTest::homeRowsExposeServerOrderAndRoles()
{
    HomeSectionsViewModel model(m_service);
    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(HomeSectionsViewModel::SectionIdRole), QByteArrayLiteral("sectionId"));
    QCOMPARE(roles.value(HomeSectionsViewModel::TitleRole), QByteArrayLiteral("title"));
    QCOMPARE(roles.value(HomeSectionsViewModel::ItemsRole), QByteArrayLiteral("items"));

    model.refresh();
    QCOMPARE(m_service->homeRequests, 1);
    QVERIFY(model.isLoading());

    m_service->loadRows(QStringLiteral("connection-1"), {
        row(QStringLiteral("bloom:watchlist"), QStringLiteral("Watchlist"),
            {card(QStringLiteral("movie:w"), QStringLiteral("Movie"))}),
        row(QStringLiteral("for-you"), QStringLiteral("For You"),
            {card(QStringLiteral("movie:a"), QStringLiteral("Movie")),
             card(QStringLiteral("series:b"), QStringLiteral("Series"))})
    });
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasError());
    QCOMPARE(sectionIds(model), (QStringList{QStringLiteral("bloom:watchlist"),
                                             QStringLiteral("for-you")}));
    QCOMPARE(model.data(model.index(1), HomeSectionsViewModel::TitleRole).toString(),
             QStringLiteral("For You"));
    const QVariantList items
        = model.data(model.index(1), HomeSectionsViewModel::ItemsRole).toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("mediaType")).toString(),
             QStringLiteral("Series"));
}

void HomeRowsAndWatchlistTest::homeRowsUpdateInPlaceWhenRowsAreUnchanged()
{
    HomeSectionsViewModel model(m_service);
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {
        row(QStringLiteral("a"), QStringLiteral("A"), {card(QStringLiteral("1"), QStringLiteral("Movie"))}),
        row(QStringLiteral("b"), QStringLiteral("B"), {card(QStringLiteral("2"), QStringLiteral("Movie"))})
    });

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {
        row(QStringLiteral("a"), QStringLiteral("A"), {card(QStringLiteral("1"), QStringLiteral("Movie"))}),
        row(QStringLiteral("b"), QStringLiteral("B"), {card(QStringLiteral("3"), QStringLiteral("Movie"))})
    });
    // Same rows, same order: only the changed row is updated, delegates stay.
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);

    // New or reordered rows reset the model.
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {
        row(QStringLiteral("b"), QStringLiteral("B"), {card(QStringLiteral("3"), QStringLiteral("Movie"))}),
        row(QStringLiteral("a"), QStringLiteral("A"), {card(QStringLiteral("1"), QStringLiteral("Movie"))})
    });
    QCOMPARE(reset.count(), 1);
    QCOMPARE(sectionIds(model), (QStringList{QStringLiteral("b"), QStringLiteral("a")}));
}

void HomeRowsAndWatchlistTest::homeRowsIgnoreOtherConnectionsAndDropThemOnSwitch()
{
    HomeSectionsViewModel model(m_service);
    // Answers nobody asked for are ignored.
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("a"), QStringLiteral("A"),
                             {card(QStringLiteral("1"), QStringLiteral("Movie"))})});
    QCOMPARE(model.rowCount(), 0);

    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("a"), QStringLiteral("A"),
                             {card(QStringLiteral("1"), QStringLiteral("Movie"))})});
    QCOMPARE(model.rowCount(), 1);

    // Another connection: the old rows go before the new ones load, and a
    // late answer for the old connection is ignored.
    m_service->connectionId = QStringLiteral("connection-2");
    model.refresh();
    QCOMPARE(model.rowCount(), 0);
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("stale"), QStringLiteral("Stale"),
                             {card(QStringLiteral("9"), QStringLiteral("Movie"))})});
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.isLoading());
    m_service->loadRows(QStringLiteral("connection-2"),
                        {row(QStringLiteral("b"), QStringLiteral("B"),
                             {card(QStringLiteral("2"), QStringLiteral("Movie"))})});
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("b")});
}

void HomeRowsAndWatchlistTest::homeRowsFailureKeepsShownRows()
{
    HomeSectionsViewModel model(m_service);
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("a"), QStringLiteral("A"),
                             {card(QStringLiteral("1"), QStringLiteral("Movie"))})});

    model.refresh();
    m_service->failRows(QStringLiteral("connection-1"), QStringLiteral("timeout"));
    QVERIFY(!model.isLoading());
    QVERIFY(model.hasError());
    QVERIFY(!model.errorMessage().isEmpty());
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("a")});

    // reload() retries and a success clears the error.
    model.reload();
    QCOMPARE(m_service->homeRequests, 3);
    QVERIFY(!model.hasError());
    m_service->loadRows(QStringLiteral("connection-1"), {});
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.hasError());
}

void HomeRowsAndWatchlistTest::homeRowsRefreshAfterWatchlistChange()
{
    HomeSectionsViewModel model(m_service);
    // Nothing loaded yet: a watchlist change does not start a request.
    m_service->confirmWatchlist(QStringLiteral("movie:a"), true);
    QCOMPARE(m_service->homeRequests, 0);

    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {});
    QCOMPARE(m_service->homeRequests, 1);
    m_service->confirmWatchlist(QStringLiteral("movie:a"), true);
    QCOMPARE(m_service->homeRequests, 2);

    // A refused change leaves the rows alone.
    m_service->loadRows(QStringLiteral("connection-1"), {});
    m_service->refuseWatchlist(QStringLiteral("movie:a"), false);
    QCOMPARE(m_service->homeRequests, 2);
}

void HomeRowsAndWatchlistTest::homeRowsRefreshAfterFavoriteChangeOnlyWithFavoritesRow()
{
    HomeSectionsViewModel model(m_service);

    // A provider with a watchlist (Silo) has no Favorites row: favorites
    // leave its rows alone.
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {});
    QCOMPARE(m_service->homeRequests, 1);
    m_service->confirmFavorite(QStringLiteral("movie:a"), true);
    QCOMPARE(m_service->homeRequests, 1);

    // Without one (Jellyfin), a favorite change reloads the rows, so the
    // Favorites row appears even when it was empty.
    m_service->watchlist = false;
    m_service->confirmFavorite(QStringLiteral("movie-a"), true);
    QCOMPARE(m_service->homeRequests, 2);
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("bloom:favorites"), QStringLiteral("Favorites"),
                             {card(QStringLiteral("movie-a"), QStringLiteral("Movie"))})});
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("bloom:favorites")});
    m_service->confirmFavorite(QStringLiteral("movie-a"), false);
    QCOMPARE(m_service->homeRequests, 3);

    // Rows of another connection are not refreshed.
    m_service->loadRows(QStringLiteral("connection-1"), {});
    m_service->connectionId = QStringLiteral("connection-2");
    m_service->confirmFavorite(QStringLiteral("movie-a"), true);
    QCOMPARE(m_service->homeRequests, 3);
}

void HomeRowsAndWatchlistTest::homeRowsRefreshAfterFavoriteChangeDuringFirstRead()
{
    m_service->watchlist = false;
    HomeSectionsViewModel model(m_service);

    // The first read may have started before the change: it is replaced.
    model.refresh();
    QCOMPARE(m_service->homeRequests, 1);
    m_service->confirmFavorite(QStringLiteral("movie-a"), true);
    QCOMPARE(m_service->homeRequests, 2);
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("bloom:favorites"), QStringLiteral("Favorites"),
                             {card(QStringLiteral("movie-a"), QStringLiteral("Movie"))})});
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("bloom:favorites")});

    // A read pending for another connection is left alone.
    m_service->connectionId = QStringLiteral("connection-2");
    model.refresh();
    QCOMPARE(m_service->homeRequests, 3);
    m_service->connectionId = QStringLiteral("connection-3");
    m_service->confirmFavorite(QStringLiteral("movie-a"), false);
    QCOMPARE(m_service->homeRequests, 3);
}

void HomeRowsAndWatchlistTest::movieLoadIgnoresOtherReadsErrors()
{
    MovieDetailsViewModel vm;
    QSignalSpy loadError(&vm, &MovieDetailsViewModel::loadError);
    vm.loadMovieDetails(QStringLiteral("movie-errors"));
    QVERIFY(vm.isLoading());

    // Home's background reads fail while the page loads: not its error.
    m_service->failRead(QStringLiteral("getResumeItems"), QStringLiteral("Not Found"));
    m_service->failRead(QStringLiteral("getHomeFavorites"), QStringLiteral("Not Found"));
    QVERIFY(vm.isLoading());
    QCOMPARE(loadError.count(), 0);

    // The movie read's own failure is.
    m_service->failRead(QStringLiteral("getItem"), QStringLiteral("Server error"));
    QVERIFY(!vm.isLoading());
    QCOMPARE(loadError.count(), 1);
}

void HomeRowsAndWatchlistTest::homeRowsClearForMergedSpaces()
{
    HomeSectionsViewModel model(m_service);
    model.refresh();
    model.clear();
    QVERIFY(!model.isLoading());
    // The answer to the dropped request never shows.
    m_service->loadRows(QStringLiteral("connection-1"),
                        {row(QStringLiteral("a"), QStringLiteral("A"),
                             {card(QStringLiteral("1"), QStringLiteral("Movie"))})});
    QCOMPARE(model.rowCount(), 0);
    m_service->confirmWatchlist(QStringLiteral("movie:a"), true);
    QCOMPARE(m_service->homeRequests, 1);
}

void HomeRowsAndWatchlistTest::movieWatchlistToggleIsOptimisticAndRollsBack()
{
    MovieDetailsViewModel vm;
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    QVERIFY(vm.watchlistSupported());
    m_service->loadMovie(QStringLiteral("movie-1"),
                         detail(QStringLiteral("movie-1"), QStringLiteral("Movie"), false));
    QVERIFY(!vm.inWatchlist());

    QSignalSpy changed(&vm, &MovieDetailsViewModel::inWatchlistChanged);
    QSignalSpy failed(&vm, &MovieDetailsViewModel::watchlistUpdateFailed);

    // The button follows the press before the server answers.
    vm.toggleWatchlist();
    QVERIFY(vm.inWatchlist());
    QCOMPARE(m_service->writes.size(), 1);
    QCOMPARE(m_service->writes.first().itemId, QStringLiteral("movie-1"));
    QCOMPARE(m_service->writes.first().inWatchlist, true);

    // A second press while the first is in flight is ignored, and details
    // that predate the change do not undo it.
    vm.toggleWatchlist();
    QCOMPARE(m_service->writes.size(), 1);
    m_service->loadMovie(QStringLiteral("movie-1"),
                         detail(QStringLiteral("movie-1"), QStringLiteral("Movie"), false));
    QVERIFY(vm.inWatchlist());

    // Another item's answer is not ours.
    m_service->refuseWatchlist(QStringLiteral("movie-2"), true);
    QVERIFY(vm.inWatchlist());

    m_service->confirmWatchlist(QStringLiteral("movie-1"), true);
    QVERIFY(vm.inWatchlist());
    QCOMPARE(failed.count(), 0);

    // Removal refused: the previous state comes back with a message.
    vm.toggleWatchlist();
    QVERIFY(!vm.inWatchlist());
    QCOMPARE(m_service->writes.size(), 2);
    QCOMPARE(m_service->writes.at(1).inWatchlist, false);
    m_service->refuseWatchlist(QStringLiteral("movie-1"), false);
    QVERIFY(vm.inWatchlist());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(0).toString().isEmpty());

    // The confirmed state reaches the details cache for the next visit.
    vm.clear();
    QVERIFY(!vm.inWatchlist());
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    QVERIFY(vm.inWatchlist());
}

void HomeRowsAndWatchlistTest::movieWatchlistIsHiddenWithoutProviderSupport()
{
    // Jellyfin has no watchlist: the action is hidden and inert.
    m_service->watchlist = false;
    MovieDetailsViewModel vm;
    vm.loadMovieDetails(QStringLiteral("movie-3"));
    m_service->loadMovie(QStringLiteral("movie-3"),
                         detail(QStringLiteral("movie-3"), QStringLiteral("Movie"), false));
    QVERIFY(!vm.watchlistSupported());
    vm.toggleWatchlist();
    QVERIFY(!vm.inWatchlist());
    QVERIFY(m_service->writes.isEmpty());

    SeriesDetailsViewModel series;
    QVERIFY(!series.watchlistSupported());
    series.toggleWatchlist();
    QVERIFY(m_service->writes.isEmpty());
}

void HomeRowsAndWatchlistTest::seriesWatchlistToggleIsOptimisticAndRollsBack()
{
    m_service->connectionId.clear();
    SeriesDetailsViewModel vm;
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    QVERIFY(vm.watchlistSupported());
    m_service->loadSeries(QStringLiteral("series-1"),
                          detail(QStringLiteral("series-1"), QStringLiteral("Series"), true));
    QVERIFY(vm.inWatchlist());

    QSignalSpy failed(&vm, &SeriesDetailsViewModel::watchlistUpdateFailed);
    vm.toggleWatchlist();
    QVERIFY(!vm.inWatchlist());
    QCOMPARE(m_service->writes.size(), 1);
    QCOMPARE(m_service->writes.first().itemId, QStringLiteral("series-1"));
    QCOMPARE(m_service->writes.first().inWatchlist, false);

    m_service->refuseWatchlist(QStringLiteral("series-1"), false);
    QVERIFY(vm.inWatchlist());
    QCOMPARE(failed.count(), 1);

    vm.toggleWatchlist();
    m_service->confirmWatchlist(QStringLiteral("series-1"), false);
    QVERIFY(!vm.inWatchlist());
    QCOMPARE(failed.count(), 1);
}

// Jellyfin details carry no extras, so the view model asks for them and
// applies only the answer for its own connection and item.
void HomeRowsAndWatchlistTest::movieExtrasComeFromServiceWhenDetailHasNone()
{
    // Without a configured connection the movie view model scopes to _local.
    const QString scope = QStringLiteral("_local");
    MovieDetailsViewModel vm;
    QSignalSpy changed(&vm, &MovieDetailsViewModel::extrasChanged);
    vm.loadMovieDetails(QStringLiteral("movie-extras-1"));
    m_service->loadMovie(QStringLiteral("movie-extras-1"),
                         detail(QStringLiteral("movie-extras-1"), QStringLiteral("Movie"), false));
    QCOMPARE(m_service->extrasRequests, QStringList{QStringLiteral("movie-extras-1")});
    QVERIFY(vm.extras().isEmpty());
    QVERIFY(vm.trailer().isEmpty());
    QVERIFY(vm.extrasLoading());

    // Refreshed details do not ask twice while the answer is pending.
    m_service->loadMovie(QStringLiteral("movie-extras-1"),
                         detail(QStringLiteral("movie-extras-1"), QStringLiteral("Movie"), false));
    QCOMPARE(m_service->extrasRequests.size(), 1);

    const QVariantList extras{extra(QStringLiteral("featurette-1"), QStringLiteral("featurette")),
                              extra(QStringLiteral("trailer-1"), QStringLiteral("trailer"))};
    m_service->loadExtras(QStringLiteral("other-connection"), QStringLiteral("movie-extras-1"), extras);
    m_service->loadExtras(scope, QStringLiteral("movie-other"), extras);
    QVERIFY(vm.extras().isEmpty());
    QCOMPARE(changed.count(), 0);

    m_service->loadExtras(scope, QStringLiteral("movie-extras-1"), extras);
    QCOMPARE(vm.extras().size(), 2);
    QVERIFY(!vm.extrasLoading());
    QCOMPARE(vm.trailer().value(QStringLiteral("itemId")).toString(), QStringLiteral("trailer-1"));
    QCOMPARE(changed.count(), 1);

    // Clearing for another movie drops the shelf; coming back shows the
    // cached shelf at once and refreshes it once in the background.
    vm.clear();
    QVERIFY(vm.extras().isEmpty());
    vm.loadMovieDetails(QStringLiteral("movie-extras-1"));
    QCOMPARE(vm.extras().size(), 2);
    QCOMPARE(m_service->extrasRequests.size(), 2);
    m_service->loadMovie(QStringLiteral("movie-extras-1"),
                         detail(QStringLiteral("movie-extras-1"), QStringLiteral("Movie"), false));
    QCOMPARE(m_service->extrasRequests.size(), 2);
    // An extra removed on the server disappears with the refresh.
    m_service->loadExtras(scope, QStringLiteral("movie-extras-1"),
                          {extra(QStringLiteral("featurette-1"), QStringLiteral("featurette"))});
    QCOMPARE(vm.extras().size(), 1);
    QVERIFY(vm.trailer().isEmpty());

    // A failed answer keeps the shelf empty and lets a later load ask again.
    vm.loadMovieDetails(QStringLiteral("movie-extras-2"));
    m_service->loadMovie(QStringLiteral("movie-extras-2"),
                         detail(QStringLiteral("movie-extras-2"), QStringLiteral("Movie"), false));
    QCOMPARE(m_service->extrasRequests.size(), 3);
    m_service->failExtras(scope, QStringLiteral("movie-extras-2"));
    QVERIFY(vm.extras().isEmpty());
    QVERIFY(!vm.extrasLoading());
    m_service->loadMovie(QStringLiteral("movie-extras-2"),
                         detail(QStringLiteral("movie-extras-2"), QStringLiteral("Movie"), false));
    QCOMPARE(m_service->extrasRequests.size(), 4);

    // A partial answer (the trailer read failed) shows but is not cached:
    // the next visit starts empty and asks again, and the full answer then
    // brings Play Trailer.
    const QVariantList partial{extra(QStringLiteral("featurette-2"), QStringLiteral("featurette"))};
    m_service->loadExtras(scope, QStringLiteral("movie-extras-2"), partial, false);
    QCOMPARE(vm.extras().size(), 1);
    QVERIFY(vm.trailer().isEmpty());
    vm.clear();
    vm.loadMovieDetails(QStringLiteral("movie-extras-2"));
    m_service->loadMovie(QStringLiteral("movie-extras-2"),
                         detail(QStringLiteral("movie-extras-2"), QStringLiteral("Movie"), false));
    QVERIFY(vm.extras().isEmpty());
    QCOMPARE(m_service->extrasRequests.size(), 5);
    m_service->loadExtras(scope, QStringLiteral("movie-extras-2"),
                          {extra(QStringLiteral("trailer-2"), QStringLiteral("trailer")),
                           extra(QStringLiteral("featurette-2"), QStringLiteral("featurette"))});
    QCOMPARE(vm.trailer().value(QStringLiteral("itemId")).toString(), QStringLiteral("trailer-2"));
}

// Silo details list their extras; nothing else is requested.
void HomeRowsAndWatchlistTest::movieExtrasInDetailNeedNoRequest()
{
    MovieDetailsViewModel vm;
    vm.loadMovieDetails(QStringLiteral("movie-silo-1"));
    QVariantMap data = detail(QStringLiteral("movie-silo-1"), QStringLiteral("Movie"), false);
    data.insert(QStringLiteral("extras"),
                QVariantList{extra(QStringLiteral("teaser-1"), QStringLiteral("teaser")),
                             extra(QStringLiteral("deleted-1"), QStringLiteral("deletedScene"))});
    m_service->loadMovie(QStringLiteral("movie-silo-1"), data);
    QVERIFY(m_service->extrasRequests.isEmpty());
    QCOMPARE(vm.extras().size(), 2);
    // A teaser stands in when there is no trailer.
    QCOMPARE(vm.trailer().value(QStringLiteral("itemId")).toString(), QStringLiteral("teaser-1"));

    data.insert(QStringLiteral("extras"),
                QVariantList{extra(QStringLiteral("deleted-1"), QStringLiteral("deletedScene"))});
    m_service->loadMovie(QStringLiteral("movie-silo-1"), data);
    QCOMPARE(vm.extras().size(), 1);
    QVERIFY(vm.trailer().isEmpty());

    data.insert(QStringLiteral("extras"), QVariantList{});
    m_service->loadMovie(QStringLiteral("movie-silo-1"), data);
    QVERIFY(vm.extras().isEmpty());
    QVERIFY(m_service->extrasRequests.isEmpty());
}

void HomeRowsAndWatchlistTest::seriesExtrasExposeTrailer()
{
    m_service->connectionId.clear();
    SeriesDetailsViewModel vm;
    QSignalSpy changed(&vm, &SeriesDetailsViewModel::extrasChanged);
    vm.loadSeriesDetails(QStringLiteral("series-extras-1"));
    QVariantMap data = detail(QStringLiteral("series-extras-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("extras"),
                QVariantList{extra(QStringLiteral("bts-1"), QStringLiteral("behindTheScenes")),
                             extra(QStringLiteral("trailer-1"), QStringLiteral("trailer"))});
    m_service->loadSeries(QStringLiteral("series-extras-1"), data);
    QVERIFY(m_service->extrasRequests.isEmpty());
    QCOMPARE(vm.extras().size(), 2);
    QCOMPARE(vm.trailer().value(QStringLiteral("itemId")).toString(), QStringLiteral("trailer-1"));
    QCOMPARE(changed.count(), 1);

    // A series whose detail has no extras asks the service.
    vm.loadSeriesDetails(QStringLiteral("series-extras-2"));
    QVERIFY(vm.extras().isEmpty());
    m_service->loadSeries(QStringLiteral("series-extras-2"),
                          detail(QStringLiteral("series-extras-2"), QStringLiteral("Series"), false));
    QCOMPARE(m_service->extrasRequests, QStringList{QStringLiteral("series-extras-2")});
    m_service->loadExtras(QString(), QStringLiteral("series-extras-2"),
                          {extra(QStringLiteral("clip-1"), QStringLiteral("clip"))});
    QCOMPARE(vm.extras().size(), 1);
    QVERIFY(vm.trailer().isEmpty());
}

QTEST_MAIN(HomeRowsAndWatchlistTest)
#include "HomeRowsAndWatchlistTest.moc"
