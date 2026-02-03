#include "CacheMigrator.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

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
        qInfo() << "Migrating cache from Reef to Bloom:" << oldCachePath << "->" << newCachePath;
        if (oldCacheDir.rename(oldCachePath, newCachePath)) {
            qInfo() << "Cache migration successful";
        } else {
            qWarning() << "Cache migration failed, will use new cache location";
        }
    }
}
