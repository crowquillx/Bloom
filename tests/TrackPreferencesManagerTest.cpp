#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

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
    void v2SchemaRoundTripsExplicitAndOffPreferences();
    void saveWritesVersionedPreferencesFile();
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

    writeJsonFile(TrackPreferencesManager::getPreferencesPath(),
                  QJsonObject{
                      {QStringLiteral("season-1"),
                       QJsonObject{
                           {QStringLiteral("audio"), 2},
                           {QStringLiteral("subtitle"), -1}
                       }}
                  });

    TrackPreferencesManager manager;
    QVERIFY(manager.getSeasonPreferences(QStringLiteral("season-1")).isEmpty());
}

void TrackPreferencesManagerTest::v2SchemaRoundTripsExplicitAndOffPreferences()
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
        manager.setSeasonPreferences(QStringLiteral("season-42"), seasonPrefs);

        ScopedTrackPreferences moviePrefs;
        moviePrefs.subtitle.mode = TrackPreferenceMode::ExplicitStream;
        moviePrefs.subtitle.streamIndex = 7;
        manager.setMoviePreferences(QStringLiteral("movie-7"), moviePrefs);

        manager.save();
    }

    TrackPreferencesManager reloaded;
    const ScopedTrackPreferences seasonPrefs = reloaded.getSeasonPreferences(QStringLiteral("season-42"));
    QCOMPARE(seasonPrefs.audio.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(seasonPrefs.audio.streamIndex, 4);
    QCOMPARE(seasonPrefs.subtitle.mode, TrackPreferenceMode::Off);

    const ScopedTrackPreferences moviePrefs = reloaded.getMoviePreferences(QStringLiteral("movie-7"));
    QCOMPARE(moviePrefs.subtitle.mode, TrackPreferenceMode::ExplicitStream);
    QCOMPARE(moviePrefs.subtitle.streamIndex, 7);
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
    QCOMPARE(root.value(QStringLiteral("version")).toInt(-1), 2);
    QVERIFY(root.value(QStringLiteral("episodes")).toObject().contains(QStringLiteral("season-9")));
}

QTEST_MAIN(TrackPreferencesManagerTest)
#include "TrackPreferencesManagerTest.moc"
