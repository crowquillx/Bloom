#ifndef IMAGECACHEPROVIDER_H
#define IMAGECACHEPROVIDER_H

#include <QQuickAsyncImageProvider>
#include <QObject>
#include <QThreadPool>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QMutex>
#include <QCache>
#include <QUrl>
#include <QSize>
#include <QVariantMap>
#include <QImage>
#include <QList>
#include <QHash>
#include <QPointer>
#ifdef BLOOM_TESTING
#include <QSemaphore>
#include <QSharedPointer>
#endif
#include <atomic>
#include <memory>
#include <optional>

class IArtworkProvider;
class ImageCacheProvider;
class ImageCacheStore;
class ImageLoadJob;

struct ImageRequestLimits {
    qint64 maximumNetworkBytes = 20 * 1024 * 1024;
    qint64 maximumDecodedBytes = 192 * 1024 * 1024;
    int networkDeadlineMs = 15'000;
};

/**
 * @brief Response handler for async image loading
 * 
 * Subscribes one QML request to a provider-owned shared load job. The response
 * owns only its result and cancellation state.
 */
class CachedImageResponse : public QQuickImageResponse
{
    Q_OBJECT
    
public:
    CachedImageResponse(const QString &url,
                        const QSize &requestedSize,
                        ImageCacheProvider *provider);
    ~CachedImageResponse() override;
    
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override;
    void cancel() override;
    
private:
    friend class ImageLoadJob;

    void finishWithImage(const QImage &image);
    void finishWithError(const QString &error);
    
    QString m_safeCacheLabel;
    QImage m_image;
    QString m_errorString;
    bool m_cancelled = false;
    bool m_finished = false;
    QPointer<ImageLoadJob> m_job;
    mutable QMutex m_mutex;
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
    explicit ImageCacheProvider(qint64 maxCacheSizeMB = 500,
                                IArtworkProvider *artworkProvider = nullptr,
                                ImageRequestLimits requestLimits = {});
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
     * @brief Snapshot disk-cache diagnostics without exposing persisted keys.
     */
    Q_INVOKABLE QVariantMap cacheStats() const;
    
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

#ifdef BLOOM_TESTING
    void blockCacheWorkerForTest(const QSharedPointer<QSemaphore> &entered,
                                 const QSharedPointer<QSemaphore> &release);
    void blockNextRoundedLookupForTest(
        const QSharedPointer<QSemaphore> &entered,
        const QSharedPointer<QSemaphore> &release);
    void advanceCacheContentRevisionForTest();
    void processPendingRoundedForTest(const QString &url,
                                      const QString &sourcePath);
#endif

signals:
    void roundedImageReady(const QString &url, const QString &fileUrl);

private:
    friend class CachedImageResponse;
    friend class ImageLoadJob;

    ImageLoadJob *subscribe(CachedImageResponse *response,
                            const QString &cacheKey,
                            const QSize &requestedSize);
    void imageJobFinished(const QString &jobKey, const QString &cacheKey,
                          ImageLoadJob *job, bool successful);
    void discardPendingRounded(const QString &cacheKey);
    
    /**
     * @brief Get cached file path for URL
     * @param url Image URL
     * @return Path to cached file, or empty if not cached
     */
    QString getCachedPath(const QString &url, qint64 *revision = nullptr);
    
    /**
     * @brief Generate a cache filename from a token-free identity.
     */
    QString hashUrl(const QString &url) const;
    QString safeCacheLabel(const QString &cacheKey) const;

    /**
     * @brief Resolve a transient artwork source without persisting credentials.
     *
     * ArtworkRef keeps the signed source URL in a bounded in-process registry;
     * only the opaque identity key is used by the disk cache.
     */
    std::optional<QNetworkRequest> resolveRequest(const QString &cacheKey) const;
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
     * @brief Coalesce an asynchronous LRU touch for a known-ready variant.
     */
    void touchRoundedVariantAsync(const QString &key, quint64 generation);
    
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
    QString saveDataForKeyIfCurrent(const QString &urlKey, const QByteArray &data,
                                    quint64 generation);

    /**
     * @brief Invalidate provider-thread cache knowledge after a disk mutation.
     */
    quint64 advanceCacheContentRevision();
    
    /**
     * @brief Get the network manager owned by the provider thread.
     */
    QNetworkAccessManager *networkManager();
    
    // Configuration
    qint64 m_maxCacheSize;  // in bytes
    QString m_cacheDir;
    int m_defaultRoundedRadius = 16;
    QSize m_defaultRoundedSize = QSize(640, 960);
    bool m_enableRoundedPreprocess = true;
    IArtworkProvider *m_artworkProvider = nullptr;
    ImageRequestLimits m_requestLimits;
    
    // Dedicated-thread owner for SQLite metadata and cache files.
    std::unique_ptr<ImageCacheStore> m_store;
    
    // Memory cache for recently accessed images (16 entries, ~50MB max)
    QCache<QString, QImage> m_memoryCache;
    mutable QMutex m_memoryCacheMutex;
    
    // Network access for fetching images
    QNetworkAccessManager *m_networkManager = nullptr;
    
    // Thread pool for async operations
    QThreadPool m_threadPool;

    std::atomic<quint64> m_cacheGeneration{1};
    // Changes after every successful cache write/eviction-capable mutation.
    // Ready-path entries are usable only while this revision still matches.
    std::atomic<quint64> m_cacheContentRevision{1};
    QMutex m_cacheMutationMutex;
    QHash<QString, ImageLoadJob *> m_inFlightImages;
    std::atomic<quint64> m_inFlightImageJobs{0};

    std::atomic<quint64> m_imageHits{0};
    std::atomic<quint64> m_networkLoads{0};
    std::atomic<quint64> m_coalescedRequests{0};
    std::atomic<quint64> m_decodeAttempts{0};
    std::atomic<quint64> m_decodedImages{0};
    std::atomic<quint64> m_totalDecodeLatencyMs{0};
    std::atomic<quint64> m_roundedGenerations{0};
    std::atomic<quint64> m_activeRoundedTasks{0};
    
    struct RoundedVariantRequest {
        int radiusPx;
        QSize size;
    };
    struct ReadyRoundedVariant {
        QString fileUrl;
        quint64 generation = 0;
        quint64 contentRevision = 0;
    };
    struct KnownBaseImage {
        QString path;
        quint64 contentRevision = 0;
    };
    static constexpr qsizetype MaximumRoundedKnowledgeEntries = 256;
    static constexpr qsizetype MaximumRoundedTouchesInFlight = 256;
    QHash<QString, QList<RoundedVariantRequest>> m_pendingRounded;
    QHash<QString, quint64> m_roundedInFlight;
    QHash<QString, ReadyRoundedVariant> m_readyRounded;
    QHash<QString, KnownBaseImage> m_knownBaseImages;
    QHash<QString, quint64> m_roundedTouchesInFlight;
    mutable QMutex m_pendingMutex;
#ifdef BLOOM_TESTING
    QSharedPointer<QSemaphore> m_roundedLookupEnteredForTest;
    QSharedPointer<QSemaphore> m_roundedLookupReleaseForTest;
    QMutex m_testHookMutex;
#endif
};

#endif // IMAGECACHEPROVIDER_H
