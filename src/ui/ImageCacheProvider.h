#ifndef IMAGECACHEPROVIDER_H
#define IMAGECACHEPROVIDER_H

#include <QQuickAsyncImageProvider>
#include <QObject>
#include <QThreadPool>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMutex>
#include <QSqlDatabase>
#include <QCache>
#include <QUrl>
#include <QSize>
#include <QRunnable>
#include <QImage>
#include <QList>
#include <QHash>
#include <QPair>

class ImageCacheProvider;

/**
 * @brief Response handler for async image loading
 * 
 * Handles the lifecycle of a single image request, coordinating between
 * disk cache lookup and network fetch.
 */
class CachedImageResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT
    
public:
    CachedImageResponse(const QString &url, const QSize &requestedSize,
                  ImageCacheProvider *provider);
    ~CachedImageResponse() override;
    
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override;
    void cancel() override;
    
    void run() override;

signals:
    void imageLoaded(const QImage &image);
    void loadFailed(const QString &error);

public:
    // Called by ImageCacheProvider for error handling
    void finishWithError(const QString &error);

private slots:
    void onNetworkReplyFinished();

private:
    void loadFromCache();
    void fetchFromNetwork();
    void saveToCache(const QByteArray &data);
    void finishWithImage(const QImage &image);
    
    QString m_url;
    QSize m_requestedSize;
    ImageCacheProvider *m_provider;
    QImage m_image;
    QString m_errorString;
    bool m_cancelled = false;
    QNetworkReply *m_reply = nullptr;
    QMutex m_mutex;
};

/**
 * @brief Async image provider with disk cache and LRU eviction
 * 
 * This provider implements a high-performance image caching system:
 * - Asynchronous image loading using thread pool
 * - SQLite-backed metadata index for fast lookups
 * - LRU (Least Recently Used) eviction policy
 * - Configurable maximum cache size
 * - Memory cache for recently used images
 * - Pre-fetching support for adjacent items
 * 
 * Usage in QML:
 *   Image { source: "image://cached/" + encodeURIComponent(imageUrl) }
 * 
 * The provider automatically handles:
 * - Cache hits (returns immediately from disk)
 * - Cache misses (fetches from network, saves to cache)
 * - Cache eviction (removes oldest entries when full)
 */
class ImageCacheProvider : public QQuickAsyncImageProvider
{
    Q_OBJECT
public:
    /**
     * @brief Construct image cache provider
     * @param maxCacheSizeMB Maximum disk cache size in megabytes (default 500MB)
     */
    explicit ImageCacheProvider(qint64 maxCacheSizeMB = 500);
    ~ImageCacheProvider() override;
    
    QQuickImageResponse *requestImageResponse(const QString &id, 
                                               const QSize &requestedSize) override;
    
    /**
     * @brief Request a pre-rounded image variant.
     * @param url Original image URL.
     * @param radiusPx Corner radius in pixels (defaults to Theme radiusLarge).
     * @param targetWidth Desired width for the rounded variant (defaults to 640).
     * @param targetHeight Desired height for the rounded variant (defaults to 960).
     * @return file:// URL if already available; empty string if scheduled/absent.
     *
     * This method is safe to call from QML. If the rounded variant is not yet
     * generated, it will be scheduled in the background. A signal will fire
     * once ready.
     */
    Q_INVOKABLE QString requestRoundedImage(const QString &url, int radiusPx = 16,
                                            int targetWidth = 640, int targetHeight = 960);

    /**
     * @brief Pre-fetch images for smoother scrolling
     * @param urls List of image URLs to prefetch
     * 
     * Queues images for background download without blocking.
     * Use this when preparing adjacent items in a list view.
     */
    void prefetch(const QStringList &urls);
    
    /**
     * @brief Clear entire cache
     * 
     * Removes all cached images from disk and database.
     */
    void clearCache();

    /**
     * @brief Clear in-memory thumbnails/textures without touching disk.
     */
    void clearMemoryCache();
    
    /**
     * @brief Get current cache size in bytes
     */
    qint64 currentCacheSize() const;
    
    /**
     * @brief Get maximum cache size in bytes
     */
    qint64 maxCacheSize() const { return m_maxCacheSize; }
    
    /**
     * @brief Set maximum cache size
     * @param bytes Maximum size in bytes
     */
    void setMaxCacheSize(qint64 bytes);
    
    /**
     * @brief Get cache directory path
     */
    QString cacheDir() const { return m_cacheDir; }

    /**
     * @brief Enable/disable rounded preprocessing pipeline at runtime.
     */
    void setRoundedPreprocessEnabled(bool enabled);

    /**
     * @brief Update default rounded radius and target size used when callers
     *        omit explicit values.
     */
    void setDefaultRoundedParams(int radiusPx, const QSize &targetSize);

signals:
    void roundedImageReady(const QString &url, const QString &fileUrl);

private:
    friend class CachedImageResponse;
    
    /**
     * @brief Initialize SQLite database for cache metadata
     */
    void initDatabase();
    
    /**
     * @brief Get cached file path for URL
     * @param url Image URL
     * @return Path to cached file, or empty if not cached
     */
    QString getCachedPath(const QString &url);
    
    /**
     * @brief Save image data to cache
     * @param url Original URL
     * @param data Image data
     * @param size File size in bytes
     */
    void saveToCache(const QString &url, const QByteArray &data);
    
    /**
     * @brief Update access time for cache entry
     * @param url Image URL
     */
    void touchCacheEntry(const QString &url);
    
    /**
     * @brief Evict oldest entries until under size limit
     */
    void evictIfNeeded();
    
    /**
     * @brief Generate cache file name from URL
     */
    QString hashUrl(const QString &url) const;
    
    /**
     * @brief Construct a stable key for a rounded variant.
     */
    QString roundedKey(const QString &url, int radiusPx, const QSize &targetSize) const;
    
    /**
     * @brief Generate a rounded variant asynchronously if missing.
     */
    void scheduleRoundedVariant(const QString &url, const QString &sourcePath,
                                int radiusPx, const QSize &targetSize,
                                bool emitSignal);
    
    /**
     * @brief Process any pending rounded variant requests once the base image is cached.
     */
    void processPendingRounded(const QString &url, const QString &sourcePath);
    
    /**
     * @brief Generate rounded PNG bytes for a source image.
     */
    bool renderRoundedPng(const QString &sourcePath, int radiusPx,
                          const QSize &targetSize, QByteArray &outData) const;
    
    /**
     * @brief Shared cache write helper for original and derived assets.
     */
    QString saveDataForKey(const QString &urlKey, const QByteArray &data);
    
    /**
     * @brief Get network manager (thread-safe)
     */
    QNetworkAccessManager *networkManager();
    
    // Configuration
    qint64 m_maxCacheSize;  // in bytes
    QString m_cacheDir;
    int m_defaultRoundedRadius = 16;
    QSize m_defaultRoundedSize = QSize(640, 960);
    bool m_enableRoundedPreprocess = true;
    
    // SQLite database for cache metadata
    QSqlDatabase m_db;
    mutable QMutex m_dbMutex;
    
    // Memory cache for recently accessed images (16 entries, ~50MB max)
    QCache<QString, QImage> m_memoryCache;
    mutable QMutex m_memoryCacheMutex;
    
    // Network access for fetching images
    QNetworkAccessManager *m_networkManager = nullptr;
    QMutex m_networkMutex;
    
    // Thread pool for async operations
    QThreadPool m_threadPool;
    
    // Track current cache size
    qint64 m_currentCacheSize = 0;
    mutable QMutex m_sizeMutex;
    
    struct RoundedVariantRequest {
        int radiusPx;
        QSize size;
    };
    QHash<QString, QList<RoundedVariantRequest>> m_pendingRounded;
    mutable QMutex m_pendingMutex;
};

#endif // IMAGECACHEPROVIDER_H
