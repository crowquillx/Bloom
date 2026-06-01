#include "CacheMigrator.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include "BloomLogging.h"

CacheMigrator::CacheMigrator(QObject *parent)
    : QObject(parent)
{
}

void CacheMigrator::migrate()
{
    QString oldCachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation).replace("/Bloom", "/Reef");
    QString newCachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir oldCacheDir(oldCachePath);
    QDir newCacheDir(newCachePath);
    
    if (oldCacheDir.exists() && !newCacheDir.exists()) {
        qCInfo(lcCache) << "Migrating cache from Reef to Bloom:" << oldCachePath << "->" << newCachePath;
        if (oldCacheDir.rename(oldCachePath, newCachePath)) {
            qCInfo(lcCache) << "Cache migration successful";
        } else {
            qCWarning(lcCache) << "Cache migration failed, will use new cache location";
        }
    }
}
