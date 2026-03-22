#include "DetailViewCache.h"

namespace DetailViewCache {

QString sanitizeCacheKey(QString key)
{
    key.replace('/', '_');
    key.replace('\\', '_');
    key.replace("..", "_");

    for (QChar &character : key) {
        if (!character.isLetterOrNumber() && character != '_' && character != '-') {
            character = '_';
        }
    }

    return key.isEmpty() ? QStringLiteral("item") : key;
}

bool loadObjectCache(QHash<QString, ObjectCacheEntry> &memoryCache,
                     const QString &cacheKey,
                     const QString &path,
                     qint64 memoryTtl,
                     qint64 diskTtl,
                     QJsonObject &data,
                     bool requireFresh)
{
    if (memoryCache.contains(cacheKey)) {
        const auto &entry = memoryCache[cacheKey];
        if (entry.hasData() && (!requireFresh || entry.isValid(memoryTtl))) {
            data = entry.data;
            return true;
        }
    }

    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    ObjectCacheEntry entry;
    entry.timestamp = static_cast<qint64>(doc.object().value("timestamp").toDouble());
    entry.data = doc.object().value("data").toObject();

    if (!entry.hasData()) {
        return false;
    }

    if (requireFresh && !entry.isValid(diskTtl)) {
        return false;
    }

    memoryCache[cacheKey] = entry;
    data = entry.data;
    return true;
}

void storeObjectCache(QHash<QString, ObjectCacheEntry> &memoryCache,
                      const QString &cacheKey,
                      const QString &path,
                      const QJsonObject &data)
{
    ObjectCacheEntry entry;
    entry.data = data;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    memoryCache[cacheKey] = entry;

    if (path.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    root.insert("timestamp", static_cast<double>(entry.timestamp));
    root.insert("data", data);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool loadArrayCache(QHash<QString, ArrayCacheEntry> &memoryCache,
                    const QString &cacheKey,
                    const QString &path,
                    qint64 memoryTtl,
                    qint64 diskTtl,
                    QJsonArray &data,
                    bool requireFresh,
                    bool allowEmpty)
{
    if (memoryCache.contains(cacheKey)) {
        const auto &entry = memoryCache[cacheKey];
        if (entry.hasData(allowEmpty) && (!requireFresh || entry.isValid(memoryTtl))) {
            data = entry.data;
            return true;
        }
    }

    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    ArrayCacheEntry entry;
    entry.timestamp = static_cast<qint64>(doc.object().value("timestamp").toDouble());
    entry.data = doc.object().value("items").toArray();

    if (!entry.hasData(allowEmpty)) {
        return false;
    }

    if (requireFresh && !entry.isValid(diskTtl)) {
        return false;
    }

    memoryCache[cacheKey] = entry;
    data = entry.data;
    return true;
}

void storeArrayCache(QHash<QString, ArrayCacheEntry> &memoryCache,
                     const QString &cacheKey,
                     const QString &path,
                     const QJsonArray &data)
{
    ArrayCacheEntry entry;
    entry.data = data;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    memoryCache[cacheKey] = entry;

    if (path.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    root.insert("timestamp", static_cast<double>(entry.timestamp));
    root.insert("items", data);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

}