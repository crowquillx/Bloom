#ifndef IMAGECACHESTORE_H
#define IMAGECACHESTORE_H

#include <QThread>
#include <QString>
#ifdef BLOOM_TESTING
#include <QSemaphore>
#endif

#include <memory>

class ImageCacheStoreWorker;

/**
 * Thread-safe façade for the on-disk image cache.
 *
 * All SQLite and cache-file operations execute on one dedicated worker thread.
 * Public methods are synchronous and block until queued worker operations have
 * completed. GUI/QML-facing provider methods such as cacheStats(), clearCache(),
 * currentCacheSize(), and setMaxCacheSize() therefore also block their caller.
 */
class ImageCacheStore final
{
public:
    struct LookupResult {
        QString path;
        qint64 revision = 0;

        [[nodiscard]] bool isValid() const { return !path.isEmpty(); }
    };

    struct Stats {
        quint64 diskHits = 0;
        quint64 diskMisses = 0;
        quint64 writes = 0;
        quint64 replacements = 0;
        quint64 evictedEntries = 0;
        quint64 evictedBytes = 0;
        quint64 deletionFailures = 0;
        quint64 recoveryActions = 0;
        quint64 databaseRecoveries = 0;
    };

    ImageCacheStore(QString cacheDirectory, qint64 maximumSizeBytes);
    ~ImageCacheStore();

    ImageCacheStore(const ImageCacheStore &) = delete;
    ImageCacheStore &operator=(const ImageCacheStore &) = delete;

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] LookupResult lookupEntry(const QString &cacheKey,
                                           bool updateAccessTime = false);
    [[nodiscard]] QString lookup(const QString &cacheKey, bool updateAccessTime = false);
    [[nodiscard]] QString write(const QString &cacheKey, const QByteArray &data);
    void touch(const QString &cacheKey);
    void invalidate(const QString &cacheKey);
    void invalidateIfCurrent(const QString &cacheKey, qint64 expectedRevision);
    void clear();
    void evictIfNeeded();
    void setMaximumSize(qint64 bytes);

    [[nodiscard]] qint64 currentSize() const;
    [[nodiscard]] Stats stats() const;
    [[nodiscard]] QString cacheDirectory() const { return m_cacheDirectory; }

    static QString filenameForKey(const QString &cacheKey);

#ifdef BLOOM_TESTING
    void blockWorkerForTest(QSemaphore *entered, QSemaphore *release);
#endif

private:
    QString m_cacheDirectory;
    QThread m_thread;
    ImageCacheStoreWorker *m_worker = nullptr;
};

#endif // IMAGECACHESTORE_H
