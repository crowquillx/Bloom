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
        emit similarItemsLoaded(itemId, similarItems);
    }

    void getSeriesDetails(const QString &seriesId) override
    {
        seriesDetailsRequests.append(seriesId);
        if (emitNotModifiedForSeriesDetails) {
            emit seriesDetailsNotModified(seriesId);
            return;
        }
        if (emitPathErrorForSeriesDetails) {
            emit errorOccurred(QStringLiteral("/Users/user-1/Items/%1?Fields=ProviderIds").arg(seriesId),
                               QStringLiteral("timeout"));
            return;
        }
        emit seriesDetailsLoaded(seriesId, seriesDetails);
    }

    void getItem(const QString &itemId, const QString &requestContext) override
    {
        itemRequests.append(itemId);
        itemRequestContexts.append(requestContext);
        emit itemLoaded(itemId, fallbackItem, requestContext);
    }

    void clearItemCacheValidation(const QString &itemId) override
    {
        clearedItemCacheIds.append(itemId);
    }

    QJsonArray similarItems;
    QJsonObject seriesDetails;
    QJsonObject fallbackItem;
    QStringList similarRequests;
    QStringList seriesDetailsRequests;
    QStringList itemRequests;
    QStringList itemRequestContexts;
    QStringList clearedItemCacheIds;
    bool emitNotModifiedForSeriesDetails = false;
    bool emitPathErrorForSeriesDetails = false;
};

class UpNextRecommendationsViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void jellyfinResultsAppearBeforeSeerrResults();
    void canonicalSeriesAreAccepted();
    void nonSeriesResultsAreFiltered();
    void duplicateTitlesCollapseToJellyfinItem();
    void resultCountIsCapped();
    void unavailableProvidersLeaveEmptyNotLoading();
    void seriesDetailsNotModifiedUsesFallbackItem();
    void seriesDetailsPathErrorClearsLoading();
};

void UpNextRecommendationsViewModelTest::cleanup()
{
    ServiceLocator::clear();
}

void UpNextRecommendationsViewModelTest::jellyfinResultsAppearBeforeSeerrResults()
{
    const QJsonArray jellyfin{
        QJsonObject{{"Id", "jf-1"}, {"Type", "Series"}, {"Name", "Local First"}}
    };
    const QJsonArray seerr{
        QJsonObject{{"itemId", "seerr:tv:2"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"name", "Discovery Second"}}
    };

    const QJsonArray merged = UpNextRecommendationsViewModel::mergeRecommendations(jellyfin, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toObject().value("Id").toString(), QStringLiteral("jf-1"));
    QCOMPARE(merged.at(1).toObject().value("itemId").toString(), QStringLiteral("seerr:tv:2"));
}

void UpNextRecommendationsViewModelTest::canonicalSeriesAreAccepted()
{
    const QJsonArray canonical{
        QJsonObject{
            {"itemId", "series-1"},
            {"mediaType", "Series"},
            {"name", "Canonical Series"}
        }
    };

    const QJsonArray merged = UpNextRecommendationsViewModel::mergeRecommendations(
        canonical, {}, 6);

    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged.at(0).toObject().value("itemId").toString(),
             QStringLiteral("series-1"));
}

void UpNextRecommendationsViewModelTest::nonSeriesResultsAreFiltered()
{
    const QJsonArray jellyfin{
        QJsonObject{{"Id", "movie-1"}, {"Type", "Movie"}, {"Name", "A Movie"}},
        QJsonObject{{"Id", "series-1"}, {"Type", "Series"}, {"Name", "A Series"}}
    };
    const QJsonArray seerr{
        QJsonObject{{"itemId", "seerr:movie:1"}, {"source", "seerr"}, {"seerrMediaType", "movie"}, {"name", "A Seerr Movie"}},
        QJsonObject{{"itemId", "seerr:tv:1"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"name", "A Seerr Series"}}
    };

    const QJsonArray merged = UpNextRecommendationsViewModel::mergeRecommendations(jellyfin, seerr, 6);

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toObject().value("Id").toString(), QStringLiteral("series-1"));
    QCOMPARE(merged.at(1).toObject().value("itemId").toString(), QStringLiteral("seerr:tv:1"));
}

void UpNextRecommendationsViewModelTest::duplicateTitlesCollapseToJellyfinItem()
{
    const QJsonArray jellyfin{
        QJsonObject{{"Id", "jf-1"}, {"Type", "Series"}, {"Name", "Same Show"}, {"ProductionYear", 2020}}
    };
    const QJsonArray seerr{
        QJsonObject{{"itemId", "seerr:tv:10"}, {"source", "seerr"}, {"seerrMediaType", "tv"}, {"name", "Same Show!"}, {"productionYear", 2020}}
    };

    const QJsonArray merged = UpNextRecommendationsViewModel::mergeRecommendations(jellyfin, seerr, 6);

    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged.at(0).toObject().value("Id").toString(), QStringLiteral("jf-1"));
}

void UpNextRecommendationsViewModelTest::resultCountIsCapped()
{
    QJsonArray jellyfin;
    for (int i = 0; i < 8; ++i) {
        jellyfin.append(QJsonObject{
            {"Id", QStringLiteral("jf-%1").arg(i)},
            {"Type", "Series"},
            {"Name", QStringLiteral("Series %1").arg(i)}
        });
    }

    const QJsonArray merged = UpNextRecommendationsViewModel::mergeRecommendations(jellyfin, {}, 6);

    QCOMPARE(merged.size(), 6);
    QCOMPARE(merged.at(5).toObject().value("Id").toString(), QStringLiteral("jf-5"));
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
    libraryService->fallbackItem = QJsonObject{
        {"Id", "series-1"},
        {"Type", "Series"},
        {"ProviderIds", QJsonObject{{"Tmdb", "1234"}}}
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

QTEST_MAIN(UpNextRecommendationsViewModelTest)
#include "UpNextRecommendationsViewModelTest.moc"
