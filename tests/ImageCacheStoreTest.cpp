#include "ui/ImageCacheStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace {

QString databaseKey(const QString &key)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

void createVersionThreeDatabase(const QString &directory,
                                const QList<QList<QVariant>> &rows)
{
    const QString connectionName = QStringLiteral("image_cache_store_seed");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(directory + QStringLiteral("/cache_index.db"));
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(R"(
            CREATE TABLE cache_entries (
                url TEXT PRIMARY KEY,
                filename TEXT NOT NULL,
                size INTEGER NOT NULL,
                last_accessed INTEGER NOT NULL,
                created_at INTEGER NOT NULL
            )
        )")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE INDEX idx_last_accessed ON cache_entries(last_accessed)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 3")));
        for (const QList<QVariant> &row : rows) {
            query.prepare(QStringLiteral(
                "INSERT INTO cache_entries "
                "(url, filename, size, last_accessed, created_at) "
                "VALUES (?, ?, ?, ?, ?)"));
            for (const QVariant &value : row) {
                query.addBindValue(value);
            }
            QVERIFY(query.exec());
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

} // namespace

class ImageCacheStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void replacementSubtractsPreviousSize();
    void evictionContinuesPastOneHundredEntries();
    void deletionFailureDoesNotStopEviction();
    void corruptDatabaseRecoversCleanly();
    void startupReconcilesRowsAndFiles();
    void clearRemovesTrackedAndOrphanFiles();
    void evictionCountsActualRemovedBytes();
    void persistedIdentityIsCredentialFree();
};

void ImageCacheStoreTest::replacementSubtractsPreviousSize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ImageCacheStore store(directory.path(), 1024 * 1024);

    const QString firstPath = store.write(QStringLiteral("artwork:item"), QByteArray(31, 'a'));
    QVERIFY(!firstPath.isEmpty());
    QCOMPARE(store.currentSize(), 31);

    const QString replacementPath =
        store.write(QStringLiteral("artwork:item"), QByteArray(7, 'b'));
    QCOMPARE(replacementPath, firstPath);
    QCOMPARE(store.currentSize(), 7);
    QFile replacement(replacementPath);
    QVERIFY(replacement.open(QIODevice::ReadOnly));
    QCOMPARE(replacement.readAll(), QByteArray(7, 'b'));

    // A file without metadata is an orphan, not a tracked replacement. If it
    // appears after startup, adopting its safe filename must add only the new
    // entry size rather than subtracting bytes that were never counted.
    const QString orphanKey = QStringLiteral("artwork:late-orphan");
    const QString orphanPath =
        directory.filePath(ImageCacheStore::filenameForKey(orphanKey));
    writeFile(orphanPath, QByteArray(40, 'o'));
    QCOMPARE(store.write(orphanKey, QByteArray(5, 'n')), orphanPath);
    QCOMPARE(store.currentSize(), 12);

    const ImageCacheStore::Stats stats = store.stats();
    QCOMPARE(stats.writes, quint64(3));
    QCOMPARE(stats.replacements, quint64(1));
}

void ImageCacheStoreTest::evictionContinuesPastOneHundredEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ImageCacheStore store(directory.path(), 1024 * 1024);

    for (int index = 0; index < 150; ++index) {
        QVERIFY(!store.write(QStringLiteral("artwork:item-%1").arg(index),
                             QByteArray(10, char('a' + index % 26)))
                     .isEmpty());
    }
    QCOMPARE(store.currentSize(), 1500);

    store.setMaximumSize(50);
    QVERIFY(store.currentSize() <= 40);
    const ImageCacheStore::Stats stats = store.stats();
    QVERIFY(stats.evictedEntries > 100);
    QCOMPARE(stats.evictedBytes, stats.evictedEntries * 10);
}

void ImageCacheStoreTest::deletionFailureDoesNotStopEviction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString blockedPath;
    {
        ImageCacheStore store(directory.path(), 1024 * 1024);
        blockedPath = store.write(QStringLiteral("artwork:blocked"), QByteArray(10, 'b'));
        QVERIFY(!blockedPath.isEmpty());
        QVERIFY(!store.write(QStringLiteral("artwork:removable"), QByteArray(10, 'r')).isEmpty());

        QVERIFY(QFile::remove(blockedPath));
        QVERIFY(QDir().mkpath(blockedPath));
        store.setMaximumSize(1);

        QCOMPARE(store.currentSize(), 0);
        const ImageCacheStore::Stats stats = store.stats();
        QVERIFY(stats.deletionFailures >= 1);
        QCOMPARE(stats.evictedEntries, quint64(1));
        QCOMPARE(stats.evictedBytes, quint64(10));
    }
    QVERIFY(QDir().rmdir(blockedPath));
}

void ImageCacheStoreTest::corruptDatabaseRecoversCleanly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeFile(directory.filePath(QStringLiteral("cache_index.db")),
              QByteArrayLiteral("not a sqlite database"));
    const QString orphanPath = directory.filePath(QStringLiteral("orphan-with-unsafe-name"));
    writeFile(orphanPath, QByteArrayLiteral("orphan"));

    ImageCacheStore store(directory.path(), 1024 * 1024);
    QVERIFY(store.isAvailable());
    QVERIFY(!QFileInfo::exists(orphanPath));
    QVERIFY(!store.write(QStringLiteral("artwork:after-recovery"), QByteArrayLiteral("ok"))
                 .isEmpty());
    const ImageCacheStore::Stats stats = store.stats();
    QVERIFY(stats.databaseRecoveries >= 1);
    QVERIFY(stats.recoveryActions >= 2);
}

void ImageCacheStoreTest::startupReconcilesRowsAndFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString presentKey = QStringLiteral("artwork:present");
    const QString missingKey = QStringLiteral("artwork:missing");
    const QString presentFilename = ImageCacheStore::filenameForKey(presentKey);
    const QString missingFilename = ImageCacheStore::filenameForKey(missingKey);
    writeFile(directory.filePath(presentFilename), QByteArrayLiteral("1234567"));
    const QString orphanPath = directory.filePath(QStringLiteral("0123456789abcdef0123456789abcdef"));
    writeFile(orphanPath, QByteArrayLiteral("orphan"));
    createVersionThreeDatabase(
        directory.path(),
        {{databaseKey(presentKey), presentFilename, 99, 1, 1},
         {databaseKey(missingKey), missingFilename, 11, 2, 2}});

    ImageCacheStore store(directory.path(), 1024 * 1024);
    QCOMPARE(store.currentSize(), 7);
    QCOMPARE(store.lookup(presentKey), directory.filePath(presentFilename));
    QVERIFY(store.lookup(missingKey).isEmpty());
    QVERIFY(!QFileInfo::exists(orphanPath));
    QVERIFY(store.stats().recoveryActions >= 3);
}

void ImageCacheStoreTest::clearRemovesTrackedAndOrphanFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ImageCacheStore store(directory.path(), 1024 * 1024);
    QVERIFY(!store.write(QStringLiteral("artwork:first"), QByteArray(9, '1')).isEmpty());
    QVERIFY(!store.write(QStringLiteral("artwork:second"), QByteArray(12, '2')).isEmpty());
    const QString orphanPath = directory.filePath(QStringLiteral("untracked.tmp"));
    writeFile(orphanPath, QByteArrayLiteral("orphan"));

    store.clear();
    QCOMPARE(store.currentSize(), 0);
    QVERIFY(store.lookup(QStringLiteral("artwork:first")).isEmpty());
    QVERIFY(store.lookup(QStringLiteral("artwork:second")).isEmpty());
    QVERIFY(!QFileInfo::exists(orphanPath));
}

void ImageCacheStoreTest::evictionCountsActualRemovedBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ImageCacheStore store(directory.path(), 1024 * 1024);
    const QString changedPath =
        store.write(QStringLiteral("artwork:changed"), QByteArray(10, 'c'));
    QVERIFY(!changedPath.isEmpty());
    QVERIFY(!store.write(QStringLiteral("artwork:other"), QByteArray(30, 'o')).isEmpty());

    writeFile(changedPath, QByteArray(50, 'x'));
    store.setMaximumSize(25);

    QCOMPARE(store.currentSize(), 0);
    const ImageCacheStore::Stats stats = store.stats();
    QCOMPARE(stats.evictedEntries, quint64(2));
    QCOMPARE(stats.evictedBytes, quint64(80));
}

void ImageCacheStoreTest::persistedIdentityIsCredentialFree()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString signedUrl = QStringLiteral(
        "https://images.example.test/poster?X-Amz-Credential=secret&X-Amz-Signature=token");
    {
        ImageCacheStore store(directory.path(), 1024 * 1024);
        const QString path = store.write(signedUrl, QByteArrayLiteral("image"));
        QVERIFY(!path.isEmpty());
        QCOMPARE(QFileInfo(path).fileName(), ImageCacheStore::filenameForKey(signedUrl));
    }

    QFile database(directory.filePath(QStringLiteral("cache_index.db")));
    QVERIFY(database.open(QIODevice::ReadOnly));
    const QByteArray bytes = database.readAll();
    QVERIFY(!bytes.contains("X-Amz-Credential"));
    QVERIFY(!bytes.contains("secret"));
    QVERIFY(!bytes.contains("X-Amz-Signature"));
    QVERIFY(!bytes.contains("token"));
}

QTEST_MAIN(ImageCacheStoreTest)
#include "ImageCacheStoreTest.moc"
