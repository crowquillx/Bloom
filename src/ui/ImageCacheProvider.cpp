#include "ImageCacheProvider.h"
#include "ImageCacheStore.h"
#include "providers/IArtworkProvider.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImageReader>
#include <QBuffer>
#include <QLoggingCategory>
#include <QThread>
#include <QtConcurrent>
#include <QPainter>
#include <QPainterPath>
#include <QImageWriter>
#include <QPointer>
#include <utility>
#include "../utils/BloomLogging.h"

// ============================================================================
// CachedImageResponse Implementation
// ============================================================================

CachedImageResponse::CachedImageResponse(
    const QString &url,
    const QSize &requestedSize,
    ImageCacheProvider *provider,
    std::optional<QNetworkRequest> resolvedRequest)
    : m_url(url)
    , m_requestedSize(requestedSize)
    , m_provider(provider)
    , m_resolvedRequest(std::move(resolvedRequest))
{
    setAutoDelete(false);
}

CachedImageResponse::~CachedImageResponse()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

QQuickTextureFactory *CachedImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString CachedImageResponse::errorString() const
{
    return m_errorString;
}

void CachedImageResponse::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort();
    }
}

void CachedImageResponse::run()
{
    // Check if cancelled
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            finishWithError("Request cancelled");
            return;
        }
    }
    
    // Try memory cache first
    {
        QMutexLocker locker(&m_provider->m_memoryCacheMutex);
        if (QImage *cached = m_provider->m_memoryCache.object(m_url)) {
            QImage img = *cached;
            locker.unlock();
            
            // Scale if needed
            if (m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
                img = img.scaled(m_requestedSize, Qt::KeepAspectRatio, 
                                 Qt::SmoothTransformation);
            }
            finishWithImage(img);
            return;
        }
    }
    
    // Try disk cache
    loadFromCache();
}

void CachedImageResponse::loadFromCache()
{
    QString cachedPath = m_provider->getCachedPath(m_url);
    
    if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
        QImageReader reader(cachedPath);
        
        // Set scale if requested size is specified (for efficient loading)
        if (m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
            reader.setScaledSize(m_requestedSize);
        }
        
        QImage image = reader.read();
        
        if (!image.isNull()) {
            qCDebug(lcImageCache) << "Cache hit:"
                                  << m_provider->safeCacheLabel(m_url);
            
            // Update access time in database
            m_provider->touchCacheEntry(m_url);
            
            // Store in memory cache (original size)
            if (!m_requestedSize.isValid() || m_requestedSize.isEmpty()) {
                QMutexLocker locker(&m_provider->m_memoryCacheMutex);
                // Estimate cost: ~4 bytes per pixel
                int cost = image.width() * image.height() * 4;
                m_provider->m_memoryCache.insert(m_url, new QImage(image), cost);
            }

            if (m_provider->m_enableRoundedPreprocess) {
                QSize roundedSize = m_requestedSize.isValid() && !m_requestedSize.isEmpty()
                    ? m_requestedSize
                    : m_provider->m_defaultRoundedSize;
                m_provider->scheduleRoundedVariant(m_url, cachedPath, m_provider->m_defaultRoundedRadius, roundedSize, true);
            }
            
            finishWithImage(image);
            return;
        } else {
            qCWarning(lcImageCache) << "Failed to read cached image:" << cachedPath 
                                  << reader.errorString();
            m_provider->m_store->invalidate(m_url);
        }
    }
    
    // Not in cache, fetch from network
    qCDebug(lcImageCache) << "Cache miss, fetching:"
                          << m_provider->safeCacheLabel(m_url);
    fetchFromNetwork();
}

void CachedImageResponse::fetchFromNetwork()
{
    QMutexLocker locker(&m_mutex);
    if (m_cancelled) {
        finishWithError("Request cancelled");
        return;
    }
    
    if (!m_resolvedRequest.has_value()) {
        finishWithError("Unable to resolve image request");
        return;
    }

    QNetworkRequest request = *m_resolvedRequest;
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, 
                        QNetworkRequest::PreferNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Bloom/1.0");
    
    // Get network manager from provider (main thread access)
    QNetworkAccessManager *nam = m_provider->networkManager();
    
    // Use invokeMethod to call network operation on main thread
    QMetaObject::invokeMethod(nam, [this, request, nam]() {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            QMetaObject::invokeMethod(this, [this]() {
                finishWithError("Request cancelled");
            }, Qt::QueuedConnection);
            return;
        }
        
        m_reply = nam->get(request);
        
        connect(m_reply, &QNetworkReply::finished,
                this, &CachedImageResponse::onNetworkReplyFinished,
                Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void CachedImageResponse::onNetworkReplyFinished()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_reply) {
        finishWithError("Network reply was null");
        return;
    }
    
    if (m_cancelled) {
        m_reply->deleteLater();
        m_reply = nullptr;
        finishWithError("Request cancelled");
        return;
    }
    
    const int httpStatus = m_reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool refreshableStatus = httpStatus == 401 || httpStatus == 403;
    if (refreshableStatus) {
        const QString error = m_reply->error() == QNetworkReply::NoError
            ? QStringLiteral("HTTP error: %1").arg(httpStatus)
            : QStringLiteral("Network error: %1").arg(m_reply->errorString());
        m_reply->deleteLater();
        m_reply = nullptr;

        if (!m_refreshAttempted
            && m_url.startsWith(QStringLiteral("artwork:"))
            && m_provider->m_artworkProvider) {
            m_refreshAttempted = true;
            locker.unlock();
            refreshArtworkRequest(error);
            return;
        }

        finishWithError(error);
        return;
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        const QString error = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        finishWithError(QStringLiteral("Network error: %1").arg(error));
        return;
    }
    
    QByteArray data = m_reply->readAll();
    m_reply->deleteLater();
    m_reply = nullptr;
    
    locker.unlock();
    
    if (data.isEmpty()) {
        finishWithError("Empty response from server");
        return;
    }
    
    // Save to cache (async)
    saveToCache(data);
    
    // Load image from data
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    
    if (m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
        reader.setScaledSize(m_requestedSize);
    }
    
    QImage image = reader.read();
    
    if (image.isNull()) {
        finishWithError("Failed to decode image: " + reader.errorString());
        return;
    }
    
    // Store in memory cache (original size)
    if (!m_requestedSize.isValid() || m_requestedSize.isEmpty()) {
        QMutexLocker cacheLock(&m_provider->m_memoryCacheMutex);
        int cost = image.width() * image.height() * 4;
        m_provider->m_memoryCache.insert(m_url, new QImage(image), cost);
    }
    
    finishWithImage(image);
}

void CachedImageResponse::refreshArtworkRequest(const QString &networkError)
{
    Bloom::ArtworkRef artwork = Bloom::ArtworkRef::fromCacheKey(m_url);
    artwork.sourceUrl = Bloom::ArtworkRef::transientSourceUrlForCacheKey(m_url);
    IArtworkProvider *artworkProvider = m_provider->m_artworkProvider;
    if (!artwork.isValid() || !artworkProvider) {
        finishWithError(networkError);
        return;
    }

    const QPointer<CachedImageResponse> guard(this);
    artworkProvider->refreshArtwork(
        artwork,
        [guard, networkError](std::optional<QNetworkRequest> refreshedRequest) {
            if (!guard) {
                return;
            }

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, networkError,
                 refreshedRequest = std::move(refreshedRequest)]() mutable {
                    if (!guard) {
                        return;
                    }

                    QMutexLocker locker(&guard->m_mutex);
                    if (guard->m_cancelled) {
                        guard->finishWithError(QStringLiteral("Request cancelled"));
                        return;
                    }
                    if (!refreshedRequest.has_value()) {
                        guard->finishWithError(networkError);
                        return;
                    }

                    guard->m_resolvedRequest = std::move(refreshedRequest);
                    locker.unlock();
                    guard->fetchFromNetwork();
                },
                Qt::QueuedConnection);
        });
}

void CachedImageResponse::saveToCache(const QByteArray &data)
{
    m_provider->saveToCache(m_url, data);
}

void CachedImageResponse::finishWithImage(const QImage &image)
{
    m_image = image;
    emit finished();
}

void CachedImageResponse::finishWithError(const QString &error)
{
    m_errorString = error;
    qCWarning(lcImageCache) << "Image load failed:"
                            << m_provider->safeCacheLabel(m_url) << "-" << error;
    emit finished();
}

// ============================================================================
// ImageCacheProvider Implementation
// ============================================================================

ImageCacheProvider::ImageCacheProvider(qint64 maxCacheSizeMB,
                                       IArtworkProvider *artworkProvider)
    : QQuickAsyncImageProvider()
    , m_maxCacheSize(maxCacheSizeMB * 1024 * 1024)  // Convert MB to bytes
    , m_artworkProvider(artworkProvider)
    , m_memoryCache(50 * 1024 * 1024)  // 50MB memory cache
{
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
    m_threadPool.waitForDone();
}

QQuickImageResponse *ImageCacheProvider::requestImageResponse(const QString &id, 
                                                               const QSize &requestedSize)
{
    // Decode URL from id (QML encodeURIComponent'd the URL)
    QString url = QUrl::fromPercentEncoding(id.toUtf8());
    
    if (url.isEmpty()) {
        qCWarning(lcImageCache) << "Empty image URL requested";
        auto *response = new CachedImageResponse("", requestedSize, this, std::nullopt);
        response->finishWithError("Empty URL");
        return response;
    }
    
    auto *response = new CachedImageResponse(url,
                                             requestedSize,
                                             this,
                                             resolveRequest(url));
    m_threadPool.start(response);
    return response;
}

void ImageCacheProvider::prefetch(const QStringList &urls)
{
    for (const QString &url : urls) {
        // Check if already cached
        QString cachedPath = getCachedPath(url);
        if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
            continue;  // Already cached
        }
        
        // Create a prefetch response (no size requirement)
        auto *response = new CachedImageResponse(url,
                                                 QSize(),
                                                 this,
                                                 resolveRequest(url));
        QObject::connect(response, &QQuickImageResponse::finished, 
                         response, &QObject::deleteLater);
        m_threadPool.start(response);
    }
}

QString ImageCacheProvider::getCachedPath(const QString &url)
{
    return m_store ? m_store->lookup(url) : QString();
}

void ImageCacheProvider::saveToCache(const QString &url, const QByteArray &data)
{
    QString filepath = saveDataForKey(url, data);
    if (filepath.isEmpty()) {
        return;
    }

    if (m_enableRoundedPreprocess) {
        // Always generate a default rounded variant for UI grids.
        scheduleRoundedVariant(url, filepath, m_defaultRoundedRadius, m_defaultRoundedSize, true);
        // Process any queued rounded requests waiting for this base asset.
        processPendingRounded(url, filepath);
    }
}

QString ImageCacheProvider::saveDataForKey(const QString &urlKey, const QByteArray &data)
{
    if (data.isEmpty()) {
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
    if (QThread::currentThread() != thread()) {
        std::optional<QNetworkRequest> resolved;
        QMetaObject::invokeMethod(const_cast<ImageCacheProvider *>(this),
                                  [this, &resolved, cacheKey]() {
                                      resolved = resolveRequest(cacheKey);
                                  },
                                  Qt::BlockingQueuedConnection);
        return resolved;
    }
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
        qCWarning(lcImageCache) << "Rounded render failed, source missing:" << sourcePath;
        return false;
    }

    QImageReader reader(sourcePath);
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        reader.setScaledSize(targetSize);
    }
    QImage src = reader.read();
    if (src.isNull()) {
        qCWarning(lcImageCache) << "Rounded render failed to decode" << sourcePath << reader.errorString();
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
        qCWarning(lcImageCache) << "Rounded render failed to write PNG for" << sourcePath << writer.errorString();
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

    QString key = roundedKey(url, radiusPx, targetSize);
    QString existing = getCachedPath(key);
    if (!existing.isEmpty() && QFile::exists(existing)) {
        if (emitSignal) {
            QString fileUrl = QUrl::fromLocalFile(existing).toString();
            QMetaObject::invokeMethod(this, [this, url, fileUrl]() {
                emit roundedImageReady(url, fileUrl);
            }, Qt::QueuedConnection);
        }
        return;
    }

    auto future = QtConcurrent::run(&m_threadPool, [this, url, key, sourcePath, radiusPx, targetSize, emitSignal]() {
        QByteArray roundedBytes;
        if (!renderRoundedPng(sourcePath, radiusPx, targetSize, roundedBytes)) {
            return;
        }
        QString destPath = saveDataForKey(key, roundedBytes);
        if (emitSignal && !destPath.isEmpty()) {
            QString fileUrl = QUrl::fromLocalFile(destPath).toString();
            QMetaObject::invokeMethod(this, [this, url, fileUrl]() {
                emit roundedImageReady(url, fileUrl);
            }, Qt::QueuedConnection);
        }
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
    QMutexLocker locker(&m_networkMutex);
    
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager();
        // Move to main thread for proper event handling
        m_networkManager->moveToThread(QCoreApplication::instance()->thread());
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
    // Clear memory cache
    {
        QMutexLocker locker(&m_memoryCacheMutex);
        m_memoryCache.clear();
    }
    
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingRounded.clear();
    }
    if (m_store) {
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
    return {
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
}

void ImageCacheProvider::setMaxCacheSize(qint64 bytes)
{
    m_maxCacheSize = bytes;
    if (m_store) {
        m_store->setMaximumSize(bytes);
    }
}
