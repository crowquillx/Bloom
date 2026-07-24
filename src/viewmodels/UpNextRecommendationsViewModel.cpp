#include "UpNextRecommendationsViewModel.h"

#include "core/ServiceLocator.h"
#include "network/LibraryService.h"
#include "network/SeerrService.h"

#include <QRegularExpression>
#include <QVariantMap>
#include <QDebug>

UpNextRecommendationsViewModel::UpNextRecommendationsViewModel(QObject *parent)
    : QObject(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_seerrService = ServiceLocator::tryGet<SeerrService>();

    if (m_libraryService) {
        connect(m_libraryService, &LibraryService::canonicalSimilarItemsLoadedForConnection,
                this, &UpNextRecommendationsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::canonicalSimilarItemsFailedForConnection,
                this, &UpNextRecommendationsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::canonicalSeriesDetailsLoaded,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsLoaded);
        connect(m_libraryService, &LibraryService::canonicalSeriesDetailsNotModified,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsNotModified);
        connect(m_libraryService, &LibraryService::canonicalSeriesDetailsFailed,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFailed);
        connect(m_libraryService,
                qOverload<const QString &, const QVariantMap &, const QString &>(&LibraryService::canonicalItemLoaded),
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackLoaded);
        connect(m_libraryService,
                qOverload<const QString &, const QString &>(&LibraryService::itemNotModified),
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackNotModified);
        connect(m_libraryService, &LibraryService::itemFailed,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackFailed);
    } else {
        qWarning() << "UpNextRecommendationsViewModel: LibraryService not available";
    }

    if (m_seerrService) {
        connect(m_seerrService, &SeerrService::similarResultsLoaded,
                this, &UpNextRecommendationsViewModel::onSeerrSimilarResultsLoaded);
        connect(m_seerrService, &SeerrService::similarResultsFailed,
                this, &UpNextRecommendationsViewModel::onSeerrSimilarResultsFailed);
    }
}

QVariantList UpNextRecommendationsViewModel::items() const
{
    if (!m_itemsDirty) {
        return m_cachedItems;
    }

    m_cachedItems = mergeRecommendations(m_libraryItems, m_seerrItems, m_limit);
    m_itemsDirty = false;
    return m_cachedItems;
}

void UpNextRecommendationsViewModel::loadForSeries(const QString &seriesId, int limit)
{
    if (seriesId.trimmed().isEmpty() || !m_libraryService) {
        clear();
        return;
    }

    if (m_seriesId == seriesId && m_loading) {
        return;
    }

    resetRequestState(seriesId, limit);
    setLoading(true);

    m_waitingForLibrary = true;
    m_libraryService->getSimilarItems(m_seriesId, m_limit);

    m_waitingForSeriesDetails = true;
    m_libraryService->getSeriesDetails(m_seriesId);
}

void UpNextRecommendationsViewModel::clear()
{
    const bool hadItems = !m_libraryItems.isEmpty() || !m_seerrItems.isEmpty();
    m_connectionId.clear();
    m_seriesId.clear();
    m_limit = 6;
    m_pendingSeerrTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_libraryItems = {};
    m_seerrItems = {};
    m_waitingForLibrary = false;
    m_waitingForSeriesDetails = false;
    m_waitingForSeerr = false;
    m_itemsDirty = true;
    m_cachedItems.clear();
    setLoading(false);
    if (hadItems) {
        emit itemsChanged();
    }
}

QVariantList UpNextRecommendationsViewModel::mergeRecommendations(const QVariantList &libraryItems,
                                                                  const QVariantList &seerrItems,
                                                                  int limit)
{
    QVariantList merged;
    QSet<QString> seen;
    const int cappedLimit = qMax(0, limit);

    const auto appendSeries = [&merged, &seen, cappedLimit](const QVariantList &items) {
        for (const QVariant &value : items) {
            if (merged.size() >= cappedLimit) {
                return;
            }

            const QVariantMap item = value.toMap();
            if (!isSeriesItem(item)) {
                continue;
            }

            const QString key = dedupeKey(item);
            if (!key.isEmpty() && seen.contains(key)) {
                continue;
            }

            if (!key.isEmpty()) {
                seen.insert(key);
            }
            merged.append(item);
        }
    };

    appendSeries(libraryItems);
    appendSeries(seerrItems);
    return merged;
}

bool UpNextRecommendationsViewModel::isSeriesItem(const QVariantMap &item)
{
    return item.value(QStringLiteral("mediaType")).toString()
               .compare(QStringLiteral("Series"), Qt::CaseInsensitive) == 0;
}

QString UpNextRecommendationsViewModel::dedupeKey(const QVariantMap &item)
{
    const QVariantMap providerIds = item.value(QStringLiteral("providerIds")).toMap();
    const QString tmdb = providerIds.value(QStringLiteral("Tmdb")).toString().trimmed();
    if (!tmdb.isEmpty()) {
        return QStringLiteral("tmdb:%1").arg(tmdb);
    }

    const QString imdb = providerIds.value(QStringLiteral("Imdb")).toString().trimmed().toLower();
    if (!imdb.isEmpty()) {
        return QStringLiteral("imdb:%1").arg(imdb);
    }

    const int seerrTmdb = item.value(QStringLiteral("seerrTmdbId"), -1).toInt();
    if (seerrTmdb > 0) {
        return QStringLiteral("tmdb:%1").arg(seerrTmdb);
    }

    const QString title = normalizedTitle(item.value(QStringLiteral("name")).toString());
    if (title.isEmpty()) {
        return {};
    }

    const int year = itemYear(item);
    return QStringLiteral("title:%1:%2").arg(title).arg(year > 0 ? QString::number(year) : QString());
}

QString UpNextRecommendationsViewModel::normalizedTitle(const QString &title)
{
    static const QRegularExpression kNonAlnumRegex(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QString normalized = title.toLower().trimmed();
    normalized.replace(kNonAlnumRegex, QStringLiteral(" "));
    normalized = normalized.simplified();
    return normalized;
}

int UpNextRecommendationsViewModel::itemYear(const QVariantMap &item)
{
    const int productionYear = item.value(QStringLiteral("productionYear")).toInt();
    if (productionYear > 0) {
        return productionYear;
    }

    const QString premiereDate = item.value(QStringLiteral("premiereDate")).toString();
    bool ok = false;
    const int year = premiereDate.left(4).toInt(&ok);
    return ok ? year : 0;
}

void UpNextRecommendationsViewModel::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void UpNextRecommendationsViewModel::finishProviderIfComplete()
{
    if (!m_waitingForLibrary && !m_waitingForSeriesDetails && !m_waitingForSeerr) {
        notifyItemsChanged();
        setLoading(false);
    }
}

void UpNextRecommendationsViewModel::notifyItemsChanged()
{
    m_itemsDirty = true;
    emit itemsChanged();
}

void UpNextRecommendationsViewModel::resetRequestState(const QString &seriesId, int limit)
{
    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    m_seriesId = seriesId;
    m_limit = qBound(1, limit, 24);
    m_pendingSeerrTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_libraryItems = {};
    m_seerrItems = {};
    m_waitingForLibrary = false;
    m_waitingForSeriesDetails = false;
    m_waitingForSeerr = false;
    m_itemsDirty = true;
    m_cachedItems.clear();
    emit itemsChanged();
}

void UpNextRecommendationsViewModel::requestSeerrSimilarIfPossible(const QVariantMap &seriesData)
{
    m_waitingForSeriesDetails = false;

    if (!m_seerrService || !m_seerrService->isConfigured()) {
        finishProviderIfComplete();
        return;
    }

    const QVariantMap providerIds = seriesData.value(QStringLiteral("providerIds")).toMap();
    bool ok = false;
    const int tmdbId = providerIds.value(QStringLiteral("Tmdb")).toString().toInt(&ok);
    if (!ok || tmdbId <= 0) {
        finishProviderIfComplete();
        return;
    }

    m_pendingSeerrTmdbId = tmdbId;
    m_waitingForSeerr = true;
    m_seerrService->getSimilar(QStringLiteral("tv"), tmdbId, 1);
}

void UpNextRecommendationsViewModel::requestSeriesDetailsFallback()
{
    if (!m_libraryService || m_seriesId.isEmpty()) {
        m_waitingForSeriesDetails = false;
        finishProviderIfComplete();
        return;
    }

    m_seriesDetailsFallbackContext = QStringLiteral("UpNextRecommendations:%1").arg(m_seriesId);
    m_libraryService->clearItemCacheValidation(m_seriesId);
    m_libraryService->getItem(m_seriesId, m_seriesDetailsFallbackContext);
}

bool UpNextRecommendationsViewModel::matchesSeriesDetailsFallback(const QString &itemId,
                                                                  const QString &requestContext) const
{
    return m_waitingForSeriesDetails
        && itemId == m_seriesId
        && requestContext == m_seriesDetailsFallbackContext
        && !m_seriesDetailsFallbackContext.isEmpty();
}

void UpNextRecommendationsViewModel::onSimilarItemsLoaded(const QString &connectionId,
                                                           const QString &itemId,
                                                           const QVariantList &items)
{
    if (connectionId != m_connectionId || itemId != m_seriesId || !m_waitingForLibrary) {
        return;
    }

    m_libraryItems = items;
    m_waitingForLibrary = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSimilarItemsFailed(const QString &connectionId,
                                                           const QString &itemId,
                                                           const QString &error)
{
    if (connectionId != m_connectionId || itemId != m_seriesId || !m_waitingForLibrary) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel library recommendations failed:" << error;
    m_libraryItems = {};
    m_waitingForLibrary = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeriesDetailsLoaded(const QString &connectionId,
                                                            const QString &seriesId,
                                                            const QVariantMap &seriesData)
{
    if (connectionId != m_connectionId || seriesId != m_seriesId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSeerrSimilarIfPossible(seriesData);
}

void UpNextRecommendationsViewModel::onSeriesDetailsNotModified(const QString &connectionId,
                                                                 const QString &seriesId)
{
    if (connectionId != m_connectionId || seriesId != m_seriesId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSeriesDetailsFallback();
}

void UpNextRecommendationsViewModel::onSeriesDetailsFailed(const QString &connectionId,
                                                            const QString &seriesId,
                                                            const QString &error)
{
    if (connectionId != m_connectionId || seriesId != m_seriesId || !m_waitingForSeriesDetails) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel series details failed:" << error;
    m_waitingForSeriesDetails = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeriesDetailsFallbackLoaded(const QString &itemId,
                                                                   const QVariantMap &itemData,
                                                                   const QString &requestContext)
{
    if (!matchesSeriesDetailsFallback(itemId, requestContext)
            || itemData.value(QStringLiteral("connectionId")).toString() != m_connectionId) {
        return;
    }

    m_seriesDetailsFallbackContext.clear();
    requestSeerrSimilarIfPossible(itemData);
}

void UpNextRecommendationsViewModel::onSeriesDetailsFallbackNotModified(const QString &itemId,
                                                                        const QString &requestContext)
{
    if (!matchesSeriesDetailsFallback(itemId, requestContext)) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel series details fallback returned not modified";
    m_seriesDetailsFallbackContext.clear();
    m_waitingForSeriesDetails = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeriesDetailsFallbackFailed(const QString &itemId,
                                                                   const QString &error,
                                                                   const QString &requestContext)
{
    if (!matchesSeriesDetailsFallback(itemId, requestContext)) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel series details fallback failed:" << error;
    m_seriesDetailsFallbackContext.clear();
    m_waitingForSeriesDetails = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeerrSimilarResultsLoaded(const QString &mediaType,
                                                                 int tmdbId,
                                                                 const QJsonArray &results)
{
    if (!m_waitingForSeerr
            || mediaType != QStringLiteral("tv")
            || tmdbId != m_pendingSeerrTmdbId) {
        return;
    }

    m_seerrItems = results.toVariantList();
    m_waitingForSeerr = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeerrSimilarResultsFailed(const QString &mediaType,
                                                                 int tmdbId,
                                                                 const QString &error)
{
    if (!m_waitingForSeerr
            || mediaType != QStringLiteral("tv")
            || tmdbId != m_pendingSeerrTmdbId) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel Seerr recommendations failed:" << error;
    m_seerrItems = {};
    m_waitingForSeerr = false;
    finishProviderIfComplete();
}
