#include <QtTest/QtTest>

#include "../src/viewmodels/MovieDetailsViewModel.h"
#include "../src/viewmodels/SeriesDetailsViewModel.h"

#include "../src/core/ServiceLocator.h"
#include "../src/network/LibraryService.h"

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

class SimilarItemsRetryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void movieSimilarItemsFailureAllowsRetry();
    void seriesSimilarItemsFailureAllowsRetry();

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

QTEST_MAIN(SimilarItemsRetryTest)
#include "SimilarItemsRetryTest.moc"
