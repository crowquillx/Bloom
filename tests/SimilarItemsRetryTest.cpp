#include <QtTest/QtTest>

#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"
#include "../src/test/MockLibraryService.h"
#include "../src/utils/ConfigManager.h"
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

    QString getCachedArtworkUrl(const QString &itemId,
                                const QString &imageType,
                                int imageIndex,
                                const QString &imageTag,
                                int width) override
    {
        Q_UNUSED(imageIndex)
        Q_UNUSED(imageTag)
        return QStringLiteral("artwork://%1/%2/%3").arg(itemId, imageType).arg(width);
    }

    QString getCachedArtworkUrlForConnection(const QString &connectionId,
                                             const QString &itemId,
                                             const QString &imageType,
                                             int imageIndex,
                                             const QString &imageTag,
                                             int width) override
    {
        Q_UNUSED(imageIndex)
        Q_UNUSED(imageTag)
        return QStringLiteral("artwork://%1/%2/%3/%4")
            .arg(connectionId, itemId, imageType)
            .arg(width);
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

    void emitLoadedAt(int index, const QVariantMap &payload)
    {
        const auto &request = requests.at(index);
        emit canonicalItemLoaded(request.itemId, payload, request.requestContext);
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

    QString getCachedArtworkUrlForConnection(const QString &connectionId,
                                             const QString &itemId,
                                             const QString &imageType,
                                             int imageIndex,
                                             const QString &imageTag,
                                             int width) override
    {
        Q_UNUSED(connectionId)
        Q_UNUSED(imageType)
        Q_UNUSED(imageTag)
        return QStringLiteral("chapter://%1/%2/%3").arg(itemId).arg(imageIndex).arg(width);
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
    void movieCanonicalSimilarItemsReplaceWireShape();
    void seriesSimilarItemsFailureAllowsRetry();
    void seriesCanonicalModelsPreserveSourceTimingAndSpecialOrder();
    void seriesEpisodeDetailsFailureIsTargeted();
    void seriesEpisodeDetailsNotModifiedRetriesOnce();
    void seriesEpisodeDetailsIgnoreForeignTokens();
    void seriesEpisodeChaptersLoadNormalizeCacheAndIgnoreStale();
    void seriesEpisodeChaptersFailureClearsVisibleState();
    void movieChaptersLoadNormalizeCacheAndIgnoreStale();
    void movieChaptersScopeMismatchAllowsCurrentScopeRequest();
    void movieChaptersFailureClearsVisibleState();
    void movieChaptersFailureForOldScopeDoesNotCancelRetry();
    void movieChaptersCacheIsScopedByConnection();
    void mockLibraryServiceTracksItemCacheInvalidation();
    void mockHomeSignalsHaveCanonicalParity();
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

void SimilarItemsRetryTest::movieCanonicalSimilarItemsReplaceWireShape()
{
    MovieDetailsViewModel vm;
    vm.m_movieId = QStringLiteral("movie-1");
    vm.m_similarItemsLoading = true;

    const QVariantList canonicalItems{
        QVariantMap{
            {QStringLiteral("itemId"), QStringLiteral("movie-2")},
            {QStringLiteral("name"), QStringLiteral("Similar")},
            {QStringLiteral("mediaType"), QStringLiteral("Movie")},
            {QStringLiteral("productionYear"), 2023},
            {QStringLiteral("primaryArtwork"), QVariantMap{
                 {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
                 {QStringLiteral("itemId"), QStringLiteral("movie-2")},
                 {QStringLiteral("kind"), QStringLiteral("primary")},
                 {QStringLiteral("index"), 0},
                 {QStringLiteral("tag"), QStringLiteral("poster-tag")},
                 {QStringLiteral("requestedWidth"), 0}
             }}
        },
        QVariantMap{
            {QStringLiteral("Id"), QStringLiteral("legacy-wire")},
            {QStringLiteral("Name"), QStringLiteral("Should Be Ignored")}
        }
    };

    vm.onSimilarItemsLoaded(QStringLiteral("movie-1"), canonicalItems);

    QCOMPARE(vm.similarItems().size(), 1);
    const QVariantMap item = vm.similarItems().first().toMap();
    QCOMPARE(item.value(QStringLiteral("itemId")).toString(), QStringLiteral("movie-2"));
    QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("Similar"));
    QVERIFY(item.contains(QStringLiteral("primaryArtwork")));
    QVERIFY(!item.contains(QStringLiteral("Id")));
    QCOMPARE(vm.similarItemsLoading(), false);
    QCOMPARE(vm.m_similarItemsAttempted, true);
}

void SimilarItemsRetryTest::seriesSimilarItemsFailureAllowsRetry()
{
    SeriesDetailsViewModel vm;
    vm.m_seriesId = QStringLiteral("series-1");
    vm.m_connectionId = QStringLiteral("connection-1");
    vm.m_similarItemsAttempted = true;
    vm.m_similarItemsLoading = true;

    vm.onSimilarItemsFailed(QStringLiteral("old-connection"),
                            QStringLiteral("series-1"),
                            QStringLiteral("stale network"));
    QCOMPARE(vm.m_similarItemsAttempted, true);
    QCOMPARE(vm.m_similarItemsLoading, true);

    vm.onSimilarItemsFailed(QStringLiteral("connection-1"),
                            QStringLiteral("series-1"),
                            QStringLiteral("network"));

    QCOMPARE(vm.m_similarItemsAttempted, false);
    QCOMPARE(vm.m_similarItemsLoading, false);

    QSignalSpy seriesDataSpy(&vm, &SeriesDetailsViewModel::seriesDataChanged);
    vm.onSeriesDetailsLoaded(
        QStringLiteral("connection-1"),
        QStringLiteral("series-1"),
        QVariantMap{{QStringLiteral("connectionId"), QStringLiteral("connection-1")},
                    {QStringLiteral("itemId"), QStringLiteral("series-1")},
                    {QStringLiteral("name"), QStringLiteral("Series")},
                    {QStringLiteral("userState"), QVariantMap{}}});
    QCOMPARE(seriesDataSpy.count(), 1);
    QCOMPARE(m_libraryService->requestedIds.size(), 1);
    QCOMPARE(m_libraryService->requestedIds.first(), QStringLiteral("series-1"));
}

void SimilarItemsRetryTest::seriesCanonicalModelsPreserveSourceTimingAndSpecialOrder()
{
    SeriesDetailsViewModel vm;
    vm.m_connectionId = QStringLiteral("connection-b");
    vm.m_seriesId = QStringLiteral("series-1");
    vm.m_selectedSeasonIndex = 0; // Prevent auto-selection from issuing a network request.

    QVariantList people;
    people.append(QVariantMap{{QStringLiteral("name"), QString()}});
    for (int index = 0; index < 20; ++index) {
        people.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("Person %1").arg(index)},
                                  {QStringLiteral("kind"), QStringLiteral("Actor")}});
    }
    vm.onSeriesDetailsLoaded(
        QStringLiteral("connection-b"), QStringLiteral("series-1"),
        QVariantMap{{QStringLiteral("connectionId"), QStringLiteral("connection-b")},
                    {QStringLiteral("itemId"), QStringLiteral("series-1")},
                    {QStringLiteral("name"), QStringLiteral("Series")},
                    {QStringLiteral("people"), people}});
    QCOMPARE(vm.people().size(), 18);
    for (const QVariant &person : vm.people()) {
        QVERIFY(!person.toMap().value(QStringLiteral("name")).toString().isEmpty());
    }

    vm.m_loadingSeasons = true;
    const QVariantMap season{
        {QStringLiteral("connectionId"), QStringLiteral("connection-b")},
        {QStringLiteral("itemId"), QStringLiteral("season-1")},
        {QStringLiteral("name"), QStringLiteral("Season 1")},
        {QStringLiteral("mediaType"), QStringLiteral("Season")},
        {QStringLiteral("indexNumber"), 1},
        {QStringLiteral("childCount"), 3},
        {QStringLiteral("watched"), false},
        {QStringLiteral("unplayedItemCount"), 3},
        {QStringLiteral("primaryArtwork"), QVariantMap{
             {QStringLiteral("connectionId"), QStringLiteral("connection-b")},
             {QStringLiteral("itemId"), QStringLiteral("season-1")},
             {QStringLiteral("kind"), QStringLiteral("primary")},
             {QStringLiteral("index"), 0},
             {QStringLiteral("tag"), QStringLiteral("season-tag")}
         }}
    };

    vm.onItemsLoaded(QStringLiteral("connection-a"), QStringLiteral("series-1"),
                     QStringLiteral("query-a"), QVariantList{season}, 1);
    QCOMPARE(vm.seasonsModel()->rowCount(), 0);

    vm.onItemsLoaded(QStringLiteral("connection-b"), QStringLiteral("series-1"),
                     QStringLiteral("query-b"), QVariantList{season}, 1);
    QCOMPARE(vm.seasonsModel()->rowCount(), 1);
    const QVariantMap storedSeason = vm.seasonsModel()->getItem(0);
    QCOMPARE(storedSeason.value(QStringLiteral("itemId")).toString(), QStringLiteral("season-1"));
    QCOMPARE(storedSeason.value(QStringLiteral("imageUrl")).toString(),
             QStringLiteral("artwork://connection-b/season-1/primary/400"));
    QVERIFY(!storedSeason.contains(QStringLiteral("Id")));

    vm.m_selectedSeasonId = QStringLiteral("season-1");
    vm.m_loadingEpisodes = true;
    const auto episode = [](const QString &id, int number) {
        return QVariantMap{
            {QStringLiteral("connectionId"), QStringLiteral("connection-b")},
            {QStringLiteral("itemId"), id},
            {QStringLiteral("name"), id},
            {QStringLiteral("mediaType"), QStringLiteral("Episode")},
            {QStringLiteral("indexNumber"), number},
            {QStringLiteral("parentIndexNumber"), 1},
            {QStringLiteral("locationType"), QStringLiteral("FileSystem")},
            {QStringLiteral("durationMs"), 1'800'000},
            {QStringLiteral("positionMs"), 120'000},
            {QStringLiteral("watched"), false},
            {QStringLiteral("favorite"), false},
            {QStringLiteral("airsBeforeSeasonNumber"), -1},
            {QStringLiteral("airsAfterSeasonNumber"), -1},
            {QStringLiteral("airsBeforeEpisodeNumber"), -1}
        };
    };
    QVariantMap special = episode(QStringLiteral("special"), 1);
    special.insert(QStringLiteral("parentIndexNumber"), 0);
    special.insert(QStringLiteral("airsBeforeSeasonNumber"), 1);
    special.insert(QStringLiteral("airsBeforeEpisodeNumber"), 2);

    vm.onEpisodesLoaded(QStringLiteral("season-1"),
                        QVariantList{episode(QStringLiteral("episode-2"), 2),
                                     special,
                                     episode(QStringLiteral("episode-1"), 1)});
    QCOMPARE(vm.episodesModel()->rowCount(), 3);
    QCOMPARE(vm.episodesModel()->getItem(0).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("episode-1"));
    QCOMPARE(vm.episodesModel()->getItem(1).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("special"));
    QCOMPARE(vm.episodesModel()->getItem(2).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("episode-2"));
    QCOMPARE(vm.episodesModel()->data(vm.episodesModel()->index(0), EpisodesModel::DurationMsRole).toLongLong(),
             1'800'000);
    QCOMPARE(vm.episodesModel()->data(vm.episodesModel()->index(0), EpisodesModel::PositionMsRole).toLongLong(),
             120'000);

    QVariantMap nextEpisode = episode(QStringLiteral("episode-2"), 2);
    nextEpisode.insert(QStringLiteral("thumbArtwork"), QVariantMap{
        {QStringLiteral("connectionId"), QStringLiteral("connection-b")},
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("kind"), QStringLiteral("thumb")},
        {QStringLiteral("index"), 0},
        {QStringLiteral("tag"), QStringLiteral("thumb-tag")}
    });
    vm.onNextEpisodeLoaded(QStringLiteral("connection-b"), QStringLiteral("series-1"),
                           nextEpisode, QString());
    QCOMPARE(vm.nextEpisodePositionMs(), 120'000);
    QCOMPARE(vm.nextEpisodeImageUrl(),
             QStringLiteral("artwork://connection-b/episode-2/thumb/400"));
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

    const QVariantMap first{
        {QStringLiteral("name"), QStringLiteral("Cold Open")},
        {QStringLiteral("startMs"), 12'000},
        {QStringLiteral("artwork"), QVariantMap{
             {QStringLiteral("connectionId"), QStringLiteral("_local")},
             {QStringLiteral("itemId"), QStringLiteral("movie-1")},
             {QStringLiteral("kind"), QStringLiteral("chapter")},
             {QStringLiteral("index"), 0},
             {QStringLiteral("tag"), QStringLiteral("tag")}
         }}
    };
    emit service->canonicalChaptersLoaded(QStringLiteral("_local"),
                                          QStringLiteral("movie-1"),
                                          QVariantList{first});
    QVERIFY(vm.chapters().isEmpty());

    QVariantMap second = first;
    second.insert(QStringLiteral("name"), QStringLiteral("Main Feature"));
    second.insert(QStringLiteral("artwork"), QVariantMap{
        {QStringLiteral("connectionId"), QStringLiteral("_local")},
        {QStringLiteral("itemId"), QStringLiteral("movie-2")},
        {QStringLiteral("kind"), QStringLiteral("chapter")},
        {QStringLiteral("index"), 1},
        {QStringLiteral("tag"), QStringLiteral("tag")}
    });
    emit service->canonicalChaptersLoaded(QStringLiteral("_local"),
                                          QStringLiteral("movie-2"),
                                          QVariantList{second});

    QCOMPARE(vm.chaptersLoading(), false);
    QCOMPARE(vm.chapters().size(), 1);
    const QVariantMap chapter = vm.chapters().first().toMap();
    QCOMPARE(chapter.value(QStringLiteral("name")).toString(), QStringLiteral("Main Feature"));
    QCOMPARE(chapter.value(QStringLiteral("startMs")).toLongLong(), 12'000LL);
    QCOMPARE(chapter.value(QStringLiteral("thumbnailUrl")).toString(), QStringLiteral("chapter://movie-2/1/480"));
    QVERIFY(loadingSpy.count() >= 2);

    vm.loadMovieChapters(QStringLiteral("movie-2"));
    QCOMPARE(service->requests.size(), 2);
    QCOMPARE(vm.chapters().size(), 1);
}

void SimilarItemsRetryTest::movieChaptersScopeMismatchAllowsCurrentScopeRequest()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    auto *config = new ConfigManager(this);
    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-a"),
        .baseUrl = QStringLiteral("https://a.example.test"),
        .accountId = QStringLiteral("user-a")
    });
    ServiceLocator::registerService<LibraryService>(service);
    ServiceLocator::registerService<ConfigManager>(config);

    MovieDetailsViewModel vm;
    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests, QStringList{QStringLiteral("movie-1")});
    QVERIFY(vm.chaptersLoading());

    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-b"),
        .baseUrl = QStringLiteral("https://b.example.test"),
        .accountId = QStringLiteral("user-b")
    });

    emit service->canonicalChaptersLoaded(QStringLiteral("connection-a"),
                                          QStringLiteral("movie-1"),
                                          QVariantList{QVariantMap{
                                              {QStringLiteral("name"), QStringLiteral("Stale")},
                                              {QStringLiteral("startMs"), 1'000}
                                          }});
    QVERIFY(vm.chapters().isEmpty());
    QVERIFY(vm.chaptersLoading());

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests,
             (QStringList{QStringLiteral("movie-1"), QStringLiteral("movie-1")}));
    QVERIFY(vm.chapters().isEmpty());
    QVERIFY(vm.chaptersLoading());
}

void SimilarItemsRetryTest::movieChaptersFailureClearsVisibleState()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    MovieDetailsViewModel vm;
    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QVERIFY(vm.chaptersLoading());

    emit service->chaptersFailed(QStringLiteral("_local"),
                                 QStringLiteral("movie-1"),
                                 QStringLiteral("network"));
    QCOMPARE(vm.chaptersLoading(), false);
    QVERIFY(vm.chapters().isEmpty());

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests.size(), 2);
}

void SimilarItemsRetryTest::movieChaptersFailureForOldScopeDoesNotCancelRetry()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    auto *config = new ConfigManager(this);
    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-a"),
        .baseUrl = QStringLiteral("https://a.example.test"),
        .accountId = QStringLiteral("user-a")
    });
    ServiceLocator::registerService<LibraryService>(service);
    ServiceLocator::registerService<ConfigManager>(config);

    MovieDetailsViewModel vm;
    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests, QStringList{QStringLiteral("movie-1")});
    QVERIFY(vm.chaptersLoading());

    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-b"),
        .baseUrl = QStringLiteral("https://b.example.test"),
        .accountId = QStringLiteral("user-b")
    });

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests,
             (QStringList{QStringLiteral("movie-1"), QStringLiteral("movie-1")}));
    QVERIFY(vm.chapters().isEmpty());
    QVERIFY(vm.chaptersLoading());

    emit service->chaptersFailed(QStringLiteral("connection-a"),
                                 QStringLiteral("movie-1"),
                                 QStringLiteral("network"));
    QVERIFY(vm.chapters().isEmpty());
    QVERIFY(vm.chaptersLoading());

    emit service->canonicalChaptersLoaded(QStringLiteral("connection-b"),
                                          QStringLiteral("movie-1"),
                                          QVariantList{QVariantMap{
                                              {QStringLiteral("name"), QStringLiteral("Fresh")},
                                              {QStringLiteral("startMs"), 2'000}
                                          }});
    QCOMPARE(vm.chaptersLoading(), false);
    QCOMPARE(vm.chapters().size(), 1);
    QCOMPARE(vm.chapters().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Fresh"));
}

void SimilarItemsRetryTest::movieChaptersCacheIsScopedByConnection()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    auto *config = new ConfigManager(this);
    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-a"),
        .baseUrl = QStringLiteral("https://a.example.test"),
        .accountId = QStringLiteral("user-a")
    });
    ServiceLocator::registerService<LibraryService>(service);
    ServiceLocator::registerService<ConfigManager>(config);

    MovieDetailsViewModel vm;
    vm.loadMovieChapters(QStringLiteral("movie-1"));
    emit service->canonicalChaptersLoaded(QStringLiteral("connection-a"),
                                          QStringLiteral("movie-1"),
                                          QVariantList{QVariantMap{
                                              {QStringLiteral("name"), QStringLiteral("A")},
                                              {QStringLiteral("startMs"), 1'000}
                                          }});
    QCOMPARE(vm.chapters().size(), 1);
    QCOMPARE(vm.chapters().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("A"));

    config->upsertConnection(ServerConnection{
        .connectionId = QStringLiteral("connection-b"),
        .baseUrl = QStringLiteral("https://b.example.test"),
        .accountId = QStringLiteral("user-b")
    });

    vm.loadMovieChapters(QStringLiteral("movie-1"));
    QCOMPARE(service->requests,
             (QStringList{QStringLiteral("movie-1"), QStringLiteral("movie-1")}));
    QVERIFY(vm.chapters().isEmpty());
    QVERIFY(vm.chaptersLoading());
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

    service->emitLoadedAt(0, QVariantMap{
        {QStringLiteral("connectionId"), QString()},
        {QStringLiteral("itemId"), QStringLiteral("shared-episode")},
        {QStringLiteral("name"), QStringLiteral("First")}
    });

    QCOMPARE(firstVm.focusedEpisodeDetails().value(QStringLiteral("name")).toString(), QStringLiteral("First"));
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

    const QVariantMap first{
        {QStringLiteral("name"), QStringLiteral("Cold Open")},
        {QStringLiteral("startMs"), 0},
        {QStringLiteral("artwork"), QVariantMap{
             {QStringLiteral("connectionId"), QString()},
             {QStringLiteral("itemId"), QStringLiteral("ep-1")},
             {QStringLiteral("kind"), QStringLiteral("chapter")},
             {QStringLiteral("index"), 0},
             {QStringLiteral("tag"), QStringLiteral("tag")}
         }}
    };
    emit service->canonicalChaptersLoaded(QString(), QStringLiteral("ep-1"), QVariantList{first});
    QVERIFY(vm.focusedEpisodeChapters().isEmpty());

    const QVariantMap second{
        {QStringLiteral("name"), QStringLiteral("Act One")},
        {QStringLiteral("startMs"), 9876},
        {QStringLiteral("artwork"), QVariantMap{
             {QStringLiteral("connectionId"), QString()},
             {QStringLiteral("itemId"), QStringLiteral("ep-2")},
             {QStringLiteral("kind"), QStringLiteral("chapter")},
             {QStringLiteral("index"), 1},
             {QStringLiteral("tag"), QString()}
         }}
    };
    emit service->canonicalChaptersLoaded(QString(), QStringLiteral("ep-2"), QVariantList{second});

    QCOMPARE(vm.focusedEpisodeChapters().size(), 1);
    const QVariantMap chapter = vm.focusedEpisodeChapters().first().toMap();
    QCOMPARE(chapter.value(QStringLiteral("name")).toString(), QStringLiteral("Act One"));
    QCOMPARE(chapter.value(QStringLiteral("startMs")).toLongLong(), 9876LL);
    QCOMPARE(chapter.value(QStringLiteral("thumbnailUrl")).toString(),
             QStringLiteral("chapter://ep-2/1/480"));
    QVERIFY(!vm.focusedEpisodeChaptersLoading());

    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-1"));
    QCOMPARE(service->requests.size(), 2);
    QCOMPARE(vm.focusedEpisodeChapters().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Cold Open"));
}

void SimilarItemsRetryTest::seriesEpisodeChaptersFailureClearsVisibleState()
{
    ServiceLocator::clear();
    auto *service = new EpisodeChaptersLibraryService(this);
    ServiceLocator::registerService<LibraryService>(service);

    SeriesDetailsViewModel vm;
    vm.loadFocusedEpisodeChapters(QStringLiteral("ep-9"));
    emit service->chaptersFailed(QString(),
                                 QStringLiteral("ep-9"),
                                 QStringLiteral("network"));

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

void SimilarItemsRetryTest::mockHomeSignalsHaveCanonicalParity()
{
    MockLibraryService service;
    QSignalSpy nextUpSpy(&service, &LibraryService::canonicalNextUpLoaded);
    QSignalSpy latestSpy(&service, &LibraryService::canonicalLatestMediaLoaded);
    QSignalSpy backdropSpy(&service, &LibraryService::canonicalHomeBackdropItemsLoaded);
    QSignalSpy heroSpy(&service, &LibraryService::canonicalHeroLibraryItemsLoaded);
    QSignalSpy overviewSpy(&service, &LibraryService::canonicalHeroSeriesOverviewsLoaded);
    QSignalSpy searchSpy(&service, &LibraryService::canonicalSearchResultsLoaded);
    QSignalSpy randomSpy(&service, &LibraryService::canonicalRandomItemsLoaded);
    QSignalSpy screensaverSpy(&service, &LibraryService::canonicalScreensaverItemsLoaded);

    QCOMPARE(service.getActiveConnectionId(), QStringLiteral("mock-connection"));
    service.getNextUp();
    service.getLatestMedia(QStringLiteral("library-1"));
    service.getHomeBackdropItems(80);
    service.getHeroLibraryItems(10, {}, false);
    service.getHeroSeriesOverviews({});
    service.search(QStringLiteral(" test "));
    service.getRandomItems(20);
    service.getScreensaverItems(80);

    QCOMPARE(nextUpSpy.count(), 1);
    QCOMPARE(latestSpy.count(), 1);
    QCOMPARE(backdropSpy.count(), 1);
    QCOMPARE(heroSpy.count(), 1);
    QCOMPARE(overviewSpy.count(), 1);
    QCOMPARE(searchSpy.count(), 1);
    QCOMPARE(randomSpy.count(), 1);
    QCOMPARE(screensaverSpy.count(), 1);
    QCOMPARE(searchSpy.first().at(0).toString(), QStringLiteral("mock-connection"));
    QCOMPARE(searchSpy.first().at(1).toString(), QStringLiteral("test"));
    QCOMPARE(randomSpy.first().at(0).toString(), QStringLiteral("mock-connection"));
    QCOMPARE(screensaverSpy.first().at(0).toString(), QStringLiteral("mock-connection"));

    const QVariantList randomItems = randomSpy.first().at(1).toList();
    for (const QVariant &value : randomItems) {
        const QVariantMap item = value.toMap();
        QVERIFY(item.contains(QStringLiteral("itemId")));
        QVERIFY(!item.contains(QStringLiteral("Id")));
        QVERIFY(!item.contains(QStringLiteral("ImageTags")));
    }

    const QVariantList screensaverItems = screensaverSpy.first().at(1).toList();
    for (const QVariant &value : screensaverItems) {
        const QVariantMap item = value.toMap();
        QVERIFY(item.contains(QStringLiteral("backdropArtwork")));
        QVERIFY(!item.contains(QStringLiteral("BackdropUrl")));
        QVERIFY(!item.contains(QStringLiteral("LogoUrl")));
    }
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
