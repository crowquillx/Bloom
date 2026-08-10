#include "LibraryCacheStore.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <QLoggingCategory>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include "BloomLogging.h"

namespace {
constexpr const char *kDefaultConnectionName = "bloom_library_cache";
constexpr qint64 kDefaultTtlMs = 600000; // 10 minutes
}

LibraryCacheStore::LibraryCacheStore(const QString &dbPath, qint64 ttlMs)
    : m_dbPath(dbPath)
    , m_ttlMs(ttlMs <= 0 ? kDefaultTtlMs : ttlMs)
{
}

LibraryCacheStore::~LibraryCacheStore()
{
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.close();
    }
    QString connectionName = m_db.connectionName();
    m_db = QSqlDatabase();
    if (!connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool LibraryCacheStore::open(const QString &dbPath)
{
    QMutexLocker locker(&m_mutex);
    if (!dbPath.isEmpty()) {
        m_dbPath = dbPath;
    }
    if (m_db.isOpen()) {
        return true;
    }

    if (m_dbPath.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (base.isEmpty()) {
            base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        }
        if (base.isEmpty()) {
            qCWarning(lcLibraryCache) << "No writable cache location available";
            return false;
        }
        m_dbPath = base + "/Bloom/library_cache.db";
    }

    QDir dir(QFileInfo(m_dbPath).absolutePath());
    if (!dir.exists() && !dir.mkpath(".")) {
        qCWarning(lcLibraryCache) << "Failed to create cache directory for" << m_dbPath;
        return false;
    }

    QString connectionName = QString("%1_%2")
        .arg(kDefaultConnectionName)
        .arg(reinterpret_cast<quintptr>(this), 0, 16);

    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qCWarning(lcLibraryCache) << "Failed to open library cache DB" << m_dbPath << m_db.lastError().text();
        return false;
    }

    if (!ensureSchema()) {
        qCWarning(lcLibraryCache) << "Failed to prepare library cache schema";
        return false;
    }
    return true;
}

bool LibraryCacheStore::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_db.isOpen();
}

bool LibraryCacheStore::ensureSchema()
{
    QSqlQuery query(m_db);
    bool ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS library_cache (
            parent_id TEXT NOT NULL,
            item_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            json TEXT NOT NULL,
            updated_at INTEGER NOT NULL,
            PRIMARY KEY(parent_id, item_id)
        )
    )");
    if (!ok) {
        qCWarning(lcLibraryCache) << "Failed to create library_cache table" << query.lastError().text();
        return false;
    }

    ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS library_meta (
            parent_id TEXT PRIMARY KEY,
            total_count INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        )
    )");
    if (!ok) {
        qCWarning(lcLibraryCache) << "Failed to create library_meta table" << query.lastError().text();
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_library_cache_parent_pos ON library_cache(parent_id, position)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_library_cache_parent_updated ON library_cache(parent_id, updated_at)");
    return true;
}

LibraryCacheStore::CachedSlice LibraryCacheStore::read(const QString &parentId, int limit, int offset) const
{
    CachedSlice slice;
    if (parentId.isEmpty()) {
        return slice;
    }

    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return slice;
    }

    QSqlQuery metaQuery(m_db);
    metaQuery.prepare("SELECT total_count, updated_at FROM library_meta WHERE parent_id = ?");
    metaQuery.addBindValue(parentId);
    if (metaQuery.exec() && metaQuery.next()) {
        slice.totalCount = metaQuery.value(0).toInt();
        slice.updatedAtMs = metaQuery.value(1).toLongLong();
    }

    QSqlQuery query(m_db);
    QString sql = "SELECT json FROM library_cache WHERE parent_id = ? ORDER BY position ASC";
    if (limit > 0) {
        sql += " LIMIT ? OFFSET ?";
    }
    query.prepare(sql);
    query.addBindValue(parentId);
    if (limit > 0) {
        query.addBindValue(limit);
        query.addBindValue(offset);
    }

    if (!query.exec()) {
        qCWarning(lcLibraryCache) << "Failed to read library cache" << parentId << query.lastError().text();
        slice.decodeError = true;
        return slice;
    }

    while (query.next()) {
        const QByteArray raw = query.value(0).toByteArray();
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            slice.decodeError = true;
            continue;
        }
        slice.items.append(doc.object());
    }
    return slice;
}

bool LibraryCacheStore::replaceAll(const QString &parentId, const QJsonArray &items, int totalCount)
{
    if (parentId.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    QSqlQuery deleteQuery(m_db);
    deleteQuery.prepare("DELETE FROM library_cache WHERE parent_id = ?");
    deleteQuery.addBindValue(parentId);
    if (!deleteQuery.exec()) {
        qCWarning(lcLibraryCache) << "Failed to replace cached rows"
                                  << deleteQuery.lastError().text();
        rollbackTransaction();
        return false;
    }

    const qint64 now = nowMs();

    QSqlQuery insert(m_db);
    insert.prepare(R"(
        INSERT OR REPLACE INTO library_cache
        (parent_id, item_id, position, json, updated_at)
        VALUES (?, ?, ?, ?, ?)
    )");

    int pos = 0;
    for (const auto &val : items) {
        const QJsonObject obj = val.toObject();
        const QString itemId = obj.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty()) {
            continue;
        }
        insert.addBindValue(parentId);
        insert.addBindValue(itemId);
        insert.addBindValue(pos++);
        insert.addBindValue(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        insert.addBindValue(now);
        if (!insert.exec()) {
            qCWarning(lcLibraryCache) << "Failed to insert cache row" << insert.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    QSqlQuery meta(m_db);
    meta.prepare(R"(
        INSERT OR REPLACE INTO library_meta (parent_id, total_count, updated_at)
        VALUES (?, ?, ?)
    )");
    meta.addBindValue(parentId);
    meta.addBindValue(totalCount);
    meta.addBindValue(now);
    if (!meta.exec()) {
        qCWarning(lcLibraryCache) << "Failed to update library_meta" << meta.lastError().text();
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool LibraryCacheStore::upsertItems(const QString &parentId, const QJsonArray &items, int totalCount, bool removeMissing, int startPosition)
{
    if (parentId.isEmpty() || startPosition < 0) {
        return false;
    }

    QStringList incomingIds;
    incomingIds.reserve(items.size());
    QSet<QString> uniqueIncomingIds;
    uniqueIncomingIds.reserve(items.size());
    for (const auto &value : items) {
        const QString itemId =
            value.toObject().value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty() || uniqueIncomingIds.contains(itemId)) {
            qCWarning(lcLibraryCache)
                << "Refusing cache page with missing or duplicate item ID for"
                << parentId;
            return false;
        }
        uniqueIncomingIds.insert(itemId);
        incomingIds.append(itemId);
    }

    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    const qint64 now = nowMs();
    bool requiresPositionNormalization = false;

    if (startPosition > 0) {
        QSqlQuery prefix(m_db);
        prefix.prepare(R"(
            SELECT COUNT(*), MIN(position), MAX(position)
            FROM library_cache
            WHERE parent_id = ? AND position < ?
        )");
        prefix.addBindValue(parentId);
        prefix.addBindValue(startPosition);
        if (!prefix.exec() || !prefix.next()
            || prefix.value(0).toInt() != startPosition
            || prefix.value(1).toInt() != 0
            || prefix.value(2).toInt() != startPosition - 1) {
            qCWarning(lcLibraryCache)
                << "Refusing non-contiguous cache page for" << parentId
                << "at position" << startPosition;
            rollbackTransaction();
            return false;
        }

        if (!incomingIds.isEmpty()) {
            QStringList placeholders;
            placeholders.fill(QStringLiteral("?"), incomingIds.size());
            QSqlQuery prefixOverlap(m_db);
            prefixOverlap.prepare(
                QStringLiteral(
                    "SELECT 1 FROM library_cache "
                    "WHERE parent_id = ? AND position < ? "
                    "AND item_id IN (%1) LIMIT 1")
                    .arg(placeholders.join(QLatin1Char(','))));
            prefixOverlap.addBindValue(parentId);
            prefixOverlap.addBindValue(startPosition);
            for (const QString &itemId : incomingIds) {
                prefixOverlap.addBindValue(itemId);
            }
            if (!prefixOverlap.exec()) {
                qCWarning(lcLibraryCache)
                    << "Failed to check cache page prefix overlap"
                    << prefixOverlap.lastError().text();
                rollbackTransaction();
                return false;
            }
            if (prefixOverlap.next()) {
                qCWarning(lcLibraryCache)
                    << "Refusing cache page that overlaps its retained prefix for"
                    << parentId << "at position" << startPosition;
                rollbackTransaction();
                return false;
            }
        }
    }

    if (!items.isEmpty()) {
        QSqlQuery clearRange(m_db);
        clearRange.prepare(R"(
            DELETE FROM library_cache
            WHERE parent_id = ? AND position >= ? AND position < ?
        )");
        clearRange.addBindValue(parentId);
        clearRange.addBindValue(startPosition);
        clearRange.addBindValue(startPosition + items.size());
        if (!clearRange.exec()) {
            qCWarning(lcLibraryCache) << "Failed to clear replaced cache page"
                                      << clearRange.lastError().text();
            rollbackTransaction();
            return false;
        }

        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), incomingIds.size());
        QSqlQuery clearPreviousPositions(m_db);
        clearPreviousPositions.prepare(
            QStringLiteral(
                "DELETE FROM library_cache "
                "WHERE parent_id = ? AND item_id IN (%1)")
                .arg(placeholders.join(QLatin1Char(','))));
        clearPreviousPositions.addBindValue(parentId);
        for (const QString &itemId : incomingIds) {
            clearPreviousPositions.addBindValue(itemId);
        }
        if (!clearPreviousPositions.exec()) {
            qCWarning(lcLibraryCache)
                << "Failed to clear previous cache positions"
                << clearPreviousPositions.lastError().text();
            rollbackTransaction();
            return false;
        }
        requiresPositionNormalization =
            clearPreviousPositions.numRowsAffected() > 0;
    }

    QSqlQuery upsert(m_db);
    upsert.prepare(R"(
        INSERT OR REPLACE INTO library_cache
        (parent_id, item_id, position, json, updated_at)
        VALUES (?, ?, ?, ?, ?)
    )");

    int pos = startPosition;
    for (const auto &val : items) {
        const QJsonObject obj = val.toObject();
        const QString itemId = obj.value(QStringLiteral("itemId")).toString();
        upsert.addBindValue(parentId);
        upsert.addBindValue(itemId);
        upsert.addBindValue(pos++);
        upsert.addBindValue(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        upsert.addBindValue(now);
        if (!upsert.exec()) {
            qCWarning(lcLibraryCache) << "Failed to upsert cache row" << upsert.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    if (removeMissing && !incomingIds.isEmpty()) {
        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), incomingIds.size());
        QSqlQuery prune(m_db);
        prune.prepare(QStringLiteral("DELETE FROM library_cache WHERE parent_id = ? AND item_id NOT IN (%1)")
                      .arg(placeholders.join(QLatin1Char(','))));
        prune.addBindValue(parentId);
        for (const auto &id : incomingIds) {
            prune.addBindValue(id);
        }
        if (!prune.exec()) {
            qCWarning(lcLibraryCache) << "Failed to prune cached rows"
                                      << prune.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    // Moving an item from a retained prefix or suffix into this page leaves a
    // positional hole after its old row is removed. Re-number the remaining
    // snapshot in one transaction so future page-contiguity checks and reads
    // observe one coherent sequence.
    if (requiresPositionNormalization) {
        QList<qint64> orderedRowIds;
        QSqlQuery orderedRows(m_db);
        orderedRows.prepare(
            "SELECT rowid FROM library_cache "
            "WHERE parent_id = ? ORDER BY position ASC, rowid ASC");
        orderedRows.addBindValue(parentId);
        if (!orderedRows.exec()) {
            qCWarning(lcLibraryCache)
                << "Failed to enumerate cache positions"
                << orderedRows.lastError().text();
            rollbackTransaction();
            return false;
        }
        while (orderedRows.next()) {
            orderedRowIds.append(orderedRows.value(0).toLongLong());
        }

        QSqlQuery reposition(m_db);
        reposition.prepare(
            "UPDATE library_cache SET position = ? WHERE rowid = ?");
        for (qsizetype index = 0; index < orderedRowIds.size(); ++index) {
            reposition.addBindValue(index);
            reposition.addBindValue(orderedRowIds.at(index));
            if (!reposition.exec()) {
                qCWarning(lcLibraryCache)
                    << "Failed to normalize cache positions"
                    << reposition.lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }

    if (!removeMissing && totalCount >= 0) {
        QSqlQuery trim(m_db);
        trim.prepare(
            "DELETE FROM library_cache WHERE parent_id = ? AND position >= ?");
        trim.addBindValue(parentId);
        trim.addBindValue(totalCount);
        if (!trim.exec()) {
            qCWarning(lcLibraryCache) << "Failed to trim cached rows"
                                      << trim.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    QSqlQuery meta(m_db);
    meta.prepare(R"(
        INSERT OR REPLACE INTO library_meta (parent_id, total_count, updated_at)
        VALUES (?, ?, ?)
    )");
    meta.addBindValue(parentId);
    meta.addBindValue(totalCount);
    meta.addBindValue(now);
    if (!meta.exec()) {
        qCWarning(lcLibraryCache) << "Failed to update library_meta" << meta.lastError().text();
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool LibraryCacheStore::clearParent(const QString &parentId)
{
    if (parentId.isEmpty()) {
        return false;
    }
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM library_cache WHERE parent_id = ?");
    del.addBindValue(parentId);
    if (!del.exec()) {
        rollbackTransaction();
        return false;
    }

    QSqlQuery meta(m_db);
    meta.prepare("DELETE FROM library_meta WHERE parent_id = ?");
    meta.addBindValue(parentId);
    if (!meta.exec()) {
        rollbackTransaction();
        return false;
    }
    return commitTransaction();
}

void LibraryCacheStore::clearAll()
{
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        return;
    }
    QSqlQuery q(m_db);
    q.exec("DELETE FROM library_cache");
    q.exec("DELETE FROM library_meta");
}

bool LibraryCacheStore::CachedSlice::isFresh(qint64 ttlMs) const
{
    if (!hasData()) {
        return false;
    }
    return (QDateTime::currentMSecsSinceEpoch() - updatedAtMs) < ttlMs;
}

bool LibraryCacheStore::beginTransaction() const
{
    QSqlQuery q(m_db);
    return q.exec("BEGIN TRANSACTION");
}

bool LibraryCacheStore::commitTransaction() const
{
    QSqlQuery q(m_db);
    return q.exec("COMMIT");
}

bool LibraryCacheStore::rollbackTransaction() const
{
    QSqlQuery q(m_db);
    return q.exec("ROLLBACK");
}

qint64 LibraryCacheStore::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}
