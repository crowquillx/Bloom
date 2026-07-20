#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "providers/ServerConnection.h"
#include "utils/ConfigManager.h"
#include "utils/TrackPreferencesManager.h"

namespace {
#ifndef Q_OS_LINUX
void requireLinuxConfigIsolation()
{
    QSKIP("TrackPreferencesManager config-path isolation currently relies on XDG_CONFIG_HOME and is only stable on Linux.");
}

class ScopedConfigHome
{
public:
    explicit ScopedConfigHome(const QString &)
    {
    }
};
#else
void requireLinuxConfigIsolation()
{
}

class ScopedConfigHome
{
public:
    explicit ScopedConfigHome(const QString &path)
        : m_previous(qgetenv("XDG_CONFIG_HOME"))
        , m_hadPrevious(!m_previous.isNull())
    {
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
    }

    ~ScopedConfigHome()
    {
        if (m_hadPrevious) {
            qputenv("XDG_CONFIG_HOME", m_previous);
        } else {
            qunsetenv("XDG_CONFIG_HOME");
        }
    }

private:
    QByteArray m_previous;
    bool m_hadPrevious = false;
};
#endif

void writeJsonFile(const QString &path, const QJsonObject &root)
{
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}
}

class TrackPreferencesManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsEmptyState();
    void legacySchemaIsDiscarded();
    void v2SchemaLoadsWithoutSubtitleDelay();
    void invalidJsonIsDeleted();
    void v3SchemaRoundTripsExplicitOffAndDelayPreferences();
    void saveWritesVersionedPreferencesFile();
    void identicalRemoteIdsAreIsolatedByConnection();
    void preActivationPreferencesAreAdopted();
    void legacyPreferencesFollowSoleInactiveConnection();
};

void TrackPreferencesManagerTest::missingFileLoadsEmptyState()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    TrackPreferencesManager manager;

    QVERIFY(manager.getSeasonPreferences(QStringLiteral("season-1")).isEmpty());
    QVERIFY(manager.getMoviePreferences(QStringLiteral("movie-1")).isEmpty());
}

void TrackPreferencesManagerTest::legacySchemaIsDiscarded()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    const QString preferencesPath = TrackPreferencesManager::getPreferencesPath();
    writeJsonFile(preferencesPath,
                  QJsonObject{
                      {QStringLiteral("season-1"),
                       QJsonObject{
                           {QStringLiteral("audio"), 2},
                           {QStringLiteral("subtitle"), -1}
                       }}
                  });

    TrackPreferencesManager manager;
    QVERIFY(manager.getSeasonPreferences(QStringLiteral("season-1")).isEmpty());
    QVERIFY(!QFileInfo::exists(preferencesPath));
}

void TrackPreferencesManagerTest::v2SchemaLoadsWithoutSubtitleDelay()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    const QString preferencesPath = TrackPreferencesManager::getPreferencesPath();
    writeJsonFile(preferencesPath,
                  QJsonObject{
                      {QStringLiteral("version"), 2},
                      {QStringLiteral("episodes"),
                       QJsonObject{
                           {QStringLiteral("season-1"),
                            QJsonObject{
                                {QStringLiteral("subtitle"),
                                 QJsonObject{
                                     {QStringLiteral("mode"), QStringLiteral("explicit")},
                                     {QStringLiteral("streamIndex"), 6}
                                 }}
                            }}
                       }},
                      {QStringLiteral("movies"), QJsonObject{}}
                  });

    TrackPreferencesManager manager;
    const ScopedTrackPreferences preferences = manager.getSeasonPreferences(QStringLiteral("season-1"));
    QCOMPARE(preferences.subtitle.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(preferences.subtitle.streamIndex, 6);
    QCOMPARE(preferences.subtitleDelayMs, 0);
}

void TrackPreferencesManagerTest::invalidJsonIsDeleted()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    const QString preferencesPath = TrackPreferencesManager::getPreferencesPath();
    QDir().mkpath(QFileInfo(preferencesPath).dir().absolutePath());
    QFile file(preferencesPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{ invalid json");
    file.close();

    TrackPreferencesManager manager;
    QVERIFY(manager.getSeasonPreferences(QStringLiteral("season-1")).isEmpty());
    QVERIFY(!QFileInfo::exists(preferencesPath));
}

void TrackPreferencesManagerTest::v3SchemaRoundTripsExplicitOffAndDelayPreferences()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    {
        TrackPreferencesManager manager;

        ScopedTrackPreferences seasonPrefs;
        seasonPrefs.audio.mode = TrackPreferenceMode::ExplicitStream;
        seasonPrefs.audio.streamIndex = 4;
        seasonPrefs.subtitle.mode = TrackPreferenceMode::Off;
        seasonPrefs.subtitleDelayMs = -125;
        manager.setSeasonPreferences(QStringLiteral("season-42"), seasonPrefs);

        ScopedTrackPreferences moviePrefs;
        moviePrefs.subtitle.mode = TrackPreferenceMode::ExplicitStream;
        moviePrefs.subtitle.streamIndex = 7;
        moviePrefs.subtitleDelayMs = 250;
        manager.setMoviePreferences(QStringLiteral("movie-7"), moviePrefs);

        manager.save();
    }

    TrackPreferencesManager reloaded;
    const ScopedTrackPreferences seasonPrefs = reloaded.getSeasonPreferences(QStringLiteral("season-42"));
    QCOMPARE(seasonPrefs.audio.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(seasonPrefs.audio.streamIndex, 4);
    QCOMPARE(seasonPrefs.subtitle.mode, TrackPreferenceMode::Off);
    QCOMPARE(seasonPrefs.subtitleDelayMs, -125);

    const ScopedTrackPreferences moviePrefs = reloaded.getMoviePreferences(QStringLiteral("movie-7"));
    QCOMPARE(moviePrefs.subtitle.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(moviePrefs.subtitle.streamIndex, 7);
    QCOMPARE(moviePrefs.subtitleDelayMs, 250);
}

void TrackPreferencesManagerTest::saveWritesVersionedPreferencesFile()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    TrackPreferencesManager manager;
    ScopedTrackPreferences preferences;
    preferences.audio.mode = TrackPreferenceMode::ExplicitStream;
    preferences.audio.streamIndex = 9;
    manager.setSeasonPreferences(QStringLiteral("season-9"), preferences);
    manager.save();

    QFile file(TrackPreferencesManager::getPreferencesPath());
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(-1), 4);
    const QJsonObject localScope = root.value(QStringLiteral("scopes")).toObject()
                                       .value(QStringLiteral("_local")).toObject();
    QVERIFY(localScope.value(QStringLiteral("episodes")).toObject()
                .contains(QStringLiteral("season-9")));
}

void TrackPreferencesManagerTest::identicalRemoteIdsAreIsolatedByConnection()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    ConfigManager config;
    config.load();
    ServerConnection first;
    first.connectionId = QStringLiteral("connection-1");
    first.baseUrl = QStringLiteral("https://one.example.test");
    first.accountId = QStringLiteral("user-1");
    first.credentialReference = ServerConnection::createCredentialReference(first.connectionId);
    config.upsertConnection(first);

    ServerConnection second = first;
    second.connectionId = QStringLiteral("connection-2");
    second.baseUrl = QStringLiteral("https://two.example.test");
    second.credentialReference = ServerConnection::createCredentialReference(second.connectionId);
    config.upsertConnection(second, false);
    QVERIFY(config.setActiveConnection(first.connectionId));

    TrackPreferencesManager manager(&config);
    ScopedTrackPreferences firstPreference;
    firstPreference.audio.mode = TrackPreferenceMode::ExplicitStream;
    firstPreference.audio.streamIndex = 2;
    manager.setMoviePreferences(QStringLiteral("shared-item"), firstPreference);

    QVERIFY(config.setActiveConnection(second.connectionId));
    QVERIFY(manager.getMoviePreferences(QStringLiteral("shared-item")).isEmpty());
    ScopedTrackPreferences secondPreference;
    secondPreference.audio.mode = TrackPreferenceMode::ExplicitStream;
    secondPreference.audio.streamIndex = 7;
    manager.setMoviePreferences(QStringLiteral("shared-item"), secondPreference);

    QVERIFY(config.setActiveConnection(first.connectionId));
    QCOMPARE(manager.getMoviePreferences(QStringLiteral("shared-item")).audio.streamIndex, 2);
    QVERIFY(config.setActiveConnection(second.connectionId));
    QCOMPARE(manager.getMoviePreferences(QStringLiteral("shared-item")).audio.streamIndex, 7);
}

void TrackPreferencesManagerTest::preActivationPreferencesAreAdopted()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    ConfigManager config;
    config.load();
    TrackPreferencesManager manager(&config);
    ScopedTrackPreferences preference;
    preference.audio.mode = TrackPreferenceMode::ExplicitStream;
    preference.audio.streamIndex = 4;
    manager.setMoviePreferences(QStringLiteral("movie-1"), preference);

    ServerConnection connection;
    connection.connectionId = QStringLiteral("connection-1");
    connection.baseUrl = QStringLiteral("https://one.example.test");
    connection.credentialReference = ServerConnection::createCredentialReference(
        connection.connectionId);
    config.upsertConnection(connection);

    QCOMPARE(manager.getMoviePreferences(QStringLiteral("movie-1")).audio.streamIndex, 4);
}

void TrackPreferencesManagerTest::legacyPreferencesFollowSoleInactiveConnection()
{
    requireLinuxConfigIsolation();
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigHome configHome(tempDir.path());

    writeJsonFile(TrackPreferencesManager::getPreferencesPath(),
                  QJsonObject{
                      {QStringLiteral("version"), 3},
                      {QStringLiteral("episodes"), QJsonObject{}},
                      {QStringLiteral("movies"),
                       QJsonObject{
                           {QStringLiteral("shared-item"),
                            QJsonObject{
                                {QStringLiteral("audio"),
                                 QJsonObject{
                                     {QStringLiteral("mode"), QStringLiteral("explicit")},
                                     {QStringLiteral("streamIndex"), 5}
                                 }}
                            }}
                       }}
                  });

    ConfigManager config;
    config.load();
    ServerConnection connection;
    connection.connectionId = QStringLiteral("connection-1");
    connection.baseUrl = QStringLiteral("https://one.example.test");
    connection.credentialReference = ServerConnection::createCredentialReference(connection.connectionId);
    config.upsertConnection(connection, false);

    TrackPreferencesManager manager(&config);
    QVERIFY(manager.getMoviePreferences(QStringLiteral("shared-item")).isEmpty());
    QVERIFY(config.setActiveConnection(connection.connectionId));
    QCOMPARE(manager.getMoviePreferences(QStringLiteral("shared-item")).audio.streamIndex, 5);
}

QTEST_MAIN(TrackPreferencesManagerTest)
#include "TrackPreferencesManagerTest.moc"
