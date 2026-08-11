#include "ImageCacheProvider.h"
#include "ImageCacheStore.h"
#include "providers/IArtworkProvider.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImageReader>
#include <QBuffer>
#include <QThread>
#include <QtConcurrent>
#include <QPainter>
#include <QPainterPath>
#include <QImageWriter>
#include <QPointer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <utility>
#include "../utils/BloomLogging.h"

namespace {

struct DecodedImage {
    QImage image;
    QString error;
    qint64 latencyMs = 0;
};

bool exceedsDecodedLimit(const QSize &size, qint64 maximumBytes)
{
    if (!size.isValid() || size.isEmpty()) {
        return false;
    }
    const qint64 pixels = qint64(size.width()) * size.height();
    return pixels <= 0 || pixels > maximumBytes / 4;
}

DecodedImage decodeImage(QImageReader &reader,
                         const QSize &requestedSize,
                         qint64 maximumDecodedBytes)
{
    if (exceedsDecodedLimit(requestedSize, maximumDecodedBytes)) {
        return {{}, QStringLiteral("Requested image size exceeds memory limit"), 0};
    }

    const QSize sourceSize = reader.size();
    if ((!requestedSize.isValid() || requestedSize.isEmpty())
        && exceedsDecodedLimit(sourceSize, maximumDecodedBytes)) {
        return {{}, QStringLiteral("Decoded image exceeds memory limit"), 0};
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        reader.setScaledSize(requestedSize);
    }

    QElapsedTimer timer;
    timer.start();
    QImage image = reader.read();
    const qint64 latencyMs = timer.elapsed();
    if (image.isNull()) {
        return {{}, QStringLiteral("Failed to decode image: %1").arg(reader.errorString()),
                latencyMs};
    }
    if (image.sizeInBytes() > maximumDecodedBytes) {
        return {{}, QStringLiteral("Decoded image exceeds memory limit"), latencyMs};
    }
    return {std::move(image), {}, latencyMs};
}

DecodedImage decodeFile(const QString &path,
                        const QSize &requestedSize,
                        qint64 maximumDecodedBytes)
{
    QImageReader reader(path);
    return decodeImage(reader, requestedSize, maximumDecodedBytes);
}

DecodedImage decodeBytes(const QByteArray &data,
                         const QSize &requestedSize,
                         qint64 maximumDecodedBytes)
{
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {{}, QStringLiteral("Unable to open downloaded image"), 0};
    }
    QImageReader reader(&buffer);
    return decodeImage(reader, requestedSize, maximumDecodedBytes);
}

QString imageJobKey(const QString &cacheKey, const QSize &requestedSize)
{
    return QStringLiteral("%1\x1f%2x%3")
        .arg(cacheKey)
        .arg(requestedSize.width())
        .arg(requestedSize.height());
}

int imageCacheCost(const QImage &image)
{
    return int(std::min<qsizetype>(image.sizeInBytes(), std::numeric_limits<int>::max()));
}

} // namespace

class ImageLoadJob final : public QObject
{
public:
    ImageLoadJob(QString jobKey,
                 QString cacheKey,
                 QSize requestedSize,
                 ImageCacheProvider *provider,
                 std::optional<QNetworkRequest> resolvedRequest)
        : QObject(provider)
        , m_jobKey(std::move(jobKey))
        , m_cacheKey(std::move(cacheKey))
        , m_requestedSize(requestedSize)
        , m_provider(provider)
        , m_cacheGeneration(provider->m_cacheGeneration.load())
        , m_resolvedRequest(std::move(resolvedRequest))
    {
        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, [this]() {
            finish({}, QStringLiteral("Image request timed out"));
        });
    }

    void addSubscriber(CachedImageResponse *response)
    {
        m_subscribers.append(QPointer<CachedImageResponse>(response));
    }

    void removeSubscriber(CachedImageResponse *response)
    {
        m_subscribers.removeIf([response](const QPointer<CachedImageResponse> &candidate) {
            return !candidate || candidate.data() == response;
        });
        if (m_subscribers.isEmpty()) {
            finish({}, QStringLiteral("Image request cancelled"));
        }
    }

    void removeExpiredSubscribers()
    {
        m_subscribers.removeIf([](const QPointer<CachedImageResponse> &candidate) {
            return !candidate;
        });
        if (m_subscribers.isEmpty()) {
            finish({}, QStringLiteral("Image request cancelled"));
        }
    }

    void start()
    {
        if (m_finished) {
            return;
        }

        QImage memoryImage;
        {
            QMutexLocker locker(&m_provider->m_memoryCacheMutex);
            if (QImage *cached = m_provider->m_memoryCache.object(m_cacheKey)) {
                memoryImage = *cached;
            }
        }
        if (!memoryImage.isNull()) {
            loadMemoryImage(std::move(memoryImage));
            return;
        }
        loadDiskImage();
    }

    void shutdown()
    {
        finish({}, QStringLiteral("Image provider shutting down"));
    }

    const QString &cacheKey() const
    {
        return m_cacheKey;
    }

private:
    struct DiskResult {
        QImage image;
        QString path;
        QString error;
        qint64 decodeLatencyMs = 0;
        bool found = false;
    };

    void loadMemoryImage(QImage image)
    {
        if (!m_requestedSize.isValid() || m_requestedSize.isEmpty()) {
            ++m_provider->m_imageHits;
            finish(std::move(image), {});
            return;
        }
        if (exceedsDecodedLimit(m_requestedSize,
                                m_provider->m_requestLimits.maximumDecodedBytes)) {
            finish({}, QStringLiteral("Requested image size exceeds memory limit"));
            return;
        }

        const QPointer<ImageLoadJob> guard(this);
        const QSize requestedSize = m_requestedSize;
        auto future = QtConcurrent::run(
            &m_provider->m_threadPool,
            [guard, image = std::move(image), requestedSize]() mutable {
                QElapsedTimer timer;
                timer.start();
                QImage scaled = image.scaled(requestedSize,
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
                const qint64 latencyMs = timer.elapsed();
                if (!guard) {
                    return;
                }
                QMetaObject::invokeMethod(
                    guard,
                    [guard, scaled = std::move(scaled), latencyMs]() mutable {
                        if (!guard) {
                            return;
                        }
                        if (guard->m_finished
                            || guard->m_provider->m_cacheGeneration.load()
                                != guard->m_cacheGeneration) {
                            return;
                        }
                        guard->recordDecode(scaled, latencyMs);
                        if (scaled.isNull()
                            || scaled.sizeInBytes()
                                > guard->m_provider->m_requestLimits.maximumDecodedBytes) {
                            guard->finish({}, QStringLiteral("Decoded image exceeds memory limit"));
                            return;
                        }
                        ++guard->m_provider->m_imageHits;
                        guard->finish(std::move(scaled), {});
                    },
                    Qt::QueuedConnection);
            });
        Q_UNUSED(future);
    }

    void loadDiskImage()
    {
        ImageCacheStore *store = m_provider->m_store.get();
        const QString cacheKey = m_cacheKey;
        const QSize requestedSize = m_requestedSize;
        const qint64 maximumDecodedBytes =
            m_provider->m_requestLimits.maximumDecodedBytes;
        const QPointer<ImageLoadJob> guard(this);
        auto future = QtConcurrent::run(
            &m_provider->m_threadPool,
            [guard, store, cacheKey, requestedSize, maximumDecodedBytes]() {
                DiskResult result;
                if (!store) {
                    return result;
                }
                const ImageCacheStore::LookupResult entry = store->lookupEntry(cacheKey);
                if (!entry.isValid()) {
                    return result;
                }
                result.found = true;
                result.path = entry.path;
                DecodedImage decoded = decodeFile(
                    entry.path, requestedSize, maximumDecodedBytes);
                result.image = std::move(decoded.image);
                result.error = std::move(decoded.error);
                result.decodeLatencyMs = decoded.latencyMs;
                if (result.image.isNull()) {
                    store->invalidateIfCurrent(cacheKey, entry.revision);
                } else {
                    store->touch(cacheKey);
                }
                if (!guard) {
                    return DiskResult{};
                }
                return result;
            });
        auto *watcher = new QFutureWatcher<DiskResult>(this);
        connect(watcher, &QFutureWatcher<DiskResult>::finished, this,
                [this, watcher]() {
                    const DiskResult result = watcher->result();
                    watcher->deleteLater();
                    if (m_finished
                        || m_provider->m_cacheGeneration.load() != m_cacheGeneration) {
                        return;
                    }
                    if (!result.image.isNull()) {
                        recordDecode(result.image, result.decodeLatencyMs);
                        ++m_provider->m_imageHits;
                        cacheOriginalImage(result.image);
                        scheduleRounded(result.path);
                        qCDebug(lcImageCache) << "Cache hit:"
                                              << m_provider->safeCacheLabel(m_cacheKey);
                        finish(result.image, {});
                        return;
                    }
                    if (result.found) {
                        recordDecode({}, result.decodeLatencyMs);
                        qCWarning(lcImageCache)
                            << "Cached image decode failed:"
                            << m_provider->safeCacheLabel(m_cacheKey)
                            << result.error;
                    }
                    beginNetworkLoad();
                });
        watcher->setFuture(future);
    }

    void beginNetworkLoad()
    {
        if (m_finished) {
            return;
        }
        if (!m_resolvedRequest.has_value()) {
            finish({}, QStringLiteral("Unable to resolve image request"));
            return;
        }
        ++m_provider->m_networkLoads;
        startNetworkAttempt(*m_resolvedRequest);
    }

    void startNetworkAttempt(QNetworkRequest request)
    {
        if (m_finished) {
            return;
        }
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                             QNetworkRequest::PreferNetwork);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Bloom/1.0"));

        m_networkData.clear();
        m_reply = m_provider->networkManager()->get(request);
        m_reply->setReadBufferSize(m_provider->m_requestLimits.maximumNetworkBytes + 1);
        connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
            readNetworkData();
        });
        connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() {
            const qint64 declaredSize = m_reply
                ? m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()
                : 0;
            if (declaredSize > m_provider->m_requestLimits.maximumNetworkBytes) {
                finish({}, QStringLiteral("Image response exceeds size limit"));
            }
        });
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            networkFinished();
        });
        m_deadline.start(m_provider->m_requestLimits.networkDeadlineMs);
    }

    void readNetworkData()
    {
        if (!m_reply || m_finished) {
            return;
        }
        m_networkData += m_reply->readAll();
        if (m_networkData.size() > m_provider->m_requestLimits.maximumNetworkBytes) {
            finish({}, QStringLiteral("Image response exceeds size limit"));
        }
    }

    void networkFinished()
    {
        if (!m_reply || m_finished) {
            return;
        }
        m_deadline.stop();
        readNetworkData();
        if (m_finished || !m_reply) {
            return;
        }

        const int status = m_reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = m_reply->error();
        releaseReply();

        if ((status == 401 || status == 403) && !m_refreshAttempted
            && m_cacheKey.startsWith(QStringLiteral("artwork:"))
            && m_provider->m_artworkProvider) {
            m_refreshAttempted = true;
            refreshArtworkRequest(QStringLiteral("HTTP error: %1").arg(status));
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            finish({}, QStringLiteral("Image network error (%1)")
                           .arg(static_cast<int>(networkError)));
            return;
        }
        if (status >= 400) {
            finish({}, QStringLiteral("HTTP error: %1").arg(status));
            return;
        }
        if (m_networkData.isEmpty()) {
            finish({}, QStringLiteral("Empty response from server"));
            return;
        }
        decodeNetworkImage(std::exchange(m_networkData, {}));
    }

    void refreshArtworkRequest(const QString &networkError)
    {
        Bloom::ArtworkRef artwork = Bloom::ArtworkRef::fromCacheKey(m_cacheKey);
        artwork.sourceUrl = Bloom::ArtworkRef::transientSourceUrlForCacheKey(m_cacheKey);
        if (!artwork.isValid() || !m_provider->m_artworkProvider) {
            finish({}, networkError);
            return;
        }

        m_deadline.start(m_provider->m_requestLimits.networkDeadlineMs);
        const QPointer<ImageLoadJob> guard(this);
        m_provider->m_artworkProvider->refreshArtwork(
            artwork,
            [guard, networkError](std::optional<QNetworkRequest> refreshedRequest) mutable {
                if (!guard) {
                    return;
                }
                QMetaObject::invokeMethod(
                    guard,
                    [guard, networkError,
                     refreshedRequest = std::move(refreshedRequest)]() mutable {
                        if (!guard) {
                            return;
                        }
                        if (!refreshedRequest.has_value()) {
                            guard->finish({}, networkError);
                            return;
                        }
                        guard->startNetworkAttempt(*refreshedRequest);
                    },
                    Qt::QueuedConnection);
            });
    }

    void decodeNetworkImage(QByteArray data)
    {
        ImageCacheStore *store = m_provider->m_store.get();
        const QString cacheKey = m_cacheKey;
        const QSize requestedSize = m_requestedSize;
        const qint64 maximumDecodedBytes =
            m_provider->m_requestLimits.maximumDecodedBytes;
        const QPointer<ImageLoadJob> guard(this);
        ImageCacheProvider *provider = m_provider;
        const quint64 generation = m_cacheGeneration;
        auto future = QtConcurrent::run(
            &m_provider->m_threadPool,
            [guard, store, provider, generation, cacheKey, requestedSize,
             maximumDecodedBytes, data = std::move(data)]() mutable {
                DecodedImage result = decodeBytes(
                    data, requestedSize, maximumDecodedBytes);
                QString path;
                if (!result.image.isNull() && store) {
                    path = provider->saveDataForKeyIfCurrent(
                        cacheKey, data, generation);
                }
                if (!guard) {
                    return;
                }
                QMetaObject::invokeMethod(
                    guard,
                    [guard, result = std::move(result), path = std::move(path)]() mutable {
                        if (!guard) {
                            return;
                        }
                        if (guard->m_finished
                            || guard->m_provider->m_cacheGeneration.load()
                                != guard->m_cacheGeneration) {
                            return;
                        }
                        guard->recordDecode(result.image, result.latencyMs);
                        if (result.image.isNull()) {
                            guard->finish({}, result.error);
                            return;
                        }
                        guard->cacheOriginalImage(result.image);
                        guard->scheduleRounded(path);
                        guard->finish(std::move(result.image), {});
                    },
                    Qt::QueuedConnection);
            });
        Q_UNUSED(future);
    }

    void cacheOriginalImage(const QImage &image)
    {
        if (m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
            return;
        }
        QMutexLocker locker(&m_provider->m_memoryCacheMutex);
        m_provider->m_memoryCache.insert(
            m_cacheKey, new QImage(image), imageCacheCost(image));
    }

    void scheduleRounded(const QString &sourcePath)
    {
        if (!m_provider->m_enableRoundedPreprocess) {
            return;
        }
        if (sourcePath.isEmpty()) {
            m_provider->discardPendingRounded(m_cacheKey);
            return;
        }
        const QSize roundedSize = m_requestedSize.isValid() && !m_requestedSize.isEmpty()
            ? m_requestedSize
            : m_provider->m_defaultRoundedSize;
        m_provider->scheduleRoundedVariant(
            m_cacheKey, sourcePath, m_provider->m_defaultRoundedRadius,
            roundedSize, true);
        m_provider->processPendingRounded(m_cacheKey, sourcePath);
    }

    void recordDecode(const QImage &image, qint64 latencyMs)
    {
        ++m_provider->m_decodeAttempts;
        m_provider->m_totalDecodeLatencyMs.fetch_add(
            static_cast<quint64>(std::max<qint64>(0, latencyMs)));
        if (!image.isNull()) {
            ++m_provider->m_decodedImages;
        }
    }

    void releaseReply()
    {
        if (!m_reply) {
            return;
        }
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        disconnect(reply, nullptr, this, nullptr);
        reply->deleteLater();
    }

    void finish(QImage image, QString error)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_deadline.stop();
        if (m_reply) {
            disconnect(m_reply, nullptr, this, nullptr);
            m_reply->abort();
            releaseReply();
        }

        m_provider->imageJobFinished(
            m_jobKey, m_cacheKey, this, error.isEmpty());
        const QList<QPointer<CachedImageResponse>> subscribers =
            std::exchange(m_subscribers, {});
        for (const QPointer<CachedImageResponse> &response : subscribers) {
            if (!response) {
                continue;
            }
            if (error.isEmpty()) {
                response->finishWithImage(image);
            } else {
                response->finishWithError(error);
            }
        }
    }

    QString m_jobKey;
    QString m_cacheKey;
    QSize m_requestedSize;
    ImageCacheProvider *m_provider = nullptr;
    quint64 m_cacheGeneration = 0;
    std::optional<QNetworkRequest> m_resolvedRequest;
    QList<QPointer<CachedImageResponse>> m_subscribers;
    QPointer<QNetworkReply> m_reply;
    QTimer m_deadline;
    QByteArray m_networkData;
    bool m_refreshAttempted = false;
    bool m_finished = false;
};

// ============================================================================
// CachedImageResponse Implementation
// ============================================================================

CachedImageResponse::CachedImageResponse(
    const QString &url,
    const QSize &requestedSize,
    ImageCacheProvider *provider)
    : m_safeCacheLabel(provider->safeCacheLabel(url))
{
    m_job = provider->subscribe(this, url, requestedSize);
}

CachedImageResponse::~CachedImageResponse()
{
    QPointer<ImageLoadJob> job;
    {
        QMutexLocker locker(&m_mutex);
        if (m_finished) {
            return;
        }
        m_cancelled = true;
        m_finished = true;
        job = std::exchange(m_job, nullptr);
    }
    if (job) {
        QMetaObject::invokeMethod(job, [job]() {
            if (job) {
                job->removeExpiredSubscribers();
            }
        }, Qt::QueuedConnection);
    }
}

QQuickTextureFactory *CachedImageResponse::textureFactory() const
{
    QMutexLocker locker(&m_mutex);
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString CachedImageResponse::errorString() const
{
    QMutexLocker locker(&m_mutex);
    return m_errorString;
}

void CachedImageResponse::cancel()
{
    QPointer<ImageLoadJob> job;
    {
        QMutexLocker locker(&m_mutex);
        if (m_finished) {
            return;
        }
        m_cancelled = true;
        m_finished = true;
        m_errorString = QStringLiteral("Image request cancelled");
        job = std::exchange(m_job, nullptr);
    }
    if (job) {
        const QPointer<CachedImageResponse> response(this);
        QMetaObject::invokeMethod(job, [job, response]() {
            if (job) {
                if (response) {
                    job->removeSubscriber(response);
                } else {
                    job->removeExpiredSubscribers();
                }
            }
        }, Qt::QueuedConnection);
    }
    emit finished();
}

void CachedImageResponse::finishWithImage(const QImage &image)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled || m_finished) {
            return;
        }
        m_finished = true;
        m_job = nullptr;
        m_image = image;
    }
    emit finished();
}

void CachedImageResponse::finishWithError(const QString &error)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled || m_finished) {
            return;
        }
        m_finished = true;
        m_job = nullptr;
        m_errorString = error;
    }
    qCWarning(lcImageCache) << "Image load failed:"
                            << m_safeCacheLabel << "-" << error;
    emit finished();
}

// ============================================================================
// ImageCacheProvider Implementation
// ============================================================================

ImageCacheProvider::ImageCacheProvider(qint64 maxCacheSizeMB,
                                       IArtworkProvider *artworkProvider,
                                       ImageRequestLimits requestLimits)
    : QQuickAsyncImageProvider()
    , m_maxCacheSize(maxCacheSizeMB * 1024 * 1024)  // Convert MB to bytes
    , m_artworkProvider(artworkProvider)
    , m_requestLimits(std::move(requestLimits))
    , m_memoryCache(50 * 1024 * 1024)  // 50MB memory cache
{
    m_requestLimits.maximumNetworkBytes = std::clamp<qint64>(
        m_requestLimits.maximumNetworkBytes, 1,
        std::numeric_limits<qint64>::max() - 1);
    m_requestLimits.maximumDecodedBytes = std::max<qint64>(4,
        m_requestLimits.maximumDecodedBytes);
    m_requestLimits.networkDeadlineMs = std::max(1,
        m_requestLimits.networkDeadlineMs);
    // Set up cache directory
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) 
                 + "/bloom_images";
    QDir().mkpath(m_cacheDir);
    
    // Configure thread pool
    m_threadPool.setMaxThreadCount(4);  // Limit concurrent loads
    
    qCInfo(lcImageCache) << "Image cache initialized at:" << m_cacheDir 
                       << "Max size:" << maxCacheSizeMB << "MB";

    m_store = std::make_unique<ImageCacheStore>(m_cacheDir, m_maxCacheSize);
}

void ImageCacheProvider::setRoundedPreprocessEnabled(bool enabled)
{
    m_enableRoundedPreprocess = enabled;
}

void ImageCacheProvider::setDefaultRoundedParams(int radiusPx, const QSize &targetSize)
{
    m_defaultRoundedRadius = qMax(0, radiusPx);
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        m_defaultRoundedSize = targetSize;
    }
}

ImageCacheProvider::~ImageCacheProvider()
{
    const QList<ImageLoadJob *> jobs = m_inFlightImages.values();
    for (ImageLoadJob *job : jobs) {
        if (job) {
            job->shutdown();
        }
    }
    m_threadPool.waitForDone();
}

QQuickImageResponse *ImageCacheProvider::requestImageResponse(const QString &id, 
                                                               const QSize &requestedSize)
{
    if (QThread::currentThread() != thread()) {
        QQuickImageResponse *response = nullptr;
        const bool delivered = QMetaObject::invokeMethod(
            this,
            [this, &response, id, requestedSize]() {
                response = requestImageResponse(id, requestedSize);
            },
            Qt::BlockingQueuedConnection);
        if (!delivered) {
            return nullptr;
        }
        return response;
    }

    // Decode URL from id (QML encodeURIComponent'd the URL)
    QString url = QUrl::fromPercentEncoding(id.toUtf8());
    
    if (url.isEmpty()) {
        qCWarning(lcImageCache) << "Empty image identity requested";
    }
    return new CachedImageResponse(url, requestedSize, this);
}

void ImageCacheProvider::prefetch(const QStringList &urls)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, urls]() { prefetch(urls); }, Qt::BlockingQueuedConnection);
        return;
    }
    for (const QString &url : urls) {
        // Check if already cached
        QString cachedPath = getCachedPath(url);
        if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
            continue;  // Already cached
        }
        
        // Create a prefetch response (no size requirement)
        auto *response = new CachedImageResponse(url, QSize(), this);
        QObject::connect(response, &QQuickImageResponse::finished, 
                         response, &QObject::deleteLater);
    }
}

ImageLoadJob *ImageCacheProvider::subscribe(CachedImageResponse *response,
                                            const QString &cacheKey,
                                            const QSize &requestedSize)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString key = imageJobKey(cacheKey, requestedSize);
    if (ImageLoadJob *existing = m_inFlightImages.value(key)) {
        ++m_coalescedRequests;
        existing->addSubscriber(response);
        return existing;
    }

    auto *job = new ImageLoadJob(
        key, cacheKey, requestedSize, this, resolveRequest(cacheKey));
    m_inFlightImages.insert(key, job);
    ++m_inFlightImageJobs;
    job->addSubscriber(response);
    QMetaObject::invokeMethod(job, [job]() { job->start(); }, Qt::QueuedConnection);
    return job;
}

void ImageCacheProvider::imageJobFinished(const QString &jobKey,
                                          const QString &cacheKey,
                                          ImageLoadJob *job,
                                          bool successful)
{
    if (m_inFlightImages.value(jobKey) != job) {
        return;
    }
    m_inFlightImages.remove(jobKey);
    --m_inFlightImageJobs;
    if (!successful) {
        const bool replacementPending = std::any_of(
            m_inFlightImages.cbegin(), m_inFlightImages.cend(),
            [&cacheKey](const ImageLoadJob *candidate) {
                return candidate && candidate->cacheKey() == cacheKey;
            });
        if (!replacementPending) {
            discardPendingRounded(cacheKey);
        }
    }
    job->deleteLater();
}

void ImageCacheProvider::discardPendingRounded(const QString &cacheKey)
{
    QMutexLocker locker(&m_pendingMutex);
    m_pendingRounded.remove(cacheKey);
}

QString ImageCacheProvider::getCachedPath(const QString &url, qint64 *revision)
{
    if (!m_store) {
        return {};
    }
    const ImageCacheStore::LookupResult result = m_store->lookupEntry(url);
    if (revision) {
        *revision = result.revision;
    }
    return result.path;
}

QString ImageCacheProvider::saveDataForKeyIfCurrent(const QString &urlKey,
                                                    const QByteArray &data,
                                                    quint64 generation)
{
    if (data.isEmpty()) {
        return QString();
    }

    QMutexLocker mutationLock(&m_cacheMutationMutex);
    if (m_cacheGeneration.load() != generation) {
        return QString();
    }
    const QString filepath = m_store ? m_store->write(urlKey, data) : QString();
    if (filepath.isEmpty()) {
        return QString();
    }
    
    qCDebug(lcImageCache) << "Cached:" << safeCacheLabel(urlKey)
                          << "size:" << data.size();
    
    return filepath;
}

void ImageCacheProvider::touchCacheEntry(const QString &url)
{
    if (m_store) {
        m_store->touch(url);
    }
}

QString ImageCacheProvider::hashUrl(const QString &url) const
{
    return ImageCacheStore::filenameForKey(url);
}

QString ImageCacheProvider::safeCacheLabel(const QString &cacheKey) const
{
    return QStringLiteral("cache:%1").arg(hashUrl(cacheKey));
}

std::optional<QNetworkRequest> ImageCacheProvider::resolveRequest(const QString &cacheKey) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (cacheKey.startsWith(QStringLiteral("artwork:"))) {
        if (!m_artworkProvider) {
            return std::nullopt;
        }
        Bloom::ArtworkRef artwork = Bloom::ArtworkRef::fromCacheKey(cacheKey);
        artwork.sourceUrl = Bloom::ArtworkRef::transientSourceUrlForCacheKey(cacheKey);
        if (!artwork.isValid()) {
            return std::nullopt;
        }
        return m_artworkProvider->resolveArtwork(artwork);
    }

    const QUrl url(cacheKey);
    if (!url.isValid() || url.isEmpty()) {
        return std::nullopt;
    }
    return QNetworkRequest(url);
}

QString ImageCacheProvider::roundedKey(const QString &url, int radiusPx, const QSize &targetSize) const
{
    return QString("%1|rounded|r%2|%3x%4")
        .arg(url)
        .arg(radiusPx)
        .arg(targetSize.width())
        .arg(targetSize.height());
}

bool ImageCacheProvider::renderRoundedPng(const QString &sourcePath, int radiusPx,
                                          const QSize &targetSize, QByteArray &outData) const
{
    if (!QFile::exists(sourcePath)) {
        qCWarning(lcImageCache) << "Rounded render failed, source missing:"
                                << safeCacheLabel(sourcePath);
        return false;
    }

    DecodedImage decoded = decodeFile(
        sourcePath, targetSize, m_requestLimits.maximumDecodedBytes);
    QImage src = std::move(decoded.image);
    if (src.isNull()) {
        qCWarning(lcImageCache) << "Rounded render failed to decode"
                                << safeCacheLabel(sourcePath) << decoded.error;
        return false;
    }

    QSize outputSize = targetSize.isValid() && !targetSize.isEmpty() ? targetSize : src.size();
    int clampedRadius = qBound(0, radiusPx, qMin(outputSize.width(), outputSize.height()) / 2);

    QImage rounded(outputSize, QImage::Format_ARGB32_Premultiplied);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(outputSize)), clampedRadius, clampedRadius);
    painter.setClipPath(path);
    painter.drawImage(QRect(QPoint(0, 0), outputSize), src);
    painter.end();

    QBuffer buffer(&outData);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    writer.setCompression(9);
    bool ok = writer.write(rounded);
    buffer.close();

    if (!ok) {
        qCWarning(lcImageCache) << "Rounded render failed to write PNG for"
                                << safeCacheLabel(sourcePath)
                                << writer.errorString();
    }
    return ok;
}

void ImageCacheProvider::scheduleRoundedVariant(const QString &url, const QString &sourcePath,
                                                int radiusPx, const QSize &targetSize,
                                                bool emitSignal)
{
    if (radiusPx <= 0 || targetSize.isEmpty()) {
        return;
    }

    const QString key = roundedKey(url, radiusPx, targetSize);
    const quint64 generation = m_cacheGeneration.load();
    {
        QMutexLocker locker(&m_pendingMutex);
        if (m_roundedInFlight.contains(key)) {
            return;
        }
        m_roundedInFlight.insert(key, generation);
    }

    const QString existing = getCachedPath(key);
    if (!existing.isEmpty() && QFile::exists(existing)) {
        {
            QMutexLocker locker(&m_pendingMutex);
            if (m_roundedInFlight.value(key) == generation) {
                m_roundedInFlight.remove(key);
            }
        }
        if (emitSignal) {
            QString fileUrl = QUrl::fromLocalFile(existing).toString();
            QMetaObject::invokeMethod(this, [this, url, fileUrl, generation]() {
                if (m_cacheGeneration.load() == generation
                    && QFile::exists(QUrl(fileUrl).toLocalFile())) {
                    emit roundedImageReady(url, fileUrl);
                }
            }, Qt::QueuedConnection);
        }
        return;
    }

    ++m_activeRoundedTasks;
    auto future = QtConcurrent::run(&m_threadPool, [this, url, key, sourcePath,
                                                    radiusPx, targetSize, emitSignal,
                                                    generation]() {
        QByteArray roundedBytes;
        const bool rendered = renderRoundedPng(
            sourcePath, radiusPx, targetSize, roundedBytes);
        QString destPath;
        if (rendered && m_cacheGeneration.load() == generation) {
            destPath = saveDataForKeyIfCurrent(key, roundedBytes, generation);
            if (!destPath.isEmpty()) {
                ++m_roundedGenerations;
            }
        }
        {
            QMutexLocker locker(&m_pendingMutex);
            if (m_roundedInFlight.value(key) == generation) {
                m_roundedInFlight.remove(key);
            }
        }
        if (emitSignal && !destPath.isEmpty()) {
            QString fileUrl = QUrl::fromLocalFile(destPath).toString();
            QMetaObject::invokeMethod(this, [this, url, fileUrl, generation]() {
                if (m_cacheGeneration.load() == generation
                    && QFile::exists(QUrl(fileUrl).toLocalFile())) {
                    emit roundedImageReady(url, fileUrl);
                }
            }, Qt::QueuedConnection);
        }
        --m_activeRoundedTasks;
    });
    Q_UNUSED(future);
}

void ImageCacheProvider::processPendingRounded(const QString &url, const QString &sourcePath)
{
    QList<RoundedVariantRequest> requests;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (!m_pendingRounded.contains(url)) {
            return;
        }
        requests = m_pendingRounded.take(url);
    }

    for (const auto &req : requests) {
        scheduleRoundedVariant(url, sourcePath, req.radiusPx, req.size, true);
    }
}

QString ImageCacheProvider::requestRoundedImage(const QString &url, int radiusPx,
                                                int targetWidth, int targetHeight)
{
    if (!m_enableRoundedPreprocess || url.isEmpty()) {
        return QString();
    }

    QSize targetSize(targetWidth, targetHeight);
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        targetSize = m_defaultRoundedSize;
    }
    if (radiusPx <= 0) {
        radiusPx = m_defaultRoundedRadius;
    }

    QString key = roundedKey(url, radiusPx, targetSize);
    QString cachedRounded = getCachedPath(key);
    if (!cachedRounded.isEmpty() && QFile::exists(cachedRounded)) {
        touchCacheEntry(key);
        return QUrl::fromLocalFile(cachedRounded).toString();
    }

    QString basePath = getCachedPath(url);
    if (!basePath.isEmpty() && QFile::exists(basePath)) {
        scheduleRoundedVariant(url, basePath, radiusPx, targetSize, true);
        return QString();
    }

    // Base not cached yet: enqueue request to process once fetched.
    {
        QMutexLocker locker(&m_pendingMutex);
        auto &queue = m_pendingRounded[url];
        bool exists = false;
        for (const auto &req : queue) {
            if (req.radiusPx == radiusPx && req.size == targetSize) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            queue.append({radiusPx, targetSize});
        }
    }
    return QString();
}

QNetworkAccessManager *ImageCacheProvider::networkManager()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }
    return m_networkManager;
}

void ImageCacheProvider::clearMemoryCache()
{
    QMutexLocker locker(&m_memoryCacheMutex);
    m_memoryCache.clear();
}

void ImageCacheProvider::clearCache()
{
    ++m_cacheGeneration;
    const QList<ImageLoadJob *> jobs = m_inFlightImages.values();
    for (ImageLoadJob *job : jobs) {
        if (job) {
            job->shutdown();
        }
    }

    // Clear memory cache
    {
        QMutexLocker locker(&m_memoryCacheMutex);
        m_memoryCache.clear();
    }
    
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingRounded.clear();
        m_roundedInFlight.clear();
    }
    if (m_store) {
        QMutexLocker mutationLock(&m_cacheMutationMutex);
        m_store->clear();
    }
    
    qCInfo(lcImageCache) << "Cache cleared";
}

qint64 ImageCacheProvider::currentCacheSize() const
{
    return m_store ? m_store->currentSize() : 0;
}

QVariantMap ImageCacheProvider::cacheStats() const
{
    if (!m_store) {
        return {};
    }
    const ImageCacheStore::Stats stats = m_store->stats();
    QVariantMap result = {
        {QStringLiteral("diskHits"), QVariant::fromValue(stats.diskHits)},
        {QStringLiteral("diskMisses"), QVariant::fromValue(stats.diskMisses)},
        {QStringLiteral("writes"), QVariant::fromValue(stats.writes)},
        {QStringLiteral("replacements"), QVariant::fromValue(stats.replacements)},
        {QStringLiteral("evictedEntries"), QVariant::fromValue(stats.evictedEntries)},
        {QStringLiteral("evictedBytes"), QVariant::fromValue(stats.evictedBytes)},
        {QStringLiteral("deletionFailures"), QVariant::fromValue(stats.deletionFailures)},
        {QStringLiteral("recoveryActions"), QVariant::fromValue(stats.recoveryActions)},
        {QStringLiteral("databaseRecoveries"), QVariant::fromValue(stats.databaseRecoveries)},
    };
    const quint64 decodeAttempts = m_decodeAttempts.load();
    const quint64 decodedImages = m_decodedImages.load();
    const quint64 totalDecodeLatencyMs = m_totalDecodeLatencyMs.load();
    result.insert(QStringLiteral("imageHits"), QVariant::fromValue(m_imageHits.load()));
    result.insert(QStringLiteral("networkLoads"), QVariant::fromValue(m_networkLoads.load()));
    result.insert(QStringLiteral("coalescedRequests"),
                  QVariant::fromValue(m_coalescedRequests.load()));
    result.insert(QStringLiteral("decodeAttempts"), QVariant::fromValue(decodeAttempts));
    result.insert(QStringLiteral("decodedImages"), QVariant::fromValue(decodedImages));
    result.insert(QStringLiteral("totalDecodeLatencyMs"),
                  QVariant::fromValue(totalDecodeLatencyMs));
    result.insert(QStringLiteral("averageDecodeLatencyMs"),
                  decodeAttempts > 0 ? double(totalDecodeLatencyMs) / decodeAttempts : 0.0);
    result.insert(QStringLiteral("roundedGenerations"),
                  QVariant::fromValue(m_roundedGenerations.load()));
    result.insert(QStringLiteral("inFlightImageJobs"),
                  QVariant::fromValue(m_inFlightImageJobs.load()));
    {
        QMutexLocker locker(&m_pendingMutex);
        qsizetype pendingRoundedRequests = 0;
        for (const auto &requests : m_pendingRounded) {
            pendingRoundedRequests += requests.size();
        }
        result.insert(QStringLiteral("pendingRoundedRequests"),
                      pendingRoundedRequests);
        result.insert(QStringLiteral("inFlightRoundedJobs"),
                      m_roundedInFlight.size());
    }
    result.insert(QStringLiteral("activeRoundedTasks"),
                  QVariant::fromValue(m_activeRoundedTasks.load()));
    return result;
}

void ImageCacheProvider::setMaxCacheSize(qint64 bytes)
{
    m_maxCacheSize = bytes;
    if (m_store) {
        m_store->setMaximumSize(bytes);
    }
}
