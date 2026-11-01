#include "UpNextRecommendationsViewModel.h"

#include "core/ServiceLocator.h"
#include "network/LibraryService.h"
#include "network/MediaRequestService.h"

#include <QRegularExpression>
#include <algorithm>
#include <QVariantMap>
#include <QDebug>

UpNextRecommendationsViewModel::UpNextRecommendationsViewModel(QObject *parent)
    : QObject(parent)
{
    m_libraryService = ServiceLocator::tryGet<LibraryService>();
    m_mediaRequests = ServiceLocator::tryGet<MediaRequestService>();
    m_movieRating = new UserRatingState(m_libraryService, this);

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
        connect(m_libraryService, &LibraryService::canonicalItemsLoadedForConnection,
                this, &UpNextRecommendationsViewModel::onSeasonItemsLoaded);
        connect(m_libraryService, &LibraryService::canonicalBecauseWatchedLoadedForConnection,
                this, &UpNextRecommendationsViewModel::onBecauseWatchedLoaded);
        connect(m_libraryService, &LibraryService::canonicalBecauseWatchedFailedForConnection,
                this, &UpNextRecommendationsViewModel::onBecauseWatchedFailed);
    } else {
        qWarning() << "UpNextRecommendationsViewModel: LibraryService not available";
    }

    if (m_mediaRequests) {
        connect(m_mediaRequests, &MediaRequestService::similarResultsLoaded,
                this, &UpNextRecommendationsViewModel::onRequestSimilarResultsLoaded);
        connect(m_mediaRequests, &MediaRequestService::similarResultsFailed,
                this, &UpNextRecommendationsViewModel::onRequestSimilarResultsFailed);
    }
}

QVariantList UpNextRecommendationsViewModel::items() const
{
    if (!m_itemsDirty) {
        return m_cachedItems;
    }

    m_cachedItems = m_movieMode
        ? selectBecauseWatched(m_libraryItems, m_sourceId, m_limit)
        : mergeRecommendations(m_libraryItems, m_requestItems, m_limit);
    m_itemsDirty = false;
    return m_cachedItems;
}

void UpNextRecommendationsViewModel::loadForSeries(const QString &seriesId, int limit)
{
    if (seriesId.trimmed().isEmpty() || !m_libraryService) {
        clear();
        return;
    }

    if (!m_movieMode && m_sourceId == seriesId && m_loading) {
        return;
    }

    resetRequestState(seriesId, limit);
    setLoading(true);

    m_waitingForLibrary = true;
    m_waitingForSeriesDetails = true;
    m_libraryService->getSimilarItems(m_sourceId, m_limit);
    m_libraryService->getSeriesDetails(m_sourceId);
}

void UpNextRecommendationsViewModel::loadForMovie(const QString &movieId, int limit)
{
    if (movieId.trimmed().isEmpty() || !m_libraryService) {
        clear();
        return;
    }
    if (m_movieMode && m_sourceId == movieId && m_loading) {
        return;
    }

    resetRequestState(movieId, limit);
    m_movieMode = true;
    m_movieRating->setItem(movieId);
    setLoading(true);

    m_waitingForLibrary = true;
    m_libraryService->getBecauseWatchedItems(m_sourceId, m_limit);

    // Fresh details for the hero and the viewer's rating; an unchanged
    // answer would carry neither.
    m_movieDetailsContext = QStringLiteral("UpNextMovie:%1:%2").arg(m_connectionId, m_sourceId);
    m_libraryService->clearItemCacheValidation(m_sourceId);
    m_libraryService->getItem(m_sourceId, m_movieDetailsContext);
}

void UpNextRecommendationsViewModel::clear()
{
    const bool hadItems = !m_libraryItems.isEmpty() || !m_requestItems.isEmpty();
    m_connectionId.clear();
    m_sourceId.clear();
    m_limit = 6;
    m_pendingRequestTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_libraryItems = {};
    m_requestItems = {};
    m_waitingForLibrary = false;
    m_waitingForSeriesDetails = false;
    m_waitingForRequests = false;
    m_movieMode = false;
    m_movieDetailsContext.clear();
    m_movieRating->clear();
    if (!m_movie.isEmpty()) {
        m_movie.clear();
        emit movieChanged();
    }
    m_itemsDirty = true;
    m_cachedItems.clear();
    setLoading(false);
    if (hadItems) {
        emit itemsChanged();
    }
}

void UpNextRecommendationsViewModel::loadUpcomingEpisodes(const QString &seasonId,
                                                          const QString &startEpisodeId,
                                                          int limit)
{
    clearUpcomingEpisodes();
    if (!m_libraryService || seasonId.trimmed().isEmpty() || startEpisodeId.isEmpty()) {
        return;
    }

    m_upcomingConnectionId = m_libraryService->getActiveConnectionId();
    m_upcomingSeasonId = seasonId;
    m_upcomingStartEpisodeId = startEpisodeId;
    m_upcomingLimit = qBound(1, limit, 24);
    // SeriesDetailsViewModel ignores season responses it did not request, so
    // this shares LibraryService's episode cache without moving its selection.
    m_libraryService->getSeasonEpisodes(seasonId);
}

void UpNextRecommendationsViewModel::clearUpcomingEpisodes()
{
    m_upcomingConnectionId.clear();
    m_upcomingSeasonId.clear();
    m_upcomingStartEpisodeId.clear();
    if (!m_upcomingEpisodes.isEmpty()) {
        m_upcomingEpisodes.clear();
        emit upcomingEpisodesChanged();
    }
}

void UpNextRecommendationsViewModel::onSeasonItemsLoaded(const QString &connectionId,
                                                         const QString &parentId,
                                                         const QString &queryKey,
                                                         const QVariantList &items,
                                                         int totalRecordCount)
{
    Q_UNUSED(queryKey)
    Q_UNUSED(totalRecordCount)
    if (m_upcomingSeasonId.isEmpty() || parentId != m_upcomingSeasonId
        || connectionId != m_upcomingConnectionId) {
        return;
    }

    m_upcomingEpisodes = selectUpcomingEpisodes(items, m_upcomingStartEpisodeId, m_upcomingLimit);
    m_upcomingSeasonId.clear();
    emit upcomingEpisodesChanged();
}

QVariantList UpNextRecommendationsViewModel::selectUpcomingEpisodes(const QVariantList &seasonItems,
                                                                    const QString &startEpisodeId,
                                                                    int limit)
{
    QVariantMap start;
    for (const QVariant &value : seasonItems) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == startEpisodeId) {
            start = item;
            break;
        }
    }
    if (start.isEmpty() || limit <= 0) {
        return {};
    }

    const int seasonNumber = start.value(QStringLiteral("parentIndexNumber")).toInt();
    const int startNumber = start.value(QStringLiteral("indexNumber")).toInt();
    QList<QVariantMap> episodes;
    for (const QVariant &value : seasonItems) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("mediaType")).toString() != QStringLiteral("Episode")
            || item.value(QStringLiteral("locationType")).toString() == QStringLiteral("Virtual")
            || item.value(QStringLiteral("parentIndexNumber")).toInt() != seasonNumber) {
            continue;
        }
        const bool isStart = item.value(QStringLiteral("itemId")).toString() == startEpisodeId;
        if (!isStart && item.value(QStringLiteral("indexNumber")).toInt() <= startNumber) {
            continue;
        }
        episodes.append(item);
    }

    std::stable_sort(episodes.begin(), episodes.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("indexNumber")).toInt()
            < b.value(QStringLiteral("indexNumber")).toInt();
    });

    QVariantList result;
    for (const QVariantMap &episode : std::as_const(episodes)) {
        if (result.size() >= limit) {
            break;
        }
        result.append(episode);
    }
    return result;
}

QVariantList UpNextRecommendationsViewModel::mergeRecommendations(const QVariantList &libraryItems,
                                                                  const QVariantList &requestItems,
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
    appendSeries(requestItems);
    return merged;
}

QVariantList UpNextRecommendationsViewModel::selectBecauseWatched(const QVariantList &items,
                                                                 const QString &sourceItemId,
                                                                 int limit)
{
    QVariantList selected;
    QSet<QString> seen;
    for (const QVariant &value : items) {
        if (selected.size() >= limit) {
            break;
        }
        const QVariantMap item = value.toMap();
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        // The app has only movie and series libraries.
        if (!isMovieOrSeriesItem(item) || itemId.isEmpty() || itemId == sourceItemId) {
            continue;
        }
        const QString key = dedupeKey(item);
        if (seen.contains(itemId) || (!key.isEmpty() && seen.contains(key))) {
            continue;
        }
        seen.insert(itemId);
        if (!key.isEmpty()) {
            seen.insert(key);
        }
        selected.append(item);
    }
    return selected;
}

bool UpNextRecommendationsViewModel::isMovieOrSeriesItem(const QVariantMap &item)
{
    return isSeriesItem(item)
        || item.value(QStringLiteral("mediaType")).toString()
               .compare(QStringLiteral("Movie"), Qt::CaseInsensitive) == 0;
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

    const int requestTmdb = item.value(QStringLiteral("requestTmdbId"), -1).toInt();
    if (requestTmdb > 0) {
        return QStringLiteral("tmdb:%1").arg(requestTmdb);
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
    if (!m_waitingForLibrary && !m_waitingForSeriesDetails && !m_waitingForRequests) {
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
    m_sourceId = seriesId;
    m_limit = qBound(1, limit, 24);
    m_movieMode = false;
    m_movieDetailsContext.clear();
    m_movieRating->clear();
    if (!m_movie.isEmpty()) {
        m_movie.clear();
        emit movieChanged();
    }
    m_pendingRequestTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_libraryItems = {};
    m_requestItems = {};
    m_waitingForLibrary = false;
    m_waitingForSeriesDetails = false;
    m_waitingForRequests = false;
    m_itemsDirty = true;
    m_cachedItems.clear();
    emit itemsChanged();
}

void UpNextRecommendationsViewModel::requestSimilarTitlesIfPossible(const QVariantMap &seriesData)
{
    m_waitingForSeriesDetails = false;

    if (!m_mediaRequests || !m_mediaRequests->isAvailable()) {
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

    m_pendingRequestTmdbId = tmdbId;
    m_waitingForRequests = true;
    m_mediaRequests->getSimilar(QStringLiteral("tv"), tmdbId);
}

void UpNextRecommendationsViewModel::requestSeriesDetailsFallback()
{
    if (!m_libraryService || m_sourceId.isEmpty()) {
        m_waitingForSeriesDetails = false;
        finishProviderIfComplete();
        return;
    }

    m_seriesDetailsFallbackContext = QStringLiteral("UpNextRecommendations:%1:%2")
                                         .arg(m_connectionId, m_sourceId);
    m_libraryService->clearItemCacheValidation(m_sourceId);
    m_libraryService->getItem(m_sourceId, m_seriesDetailsFallbackContext);
}

bool UpNextRecommendationsViewModel::matchesSeriesDetailsFallback(const QString &itemId,
                                                                  const QString &requestContext) const
{
    return m_waitingForSeriesDetails
        && itemId == m_sourceId
        && requestContext == m_seriesDetailsFallbackContext
        && !m_seriesDetailsFallbackContext.isEmpty();
}

void UpNextRecommendationsViewModel::onSimilarItemsLoaded(const QString &connectionId,
                                                           const QString &itemId,
                                                           const QVariantList &items)
{
    if (m_movieMode || connectionId != m_connectionId || itemId != m_sourceId
        || !m_waitingForLibrary) {
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
    if (m_movieMode || connectionId != m_connectionId || itemId != m_sourceId
        || !m_waitingForLibrary) {
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
    if (connectionId != m_connectionId || seriesId != m_sourceId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSimilarTitlesIfPossible(seriesData);
}

void UpNextRecommendationsViewModel::onSeriesDetailsNotModified(const QString &connectionId,
                                                                 const QString &seriesId)
{
    if (connectionId != m_connectionId || seriesId != m_sourceId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSeriesDetailsFallback();
}

void UpNextRecommendationsViewModel::onSeriesDetailsFailed(const QString &connectionId,
                                                            const QString &seriesId,
                                                            const QString &error)
{
    if (connectionId != m_connectionId || seriesId != m_sourceId || !m_waitingForSeriesDetails) {
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
    if (m_movieMode && !m_movieDetailsContext.isEmpty()
        && requestContext == m_movieDetailsContext && itemId == m_sourceId) {
        if (itemData.value(QStringLiteral("connectionId")).toString() != m_connectionId) {
            return;
        }
        m_movieDetailsContext.clear();
        m_movie = itemData;
        emit movieChanged();
        m_movieRating->updateFromItem(itemData);
        return;
    }
    if (!matchesSeriesDetailsFallback(itemId, requestContext)
            || itemData.value(QStringLiteral("connectionId")).toString() != m_connectionId) {
        return;
    }

    m_seriesDetailsFallbackContext.clear();
    requestSimilarTitlesIfPossible(itemData);
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

void UpNextRecommendationsViewModel::onRequestSimilarResultsLoaded(const QString &mediaType,
                                                                 int tmdbId,
                                                                 const QVariantList &results)
{
    if (!m_waitingForRequests
            || mediaType != QStringLiteral("tv")
            || tmdbId != m_pendingRequestTmdbId) {
        return;
    }

    m_requestItems = results;
    m_waitingForRequests = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onRequestSimilarResultsFailed(const QString &mediaType,
                                                                 int tmdbId,
                                                                 const QString &error)
{
    if (!m_waitingForRequests
            || mediaType != QStringLiteral("tv")
            || tmdbId != m_pendingRequestTmdbId) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel request recommendations failed:" << error;
    m_requestItems = {};
    m_waitingForRequests = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onBecauseWatchedLoaded(const QString &connectionId,
                                                            const QString &itemId,
                                                            const QVariantList &items)
{
    if (!m_movieMode || connectionId != m_connectionId || itemId != m_sourceId
        || !m_waitingForLibrary) {
        return;
    }

    m_libraryItems = items;
    m_waitingForLibrary = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onBecauseWatchedFailed(const QString &connectionId,
                                                            const QString &itemId,
                                                            const QString &error)
{
    if (!m_movieMode || connectionId != m_connectionId || itemId != m_sourceId
        || !m_waitingForLibrary) {
        return;
    }

    // Silent on the after-movie screen: the shelf just stays away.
    qWarning() << "UpNextRecommendationsViewModel because-watched failed:" << error;
    m_libraryItems = {};
    m_waitingForLibrary = false;
    finishProviderIfComplete();
}
