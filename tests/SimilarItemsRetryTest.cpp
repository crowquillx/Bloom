#include <QtTest/QtTest>

#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"
#include "../src/test/MockLibraryService.h"

class CountingLibraryService : public LibraryService
{
    Q_OBJECT

public:
    explicit CountingLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    void getSimilarItems(const QString &itemId, int limit = 12) override
    {
        Q_UNUSED(limit)
        requestedIds.append(itemId);
    }

    QString getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width) override
    {
        return QString("image://%1/%2/%3").arg(itemId, imageType).arg(width);
    }

    QStringList requestedIds;
};

class EpisodeDetailsLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct ItemRequest {
        QString itemId;
        QString requestContext;
    };

    explicit EpisodeDetailsLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    void getItem(const QString &itemId, const QString &requestContext) override
    {
        requests.append({itemId, requestContext});

        if (emitNotModifiedImmediately) {
            emit itemNotModified(itemId, requestContext);
        }
    }

    void clearItemCacheValidation(const QString &itemId) override
    {
        clearItemCacheValidationCalls.append(itemId);
    }

    void emitLoadedAt(int index, const QJsonObject &payload)
    {
        const auto &request = requests.at(index);
        emit itemLoaded(request.itemId, payload, request.requestContext);
    }

    void emitFailedAt(int index, const QString &error)
    {
        const auto &request = requests.at(index);
        emit itemFailed(request.itemId, error, request.requestContext);
    }

    bool emitNotModifiedImmediately = false;
    QList<ItemRequest> requests;
    QStringList clearItemCacheValidationCalls;
};

class SimilarItemsRetryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void movieSimilarItemsFailureAllowsRetry();
    void seriesSimilarItemsFailureAllowsRetry();
    void seriesEpisodeDetailsFailureIsTargeted();
    void seriesEpisodeDetailsNotModifiedRetriesOnce();
    void seriesEpisodeDetailsIgnoreForeignTokens();
    void mockLibraryServiceTracksItemCacheInvalidation();

private:
    CountingLibraryService *m_libraryService = nullptr;
};

void SimilarItemsRetryTest::init()
{
    ServiceLocator::clear();
    m_libraryService = new CountingLibraryService(this);
    ServiceLocator::registerService<LibraryService>(m_libraryService);
}

void SimilarItemsRetryTest::cleanup()
{
    ServiceLocator::clear();
    delete m_libraryService;
    m_libraryService = nullptr;
}

void SimilarItemsRetryTest::movieSimilarItemsFailureAllowsRetry()
{
    MovieDetailsViewModel vm;
    vm.m_movieId = QStringLiteral("movie-1");
    vm.m_similarItemsAttempted = true;
    vm.m_similarItemsLoading = true;

    vm.onSimilarItemsFailed(QStringLiteral("movie-1"), QStringLiteral("network"));

    QCOMPARE(vm.m_similarItemsAttempted, false);
    QCOMPARE(vm.m_similarItemsLoading, false);

    vm.onMovieDetailsNotModified(QStringLiteral("movie-1"));
    QCOMPARE(m_libraryService->requestedIds.size(), 1);
    QCOMPARE(m_libraryService->requestedIds.first(), QStringLiteral("movie-1"));
}

void SimilarItemsRetryTest::seriesSimilarItemsFailureAllowsRetry()
{
    SeriesDetailsViewModel vm;
    vm.m_seriesId = QStringLiteral("series-1");
    vm.m_similarItemsAttempted = true;
    vm.m_similarItemsLoading = true;

    vm.onSimilarItemsFailed(QStringLiteral("series-1"), QStringLiteral("network"));

    QCOMPARE(vm.m_similarItemsAttempted, false);
    QCOMPARE(vm.m_similarItemsLoading, false);

    vm.onSeriesDetailsLoaded(QStringLiteral("series-1"), QJsonObject{{"Id", "series-1"}, {"UserData", QJsonObject{}}});
    QCOMPARE(m_libraryService->requestedIds.size(), 1);
    QCOMPARE(m_libraryService->requestedIds.first(), QStringLiteral("series-1"));
}

void SimilarItemsRetryTest::seriesEpisodeDetailsFailureIsTargeted()
{
    ServiceLocator::clear();
    auto *service = new EpisodeDetailsLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel vm;
    vm.m_focusedEpisodeDetailId = QStringLiteral("ep-1");
    vm.setFocusedEpisodeDetailsLoading(true);

    const QString focusedToken = vm.startEpisodeDetailsRequest(QStringLiteral("ep-1"));
    const QString secondaryToken = vm.startEpisodeDetailsRequest(QStringLiteral("ep-2"));

    QSignalSpy detailsSpy(&vm, &SeriesDetailsViewModel::focusedEpisodeDetailsChanged);

    vm.onEpisodeDetailsFailed(QStringLiteral("ep-2"), QStringLiteral("boom"), secondaryToken);

    QVERIFY(vm.m_pendingEpisodeDetailIds.contains(QStringLiteral("ep-1")));
    QVERIFY(!vm.m_pendingEpisodeDetailIds.contains(QStringLiteral("ep-2")));
    QCOMPARE(vm.m_episodeDetailRequestTokens.value(QStringLiteral("ep-1")), focusedToken);
    QVERIFY(vm.focusedEpisodeDetailsLoading());
    QCOMPARE(detailsSpy.count(), 0);
}

void SimilarItemsRetryTest::seriesEpisodeDetailsNotModifiedRetriesOnce()
{
    ServiceLocator::clear();
    auto *service = new EpisodeDetailsLibraryService(this);
    service->emitNotModifiedImmediately = true;
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel vm;
    QSignalSpy detailsSpy(&vm, &SeriesDetailsViewModel::focusedEpisodeDetailsChanged);

    vm.loadFocusedEpisodeDetails(QStringLiteral("ep-1"));

    QCOMPARE(service->requests.size(), 2);
    QCOMPARE(service->clearItemCacheValidationCalls.size(), 1);
    QCOMPARE(service->clearItemCacheValidationCalls.first(), QStringLiteral("ep-1"));
    QVERIFY(vm.m_pendingEpisodeDetailIds.isEmpty());
    QVERIFY(vm.m_episodeDetailRequestTokens.isEmpty());
    QVERIFY(vm.m_episodeDetailRetried.isEmpty());
    QVERIFY(!vm.focusedEpisodeDetailsLoading());
    QVERIFY(detailsSpy.count() >= 1);
}

void SimilarItemsRetryTest::seriesEpisodeDetailsIgnoreForeignTokens()
{
    ServiceLocator::clear();
    auto *service = new EpisodeDetailsLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel firstVm;
    SeriesDetailsViewModel secondVm;

    firstVm.loadFocusedEpisodeDetails(QStringLiteral("shared-episode"));
    secondVm.loadFocusedEpisodeDetails(QStringLiteral("shared-episode"));

    QCOMPARE(service->requests.size(), 2);

    service->emitLoadedAt(0, QJsonObject{{"Id", "shared-episode"}, {"Name", "First"}}); 

    QCOMPARE(firstVm.focusedEpisodeDetails().value(QStringLiteral("Name")).toString(), QStringLiteral("First"));
    QVERIFY(firstVm.m_pendingEpisodeDetailIds.isEmpty());
    QVERIFY(secondVm.focusedEpisodeDetails().isEmpty());
    QVERIFY(secondVm.focusedEpisodeDetailsLoading());
    QVERIFY(secondVm.m_pendingEpisodeDetailIds.contains(QStringLiteral("shared-episode")));
}

void SimilarItemsRetryTest::mockLibraryServiceTracksItemCacheInvalidation()
{
    MockLibraryService service;

    service.clearItemCacheValidation(QStringLiteral("ep-9"));

    QCOMPARE(service.clearItemCacheValidationCallCount(), 1);
    QVERIFY(service.wasItemCacheValidationCleared(QStringLiteral("ep-9")));
}

QTEST_MAIN(SimilarItemsRetryTest)
#include "SimilarItemsRetryTest.moc"
