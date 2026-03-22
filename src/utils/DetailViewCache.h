#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace DetailViewCache {

struct ObjectCacheEntry {
    QJsonObject data;
    qint64 timestamp = 0;

    bool hasData() const { return !data.isEmpty(); }
    bool isValid(qint64 ttl) const {
        return timestamp > 0 && (QDateTime::currentMSecsSinceEpoch() - timestamp) <= ttl;
    }
};

struct ArrayCacheEntry {
    QJsonArray data;
    qint64 timestamp = 0;

    bool hasData(bool allowEmpty) const {
        return allowEmpty ? (timestamp > 0) : !data.isEmpty();
    }
    bool isValid(qint64 ttl) const {
        return timestamp > 0 && (QDateTime::currentMSecsSinceEpoch() - timestamp) <= ttl;
    }
};

QString sanitizeCacheKey(QString key);

bool loadObjectCache(QHash<QString, ObjectCacheEntry> &memoryCache,
                     const QString &cacheKey,
                     const QString &path,
                     qint64 memoryTtl,
                     qint64 diskTtl,
                     QJsonObject &data,
                     bool requireFresh);

void storeObjectCache(QHash<QString, ObjectCacheEntry> &memoryCache,
                      const QString &cacheKey,
                      const QString &path,
                      const QJsonObject &data);

bool loadArrayCache(QHash<QString, ArrayCacheEntry> &memoryCache,
                    const QString &cacheKey,
                    const QString &path,
                    qint64 memoryTtl,
                    qint64 diskTtl,
                    QJsonArray &data,
                    bool requireFresh,
                    bool allowEmpty);

void storeArrayCache(QHash<QString, ArrayCacheEntry> &memoryCache,
                     const QString &cacheKey,
                     const QString &path,
                     const QJsonArray &data);

}
