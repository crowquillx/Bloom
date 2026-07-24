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
                QStringLiteral("connection-1"), itemId, similarItems);
        }
    }

    void getSeriesDetails(const QString &seriesId) override
    {
        seriesDetailsRequests.append(seriesId);
        if (deferSeriesDetails) {
            return;
        }
        if (emitNotModifiedForSeriesDetails) {
            emit canonicalSeriesDetailsNotModified(QStringLiteral("connection-1"), seriesId);
            return;
        }
        if (emitPathErrorForSeriesDetails) {
            emit canonicalSeriesDetailsFailed(
                QStringLiteral("connection-1"), seriesId, QStringLiteral("timeout"));
            return;
        }
        emit canonicalSeriesDetailsLoaded(QStringLiteral("connection-1"), seriesId, seriesDetails);
    }

    void getItem(const QString &itemId, const QString &requestContext) override
    {
        itemRequests.append(itemId);
        itemRequestContexts.append(requestContext);
        emit canonicalItemLoaded(itemId, fallbackItem, requestContext);
    }

    void clearItemCacheValidation(const QString &itemId) override
    {
        clearedItemCacheIds.append(itemId);
    }

    QVariantList similarItems;
    QVariantMap seriesDetails;
    QVariantMap fallbackItem;
    QStringList similarRequests;
    QStringList seriesDetailsRequests;
    QStringList itemRequests;
    QStringList itemRequestContexts;
    QStringList clearedItemCacheIds;
    bool emitNotModifiedForSeriesDetails = false;
    bool emitPathErrorForSeriesDetails = false;
    bool deferSimilar = false;
    bool deferSeriesDetails = false;

    QString getActiveConnectionId() const override
    {
        return QStringLiteral("connection-1");
    }
};

class UpNextRecommendationsViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void libraryResultsAppearBeforeSeerrResults();
    void nonSeriesResultsAreFiltered();
    void duplicateTitlesCollapseToJellyfinItem();
    void resultCountIsCapped();
    void unavailableProvidersLeaveEmptyNotLoading();
    void seriesDetailsNotModifiedUsesFallbackItem();
    void seriesDetailsPathErrorClearsLoading();
    void staleConnectionSignalsAreIgnored();
};

void UpNextRecommendationsViewModelTest::cleanup()
{
    ServiceLocator::clear();
}

void UpNextRecommendationsViewModelTest::libraryResultsAppearBeforeSeerrResults()
{
    const QVariantList library{
        QVariantMap{{"itemId", "library-1"}, {"mediaType", "Series"}, {"name", "Local First"}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "seerr:tv:2"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"mediaType", "Series"}, {"name", "Discovery Second"}}
    };

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value("itemId").toString(), QStringLiteral("library-1"));
    QCOMPARE(merged.at(1).toMap().value("itemId").toString(), QStringLiteral("seerr:tv:2"));
}

void UpNextRecommendationsViewModelTest::nonSeriesResultsAreFiltered()
{
    const QVariantList library{
        QVariantMap{{"itemId", "movie-1"}, {"mediaType", "Movie"}, {"name", "A Movie"}},
        QVariantMap{{"itemId", "series-1"}, {"mediaType", "Series"}, {"name", "A Series"}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "seerr:movie:1"}, {"source", "seerr"}, {"seerrMediaType", "movie"}, {"mediaType", "Movie"}, {"name", "A Seerr Movie"}},
        QVariantMap{{"itemId", "seerr:tv:1"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"mediaType", "Series"}, {"name", "A Seerr Series"}}
    };

    const QVariantList merged = UpNextRecommendationsViewModel::mergeRecommendations(library, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value("itemId").toString(), QStringLiteral("series-1"));
    QCOMPARE(merged.at(1).toMap().value("itemId").toString(), QStringLiteral("seerr:tv:1"));
}

void UpNextRecommendationsViewModelTest::duplicateTitlesCollapseToJellyfinItem()
{
    const QVariantList library{
        QVariantMap{{"itemId", "library-1"}, {"mediaType", "Series"}, {"name", "Same Show"}, {"productionYear", 2020}}
    };
    const QVariantList seerr{
        QVariantMap{{"itemId", "seerr:tv:10"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"mediaType", "Series"}, {"name", "Same Show!"}, {"productionYear", 2020}}
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

QTEST_MAIN(UpNextRecommendationsViewModelTest)
#include "UpNextRecommendationsViewModelTest.moc"
