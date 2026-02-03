#include "ImageCacheProvider.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
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

Q_LOGGING_CATEGORY(imageCache, "bloom.imagecache")

// ============================================================================
// CachedImageResponse Implementation
// ============================================================================

CachedImageResponse::CachedImageResponse(const QString &url, const QSize &requestedSize,
                             ImageCacheProvider *provider)
    : m_url(url)
    , m_requestedSize(requestedSize)
    , m_provider(provider)
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
            qCDebug(imageCache) << "Cache hit:" << m_url;
            
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
            qCWarning(imageCache) << "Failed to read cached image:" << cachedPath 
                                  << reader.errorString();
        }
    }
    
    // Not in cache, fetch from network
    qCDebug(imageCache) << "Cache miss, fetching:" << m_url;
    fetchFromNetwork();
}

void CachedImageResponse::fetchFromNetwork()
{
    QMutexLocker locker(&m_mutex);
    if (m_cancelled) {
        finishWithError("Request cancelled");
        return;
    }
    
    QUrl url(m_url);
    if (!url.isValid()) {
        finishWithError("Invalid URL: " + m_url);
        return;
    }
    
    QNetworkRequest request(url);
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
    
    if (m_reply->error() != QNetworkReply::NoError) {
        QString error = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        finishWithError("Network error: " + error);
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
    qCWarning(imageCache) << "Image load failed:" << m_url << "-" << error;
    emit finished();
}

// ============================================================================
// ImageCacheProvider Implementation
// ============================================================================

ImageCacheProvider::ImageCacheProvider(qint64 maxCacheSizeMB)
    : QQuickAsyncImageProvider()
    , m_maxCacheSize(maxCacheSizeMB * 1024 * 1024)  // Convert MB to bytes
    , m_memoryCache(50 * 1024 * 1024)  // 50MB memory cache
{
    // Set up cache directory
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) 
                 + "/bloom_images";
    QDir().mkpath(m_cacheDir);
    
    // Configure thread pool
    m_threadPool.setMaxThreadCount(4);  // Limit concurrent loads
    
    qCInfo(imageCache) << "Image cache initialized at:" << m_cacheDir 
                       << "Max size:" << maxCacheSizeMB << "MB";
    
    // Initialize database
    initDatabase();
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
    
    {
        QMutexLocker locker(&m_dbMutex);
        if (m_db.isOpen()) {
            m_db.close();
        }
    }
    
    // Use unique connection name for cleanup
    QString connectionName = m_db.connectionName();
    m_db = QSqlDatabase();  // Reset before removing
    QSqlDatabase::removeDatabase(connectionName);
}

void ImageCacheProvider::initDatabase()
{
    QMutexLocker locker(&m_dbMutex);
    
    // Use unique connection name to avoid conflicts
    QString connectionName = QString("bloom_image_cache_%1")
                            .arg(reinterpret_cast<quintptr>(this), 0, 16);
    
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_cacheDir + "/cache_index.db");
    
    if (!m_db.open()) {
        qCCritical(imageCache) << "Failed to open cache database:" 
                               << m_db.lastError().text();
        return;
    }
    
    // Create cache metadata table
    QSqlQuery query(m_db);
    bool success = query.exec(R"(
        CREATE TABLE IF NOT EXISTS cache_entries (
            url TEXT PRIMARY KEY,
            filename TEXT NOT NULL,
            size INTEGER NOT NULL,
            last_accessed INTEGER NOT NULL,
            created_at INTEGER NOT NULL
        )
    )");
    
    if (!success) {
        qCCritical(imageCache) << "Failed to create cache table:" 
                               << query.lastError().text();
        return;
    }
    
    // Create index for LRU queries
    query.exec("CREATE INDEX IF NOT EXISTS idx_last_accessed ON cache_entries(last_accessed)");
    
    // Calculate current cache size
    query.exec("SELECT COALESCE(SUM(size), 0) FROM cache_entries");
    if (query.next()) {
        m_currentCacheSize = query.value(0).toLongLong();
        qCInfo(imageCache) << "Current cache size:" << m_currentCacheSize / (1024.0 * 1024.0) << "MB";
    }
    
    // Run eviction if needed on startup
    locker.unlock();
    evictIfNeeded();
}

QQuickImageResponse *ImageCacheProvider::requestImageResponse(const QString &id, 
                                                               const QSize &requestedSize)
{
    // Decode URL from id (QML encodeURIComponent'd the URL)
    QString url = QUrl::fromPercentEncoding(id.toUtf8());
    
    if (url.isEmpty()) {
        qCWarning(imageCache) << "Empty image URL requested";
        auto *response = new CachedImageResponse("", requestedSize, this);
        response->finishWithError("Empty URL");
        return response;
    }
    
    auto *response = new CachedImageResponse(url, requestedSize, this);
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
        auto *response = new CachedImageResponse(url, QSize(), this);
        QObject::connect(response, &QQuickImageResponse::finished, 
                         response, &QObject::deleteLater);
        m_threadPool.start(response);
    }
}

QString ImageCacheProvider::getCachedPath(const QString &url)
{
    QMutexLocker locker(&m_dbMutex);
    
    if (!m_db.isOpen()) {
        return QString();
    }
    
    QSqlQuery query(m_db);
    query.prepare("SELECT filename FROM cache_entries WHERE url = ?");
    query.addBindValue(url);
    
    if (query.exec() && query.next()) {
        return m_cacheDir + "/" + query.value(0).toString();
    }
    
    return QString();
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
    
    QString filename = hashUrl(urlKey);
    QString filepath = m_cacheDir + "/" + filename;
    
    // Write file
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(imageCache) << "Failed to write cache file:" << filepath;
        return QString();
    }
    
    qint64 written = file.write(data);
    file.close();
    
    if (written != data.size()) {
        qCWarning(imageCache) << "Incomplete write to cache file:" << filepath;
        QFile::remove(filepath);
        return QString();
    }
    
    qint64 now = QDateTime::currentSecsSinceEpoch();
    
    // Update database
    {
        QMutexLocker locker(&m_dbMutex);
        
        if (!m_db.isOpen()) {
            return QString();
        }
        
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT OR REPLACE INTO cache_entries 
            (url, filename, size, last_accessed, created_at)
            VALUES (?, ?, ?, ?, ?)
        )");
        query.addBindValue(urlKey);
        query.addBindValue(filename);
        query.addBindValue(data.size());
        query.addBindValue(now);
        query.addBindValue(now);
        
        if (!query.exec()) {
            qCWarning(imageCache) << "Failed to update cache database:" 
                                  << query.lastError().text();
            return QString();
        }
    }
    
    // Update size tracking
    {
        QMutexLocker locker(&m_sizeMutex);
        m_currentCacheSize += data.size();
    }
    
    qCDebug(imageCache) << "Cached:" << urlKey << "size:" << data.size();
    
    // Check if eviction is needed
    evictIfNeeded();
    return filepath;
}

void ImageCacheProvider::touchCacheEntry(const QString &url)
{
    QMutexLocker locker(&m_dbMutex);
    
    if (!m_db.isOpen()) {
        return;
    }
    
    QSqlQuery query(m_db);
    query.prepare("UPDATE cache_entries SET last_accessed = ? WHERE url = ?");
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(url);
    query.exec();
}

void ImageCacheProvider::evictIfNeeded()
{
    QMutexLocker sizeLock(&m_sizeMutex);
    
    if (m_currentCacheSize <= m_maxCacheSize) {
        return;  // Under limit, no eviction needed
    }
    
    qint64 targetSize = static_cast<qint64>(m_maxCacheSize * 0.8);  // Evict to 80% of max
    qint64 bytesToFree = m_currentCacheSize - targetSize;
    
    sizeLock.unlock();
    
    qCInfo(imageCache) << "Cache eviction needed. Current:" 
                       << m_currentCacheSize / (1024.0 * 1024.0) << "MB"
                       << "Target:" << targetSize / (1024.0 * 1024.0) << "MB";
    
    QMutexLocker dbLock(&m_dbMutex);
    
    if (!m_db.isOpen()) {
        return;
    }
    
    // Get oldest entries (LRU)
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT url, filename, size FROM cache_entries 
        ORDER BY last_accessed ASC
        LIMIT 100
    )");
    
    if (!query.exec()) {
        qCWarning(imageCache) << "Failed to query cache for eviction:" 
                              << query.lastError().text();
        return;
    }
    
    QStringList urlsToDelete;
    QStringList filesToDelete;
    qint64 freedBytes = 0;
    
    while (query.next() && freedBytes < bytesToFree) {
        QString url = query.value(0).toString();
        QString filename = query.value(1).toString();
        qint64 size = query.value(2).toLongLong();
        
        urlsToDelete.append(url);
        filesToDelete.append(m_cacheDir + "/" + filename);
        freedBytes += size;
    }
    
    // Delete database entries
    if (!urlsToDelete.isEmpty()) {
        QSqlQuery deleteQuery(m_db);
        for (const QString &url : urlsToDelete) {
            deleteQuery.prepare("DELETE FROM cache_entries WHERE url = ?");
            deleteQuery.addBindValue(url);
            deleteQuery.exec();
        }
    }
    
    dbLock.unlock();
    
    // Delete files
    for (const QString &filepath : filesToDelete) {
        if (QFile::remove(filepath)) {
            qCDebug(imageCache) << "Evicted:" << filepath;
        }
    }
    
    // Update size tracking
    sizeLock.relock();
    m_currentCacheSize -= freedBytes;
    
    qCInfo(imageCache) << "Evicted" << urlsToDelete.size() << "entries,"
                       << freedBytes / (1024.0 * 1024.0) << "MB freed";
}

QString ImageCacheProvider::hashUrl(const QString &url) const
{
    QByteArray hash = QCryptographicHash::hash(url.toUtf8(), 
                                                QCryptographicHash::Sha256);
    return hash.toHex().left(32);  // Use first 32 chars of SHA256
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
        qCWarning(imageCache) << "Rounded render failed, source missing:" << sourcePath;
        return false;
    }

    QImageReader reader(sourcePath);
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        reader.setScaledSize(targetSize);
    }
    QImage src = reader.read();
    if (src.isNull()) {
        qCWarning(imageCache) << "Rounded render failed to decode" << sourcePath << reader.errorString();
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
        qCWarning(imageCache) << "Rounded render failed to write PNG for" << sourcePath << writer.errorString();
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
    
    // Clear database
    {
        QMutexLocker locker(&m_dbMutex);
        
        if (m_db.isOpen()) {
            QSqlQuery query(m_db);
            query.exec("DELETE FROM cache_entries");
        }
    }
    
    // Remove all cached files
    QDir cacheDir(m_cacheDir);
    QStringList files = cacheDir.entryList(QDir::Files);
    for (const QString &file : files) {
        if (file != "cache_index.db" && file != "cache_index.db-journal") {
            QFile::remove(m_cacheDir + "/" + file);
        }
    }
    
    // Reset size
    {
        QMutexLocker locker(&m_sizeMutex);
        m_currentCacheSize = 0;
    }
    
    qCInfo(imageCache) << "Cache cleared";
}

qint64 ImageCacheProvider::currentCacheSize() const
{
    QMutexLocker locker(&m_sizeMutex);
    return m_currentCacheSize;
}

void ImageCacheProvider::setMaxCacheSize(qint64 bytes)
{
    m_maxCacheSize = bytes;
    evictIfNeeded();
}
