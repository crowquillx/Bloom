#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"
#include "../src/viewmodels/HomeSectionsViewModel.h"
#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"
#include "../src/utils/ExternalRatingsHelper.h"

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

    void getHomeSections(const QStringList &movieLibraryIds, bool reloadRecommendations) override
    {
        ++homeRequests;
        lastMovieLibraryIds = movieLibraryIds;
        lastReloadRecommendations = reloadRecommendations;
    }
    void setItemWatchlisted(const QString &itemId, bool inWatchlist) override
    {
        writes.append({itemId, inWatchlist});
    }
    bool supportsNotInterested() const override { return notInterestedAllowed; }
    void loadNotInterestedCapability() override { ++notInterestedCapabilityReads; }
    void setItemNotInterested(const QString &itemId, bool notInterested) override
    {
        notInterestedWrites.append({itemId, notInterested});
    }
    QString userRatingScale() const override { return ratingScale; }
    void loadUserRatingCapability() override { ++ratingCapabilityReads; }
    void setUserRating(const QString &itemId, int value) override
    {
        ratingWrites.append({itemId, value});
    }
    bool supportsNextAiring() const override { return airingCalendar; }
    void getNextAiring(const QString &seriesId) override
    {
        airingRequests.append(seriesId);
        // A cached calendar answers before the call returns.
        if (answerAiringAtOnce) {
            emit nextAiringLoaded(seriesId, {});
        }
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
    void allowNotInterested()
    {
        notInterestedAllowed = true;
        emit notInterestedSupportChanged();
    }
    void confirmNotInterested(const QString &itemId, bool notInterested)
    {
        emit notInterestedStatusChanged(itemId, notInterested);
    }
    void refuseNotInterested(const QString &itemId, bool notInterested)
    {
        emit notInterestedStatusFailed(itemId, notInterested, QStringLiteral("Unprocessable"));
    }
    void allowRatings(const QString &scale)
    {
        ratingScale = scale;
        emit userRatingSupportChanged();
    }
    void confirmRating(const QString &itemId, int value)
    {
        emit userRatingChanged(itemId, value);
    }
    void refuseRating(const QString &itemId, int value)
    {
        emit userRatingFailed(itemId, value, QStringLiteral("Unprocessable"));
    }
    void loadAiring(const QString &seriesId, const QVariantMap &airing)
    {
        emit nextAiringLoaded(seriesId, airing);
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
    QStringList lastMovieLibraryIds;
    bool lastReloadRecommendations = false;
    QList<WatchlistWrite> writes;
    bool notInterestedAllowed = false;
    int notInterestedCapabilityReads = 0;
    QList<WatchlistWrite> notInterestedWrites;
    QString ratingScale;
    int ratingCapabilityReads = 0;
    QList<std::pair<QString, int>> ratingWrites;
    bool airingCalendar = true;
    bool answerAiringAtOnce = false;
    QStringList airingRequests;
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
    void homeRowsReadRecommendationsFromShownMovieLibraries();
    void homeRowsReloadRecommendationsOnlyWhenAsked();
    void homeRowsDropRecommendationRowsAtOnce();
    void homeRowsRestoreFocusToNearestRow();

    void movieWatchlistToggleIsOptimisticAndRollsBack();
    void movieWatchlistIsHiddenWithoutProviderSupport();
    void seriesWatchlistToggleIsOptimisticAndRollsBack();
    void movieLoadIgnoresOtherReadsErrors();

    void movieExtrasComeFromServiceWhenDetailHasNone();
    void movieExtrasInDetailNeedNoRequest();
    void seriesExtrasExposeTrailer();

    void homeRowsRefreshAfterNotInterestedChange();
    void movieNotInterestedWaitsForCapabilityAndRollsBack();
    void movieStarRatingWaitsForCapabilityAndRollsBack();
    void seriesLikeRatingIsOptimisticAndRollsBack();
    void seriesNotInterestedToggleIsOptimisticAndRollsBack();
    void seriesNextAiringComesFromDetailsOrCalendar();
    void seriesNextAiringReservesTheChipOnlyWhileAsking();
    void seriesNextAiringDropsPassedAirings();
    void nextAiringLabelUsesLocalDays();
    void serverRatingsLeadExternalRatings();

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

void HomeRowsAndWatchlistTest::homeRowsReadRecommendationsFromShownMovieLibraries()
{
    m_service->watchlist = false;
    HomeSectionsViewModel model(m_service);
    QVariantMap movies{{QStringLiteral("itemId"), QStringLiteral("lib-movies")},
                       {QStringLiteral("collectionType"), QStringLiteral("movies")}};
    QVariantMap tagged{{QStringLiteral("itemId"), QStringLiteral("lib-films")},
                       {QStringLiteral("libraryKind"), QStringLiteral("movies")}};
    QVariantMap shows{{QStringLiteral("itemId"), QStringLiteral("lib-shows")},
                      {QStringLiteral("collectionType"), QStringLiteral("tvshows")}};
    QVariantMap music{{QStringLiteral("itemId"), QStringLiteral("lib-music")},
                      {QStringLiteral("collectionType"), QStringLiteral("music")}};

    // Before Home's libraries are known, there are none to read.
    model.refresh();
    QVERIFY(m_service->lastMovieLibraryIds.isEmpty());

    // Only the movie libraries Home shows, in its order, each once.
    model.setLibraries({shows, movies, music, tagged, movies});
    model.refresh();
    QCOMPARE(m_service->lastMovieLibraryIds,
             (QStringList{QStringLiteral("lib-movies"), QStringLiteral("lib-films")}));
    // Later refreshes (a favorite change, the setting) reuse them.
    m_service->loadRows(QStringLiteral("connection-1"), {});
    m_service->confirmFavorite(QStringLiteral("movie-1"), true);
    QCOMPARE(m_service->homeRequests, 3);
    QCOMPARE(m_service->lastMovieLibraryIds.size(), 2);

    // Libraries of another connection are not read under this one.
    m_service->connectionId = QStringLiteral("connection-2");
    model.refresh();
    QVERIFY(m_service->lastMovieLibraryIds.isEmpty());

    // A new Home (clear) forgets them.
    m_service->connectionId = QStringLiteral("connection-1");
    model.clear();
    model.refresh();
    QVERIFY(m_service->lastMovieLibraryIds.isEmpty());
}

void HomeRowsAndWatchlistTest::homeRowsReloadRecommendationsOnlyWhenAsked()
{
    m_service->watchlist = false;
    HomeSectionsViewModel model(m_service);

    // A new Home, playback or the setting turned on ask for a new read.
    model.refresh(true);
    QCOMPARE(m_service->homeRequests, 1);
    QVERIFY(m_service->lastReloadRecommendations);
    m_service->loadRows(QStringLiteral("connection-1"), {});

    // Other refreshes reuse them.
    model.refresh();
    QCOMPARE(m_service->homeRequests, 2);
    QVERIFY(!m_service->lastReloadRecommendations);
    m_service->loadRows(QStringLiteral("connection-1"), {});

    // A favorite change re-reads the Favorites row only.
    m_service->confirmFavorite(QStringLiteral("movie-a"), true);
    QCOMPARE(m_service->homeRequests, 3);
    QVERIFY(!m_service->lastReloadRecommendations);
    m_service->loadRows(QStringLiteral("connection-1"), {});

    // So does a watchlist change.
    model.refresh(true);
    m_service->loadRows(QStringLiteral("connection-1"), {});
    m_service->confirmWatchlist(QStringLiteral("movie-a"), true);
    QCOMPARE(m_service->homeRequests, 5);
    QVERIFY(!m_service->lastReloadRecommendations);
}

void HomeRowsAndWatchlistTest::homeRowsDropRecommendationRowsAtOnce()
{
    m_service->watchlist = false;
    HomeSectionsViewModel model(m_service);
    model.refresh();
    m_service->loadRows(
        QStringLiteral("connection-1"),
        {row(QStringLiteral("bloom:favorites"), QStringLiteral("Favorites"),
             {card(QStringLiteral("movie-a"), QStringLiteral("Movie"))}),
         row(QStringLiteral("bloom:recommendations:HasLikedActor:al pacino"),
             QStringLiteral("Starring Al Pacino"),
             {card(QStringLiteral("movie-b"), QStringLiteral("Movie"))}),
         row(QStringLiteral("bloom:recommendations:HasLikedDirector:michael mann"),
             QStringLiteral("Directed by Michael Mann"),
             {card(QStringLiteral("movie-c"), QStringLiteral("Movie"))})});
    QCOMPARE(model.rowCount(), 3);

    // The setting turned off: the rows go before any read answers, and a
    // failed refresh cannot bring them back. Other rows keep their place.
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.removeRecommendationRows();
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("bloom:favorites")});
    QCOMPARE(removed.count(), 2);
    QCOMPARE(reset.count(), 0);
    model.refresh();
    m_service->failRows(QStringLiteral("connection-1"), QStringLiteral("Not Found"));
    QCOMPARE(sectionIds(model), QStringList{QStringLiteral("bloom:favorites")});

    // Nothing to drop: nothing changes.
    model.removeRecommendationRows();
    QCOMPARE(removed.count(), 2);
}

void HomeRowsAndWatchlistTest::homeRowsRestoreFocusToNearestRow()
{
    m_service->watchlist = false;
    HomeSectionsViewModel model(m_service);
    QCOMPARE(model.restoreRowIndex(QStringLiteral("bloom:favorites"), 0), -1);

    model.refresh();
    m_service->loadRows(
        QStringLiteral("connection-1"),
        {row(QStringLiteral("bloom:favorites"), QStringLiteral("Favorites"),
             {card(QStringLiteral("movie-a"), QStringLiteral("Movie"))}),
         row(QStringLiteral("bloom:recommendations:a"), QStringLiteral("A"),
             {card(QStringLiteral("movie-b"), QStringLiteral("Movie"))}),
         row(QStringLiteral("bloom:recommendations:b"), QStringLiteral("B"),
             {card(QStringLiteral("movie-c"), QStringLiteral("Movie"))})});
    // The same row wherever it moved.
    QCOMPARE(model.restoreRowIndex(QStringLiteral("bloom:recommendations:b"), 0), 2);
    // A removed row: the one now in its place, or the last row above it.
    QCOMPARE(model.restoreRowIndex(QStringLiteral("bloom:recommendations:gone"), 1), 1);
    QCOMPARE(model.restoreRowIndex(QStringLiteral("bloom:recommendations:gone"), 7), 2);
    QCOMPARE(model.restoreRowIndex(QString(), -3), 0);
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

void HomeRowsAndWatchlistTest::homeRowsRefreshAfterNotInterestedChange()
{
    HomeSectionsViewModel model(m_service);
    model.refresh();
    m_service->loadRows(QStringLiteral("connection-1"), {});
    QCOMPARE(m_service->homeRequests, 1);
    // Recommendation rows leave the title out once it is marked.
    m_service->confirmNotInterested(QStringLiteral("movie:a"), true);
    QCOMPARE(m_service->homeRequests, 2);
    m_service->loadRows(QStringLiteral("connection-1"), {});
    m_service->refuseNotInterested(QStringLiteral("movie:a"), false);
    QCOMPARE(m_service->homeRequests, 2);
}

void HomeRowsAndWatchlistTest::movieNotInterestedWaitsForCapabilityAndRollsBack()
{
    MovieDetailsViewModel vm;
    QSignalSpy supported(&vm, &MovieDetailsViewModel::notInterestedSupportedChanged);
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    // The page asks for the profile's capability and stays hidden until then.
    QCOMPARE(m_service->notInterestedCapabilityReads, 1);
    QVERIFY(!vm.notInterestedSupported());
    QVariantMap data = detail(QStringLiteral("movie-1"), QStringLiteral("Movie"), false);
    data.insert(QStringLiteral("notInterested"), false);
    m_service->loadMovie(QStringLiteral("movie-1"), data);
    vm.toggleNotInterested();
    QVERIFY(!vm.notInterested());
    QVERIFY(m_service->notInterestedWrites.isEmpty());

    const int before = static_cast<int>(supported.count());
    m_service->allowNotInterested();
    QCOMPARE(supported.count(), before + 1);
    QVERIFY(vm.notInterestedSupported());

    QSignalSpy failed(&vm, &MovieDetailsViewModel::notInterestedUpdateFailed);
    vm.toggleNotInterested();
    QVERIFY(vm.notInterested());
    QCOMPARE(m_service->notInterestedWrites.size(), 1);
    QCOMPARE(m_service->notInterestedWrites.first().itemId, QStringLiteral("movie-1"));
    QCOMPARE(m_service->notInterestedWrites.first().inWatchlist, true);
    // In flight: a second press is ignored and older details do not undo it.
    vm.toggleNotInterested();
    QCOMPARE(m_service->notInterestedWrites.size(), 1);
    m_service->loadMovie(QStringLiteral("movie-1"), data);
    QVERIFY(vm.notInterested());

    m_service->refuseNotInterested(QStringLiteral("movie-1"), true);
    QVERIFY(!vm.notInterested());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(0).toString().isEmpty());

    vm.toggleNotInterested();
    m_service->confirmNotInterested(QStringLiteral("movie-1"), true);
    QVERIFY(vm.notInterested());
    QCOMPARE(failed.count(), 1);

    // The confirmed mark reaches the details cache for the next visit.
    vm.clear();
    QVERIFY(!vm.notInterested());
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    QVERIFY(vm.notInterested());
}

void HomeRowsAndWatchlistTest::movieStarRatingWaitsForCapabilityAndRollsBack()
{
    MovieDetailsViewModel vm;
    UserRatingState *rating = vm.userRating();
    QVERIFY(rating);
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    // The page asks for the capability; no control until it answers.
    QCOMPARE(m_service->ratingCapabilityReads, 1);
    QVERIFY(!rating->supported());
    QVariantMap data = detail(QStringLiteral("movie-1"), QStringLiteral("Movie"), false);
    data.insert(QStringLiteral("userRating"), 3);
    m_service->loadMovie(QStringLiteral("movie-1"), data);
    rating->rate(4);
    QVERIFY(m_service->ratingWrites.isEmpty());

    // The details' rating shows once the capability allows stars.
    QSignalSpy scale(rating, &UserRatingState::scaleChanged);
    m_service->allowRatings(QStringLiteral("stars"));
    QCOMPARE(scale.count(), 1);
    QVERIFY(rating->supported());
    QCOMPARE(rating->scale(), QStringLiteral("stars"));
    QCOMPARE(rating->value(), 3);

    // Optimistic, and ignored while in flight; older details do not undo it.
    QSignalSpy failed(rating, &UserRatingState::updateFailed);
    rating->rate(5);
    QCOMPARE(rating->value(), 5);
    QVERIFY(rating->pending());
    QCOMPARE(m_service->ratingWrites.size(), 1);
    QCOMPARE(m_service->ratingWrites.first(), std::make_pair(QStringLiteral("movie-1"), 5));
    rating->rate(2);
    QCOMPARE(m_service->ratingWrites.size(), 1);
    m_service->loadMovie(QStringLiteral("movie-1"), data);
    QCOMPARE(rating->value(), 5);

    // A refusal restores the earlier rating.
    m_service->refuseRating(QStringLiteral("movie-1"), 5);
    QCOMPARE(rating->value(), 3);
    QVERIFY(!rating->pending());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(0).toString().isEmpty());

    // Out-of-scale values are not sent; the current value clears it.
    rating->rate(-1);
    rating->rate(6);
    QVERIFY(m_service->ratingWrites.size() == 1);
    rating->rate(3);
    QCOMPARE(rating->value(), 0);
    QCOMPARE(m_service->ratingWrites.last(), std::make_pair(QStringLiteral("movie-1"), 0));
    m_service->confirmRating(QStringLiteral("movie-1"), 0);
    QCOMPARE(rating->value(), 0);
    rating->rate(4);
    m_service->confirmRating(QStringLiteral("movie-1"), 4);
    QCOMPARE(rating->value(), 4);
    QCOMPARE(failed.count(), 1);
    // Another item's answer changes nothing.
    m_service->confirmRating(QStringLiteral("movie-2"), 1);
    QCOMPARE(rating->value(), 4);

    // The confirmed rating reaches the details cache for the next visit.
    vm.clear();
    QCOMPARE(rating->value(), 0);
    vm.loadMovieDetails(QStringLiteral("movie-1"));
    QCOMPARE(rating->value(), 4);
}

void HomeRowsAndWatchlistTest::seriesLikeRatingIsOptimisticAndRollsBack()
{
    m_service->connectionId.clear();
    m_service->ratingScale = QStringLiteral("likes");
    SeriesDetailsViewModel vm;
    UserRatingState *rating = vm.userRating();
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    QVERIFY(rating->supported());
    QCOMPARE(rating->scale(), QStringLiteral("likes"));
    QVariantMap data = detail(QStringLiteral("series-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("userLikes"), -1);
    m_service->loadSeries(QStringLiteral("series-1"), data);
    QCOMPARE(rating->value(), -1);

    QSignalSpy failed(rating, &UserRatingState::updateFailed);
    rating->rate(1);
    QCOMPARE(rating->value(), 1);
    QCOMPARE(m_service->ratingWrites.size(), 1);
    QCOMPARE(m_service->ratingWrites.first(), std::make_pair(QStringLiteral("series-1"), 1));
    m_service->refuseRating(QStringLiteral("series-1"), 1);
    QCOMPARE(rating->value(), -1);
    QCOMPARE(failed.count(), 1);

    // Disliking a disliked series clears it; stars are not likes.
    rating->rate(3);
    QCOMPARE(m_service->ratingWrites.size(), 1);
    rating->rate(-1);
    QCOMPARE(rating->value(), 0);
    QCOMPARE(m_service->ratingWrites.last(), std::make_pair(QStringLiteral("series-1"), 0));
    m_service->confirmRating(QStringLiteral("series-1"), 0);
    QCOMPARE(rating->value(), 0);
    QVERIFY(!rating->pending());
}

void HomeRowsAndWatchlistTest::seriesNotInterestedToggleIsOptimisticAndRollsBack()
{
    m_service->connectionId.clear();
    m_service->notInterestedAllowed = true;
    SeriesDetailsViewModel vm;
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    QCOMPARE(m_service->notInterestedCapabilityReads, 1);
    QVERIFY(vm.notInterestedSupported());
    QVariantMap data = detail(QStringLiteral("series-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("notInterested"), true);
    m_service->loadSeries(QStringLiteral("series-1"), data);
    QVERIFY(vm.notInterested());

    QSignalSpy failed(&vm, &SeriesDetailsViewModel::notInterestedUpdateFailed);
    vm.toggleNotInterested();
    QVERIFY(!vm.notInterested());
    QCOMPARE(m_service->notInterestedWrites.size(), 1);
    QCOMPARE(m_service->notInterestedWrites.first().inWatchlist, false);
    m_service->refuseNotInterested(QStringLiteral("series-1"), false);
    QVERIFY(vm.notInterested());
    QCOMPARE(failed.count(), 1);

    vm.toggleNotInterested();
    m_service->confirmNotInterested(QStringLiteral("series-1"), false);
    QVERIFY(!vm.notInterested());
}

void HomeRowsAndWatchlistTest::seriesNextAiringComesFromDetailsOrCalendar()
{
    m_service->connectionId.clear();
    SeriesDetailsViewModel vm;
    QSignalSpy changed(&vm, &SeriesDetailsViewModel::nextAiringChanged);

    // A running series without an airing in its details asks the calendar once.
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    QVariantMap data = detail(QStringLiteral("series-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("status"), QStringLiteral("returning"));
    m_service->loadSeries(QStringLiteral("series-1"), data);
    m_service->loadSeries(QStringLiteral("series-1"), data);
    QCOMPARE(m_service->airingRequests, QStringList{QStringLiteral("series-1")});
    QVERIFY(vm.nextAiringLabel().isEmpty());
    // The hero keeps the chip's slot while the answer is out.
    QVERIFY(vm.expectsNextAiring());

    const QVariantMap airing{{QStringLiteral("seasonNumber"), 2},
                             {QStringLiteral("episodeNumber"), 5},
                             {QStringLiteral("airDate"), QStringLiteral("2099-11-05")}};
    // Another series' answer is not ours.
    m_service->loadAiring(QStringLiteral("series-2"), airing);
    QVERIFY(vm.nextAiring().isEmpty());
    m_service->loadAiring(QStringLiteral("series-1"), airing);
    QCOMPARE(vm.nextAiring(), airing);
    QVERIFY(vm.nextAiringLabel().startsWith(QStringLiteral("S2 E5 airs ")));
    QVERIFY(vm.expectsNextAiring());

    // An ended series asks nothing.
    vm.loadSeriesDetails(QStringLiteral("series-3"));
    QVERIFY(vm.nextAiring().isEmpty());
    QVariantMap ended = detail(QStringLiteral("series-3"), QStringLiteral("Series"), false);
    ended.insert(QStringLiteral("status"), QStringLiteral("Ended"));
    m_service->loadSeries(QStringLiteral("series-3"), ended);
    QCOMPARE(m_service->airingRequests.size(), 1);
    QVERIFY(!vm.expectsNextAiring());

    // Details that name the airing need no calendar read.
    vm.loadSeriesDetails(QStringLiteral("series-4"));
    QVariantMap announced = detail(QStringLiteral("series-4"), QStringLiteral("Series"), false);
    announced.insert(QStringLiteral("nextAiring"), airing);
    m_service->loadSeries(QStringLiteral("series-4"), announced);
    QCOMPARE(m_service->airingRequests.size(), 1);
    QCOMPARE(vm.nextAiring(), airing);
    QVERIFY(changed.count() > 0);
}

void HomeRowsAndWatchlistTest::seriesNextAiringReservesTheChipOnlyWhileAsking()
{
    m_service->connectionId.clear();
    QVariantMap data = detail(QStringLiteral("series-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("status"), QStringLiteral("Continuing"));

    // A provider without a calendar (Jellyfin) neither asks nor reserves.
    m_service->airingCalendar = false;
    {
        SeriesDetailsViewModel vm;
        vm.loadSeriesDetails(QStringLiteral("series-1"));
        m_service->loadSeries(QStringLiteral("series-1"), data);
        QVERIFY(m_service->airingRequests.isEmpty());
        QVERIFY(!vm.expectsNextAiring());
    }

    // A calendar that answers at once with nothing releases the slot.
    m_service->airingCalendar = true;
    m_service->answerAiringAtOnce = true;
    {
        SeriesDetailsViewModel vm;
        vm.loadSeriesDetails(QStringLiteral("series-1"));
        m_service->loadSeries(QStringLiteral("series-1"), data);
        QCOMPARE(m_service->airingRequests.size(), 1);
        QVERIFY(!vm.expectsNextAiring());
        QVERIFY(vm.nextAiringLabel().isEmpty());
    }

    // An empty (or failed) answer that arrives later releases it too.
    m_service->answerAiringAtOnce = false;
    SeriesDetailsViewModel vm;
    QSignalSpy changed(&vm, &SeriesDetailsViewModel::nextAiringChanged);
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    m_service->loadSeries(QStringLiteral("series-1"), data);
    QVERIFY(vm.expectsNextAiring());
    changed.clear();
    m_service->loadAiring(QStringLiteral("series-1"), {});
    QVERIFY(!vm.expectsNextAiring());
    QCOMPARE(changed.count(), 1);
}

void HomeRowsAndWatchlistTest::seriesNextAiringDropsPassedAirings()
{
    m_service->connectionId.clear();
    SeriesDetailsViewModel vm;
    vm.loadSeriesDetails(QStringLiteral("series-1"));
    // A card's airing from a day already over is not shown; the calendar
    // is asked instead.
    QVariantMap data = detail(QStringLiteral("series-1"), QStringLiteral("Series"), false);
    data.insert(QStringLiteral("nextAiring"),
                QVariantMap{{QStringLiteral("seasonNumber"), 1},
                            {QStringLiteral("episodeNumber"), 2},
                            {QStringLiteral("airDate"),
                             QDate::currentDate().addDays(-1).toString(Qt::ISODate)}});
    m_service->loadSeries(QStringLiteral("series-1"), data);
    QVERIFY(vm.nextAiring().isEmpty());
    QVERIFY(vm.nextAiringLabel().isEmpty());
    QCOMPARE(m_service->airingRequests, QStringList{QStringLiteral("series-1")});

    // An airing whose instant has passed is dropped as well.
    const QVariantMap aired{{QStringLiteral("seasonNumber"), 1},
                            {QStringLiteral("episodeNumber"), 3},
                            {QStringLiteral("airDate"), QDate::currentDate().toString(Qt::ISODate)},
                            {QStringLiteral("airAt"),
                             QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODate)}};
    m_service->loadAiring(QStringLiteral("series-1"), aired);
    QVERIFY(vm.nextAiring().isEmpty());
    QVERIFY(!vm.expectsNextAiring());
}

void HomeRowsAndWatchlistTest::nextAiringLabelUsesLocalDays()
{
    const QDateTime now(QDate(2026, 11, 3), QTime(12, 0));
    QVERIFY(SeriesDetailsViewModel::nextAiringLabel({}, now).isEmpty());
    QVariantMap airing{{QStringLiteral("seasonNumber"), 2},
                       {QStringLiteral("episodeNumber"), 5},
                       {QStringLiteral("airDate"), QStringLiteral("2026-11-06")}};
    QCOMPARE(SeriesDetailsViewModel::nextAiringLabel(airing, now),
             QStringLiteral("S2 E5 airs %1")
                 .arg(QLocale().toString(QDate(2026, 11, 6), QStringLiteral("ddd, MMM d"))));
    airing.insert(QStringLiteral("airDate"), QStringLiteral("2026-11-03"));
    QCOMPARE(SeriesDetailsViewModel::nextAiringLabel(airing, now), QStringLiteral("S2 E5 airs today"));
    airing.insert(QStringLiteral("airDate"), QStringLiteral("2026-11-04"));
    QCOMPARE(SeriesDetailsViewModel::nextAiringLabel(airing, now),
             QStringLiteral("S2 E5 airs tomorrow"));
    // An instant is shown on the viewer's clock.
    const QDateTime instant(QDate(2026, 11, 3), QTime(21, 30));
    airing.insert(QStringLiteral("airAt"), instant.toUTC().toString(Qt::ISODate));
    QCOMPARE(SeriesDetailsViewModel::nextAiringLabel(airing, now),
             QStringLiteral("S2 E5 airs today at %1")
                 .arg(QLocale().toString(QTime(21, 30), QLocale::ShortFormat)));
    airing.remove(QStringLiteral("airAt"));
    airing.insert(QStringLiteral("episodeNumber"), -1);
    QVERIFY(SeriesDetailsViewModel::nextAiringLabel(airing, now).startsWith(QStringLiteral("Season 2 airs")));
}

void HomeRowsAndWatchlistTest::serverRatingsLeadExternalRatings()
{
    const QVariantList server{
        QVariantMap{{QStringLiteral("source"), QStringLiteral("rottenTomatoesCritic")},
                    {QStringLiteral("score"), 83.0},
                    {QStringLiteral("display"), QStringLiteral("83%")}},
        QVariantMap{{QStringLiteral("source"), QStringLiteral("imdb")},
                    {QStringLiteral("score"), 83.0},
                    {QStringLiteral("display"), QStringLiteral("8.3")}}};
    const QVariantMap mdbList{{QStringLiteral("ratings"), QVariantList{
        QVariantMap{{QStringLiteral("source"), QStringLiteral("imdb")}, {QStringLiteral("value"), 8.2}},
        QVariantMap{{QStringLiteral("source"), QStringLiteral("tomatoes")}, {QStringLiteral("value"), 80}},
        QVariantMap{{QStringLiteral("source"), QStringLiteral("metacritic")}, {QStringLiteral("value"), 76}}}}};
    const QVariantList merged = ExternalRatingsHelper::withServerRatings(server, mdbList)
                                    .value(QStringLiteral("ratings")).toList();
    QCOMPARE(merged.size(), 3);
    QCOMPARE(merged.at(0).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("rottenTomatoesCritic"));
    QCOMPARE(merged.at(1).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("8.3"));
    QCOMPARE(merged.at(2).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("metacritic"));
    // Server ratings are marked; every rating carries its source key.
    QVERIFY(merged.at(0).toMap().value(QStringLiteral("fromServer")).toBool());
    QVERIFY(!merged.at(2).toMap().value(QStringLiteral("fromServer")).toBool());
    QCOMPARE(merged.at(0).toMap().value(QStringLiteral("sourceKey")).toString(), QStringLiteral("tomatoes"));
    QCOMPARE(merged.at(2).toMap().value(QStringLiteral("sourceKey")).toString(), QStringLiteral("metacritic"));
    const QVariantList externalOnly = ExternalRatingsHelper::withServerRatings({}, mdbList)
                                          .value(QStringLiteral("ratings")).toList();
    QCOMPARE(externalOnly.size(), 3);
    QCOMPARE(externalOnly.at(1).toMap().value(QStringLiteral("sourceKey")).toString(), QStringLiteral("tomatoes"));

    // One alias table for every spelling of a source.
    using ExternalRatingsHelper::ratingSourceKey;
    QCOMPARE(ratingSourceKey(QStringLiteral("rottenTomatoesAudience")), QStringLiteral("audience"));
    QCOMPARE(ratingSourceKey(QStringLiteral("popcorn")), QStringLiteral("audience"));
    QCOMPARE(ratingSourceKey(QStringLiteral("Rotten Tomatoes")), QStringLiteral("tomatoes"));
    QCOMPARE(ratingSourceKey(QStringLiteral("myanimelist")), QStringLiteral("mal"));
    QCOMPARE(ratingSourceKey(QStringLiteral("MAL")), QStringLiteral("mal"));
    QCOMPARE(ratingSourceKey(QStringLiteral("metacriticuser")), QStringLiteral("metacriticuser"));
    QCOMPARE(ratingSourceKey(QStringLiteral("metacritic")), QStringLiteral("metacritic"));
    QCOMPARE(ratingSourceKey(QStringLiteral("AniList")), QStringLiteral("anilist"));
    QCOMPARE(ratingSourceKey(QStringLiteral("plugin_score")), QStringLiteral("pluginscore"));
    // A server already showing MyAnimeList hides MDBList's copy.
    const QVariantList malMerged = ExternalRatingsHelper::withServerRatings(
        {QVariantMap{{QStringLiteral("source"), QStringLiteral("mal")},
                     {QStringLiteral("score"), 81.0},
                     {QStringLiteral("display"), QStringLiteral("8.1")}}},
        QVariantMap{{QStringLiteral("ratings"), QVariantList{
            QVariantMap{{QStringLiteral("source"), QStringLiteral("myanimelist")},
                        {QStringLiteral("value"), 8.0}}}}}).value(QStringLiteral("ratings")).toList();
    QCOMPARE(malMerged.size(), 1);

    // The detail pages show the server's ratings without an MDBList key,
    // a score of 0 included.
    QVariantList withZero = server;
    withZero.append(QVariantMap{{QStringLiteral("source"), QStringLiteral("rottenTomatoesAudience")},
                                {QStringLiteral("score"), 0.0},
                                {QStringLiteral("display"), QStringLiteral("0%")}});
    MovieDetailsViewModel vm;
    vm.loadMovieDetails(QStringLiteral("movie-9"));
    QVariantMap data = detail(QStringLiteral("movie-9"), QStringLiteral("Movie"), false);
    data.insert(QStringLiteral("serverRatings"), withZero);
    m_service->loadMovie(QStringLiteral("movie-9"), data);
    const QVariantList movieRatings = vm.mdbListRatings().value(QStringLiteral("ratings")).toList();
    QCOMPARE(movieRatings.size(), 3);
    QCOMPARE(movieRatings.at(2).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("0%"));
    QCOMPARE(movieRatings.at(2).toMap().value(QStringLiteral("sourceKey")).toString(), QStringLiteral("audience"));

    m_service->connectionId.clear();
    SeriesDetailsViewModel series;
    series.loadSeriesDetails(QStringLiteral("series-9"));
    QVariantMap seriesData = detail(QStringLiteral("series-9"), QStringLiteral("Series"), false);
    seriesData.insert(QStringLiteral("serverRatings"), withZero);
    m_service->loadSeries(QStringLiteral("series-9"), seriesData);
    const QVariantList seriesRatings = series.mdbListRatings().value(QStringLiteral("ratings")).toList();
    QCOMPARE(seriesRatings.size(), 3);
    QCOMPARE(seriesRatings.at(2).toMap().value(QStringLiteral("display")).toString(), QStringLiteral("0%"));
}

QTEST_MAIN(HomeRowsAndWatchlistTest)
#include "HomeRowsAndWatchlistTest.moc"
