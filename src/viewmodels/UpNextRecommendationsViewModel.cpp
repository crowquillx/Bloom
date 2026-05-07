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
        connect(m_libraryService, &LibraryService::similarItemsLoaded,
                this, &UpNextRecommendationsViewModel::onSimilarItemsLoaded);
        connect(m_libraryService, &LibraryService::similarItemsFailed,
                this, &UpNextRecommendationsViewModel::onSimilarItemsFailed);
        connect(m_libraryService, &LibraryService::seriesDetailsLoaded,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsLoaded);
        connect(m_libraryService, &LibraryService::seriesDetailsNotModified,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsNotModified);
        connect(m_libraryService,
                qOverload<const QString &, const QJsonObject &, const QString &>(&LibraryService::itemLoaded),
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackLoaded);
        connect(m_libraryService,
                qOverload<const QString &, const QString &>(&LibraryService::itemNotModified),
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackNotModified);
        connect(m_libraryService, &LibraryService::itemFailed,
                this, &UpNextRecommendationsViewModel::onSeriesDetailsFallbackFailed);
        connect(m_libraryService, &LibraryService::errorOccurred,
                this, &UpNextRecommendationsViewModel::onLibraryErrorOccurred);
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
    QVariantList list;
    list.reserve(m_jellyfinItems.size() + m_seerrItems.size());
    const QJsonArray merged = mergeRecommendations(m_jellyfinItems, m_seerrItems, m_limit);
    for (const QJsonValue &value : merged) {
        list.append(value.toObject().toVariantMap());
    }
    return list;
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

    m_waitingForJellyfin = true;
    m_libraryService->getSimilarItems(m_seriesId, m_limit);

    m_waitingForSeriesDetails = true;
    m_libraryService->getSeriesDetails(m_seriesId);
}

void UpNextRecommendationsViewModel::clear()
{
    const bool hadItems = !m_jellyfinItems.isEmpty() || !m_seerrItems.isEmpty();
    m_seriesId.clear();
    m_limit = 6;
    m_pendingSeerrTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_jellyfinItems = {};
    m_seerrItems = {};
    m_waitingForJellyfin = false;
    m_waitingForSeriesDetails = false;
    m_waitingForSeerr = false;
    setLoading(false);
    if (hadItems) {
        emit itemsChanged();
    }
}

QJsonArray UpNextRecommendationsViewModel::mergeRecommendations(const QJsonArray &jellyfinItems,
                                                                const QJsonArray &seerrItems,
                                                                int limit)
{
    QJsonArray merged;
    QSet<QString> seen;
    const int cappedLimit = qMax(0, limit);

    const auto appendSeries = [&merged, &seen, cappedLimit](const QJsonArray &items) {
        for (const QJsonValue &value : items) {
            if (cappedLimit > 0 && merged.size() >= cappedLimit) {
                return;
            }

            const QJsonObject item = value.toObject();
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

    appendSeries(jellyfinItems);
    appendSeries(seerrItems);
    return merged;
}

bool UpNextRecommendationsViewModel::isSeriesItem(const QJsonObject &item)
{
    const QString type = item.value(QStringLiteral("Type")).toString();
    const QString seerrType = item.value(QStringLiteral("SeerrMediaType")).toString().toLower();
    return type.compare(QStringLiteral("Series"), Qt::CaseInsensitive) == 0
        || seerrType == QStringLiteral("tv");
}

QString UpNextRecommendationsViewModel::dedupeKey(const QJsonObject &item)
{
    const QJsonObject providerIds = item.value(QStringLiteral("ProviderIds")).toObject();
    const QString tmdb = providerIds.value(QStringLiteral("Tmdb")).toVariant().toString().trimmed();
    if (!tmdb.isEmpty()) {
        return QStringLiteral("tmdb:%1").arg(tmdb);
    }

    const QString imdb = providerIds.value(QStringLiteral("Imdb")).toVariant().toString().trimmed().toLower();
    if (!imdb.isEmpty()) {
        return QStringLiteral("imdb:%1").arg(imdb);
    }

    const int seerrTmdb = item.value(QStringLiteral("SeerrTmdbId")).toInt(-1);
    if (seerrTmdb > 0) {
        return QStringLiteral("tmdb:%1").arg(seerrTmdb);
    }

    const QString title = normalizedTitle(item.value(QStringLiteral("Name")).toString());
    if (title.isEmpty()) {
        return {};
    }

    const int year = itemYear(item);
    return QStringLiteral("title:%1:%2").arg(title).arg(year > 0 ? QString::number(year) : QString());
}

QString UpNextRecommendationsViewModel::normalizedTitle(const QString &title)
{
    QString normalized = title.toLower().trimmed();
    normalized.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    normalized = normalized.simplified();
    return normalized;
}

int UpNextRecommendationsViewModel::itemYear(const QJsonObject &item)
{
    const int productionYear = item.value(QStringLiteral("ProductionYear")).toInt(0);
    if (productionYear > 0) {
        return productionYear;
    }

    const QString premiereDate = item.value(QStringLiteral("PremiereDate")).toString();
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
    if (!m_waitingForJellyfin && !m_waitingForSeriesDetails && !m_waitingForSeerr) {
        notifyItemsChanged();
        setLoading(false);
    }
}

void UpNextRecommendationsViewModel::notifyItemsChanged()
{
    emit itemsChanged();
}

void UpNextRecommendationsViewModel::resetRequestState(const QString &seriesId, int limit)
{
    m_seriesId = seriesId;
    m_limit = qBound(1, limit, 24);
    m_pendingSeerrTmdbId = -1;
    m_seriesDetailsFallbackContext.clear();
    m_jellyfinItems = {};
    m_seerrItems = {};
    m_waitingForJellyfin = false;
    m_waitingForSeriesDetails = false;
    m_waitingForSeerr = false;
    emit itemsChanged();
}

void UpNextRecommendationsViewModel::requestSeerrSimilarIfPossible(const QJsonObject &seriesData)
{
    m_waitingForSeriesDetails = false;

    if (!m_seerrService || !m_seerrService->isConfigured()) {
        finishProviderIfComplete();
        return;
    }

    const QJsonObject providerIds = seriesData.value(QStringLiteral("ProviderIds")).toObject();
    bool ok = false;
    const int tmdbId = providerIds.value(QStringLiteral("Tmdb")).toVariant().toString().toInt(&ok);
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

void UpNextRecommendationsViewModel::onSimilarItemsLoaded(const QString &itemId, const QJsonArray &items)
{
    if (itemId != m_seriesId || !m_waitingForJellyfin) {
        return;
    }

    m_jellyfinItems = items;
    m_waitingForJellyfin = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSimilarItemsFailed(const QString &itemId, const QString &error)
{
    if (itemId != m_seriesId || !m_waitingForJellyfin) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel Jellyfin recommendations failed:" << error;
    m_jellyfinItems = {};
    m_waitingForJellyfin = false;
    finishProviderIfComplete();
}

void UpNextRecommendationsViewModel::onSeriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData)
{
    if (seriesId != m_seriesId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSeerrSimilarIfPossible(seriesData);
}

void UpNextRecommendationsViewModel::onSeriesDetailsNotModified(const QString &seriesId)
{
    if (seriesId != m_seriesId || !m_waitingForSeriesDetails) {
        return;
    }

    requestSeriesDetailsFallback();
}

void UpNextRecommendationsViewModel::onSeriesDetailsFallbackLoaded(const QString &itemId,
                                                                   const QJsonObject &itemData,
                                                                   const QString &requestContext)
{
    if (!matchesSeriesDetailsFallback(itemId, requestContext)) {
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

    m_seerrItems = results;
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

void UpNextRecommendationsViewModel::onLibraryErrorOccurred(const QString &endpoint, const QString &error)
{
    if (!m_waitingForSeriesDetails || !m_seriesDetailsFallbackContext.isEmpty()) {
        return;
    }

    const bool isSeriesDetailsError = endpoint == QStringLiteral("getSeriesDetails")
        || endpoint.contains(QStringLiteral("/Items/%1").arg(m_seriesId));
    if (!isSeriesDetailsError) {
        return;
    }

    qWarning() << "UpNextRecommendationsViewModel series details failed:" << error;
    m_waitingForSeriesDetails = false;
    finishProviderIfComplete();
}
