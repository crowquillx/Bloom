#include "ImageCacheStore.h"

#include "utils/BloomLogging.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

constexpr int kSchemaVersion = 3;

bool isDatabaseFile(const QString &name)
{
    return name == QStringLiteral("cache_index.db")
        || name == QStringLiteral("cache_index.db-journal")
        || name == QStringLiteral("cache_index.db-shm")
        || name == QStringLiteral("cache_index.db-wal");
}

bool isSafeCacheFilename(const QString &filename)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{32}$"));
    return pattern.match(filename).hasMatch();
}

QString databaseKeyFor(const QString &cacheKey)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool isSafeDatabaseKey(const QString &databaseKey)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(databaseKey).hasMatch();
}

template<typename Function>
auto onWorker(QObject *worker, Function &&function)
{
    using Result = std::invoke_result_t<Function>;
    if (QThread::currentThread() == worker->thread()) {
        if constexpr (std::is_void_v<Result>) {
            std::forward<Function>(function)();
            return;
        } else {
            return std::forward<Function>(function)();
        }
    }

    if constexpr (std::is_void_v<Result>) {
        QMetaObject::invokeMethod(worker,
                                  std::forward<Function>(function),
                                  Qt::BlockingQueuedConnection);
    } else {
        std::optional<Result> result;
        QMetaObject::invokeMethod(worker,
                                  [&result, function = std::forward<Function>(function)]() mutable {
                                      result.emplace(function());
                                  },
                                  Qt::BlockingQueuedConnection);
        return std::move(result).value();
    }
}

} // namespace

class ImageCacheStoreWorker final : public QObject
{
public:
    ImageCacheStoreWorker(QString cacheDirectory, qint64 maximumSizeBytes)
        : m_cacheDirectory(std::move(cacheDirectory))
        , m_databasePath(m_cacheDirectory + QStringLiteral("/cache_index.db"))
        , m_maximumSize(std::max<qint64>(0, maximumSizeBytes))
    {
    }

    void initialize()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!QDir().mkpath(m_cacheDirectory)) {
            qCWarning(lcImageCache) << "Unable to create image cache directory";
            return;
        }

        const bool databaseExisted = QFileInfo::exists(m_databasePath);
        if (!openDatabase()) {
            recoverDatabase();
        }
        if (!m_database.isOpen()) {
            return;
        }

        int schemaVersion = -1;
        {
            QSqlQuery versionQuery(m_database);
            if (versionQuery.exec(QStringLiteral("PRAGMA user_version")) && versionQuery.next()) {
                schemaVersion = versionQuery.value(0).toInt();
            }
        }
        if (schemaVersion < 0) {
            recoverDatabase();
        } else {
            if (databaseExisted && schemaVersion < kSchemaVersion) {
                // Older indexes persisted caller-provided identities. Reset the
                // database itself so authenticated URLs cannot remain in live,
                // WAL, or freelist pages. Artwork identities remain stable
                // because callers continue to address entries by ArtworkRef.
                ++m_stats.recoveryActions;
                closeDatabase();
                removeDatabaseFiles();
                removeAllDataFiles();
                if (!openDatabase()) {
                    recoverDatabase();
                }
            }
        }

        if (!m_database.isOpen()) {
            return;
        }
        {
            QSqlQuery setVersion(m_database);
            setVersion.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
        }
        reconcile();
        evictIfNeeded();
    }

    void shutdown()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        closeDatabase();
    }

    [[nodiscard]] bool isAvailable() const { return m_database.isOpen(); }

    QString lookup(const QString &cacheKey, bool updateAccessTime)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen()) {
            ++m_stats.diskMisses;
            return {};
        }

        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "SELECT filename, size FROM cache_entries WHERE url = ?"));
        const QString databaseKey = databaseKeyFor(cacheKey);
        query.addBindValue(databaseKey);
        if (!query.exec() || !query.next()) {
            ++m_stats.diskMisses;
            return {};
        }

        const QString filename = query.value(0).toString();
        const qint64 recordedSize = query.value(1).toLongLong();
        query.finish();
        if (!validFilenameForKey(databaseKey, filename)) {
            deleteRow(databaseKey);
            ++m_stats.diskMisses;
            ++m_stats.recoveryActions;
            return {};
        }

        const QString path = dataPath(filename);
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile() || info.isSymLink()) {
            deleteRow(databaseKey);
            m_currentSize = std::max<qint64>(0, m_currentSize - recordedSize);
            ++m_stats.diskMisses;
            ++m_stats.recoveryActions;
            return {};
        }

        const qint64 actualSize = info.size();
        if (actualSize != recordedSize) {
            updateRecordedSize(databaseKey, actualSize);
            m_currentSize = std::max<qint64>(0, m_currentSize - recordedSize + actualSize);
            ++m_stats.recoveryActions;
        }
        if (updateAccessTime) {
            touch(cacheKey);
        }
        ++m_stats.diskHits;
        return path;
    }

    QString write(const QString &cacheKey, const QByteArray &data)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen() || cacheKey.isEmpty() || data.isEmpty()) {
            return {};
        }

        const QString filename = ImageCacheStore::filenameForKey(cacheKey);
        const QString databaseKey = databaseKeyFor(cacheKey);
        const Entry previousEntry = findEntry(databaseKey);
        const QString path = dataPath(filename);
        const QFileInfo previousInfo(path);
        const qint64 previousSize = previousInfo.isFile() && !previousInfo.isSymLink()
            ? previousInfo.size()
            : 0;
        const qint64 trackedPreviousSize = previousEntry.valid ? previousSize : 0;

        if (!m_database.transaction()) {
            qCWarning(lcImageCache) << "Failed to start image cache write transaction";
            return {};
        }

        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(data) != data.size()
            || !file.commit()) {
            qCWarning(lcImageCache) << "Failed to atomically write image cache entry";
            file.cancelWriting();
            m_database.rollback();
            return {};
        }

        const qint64 now = nextTimestamp();
        bool metadataUpdated = false;
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral(R"(
                INSERT INTO cache_entries (url, filename, size, last_accessed, created_at)
                VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(url) DO UPDATE SET
                    filename = excluded.filename,
                    size = excluded.size,
                    last_accessed = excluded.last_accessed
            )"));
            query.addBindValue(databaseKey);
            query.addBindValue(filename);
            query.addBindValue(data.size());
            query.addBindValue(now);
            query.addBindValue(now);
            metadataUpdated = query.exec();
        }
        if (!metadataUpdated || !m_database.commit()) {
            m_database.rollback();
            qCWarning(lcImageCache) << "Failed to commit image cache metadata";
            if (!previousEntry.valid) {
                QFile::remove(path);
                return {};
            }
            // Atomic replacement has already installed the new bytes. Preserve
            // accurate real-file accounting even if SQLite must repair the row
            // on a later lookup/startup.
            m_currentSize = std::max<qint64>(0,
                                             m_currentSize - trackedPreviousSize + data.size());
            updateRecordedSize(databaseKey, data.size());
            ++m_stats.writes;
            ++m_stats.replacements;
            ++m_stats.recoveryActions;
            evictIfNeeded();
            return path;
        }

        m_currentSize = std::max<qint64>(
            0, m_currentSize - trackedPreviousSize + data.size());
        ++m_stats.writes;
        if (previousEntry.valid) {
            ++m_stats.replacements;
        }
        evictIfNeeded();
        return QFileInfo::exists(path) ? path : QString();
    }

    void touch(const QString &cacheKey)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen()) {
            return;
        }
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "UPDATE cache_entries SET last_accessed = ? WHERE url = ?"));
        query.addBindValue(nextTimestamp());
        query.addBindValue(databaseKeyFor(cacheKey));
        query.exec();
    }

    void invalidate(const QString &cacheKey)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen()) {
            return;
        }
        const Entry entry = findEntry(databaseKeyFor(cacheKey));
        if (!entry.valid) {
            return;
        }
        removeEntry(entry, false);
    }

    void clear()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen()) {
            removeAllDataFiles();
            m_currentSize = 0;
            return;
        }

        const QList<Entry> entries = allEntries();
        QSet<QString> retainedFiles;
        for (const Entry &entry : entries) {
            if (!validFilenameForKey(entry.databaseKey, entry.filename)) {
                deleteRow(entry.databaseKey);
                ++m_stats.recoveryActions;
                continue;
            }
            if (!removeEntry(entry, false)) {
                retainedFiles.insert(entry.filename);
            }
        }

        const QFileInfoList files = QDir(m_cacheDirectory).entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &file : files) {
            if (isDatabaseFile(file.fileName()) || retainedFiles.contains(file.fileName())) {
                continue;
            }
            if (!QFile::remove(file.absoluteFilePath())) {
                ++m_stats.deletionFailures;
            }
        }
        recomputeCurrentSize();
    }

    void setMaximumSize(qint64 bytes)
    {
        m_maximumSize = std::max<qint64>(0, bytes);
        evictIfNeeded();
    }

    void evictIfNeeded()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen() || m_currentSize <= m_maximumSize) {
            return;
        }

        const qint64 target = m_maximumSize * 8 / 10;
        const QList<Entry> candidates = allEntries(true);
        for (const Entry &entry : candidates) {
            if (m_currentSize <= target) {
                break;
            }
            removeEntry(entry, true);
        }

        if (m_currentSize > target) {
            qCWarning(lcImageCache)
                << "Image cache could not reach eviction target after trying all entries"
                << "remaining bytes:" << m_currentSize << "target bytes:" << target;
        }
    }

    [[nodiscard]] qint64 currentSize() const { return m_currentSize; }
    [[nodiscard]] ImageCacheStore::Stats stats() const { return m_stats; }

private:
    struct Entry {
        QString databaseKey;
        QString filename;
        qint64 recordedSize = 0;
        bool valid = false;
    };

    bool openDatabase()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        closeDatabase();
        m_connectionName = QStringLiteral("bloom_image_cache_%1")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_database.setDatabaseName(m_databasePath);
        if (!m_database.open()) {
            qCWarning(lcImageCache) << "Failed to open image cache database:"
                                    << m_database.lastError().text();
            closeDatabase();
            return false;
        }

        bool initialized = false;
        QString initializationError;
        {
            QSqlQuery query(m_database);
            initialized = query.exec(QStringLiteral(R"(
                              CREATE TABLE IF NOT EXISTS cache_entries (
                                  url TEXT PRIMARY KEY,
                                  filename TEXT NOT NULL,
                                  size INTEGER NOT NULL,
                                  last_accessed INTEGER NOT NULL,
                                  created_at INTEGER NOT NULL
                              )
                          )"))
                && query.exec(QStringLiteral(
                    "CREATE INDEX IF NOT EXISTS idx_last_accessed "
                    "ON cache_entries(last_accessed)"));
            initializationError = query.lastError().text();
        }
        if (!initialized) {
            qCWarning(lcImageCache) << "Failed to initialize image cache database:"
                                    << initializationError;
            closeDatabase();
            return false;
        }
        return true;
    }

    void closeDatabase()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_connectionName.isEmpty()) {
            if (m_database.isOpen()) {
                m_database.close();
            }
            m_database = QSqlDatabase();
            const QString connectionName = std::exchange(m_connectionName, {});
            QSqlDatabase::removeDatabase(connectionName);
        }
    }

    void recoverDatabase()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        ++m_stats.databaseRecoveries;
        ++m_stats.recoveryActions;
        closeDatabase();
        removeDatabaseFiles();
        if (!openDatabase()) {
            qCWarning(lcImageCache) << "Image cache database is unavailable; disk cache disabled";
            return;
        }
        {
            QSqlQuery version(m_database);
            version.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
        }
    }

    void removeDatabaseFiles()
    {
        for (const QString &suffix : {QString(), QStringLiteral("-journal"),
                                      QStringLiteral("-shm"), QStringLiteral("-wal")}) {
            const QString path = m_databasePath + suffix;
            if (QFileInfo::exists(path) && !QFile::remove(path)) {
                ++m_stats.deletionFailures;
            }
        }
    }

    void removeAllDataFiles()
    {
        const QFileInfoList files = QDir(m_cacheDirectory).entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &file : files) {
            if (!isDatabaseFile(file.fileName()) && !QFile::remove(file.absoluteFilePath())) {
                ++m_stats.deletionFailures;
            }
        }
    }

    void reconcile()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!m_database.isOpen()) {
            return;
        }

        QList<Entry> entries = allEntries();
        QSet<QString> referenced;
        m_currentSize = 0;
        if (!m_database.transaction()) {
            qCWarning(lcImageCache) << "Failed to start image cache recovery transaction";
            return;
        }

        bool transactionOk = true;
        for (const Entry &entry : entries) {
            if (!validFilenameForKey(entry.databaseKey, entry.filename)) {
                transactionOk = deleteRow(entry.databaseKey, false) && transactionOk;
                ++m_stats.recoveryActions;
                continue;
            }

            const QFileInfo info(dataPath(entry.filename));
            if (!info.exists() || !info.isFile() || info.isSymLink()) {
                transactionOk = deleteRow(entry.databaseKey, false) && transactionOk;
                ++m_stats.recoveryActions;
                continue;
            }

            referenced.insert(entry.filename);
            m_currentSize += info.size();
            if (info.size() != entry.recordedSize) {
                transactionOk = updateRecordedSize(entry.databaseKey, info.size()) && transactionOk;
                ++m_stats.recoveryActions;
            }
        }

        if (!transactionOk || !m_database.commit()) {
            m_database.rollback();
            qCWarning(lcImageCache) << "Failed to commit image cache recovery transaction";
        }

        const QFileInfoList files = QDir(m_cacheDirectory).entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &file : files) {
            if (isDatabaseFile(file.fileName()) || referenced.contains(file.fileName())) {
                continue;
            }
            if (QFile::remove(file.absoluteFilePath())) {
                ++m_stats.recoveryActions;
            } else {
                ++m_stats.deletionFailures;
            }
        }
    }

    QList<Entry> allEntries(bool oldestFirst = false) const
    {
        QList<Entry> entries;
        if (!m_database.isOpen()) {
            return entries;
        }
        QSqlQuery query(m_database);
        const QString statement = oldestFirst
            ? QStringLiteral(
                  "SELECT url, filename, size FROM cache_entries "
                  "ORDER BY last_accessed ASC, rowid ASC")
            : QStringLiteral("SELECT url, filename, size FROM cache_entries");
        if (!query.exec(statement)) {
            return entries;
        }
        while (query.next()) {
            entries.append({query.value(0).toString(), query.value(1).toString(),
                            query.value(2).toLongLong(), true});
        }
        return entries;
    }

    Entry findEntry(const QString &databaseKey) const
    {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "SELECT filename, size FROM cache_entries WHERE url = ?"));
        query.addBindValue(databaseKey);
        if (!query.exec() || !query.next()) {
            return {};
        }
        return {databaseKey, query.value(0).toString(), query.value(1).toLongLong(), true};
    }

    bool removeEntry(const Entry &entry, bool eviction)
    {
        if (!entry.valid || !validFilenameForKey(entry.databaseKey, entry.filename)) {
            if (entry.valid) {
                deleteRow(entry.databaseKey);
                ++m_stats.recoveryActions;
            }
            return true;
        }

        const QString path = dataPath(entry.filename);
        const QFileInfo info(path);
        const qint64 actualSize = info.isFile() && !info.isSymLink() ? info.size() : 0;
        if (actualSize != entry.recordedSize) {
            m_currentSize = std::max<qint64>(0,
                                             m_currentSize - entry.recordedSize + actualSize);
            updateRecordedSize(entry.databaseKey, actualSize);
            ++m_stats.recoveryActions;
        }

        const bool fileExisted = info.exists();
        if (fileExisted && !QFile::remove(path)) {
            ++m_stats.deletionFailures;
            return false;
        }

        if (eviction && fileExisted) {
            ++m_stats.evictedEntries;
            m_stats.evictedBytes += static_cast<quint64>(actualSize);
        }

        if (!deleteRow(entry.databaseKey)) {
            // The file is gone, so its real bytes no longer count even if a
            // stale row must be reconciled on the next lookup/startup.
            m_currentSize = std::max<qint64>(0, m_currentSize - actualSize);
            return false;
        }

        m_currentSize = std::max<qint64>(0, m_currentSize - actualSize);
        return true;
    }

    bool deleteRow(const QString &databaseKey, bool ownTransaction = true)
    {
        if (ownTransaction && !m_database.transaction()) {
            return false;
        }
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("DELETE FROM cache_entries WHERE url = ?"));
        query.addBindValue(databaseKey);
        const bool ok = query.exec();
        if (!ownTransaction) {
            return ok;
        }
        if (ok && m_database.commit()) {
            return true;
        }
        m_database.rollback();
        return false;
    }

    bool updateRecordedSize(const QString &databaseKey, qint64 size)
    {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("UPDATE cache_entries SET size = ? WHERE url = ?"));
        query.addBindValue(size);
        query.addBindValue(databaseKey);
        return query.exec();
    }

    void recomputeCurrentSize()
    {
        m_currentSize = 0;
        for (const Entry &entry : allEntries()) {
            if (!validFilenameForKey(entry.databaseKey, entry.filename)) {
                continue;
            }
            const QFileInfo info(dataPath(entry.filename));
            if (info.isFile() && !info.isSymLink()) {
                m_currentSize += info.size();
            }
        }
    }

    bool validFilenameForKey(const QString &databaseKey, const QString &filename) const
    {
        return isSafeCacheFilename(filename)
            && isSafeDatabaseKey(databaseKey)
            && filename == databaseKey.left(32);
    }

    QString dataPath(const QString &filename) const
    {
        return m_cacheDirectory + QLatin1Char('/') + filename;
    }

    qint64 nextTimestamp()
    {
        m_lastTimestamp = std::max(QDateTime::currentMSecsSinceEpoch(), m_lastTimestamp + 1);
        return m_lastTimestamp;
    }

    QString m_cacheDirectory;
    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
    qint64 m_maximumSize = 0;
    qint64 m_currentSize = 0;
    qint64 m_lastTimestamp = 0;
    ImageCacheStore::Stats m_stats;
};

ImageCacheStore::ImageCacheStore(QString cacheDirectory, qint64 maximumSizeBytes)
    : m_cacheDirectory(QDir::cleanPath(std::move(cacheDirectory)))
    , m_worker(new ImageCacheStoreWorker(m_cacheDirectory, maximumSizeBytes))
{
    m_thread.setObjectName(QStringLiteral("BloomImageCacheStore"));
    m_worker->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    onWorker(m_worker, [worker = m_worker]() { worker->initialize(); });
}

ImageCacheStore::~ImageCacheStore()
{
    if (!m_worker) {
        return;
    }
    onWorker(m_worker, [worker = m_worker]() { worker->shutdown(); });
    m_thread.quit();
    m_thread.wait();
    m_worker = nullptr;
}

bool ImageCacheStore::isAvailable() const
{
    return onWorker(m_worker, [worker = m_worker]() { return worker->isAvailable(); });
}

QString ImageCacheStore::lookup(const QString &cacheKey, bool updateAccessTime)
{
    return onWorker(m_worker, [worker = m_worker, cacheKey, updateAccessTime]() {
        return worker->lookup(cacheKey, updateAccessTime);
    });
}

QString ImageCacheStore::write(const QString &cacheKey, const QByteArray &data)
{
    return onWorker(m_worker, [worker = m_worker, cacheKey, data]() {
        return worker->write(cacheKey, data);
    });
}

void ImageCacheStore::touch(const QString &cacheKey)
{
    onWorker(m_worker, [worker = m_worker, cacheKey]() { worker->touch(cacheKey); });
}

void ImageCacheStore::invalidate(const QString &cacheKey)
{
    onWorker(m_worker, [worker = m_worker, cacheKey]() { worker->invalidate(cacheKey); });
}

void ImageCacheStore::clear()
{
    onWorker(m_worker, [worker = m_worker]() { worker->clear(); });
}

void ImageCacheStore::evictIfNeeded()
{
    onWorker(m_worker, [worker = m_worker]() { worker->evictIfNeeded(); });
}

void ImageCacheStore::setMaximumSize(qint64 bytes)
{
    onWorker(m_worker, [worker = m_worker, bytes]() { worker->setMaximumSize(bytes); });
}

qint64 ImageCacheStore::currentSize() const
{
    return onWorker(m_worker, [worker = m_worker]() { return worker->currentSize(); });
}

ImageCacheStore::Stats ImageCacheStore::stats() const
{
    return onWorker(m_worker, [worker = m_worker]() { return worker->stats(); });
}

QString ImageCacheStore::filenameForKey(const QString &cacheKey)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .left(32));
}
