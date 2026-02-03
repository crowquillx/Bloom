#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class LibraryCacheStore
{
public:
    struct CachedSlice {
        QJsonArray items;
        int totalCount = 0;
        qint64 updatedAtMs = 0;
        bool hasData() const { return !items.isEmpty(); }
        bool isFresh(qint64 ttlMs) const;
    };

    explicit LibraryCacheStore(const QString &dbPath = QString(), qint64 ttlMs = 600000);
    ~LibraryCacheStore();

    bool open(const QString &dbPath = QString());
    bool isOpen() const;

    CachedSlice read(const QString &parentId, int limit = 0, int offset = 0) const;
    bool replaceAll(const QString &parentId, const QJsonArray &items, int totalCount);
    bool upsertItems(const QString &parentId, const QJsonArray &items, int totalCount, bool removeMissing = false, int startPosition = 0);

    bool clearParent(const QString &parentId);
    void clearAll();

    QString path() const { return m_dbPath; }
    void setTtlMs(qint64 ttlMs) { m_ttlMs = ttlMs; }

private:
    bool ensureSchema();
    bool beginTransaction() const;
    bool commitTransaction() const;
    bool rollbackTransaction() const;
    qint64 nowMs() const;

    QString m_dbPath;
    mutable QSqlDatabase m_db;
    mutable QMutex m_mutex;
    qint64 m_ttlMs;
};






