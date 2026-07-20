#include <QtTest/QtTest>

#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"
#include "../src/test/MockLibraryService.h"
#include "../src/utils/DetailViewCache.h"

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

class EpisodeChaptersLibraryService : public LibraryService
{
    Q_OBJECT

public:
    explicit EpisodeChaptersLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    void getChapters(const QString &itemId) override
    {
        requests.append(itemId);
    }

    QString getCachedChapterThumbnailUrl(const QString &itemId,
                                         int chapterIndex,
                                         const QString &imageTag,
                                         const QString &imagePath,
                                         int width) override
    {
        Q_UNUSED(imageTag)
        Q_UNUSED(imagePath)
        return QStringLiteral("chapter://%1/%2/%3").arg(itemId).arg(chapterIndex).arg(width);
    }

    QStringList requests;
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
    void seriesEpisodeChaptersLoadNormalizeCacheAndIgnoreStale();
    void seriesEpisodeChaptersFailureClearsVisibleState();
    void movieChaptersLoadNormalizeCacheAndIgnoreStale();
    void movieChaptersFailureClearsVisibleState();
    void mockLibraryServiceTracksItemCacheInvalidation();
    void connectionScopeCacheKeysAreCollisionResistant();

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

void SimilarItemsRetryTest::movieChaptersLoadNormalizeCacheAndIgnoreStale()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    MovieDetailsViewModel vm;
    QSignalSpy loadingSpy(&vm, &MovieDetailsViewModel::chaptersLoadingChanged);

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests, QStringList{QStringLiteral("movie-1")});
    QVERIFY(vm.chaptersLoading());

    vm.loadMovieChapters(QStringLiteral("movie-2"));
    QCOMPARE(service->requests, (QStringList{QStringLiteral("movie-1"), QStringLiteral("movie-2")}));

    ChapterInfo first;
    first.index = 0;
    first.title = QStringLiteral("Cold Open");
    first.startPositionTicks = 120000000;
    first.imageTag = QStringLiteral("tag");
    emit service->chaptersLoaded(QStringLiteral("movie-1"), QList<ChapterInfo>{first});
    QVERIFY(vm.chapters().isEmpty());

    ChapterInfo second = first;
    second.title = QStringLiteral("Main Feature");
    second.index = 1;
    emit service->chaptersLoaded(QStringLiteral("movie-2"), QList<ChapterInfo>{second});

    QCOMPARE(vm.chaptersLoading(), false);
    QCOMPARE(vm.chapters().size(), 1);
    const QVariantMap chapter = vm.chapters().first().toMap();
    QCOMPARE(chapter.value(QStringLiteral("title")).toString(), QStringLiteral("Main Feature"));
    QCOMPARE(chapter.value(QStringLiteral("thumbnailUrl")).toString(), QStringLiteral("chapter://movie-2/1/480"));
    QVERIFY(loadingSpy.count() >= 2);

    vm.loadMovieChapters(QStringLiteral("movie-2"));
    QCOMPARE(service->requests.size(), 2);
    QCOMPARE(vm.chapters().size(), 1);
}

void SimilarItemsRetryTest::movieChaptersFailureClearsVisibleState()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    MovieDetailsViewModel vm;
    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QVERIFY(vm.chaptersLoading());

    emit service->chaptersFailed(QStringLiteral("movie-1"), QStringLiteral("network"));
    QCOMPARE(vm.chaptersLoading(), false);
    QVERIFY(vm.chapters().isEmpty());

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests.size(), 2);
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

void SimilarItemsRetryTest::seriesEpisodeChaptersLoadNormalizeCacheAndIgnoreStale()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel vm;
    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-1"));
    QCOMPARE(service->requests, QStringList{QStringLiteral("ep-1")});
    QVERIFY(vm.focusedEpisodeChaptersLoading());

    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-2"));
    QCOMPARE(service->requests, QStringList({QStringLiteral("ep-1"), QStringLiteral("ep-2")}));

    ChapterInfo first;
    first.index = 0;
    first.title = QStringLiteral("Cold Open");
    first.startPositionTicks = 1234;
    first.imageTag = QStringLiteral("tag");
    emit service->chaptersLoaded(QStringLiteral("ep-1"), QList<ChapterInfo>{first});
    QVERIFY(vm.focusedEpisodeChapters().isEmpty());

    ChapterInfo second;
    second.index = 1;
    second.title = QStringLiteral("Act One");
    second.startPositionTicks = 9876;
    second.imagePath = QStringLiteral("/Images/Chapter/1");
    emit service->chaptersLoaded(QStringLiteral("ep-2"), QList<ChapterInfo>{second});

    QCOMPARE(vm.focusedEpisodeChapters().size(), 1);
    const QVariantMap chapter = vm.focusedEpisodeChapters().first().toMap();
    QCOMPARE(chapter.value(QStringLiteral("title")).toString(), QStringLiteral("Act One"));
    QCOMPARE(chapter.value(QStringLiteral("startPositionTicks")).toLongLong(), 9876LL);
    QCOMPARE(chapter.value(QStringLiteral("thumbnailUrl")).toString(),
             QStringLiteral("chapter://ep-2/1/480"));
    QVERIFY(!vm.focusedEpisodeChaptersLoading());

    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-1"));
    QCOMPARE(service->requests.size(), 2);
    QCOMPARE(vm.focusedEpisodeChapters().first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Cold Open"));
}

void SimilarItemsRetryTest::seriesEpisodeChaptersFailureClearsVisibleState()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel vm;
    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-9"));
    emit service->chaptersFailed(QStringLiteral("ep-9"), QStringLiteral("network"));

    QVERIFY(vm.focusedEpisodeChapters().isEmpty());
    QVERIFY(!vm.focusedEpisodeChaptersLoading());
}

void SimilarItemsRetryTest::mockLibraryServiceTracksItemCacheInvalidation()
{
    MockLibraryService service;

    service.clearItemCacheValidation(QStringLiteral("ep-9"));

    QCOMPARE(service.clearItemCacheValidationCallCount(), 1);
    QVERIFY(service.wasItemCacheValidationCleared(QStringLiteral("ep-9")));
}

void SimilarItemsRetryTest::connectionScopeCacheKeysAreCollisionResistant()
{
    const QString slashScope = DetailViewCache::connectionScopeCacheKey(
        QStringLiteral("a/b"));
    const QString underscoreScope = DetailViewCache::connectionScopeCacheKey(
        QStringLiteral("a_b"));

    QVERIFY(!slashScope.isEmpty());
    QVERIFY(slashScope != underscoreScope);
    QCOMPARE(slashScope, DetailViewCache::connectionScopeCacheKey(QStringLiteral("a/b")));
    QVERIFY(!slashScope.contains(QLatin1Char('/')));
}

QTEST_MAIN(SimilarItemsRetryTest)
#include "SimilarItemsRetryTest.moc"
