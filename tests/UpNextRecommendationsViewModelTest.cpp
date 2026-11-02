#include <QtTest/QtTest>

#include "../src/viewmodels/UpNextRecommendationsViewModel.h"
#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"

class UpNextTestLibraryService : public LibraryService
{
    Q_OBJECT

public:
    explicit UpNextTestLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    void getSimilarItems(const QString &itemId, int limit = 12) override
    {
        Q_UNUSED(limit)
        similarRequests.append(itemId);
        if (!deferSimilar) {
            emit canonicalSimilarItemsLoadedForConnection(
                activeConnectionId, itemId, similarItems);
        }
    }

    void getSeriesDetails(const QString &seriesId) override
    {
        seriesDetailsRequests.append(seriesId);
        if (deferSeriesDetails) {
            return;
        }
        if (emitNotModifiedForSeriesDetails) {
            emit canonicalSeriesDetailsNotModified(activeConnectionId, seriesId);
            return;
        }
        if (emitPathErrorForSeriesDetails) {
            emit canonicalSeriesDetailsFailed(
                activeConnectionId, seriesId, QStringLiteral("timeout"));
            return;
        }
        emit canonicalSeriesDetailsLoaded(activeConnectionId, seriesId, seriesDetails);
    }

    void getItem(const QString &itemId, const QString &requestContext) override
    {
        itemRequests.append(itemId);
        itemRequestContexts.append(requestContext);
        if (!deferItem) {
            emit canonicalItemLoaded(itemId, fallbackItem, requestContext);
        }
    }

    void getBecauseWatchedItems(const QString &itemId, int limit = 12) override
    {
        becauseWatchedRequests.append(itemId);
        becauseWatchedLimits.append(limit);
        if (!deferBecauseWatched) {
            emit canonicalBecauseWatchedLoadedForConnection(
                activeConnectionId, itemId, becauseWatchedItems);
        }
    }

    QString userRatingScale() const override { return ratingScale; }

    void clearItemCacheValidation(const QString &itemId) override
    {
        clearedItemCacheIds.append(itemId);
    }

    void getItems(const LibraryItemQuery &query) override
    {
        itemsRequests.append(query.parentId);
    }

    QVariantList similarItems;
    QVariantList becauseWatchedItems;
    QStringList becauseWatchedRequests;
    QList<int> becauseWatchedLimits;
    bool deferBecauseWatched = false;
    QString ratingScale;
    QVariantMap seriesDetails;
    QVariantMap fallbackItem;
    QStringList similarRequests;
    QStringList seriesDetailsRequests;
    QStringList itemRequests;
    QStringList itemRequestContexts;
    QStringList clearedItemCacheIds;
    QStringList itemsRequests;
    bool emitNotModifiedForSeriesDetails = false;
    bool emitPathErrorForSeriesDetails = false;
    bool deferSimilar = false;
    bool deferSeriesDetails = false;
    bool deferItem = false;
    QString activeConnectionId = QStringLiteral("connection-1");

    QString getActiveConnectionId() const override
    {
        return activeConnectionId;
    }
};

class UpNextRecommendationsViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void libraryResultsAppearBeforeRequestResults();
    void nonSeriesResultsAreFiltered();
    void duplicateTitlesCollapseToJellyfinItem();
    void resultCountIsCapped();
    void unavailableProvidersLeaveEmptyNotLoading();
    void seriesDetailsNotModifiedUsesFallbackItem();
    void seriesDetailsPathErrorClearsLoading();
    void staleConnectionSignalsAreIgnored();
    void staleFallbackTerminalSignalIsIgnored();
    void upcomingEpisodesStartAtNextEpisode();
    void upcomingEpisodesMissingStartIsEmpty();
    void upcomingEpisodesIgnoreOtherRequests();
    void becauseWatchedKeepsMoviesAndSeriesOnly();
    void movieLoadReadsBecauseWatchedDetailsAndRating();
    void becauseWatchedFailureIsSilentAndStaleAnswersIgnored();
    void seriesLoadAfterMovieDropsTheMovie();
};

void UpNextRecommendationsViewModelTest::cleanup()
{
    ServiceLocator::clear();
}

void UpNextRecommendationsViewModelTest::libraryResultsAppearBeforeRequestResults()
{
    const QVariantList library{
        QVariantMap{{"itemId", "library-1"}, {"mediaType", "Series"}, {"name", "Local First"}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "request:tv:2"}, {"source", "request"}, {"requestMediaType", "tv"}, {"mediaType", "Series"}, {"name", "Discovery Second"}}
    };

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value("itemId").toString(), QStringLiteral("library-1"));
    QCOMPARE(merged.at(1).toMap().value("itemId").toString(), QStringLiteral("request:tv:2"));
}

void UpNextRecommendationsViewModelTest::nonSeriesResultsAreFiltered()
{
    const QVariantList library{
        QVariantMap{{"itemId", "movie-1"}, {"mediaType", "Movie"}, {"name", "A Movie"}},
        QVariantMap{{"itemId", "series-1"}, {"mediaType", "Series"}, {"name", "A Series"}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "request:movie:1"}, {"source", "request"}, {"requestMediaType", "movie"}, {"mediaType", "Movie"}, {"name", "A Seerr Movie"}},
        QVariantMap{{"itemId", "request:tv:1"}, {"source", "request"}, {"requestMediaType", "tv"}, {"mediaType", "Series"}, {"name", "A Seerr Series"}}
    };

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value("itemId").toString(), QStringLiteral("series-1"));
    QCOMPARE(merged.at(1).toMap().value("itemId").toString(), QStringLiteral("request:tv:1"));
}

void UpNextRecommendationsViewModelTest::duplicateTitlesCollapseToJellyfinItem()
{
    const QVariantList library{
        QVariantMap{{"itemId", "library-1"}, {"mediaType", "Series"}, {"name", "Same Show"}, {"productionYear", 2020}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "request:tv:10"}, {"source", "request"}, {"requestMediaType", "tv"}, {"mediaType", "Series"}, {"name", "Same Show!"}, {"productionYear", 2020}}
    };

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, seerr, 6);

    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged.at(0).toMap().value("itemId").toString(), QStringLiteral("library-1"));
}

void UpNextRecommendationsViewModelTest::resultCountIsCapped()
{
    QVariantList library;
    for (int i = 0; i < 8; ++i) {
        library.append(QVariantMap{
            {"itemId", QStringLiteral("library-%1").arg(i)},
            {"mediaType", "Series"},
            {"name", QStringLiteral("Series %1").arg(i)}
        });
    }

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, {}, 6);

    QCOMPARE(merged.size(), 6);
    QCOMPARE(merged.at(5).toMap().value("itemId").toString(), QStringLiteral("library-5"));
}

void UpNextRecommendationsViewModelTest::unavailableProvidersLeaveEmptyNotLoading()
{
    ServiceLocator::clear();
    UpNextRecommendationsViewModel vm;

    vm.loadForSeries(QStringLiteral("series-1"), 6);

    QVERIFY(vm.items().isEmpty());
    QVERIFY(!vm.loading());
}

void UpNextRecommendationsViewModelTest::seriesDetailsNotModifiedUsesFallbackItem()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->emitNotModifiedForSeriesDetails = true;
    libraryService->fallbackItem = QVariantMap{
        {"connectionId", "connection-1"},
        {"itemId", "series-1"},
        {"mediaType", "Series"},
        {"providerIds", QVariantMap{{"Tmdb", "1234"}}}
    };
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForSeries(QStringLiteral("series-1"), 6);

    QCOMPARE(libraryService->clearedItemCacheIds, QStringList{QStringLiteral("series-1")});
    QCOMPARE(libraryService->itemRequests, QStringList{QStringLiteral("series-1")});
    QVERIFY(libraryService->itemRequestContexts.first().startsWith(QStringLiteral("UpNextRecommendations:")));
    QVERIFY(!vm.loading());
}

void UpNextRecommendationsViewModelTest::seriesDetailsPathErrorClearsLoading()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->emitPathErrorForSeriesDetails = true;
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForSeries(QStringLiteral("series-1"), 6);

    QVERIFY(!vm.loading());
}

void UpNextRecommendationsViewModelTest::staleConnectionSignalsAreIgnored()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->deferSimilar = true;
    libraryService->deferSeriesDetails = true;
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForSeries(QStringLiteral("series-1"), 6);

    const QVariantList recommendations{
        QVariantMap{{"connectionId", "connection-1"},
                    {"itemId", "series-2"},
                    {"mediaType", "Series"},
                    {"name", "Next Show"}}
    };
    emit libraryService->canonicalSimilarItemsLoadedForConnection(
        QStringLiteral("connection-2"), QStringLiteral("series-1"), recommendations);
    emit libraryService->canonicalSeriesDetailsFailed(
        QStringLiteral("connection-2"), QStringLiteral("series-1"), QStringLiteral("stale"));

    QVERIFY(vm.loading());
    QVERIFY(vm.items().isEmpty());

    emit libraryService->canonicalSimilarItemsLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("series-1"), recommendations);
    emit libraryService->canonicalSeriesDetailsFailed(
        QStringLiteral("connection-1"), QStringLiteral("series-1"), QStringLiteral("done"));

    QVERIFY(!vm.loading());
    QCOMPARE(vm.items().size(), 1);
    QCOMPARE(vm.items().first().toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("series-2"));
}

void UpNextRecommendationsViewModelTest::staleFallbackTerminalSignalIsIgnored()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->emitNotModifiedForSeriesDetails = true;
    libraryService->deferItem = true;
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForSeries(QStringLiteral("series-1"), 6);
    const QString staleContext = libraryService->itemRequestContexts.last();

    libraryService->activeConnectionId = QStringLiteral("connection-2");
    vm.clear();
    vm.loadForSeries(QStringLiteral("series-1"), 6);
    const QString currentContext = libraryService->itemRequestContexts.last();
    QVERIFY(staleContext != currentContext);

    emit libraryService->itemNotModified(QStringLiteral("series-1"), staleContext);
    QVERIFY(vm.loading());

    emit libraryService->itemFailed(QStringLiteral("series-1"),
                                    QStringLiteral("done"), currentContext);
    QVERIFY(!vm.loading());
}

namespace {
QVariantMap episode(const QString &id, int season, int number,
                    const QString &locationType = QStringLiteral("FileSystem"))
{
    return {{"itemId", id},
            {"mediaType", "Episode"},
            {"parentIndexNumber", season},
            {"indexNumber", number},
            {"locationType", locationType}};
}

QStringList itemIds(const QVariantList &items)
{
    QStringList ids;
    for (const QVariant &item : items) {
        ids.append(item.toMap().value(QStringLiteral("itemId")).toString());
    }
    return ids;
}
} // namespace

void UpNextRecommendationsViewModelTest::upcomingEpisodesStartAtNextEpisode()
{
    const QVariantList season{
        episode(QStringLiteral("e9"), 1, 9),
        episode(QStringLiteral("e6"), 1, 6),
        episode(QStringLiteral("e7"), 1, 7),
        episode(QStringLiteral("e8-missing"), 1, 8, QStringLiteral("Virtual")),
        episode(QStringLiteral("special"), 0, 1),
        episode(QStringLiteral("e10"), 1, 10),
        episode(QStringLiteral("e11"), 1, 11),
    };

    QCOMPARE(itemIds(UpNextRecommendationsViewModel::selectUpcomingEpisodes(
                 season, QStringLiteral("e7"), 3)),
             (QStringList{"e7", "e9", "e10"}));
}

void UpNextRecommendationsViewModelTest::upcomingEpisodesMissingStartIsEmpty()
{
    const QVariantList season{episode(QStringLiteral("e1"), 1, 1)};
    QVERIFY(UpNextRecommendationsViewModel::selectUpcomingEpisodes(
                season, QStringLiteral("other"), 8).isEmpty());
}

void UpNextRecommendationsViewModelTest::upcomingEpisodesIgnoreOtherRequests()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadUpcomingEpisodes(QStringLiteral("season-1"), QStringLiteral("e1"), 8);
    QCOMPARE(libraryService->itemsRequests, QStringList{QStringLiteral("season-1")});

    const QVariantList season{episode(QStringLiteral("e1"), 1, 1),
                              episode(QStringLiteral("e2"), 1, 2)};
    emit libraryService->canonicalItemsLoadedForConnection(
        QStringLiteral("connection-2"), QStringLiteral("season-1"), QString(), season, 2);
    emit libraryService->canonicalItemsLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("season-2"), QString(), season, 2);
    QVERIFY(vm.upcomingEpisodes().isEmpty());

    emit libraryService->canonicalItemsLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("season-1"), QString(), season, 2);
    QCOMPARE(itemIds(vm.upcomingEpisodes()), (QStringList{"e1", "e2"}));

    vm.clearUpcomingEpisodes();
    QVERIFY(vm.upcomingEpisodes().isEmpty());
    emit libraryService->canonicalItemsLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("season-1"), QString(), season, 2);
    QVERIFY(vm.upcomingEpisodes().isEmpty());
}

QTEST_MAIN(UpNextRecommendationsViewModelTest)
void UpNextRecommendationsViewModelTest::becauseWatchedKeepsMoviesAndSeriesOnly()
{
    const QVariantList items{
        QVariantMap{{"itemId", "movie-1"}, {"mediaType", "Movie"}, {"name", "Source"}},
        QVariantMap{{"itemId", "movie-2"}, {"mediaType", "Movie"}, {"name", "Ronin"},
                    {"providerIds", QVariantMap{{"Tmdb", "8195"}}}},
        QVariantMap{{"itemId", "episode-1"}, {"mediaType", "Episode"}, {"name", "Pilot"}},
        QVariantMap{{"itemId", "series-1"}, {"mediaType", "Series"}, {"name", "The Wire"}},
        QVariantMap{{"itemId", "box-1"}, {"mediaType", "BoxSet"}, {"name", "Collection"}},
        QVariantMap{{"itemId", "movie-2"}, {"mediaType", "Movie"}, {"name", "Ronin"},
                    {"providerIds", QVariantMap{{"Tmdb", "8195"}}}},
        QVariantMap{{"itemId", "movie-4"}, {"mediaType", "movie"}, {"name", "Collateral"}}
    };

    QVariantList selected = UpNextRecommendationsViewModel::selectBecauseWatched(
        items, QStringLiteral("movie-1"), 12);
    QStringList ids;
    for (const QVariant &item : std::as_const(selected)) {
        ids.append(item.toMap().value(QStringLiteral("itemId")).toString());
    }
    QCOMPARE(ids, QStringList({QStringLiteral("movie-2"), QStringLiteral("series-1"),
                               QStringLiteral("movie-4")}));

    selected = UpNextRecommendationsViewModel::selectBecauseWatched(items, QStringLiteral("movie-1"), 2);
    QCOMPARE(selected.size(), 2);

    // Only the same title repeats: a movie and a series sharing a TMDB
    // number, remakes sharing a name, and the same id on another connection
    // are different titles.
    const QVariantList distinct{
        QVariantMap{{"connectionId", "c1"}, {"itemId", "movie-10"}, {"mediaType", "Movie"},
                    {"name", "Shogun"}, {"providerIds", QVariantMap{{"Tmdb", "126308"}}}},
        QVariantMap{{"connectionId", "c1"}, {"itemId", "series-10"}, {"mediaType", "Series"},
                    {"name", "Shogun"}, {"providerIds", QVariantMap{{"Tmdb", "126308"}}}},
        QVariantMap{{"connectionId", "c1"}, {"itemId", "movie-11"}, {"mediaType", "Movie"},
                    {"name", "Shogun"}},
        QVariantMap{{"connectionId", "c2"}, {"itemId", "movie-10"}, {"mediaType", "Movie"},
                    {"name", "Shogun"}},
        QVariantMap{{"connectionId", "c1"}, {"itemId", "movie-10"}, {"mediaType", "Movie"},
                    {"name", "Shogun"}}
    };
    selected = UpNextRecommendationsViewModel::selectBecauseWatched(
        distinct, QStringLiteral("movie-1"), 12);
    QCOMPARE(selected.size(), 4);
    QCOMPARE(selected.at(3).toMap().value(QStringLiteral("connectionId")).toString(),
             QStringLiteral("c2"));
}

void UpNextRecommendationsViewModelTest::movieLoadReadsBecauseWatchedDetailsAndRating()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->ratingScale = QStringLiteral("stars");
    libraryService->becauseWatchedItems = {
        QVariantMap{{"connectionId", "connection-1"}, {"itemId", "movie-2"},
                    {"mediaType", "Movie"}, {"name", "Ronin"}},
        QVariantMap{{"connectionId", "connection-1"}, {"itemId", "episode-1"},
                    {"mediaType", "Episode"}, {"name", "Pilot"}}
    };
    libraryService->fallbackItem = QVariantMap{
        {"connectionId", "connection-1"},
        {"itemId", "movie-1"},
        {"mediaType", "Movie"},
        {"name", "Heat"},
        {"userRating", 4}
    };
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    QSignalSpy movieChanged(&vm, &UpNextRecommendationsViewModel::movieChanged);
    vm.loadForMovie(QStringLiteral("movie-1"), 12);

    QCOMPARE(libraryService->becauseWatchedRequests, QStringList{QStringLiteral("movie-1")});
    QCOMPARE(libraryService->becauseWatchedLimits, QList<int>{12});
    // Fresh details, not an unchanged answer, for the hero and the rating.
    QCOMPARE(libraryService->clearedItemCacheIds, QStringList{QStringLiteral("movie-1")});
    QCOMPARE(libraryService->itemRequests, QStringList{QStringLiteral("movie-1")});
    QVERIFY(libraryService->itemRequestContexts.first().startsWith(QStringLiteral("UpNextMovie:")));
    QVERIFY(libraryService->similarRequests.isEmpty());
    QVERIFY(libraryService->seriesDetailsRequests.isEmpty());

    QVERIFY(!vm.loading());
    QCOMPARE(vm.items().size(), 1);
    QCOMPARE(vm.items().first().toMap().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("movie-2"));
    QVERIFY(movieChanged.count() >= 1);
    QCOMPARE(vm.movie().value(QStringLiteral("name")).toString(), QStringLiteral("Heat"));
    UserRatingState *rating = vm.movieRating();
    QCOMPARE(rating->itemId(), QStringLiteral("movie-1"));
    QCOMPARE(rating->connectionId(), QStringLiteral("connection-1"));
    QVERIFY(rating->supported());
    QCOMPARE(rating->value(), 4);

    // Another page's details for the same movie do not replace them.
    emit libraryService->canonicalItemLoaded(
        QStringLiteral("movie-1"),
        QVariantMap{{"connectionId", "connection-1"}, {"itemId", "movie-1"}, {"name", "Other"}},
        QStringLiteral("MovieDetails:movie-1"));
    QCOMPARE(vm.movie().value(QStringLiteral("name")).toString(), QStringLiteral("Heat"));
}

void UpNextRecommendationsViewModelTest::becauseWatchedFailureIsSilentAndStaleAnswersIgnored()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->deferBecauseWatched = true;
    libraryService->deferItem = true;
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForMovie(QStringLiteral("movie-1"));
    QVERIFY(vm.loading());

    const QVariantList recommendations{
        QVariantMap{{"connectionId", "connection-1"}, {"itemId", "movie-2"},
                    {"mediaType", "Movie"}, {"name", "Ronin"}}
    };
    // Another connection, another movie, and the movie page's own similar
    // items for this movie are not this screen's answer.
    emit libraryService->canonicalBecauseWatchedLoadedForConnection(
        QStringLiteral("connection-2"), QStringLiteral("movie-1"), recommendations);
    emit libraryService->canonicalBecauseWatchedLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("movie-9"), recommendations);
    emit libraryService->canonicalSimilarItemsLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("movie-1"), recommendations);
    QVERIFY(vm.loading());
    QVERIFY(vm.items().isEmpty());

    emit libraryService->canonicalBecauseWatchedFailedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("movie-1"), QStringLiteral("Not Found"));
    QVERIFY(!vm.loading());
    QVERIFY(vm.items().isEmpty());
    // A late answer after the failure changes nothing.
    emit libraryService->canonicalBecauseWatchedLoadedForConnection(
        QStringLiteral("connection-1"), QStringLiteral("movie-1"), recommendations);
    QVERIFY(vm.items().isEmpty());
}

void UpNextRecommendationsViewModelTest::seriesLoadAfterMovieDropsTheMovie()
{
    auto *libraryService = new UpNextTestLibraryService(this);
    libraryService->fallbackItem = QVariantMap{
        {"connectionId", "connection-1"}, {"itemId", "movie-1"}, {"mediaType", "Movie"}, {"name", "Heat"}
    };
    ServiceLocator::registerService<LibraryService>(libraryService);

    UpNextRecommendationsViewModel vm;
    vm.loadForMovie(QStringLiteral("movie-1"));
    QVERIFY(!vm.movie().isEmpty());

    libraryService->fallbackItem.clear();
    vm.loadForSeries(QStringLiteral("series-1"), 6);
    QVERIFY(vm.movie().isEmpty());
    QVERIFY(vm.movieRating()->itemId().isEmpty());
    QCOMPARE(libraryService->similarRequests, QStringList{QStringLiteral("series-1")});

    vm.clear();
    QVERIFY(vm.movie().isEmpty());
}

#include "UpNextRecommendationsViewModelTest.moc"
