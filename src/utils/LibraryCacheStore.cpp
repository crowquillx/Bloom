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
#include <QStringList>

Q_LOGGING_CATEGORY(libraryCacheStore, "bloom.librarycache")

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
            qCWarning(libraryCacheStore) << "No writable cache location available";
            return false;
        }
        m_dbPath = base + "/Bloom/library_cache.db";
    }

    QDir dir(QFileInfo(m_dbPath).absolutePath());
    if (!dir.exists() && !dir.mkpath(".")) {
        qCWarning(libraryCacheStore) << "Failed to create cache directory for" << m_dbPath;
        return false;
    }

    QString connectionName = QString("%1_%2")
        .arg(kDefaultConnectionName)
        .arg(reinterpret_cast<quintptr>(this), 0, 16);

    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qCWarning(libraryCacheStore) << "Failed to open library cache DB" << m_dbPath << m_db.lastError().text();
        return false;
    }

    if (!ensureSchema()) {
        qCWarning(libraryCacheStore) << "Failed to prepare library cache schema";
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
        qCWarning(libraryCacheStore) << "Failed to create library_cache table" << query.lastError().text();
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
        qCWarning(libraryCacheStore) << "Failed to create library_meta table" << query.lastError().text();
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
        qCWarning(libraryCacheStore) << "Failed to read library cache" << parentId << query.lastError().text();
        return slice;
    }

    while (query.next()) {
        const QByteArray raw = query.value(0).toByteArray();
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
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
    deleteQuery.exec();

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
        const QString itemId = obj.value("Id").toString();
        if (itemId.isEmpty()) {
            continue;
        }
        insert.addBindValue(parentId);
        insert.addBindValue(itemId);
        insert.addBindValue(pos++);
        insert.addBindValue(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        insert.addBindValue(now);
        if (!insert.exec()) {
            qCWarning(libraryCacheStore) << "Failed to insert cache row" << insert.lastError().text();
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
        qCWarning(libraryCacheStore) << "Failed to update library_meta" << meta.lastError().text();
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool LibraryCacheStore::upsertItems(const QString &parentId, const QJsonArray &items, int totalCount, bool removeMissing, int startPosition)
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

    const qint64 now = nowMs();
    QSqlQuery upsert(m_db);
    upsert.prepare(R"(
        INSERT OR REPLACE INTO library_cache
        (parent_id, item_id, position, json, updated_at)
        VALUES (?, ?, ?, ?, ?)
    )");

    int pos = startPosition;
    QStringList incomingIds;
    incomingIds.reserve(items.size());
    for (const auto &val : items) {
        const QJsonObject obj = val.toObject();
        const QString itemId = obj.value("Id").toString();
        if (itemId.isEmpty()) {
            continue;
        }
        incomingIds.append(itemId);
        upsert.addBindValue(parentId);
        upsert.addBindValue(itemId);
        upsert.addBindValue(pos++);
        upsert.addBindValue(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        upsert.addBindValue(now);
        if (!upsert.exec()) {
            qCWarning(libraryCacheStore) << "Failed to upsert cache row" << upsert.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    if (removeMissing && !incomingIds.isEmpty()) {
        QStringList placeholders;
        placeholders.fill("?", incomingIds.size());
        QSqlQuery prune(m_db);
        prune.prepare(QStringLiteral("DELETE FROM library_cache WHERE parent_id = ? AND item_id NOT IN (%1)")
                      .arg(placeholders.join(",")));
        prune.addBindValue(parentId);
        for (const auto &id : incomingIds) {
            prune.addBindValue(id);
        }
        prune.exec();
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
        qCWarning(libraryCacheStore) << "Failed to update library_meta" << meta.lastError().text();
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
    del.exec();

    QSqlQuery meta(m_db);
    meta.prepare("DELETE FROM library_meta WHERE parent_id = ?");
    meta.addBindValue(parentId);
    meta.exec();
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
    if (updatedAtMs <= 0) {
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






