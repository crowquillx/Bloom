#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "network/SessionManager.h"
#include "providers/ServerConnection.h"
#include "security/CredentialStore.h"
#include "security/ISecretStore.h"
#include "utils/ConfigManager.h"

namespace {

class ScopedConfigIsolation
{
public:
    explicit ScopedConfigIsolation(const QString &path)
        : m_previousConfigHome(qgetenv("XDG_CONFIG_HOME"))
        , m_previousAppData(qgetenv("APPDATA"))
        , m_previousHome(qgetenv("HOME"))
        , m_hadPreviousConfigHome(!m_previousConfigHome.isNull())
        , m_hadPreviousAppData(!m_previousAppData.isNull())
        , m_hadPreviousHome(!m_previousHome.isNull())
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_CONFIG_HOME", path.toUtf8());
        qputenv("APPDATA", path.toUtf8());
        qputenv("HOME", path.toUtf8());
        QDir().mkpath(path + QStringLiteral("/Library/Preferences"));
    }

    ~ScopedConfigIsolation()
    {
        restore("XDG_CONFIG_HOME", m_previousConfigHome, m_hadPreviousConfigHome);
        restore("APPDATA", m_previousAppData, m_hadPreviousAppData);
        restore("HOME", m_previousHome, m_hadPreviousHome);
        QStandardPaths::setTestModeEnabled(false);
    }

private:
    static void restore(const char *name, const QByteArray &value, bool hadPrevious)
    {
        if (hadPrevious) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    QByteArray m_previousConfigHome;
    QByteArray m_previousAppData;
    QByteArray m_previousHome;
    bool m_hadPreviousConfigHome;
    bool m_hadPreviousAppData;
    bool m_hadPreviousHome;
};

class FakeSecretStore : public ISecretStore
{
public:
    bool setSecret(const QString &service, const QString &account, const QString &secret) override
    {
        m_lastError.clear();
        if (failWrites) {
            m_lastError = QStringLiteral("write failed");
            return false;
        }
        if (discardWrites) {
            return true;
        }
        secrets[key(service, account)] = secret;
        return true;
    }

    QString getSecret(const QString &service, const QString &account) override
    {
        m_lastError.clear();
        if (failReads
            || (failLegacyReads && service == CredentialStore::legacyJellyfinServiceName())) {
            m_lastError = QStringLiteral("read failed");
            return {};
        }
        return secrets.value(key(service, account));
    }

    bool deleteSecret(const QString &service, const QString &account) override
    {
        m_lastError.clear();
        if (failDeletes) {
            m_lastError = QStringLiteral("delete failed");
            return false;
        }
        secrets.remove(key(service, account));
        return true;
    }

    QString lastError() const override { return m_lastError; }

    QStringList listAccounts(const QString &service) override
    {
        QStringList accounts;
        const QString prefix = service + QLatin1Char('\n');
        for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                accounts.append(it.key().mid(prefix.size()));
            }
        }
        return accounts;
    }

    static QString key(const QString &service, const QString &account)
    {
        return service + QLatin1Char('\n') + account;
    }

    QHash<QString, QString> secrets;
    bool failWrites = false;
    bool discardWrites = false;
    bool failReads = false;
    bool failLegacyReads = false;
    bool failDeletes = false;
    QString m_lastError;
};

QJsonObject legacyV27Config(const QString &accessToken = QStringLiteral("legacy-config-token"))
{
    QJsonObject jellyfin;
    jellyfin[QStringLiteral("server_url")] = QStringLiteral("https://media.example.test/");
    jellyfin[QStringLiteral("user_id")] = QStringLiteral("user-1");
    jellyfin[QStringLiteral("username")] = QStringLiteral("Alice");
    jellyfin[QStringLiteral("access_token")] = accessToken;

    QJsonObject settings;
    settings[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("completion_threshold"), 90}};
    settings[QStringLiteral("jellyfin")] = jellyfin;

    QJsonObject config;
    config[QStringLiteral("version")] = 27;
    config[QStringLiteral("settings")] = settings;
    return config;
}

void writeConfig(const QJsonObject &config)
{
    QVERIFY(QDir().mkpath(ConfigManager::getConfigDir()));
    QFile file(ConfigManager::getConfigPath());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
}

ServerConnection jellyfinConnection()
{
    ServerConnection connection;
    connection.connectionId = QStringLiteral("connection-1");
    connection.providerKind = ProviderKind::Jellyfin;
    connection.protocolMode = ProtocolMode::Native;
    connection.baseUrl = QStringLiteral("https://media.example.test");
    connection.accountId = QStringLiteral("user-1");
    connection.profileId = QStringLiteral("user-1");
    connection.username = QStringLiteral("Alice");
    connection.displayName = QStringLiteral("Alice");
    connection.credentialReference = ServerConnection::createCredentialReference(connection.connectionId);
    return connection;
}

} // namespace

class ConnectionPersistenceTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultConfigHasEmptyConnectionSchema();
    void v27MigrationCreatesActiveJellyfinConnectionAndRetainsRollbackMetadata();
    void v27MigrationRepairsIncompleteConnectionSchema();
    void v27MigrationUsesStableConnectionIdentityBeforeFirstSave();
    void finalizeMigrationRemovesLegacyMetadataAndConfigToken();
    void jellyfinFacadeUpsertsStableConnectionWithoutPersistingToken();
    void pendingLegacyTokenIsNotAttachedToAnotherActiveAccount();
    void loggingOutAnotherAccountRetainsPendingLegacyMigration();
    void loggingOutMatchingAccountClearsPendingLegacyMigration();
    void jellyfinFacadeDoesNotOverwriteAnotherServerOrAccount();
    void connectionPersistenceSupportsMultipleServers();
    void connectionScopedSettingsPreventRemoteIdCollisions();
    void preActivationConnectionStateIsAdopted();
    void reservedConnectionScopeIdsAreRejected();
    void inactiveSoleConnectionReceivesV28StateMigration();
    void credentialStoreMigratesLegacyEntryAfterVerifiedCopy();
    void credentialStoreFindsLegacyEntryWithOriginalTrailingSlashUrl();
    void credentialStoreRetainsLegacyEntryWhenCopyFails();
    void credentialStoreRetainsLegacyEntryWhenVerificationFails();
    void credentialStoreCleansLegacyEntryWhenNewCredentialAlreadyExists();
    void credentialStoreReportsCleanupFailureSeparately();
    void credentialStoreTreatsLegacyProbeFailureAsCleanupOnly();
    void credentialStorePrefersCurrentCredentialOverConfigFallback();
    void credentialStoreMigratesConfigFallbackWhenSecureEntriesAreEmpty();
    void credentialStoreDoesNotOverwriteCredentialWhenSecureReadFails();
    void deviceRotationMigratesLegacyConfigFallback();
    void deviceRotationAbortsWhenActiveCredentialsCannotBePreserved();
};

void ConnectionPersistenceTest::defaultConfigHasEmptyConnectionSchema()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();

    QVERIFY(config.getConnections().isEmpty());
    QVERIFY(!config.getActiveConnection().has_value());

    QFile file(ConfigManager::getConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject settings = QJsonDocument::fromJson(file.readAll()).object()
                                     .value(QStringLiteral("settings")).toObject();
    const QJsonObject state = settings.value(QStringLiteral("connection_state")).toObject();
    QCOMPARE(state.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(state.value(QStringLiteral("scopes")).toObject().isEmpty());
}

void ConnectionPersistenceTest::v27MigrationCreatesActiveJellyfinConnectionAndRetainsRollbackMetadata()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config());

    ConfigManager config;
    config.load();

    const auto active = config.getActiveConnection();
    QVERIFY(active.has_value());
    QCOMPARE(active->providerKind, ProviderKind::Jellyfin);
    QCOMPARE(active->protocolMode, ProtocolMode::Native);
    QCOMPARE(active->baseUrl, QStringLiteral("https://media.example.test"));
    QCOMPARE(active->accountId, QStringLiteral("user-1"));
    QCOMPARE(active->profileId, QStringLiteral("user-1"));
    QCOMPARE(active->username, QStringLiteral("Alice"));
    QVERIFY(!active->connectionId.isEmpty());
    QVERIFY(!active->credentialReference.isEmpty());
    QVERIFY(config.hasPendingLegacyJellyfinMigration());
    QCOMPARE(config.getJellyfinSession().accessToken, QStringLiteral("legacy-config-token"));
}

void ConnectionPersistenceTest::v27MigrationRepairsIncompleteConnectionSchema()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    QJsonObject legacy = legacyV27Config();
    QJsonObject settings = legacy.value(QStringLiteral("settings")).toObject();
    settings[QStringLiteral("connections")] = QJsonObject{
        {QStringLiteral("unexpected"), true}
    };
    legacy[QStringLiteral("settings")] = settings;
    writeConfig(legacy);

    ConfigManager config;
    config.load();

    const auto active = config.getActiveConnection();
    QVERIFY(active.has_value());
    QCOMPARE(active->accountId, QStringLiteral("user-1"));
    QCOMPARE(config.getConnections().size(), 1);
    config.save();

    QFile file(ConfigManager::getConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject persistedSettings = QJsonDocument::fromJson(file.readAll()).object()
                                              .value(QStringLiteral("settings")).toObject();
    const QJsonObject connections = persistedSettings.value(QStringLiteral("connections")).toObject();
    QCOMPARE(connections.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(connections.value(QStringLiteral("active")).isString());
    QVERIFY(connections.value(QStringLiteral("items")).isArray());
    QCOMPARE(persistedSettings.value(QStringLiteral("playback")).toObject()
                 .value(QStringLiteral("completion_threshold")).toInt(), 90);
}

void ConnectionPersistenceTest::v27MigrationUsesStableConnectionIdentityBeforeFirstSave()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    const QJsonObject legacy = legacyV27Config();
    writeConfig(legacy);

    ConfigManager firstLoad;
    firstLoad.load();
    const QString firstId = firstLoad.getActiveConnection()->connectionId;

    writeConfig(legacy);
    ConfigManager secondLoad;
    secondLoad.load();
    QCOMPARE(secondLoad.getActiveConnection()->connectionId, firstId);
}

void ConnectionPersistenceTest::finalizeMigrationRemovesLegacyMetadataAndConfigToken()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config());

    ConfigManager config;
    config.load();
    config.finalizeLegacyJellyfinMigration();

    QVERIFY(!config.hasPendingLegacyJellyfinMigration());
    QVERIFY(config.getActiveConnection().has_value());
    QVERIFY(config.getJellyfinSession().accessToken.isEmpty());

    QFile file(ConfigManager::getConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(!persisted.contains("legacy-config-token"));
    const QJsonObject root = QJsonDocument::fromJson(persisted).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 29);
    QVERIFY(!root.value(QStringLiteral("settings")).toObject().contains(QStringLiteral("jellyfin")));
}

void ConnectionPersistenceTest::jellyfinFacadeUpsertsStableConnectionWithoutPersistingToken()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setJellyfinSession(QStringLiteral("https://media.example.test/"),
                               QStringLiteral("user-1"),
                               QStringLiteral("must-not-be-persisted"),
                               QStringLiteral("Alice"));
    const QString connectionId = config.getActiveConnection()->connectionId;

    config.setJellyfinSession(QStringLiteral("https://media.example.test"),
                               QStringLiteral("user-1"),
                               QStringLiteral("another-secret"),
                               QStringLiteral("Alice Updated"));

    QCOMPARE(config.getConnections().size(), 1);
    QCOMPARE(config.getActiveConnection()->connectionId, connectionId);
    QCOMPARE(config.getActiveConnection()->accountId, QStringLiteral("user-1"));
    QCOMPARE(config.getActiveConnection()->username, QStringLiteral("Alice Updated"));

    QFile file(ConfigManager::getConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(!persisted.contains("must-not-be-persisted"));
    QVERIFY(!persisted.contains("another-secret"));
}

void ConnectionPersistenceTest::pendingLegacyTokenIsNotAttachedToAnotherActiveAccount()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config(QStringLiteral("alice-token")));

    ConfigManager config;
    config.load();
    ServerConnection bob = jellyfinConnection();
    bob.connectionId = QStringLiteral("bob-connection");
    bob.accountId = QStringLiteral("user-2");
    bob.profileId = QStringLiteral("user-2");
    bob.username = QStringLiteral("Bob");
    bob.displayName = QStringLiteral("Bob");
    bob.credentialReference = ServerConnection::createCredentialReference(bob.connectionId);
    config.upsertConnection(bob);

    const ConfigManager::SessionData activeSession = config.getJellyfinSession();
    QCOMPARE(activeSession.userId, QStringLiteral("user-2"));
    QCOMPARE(activeSession.username, QStringLiteral("Bob"));
    QVERIFY(activeSession.accessToken.isEmpty());
    QCOMPARE(config.getPendingLegacyJellyfinSession().accessToken,
             QStringLiteral("alice-token"));
}

void ConnectionPersistenceTest::loggingOutAnotherAccountRetainsPendingLegacyMigration()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config(QStringLiteral("alice-token")));

    ConfigManager config;
    config.load();
    ServerConnection bob = jellyfinConnection();
    bob.connectionId = QStringLiteral("bob-connection");
    bob.accountId = QStringLiteral("user-2");
    bob.profileId = QStringLiteral("user-2");
    bob.username = QStringLiteral("Bob");
    bob.displayName = QStringLiteral("Bob");
    bob.credentialReference = ServerConnection::createCredentialReference(bob.connectionId);
    config.upsertConnection(bob);

    config.clearJellyfinSession();

    QVERIFY(!config.getActiveConnection().has_value());
    QVERIFY(config.hasPendingLegacyJellyfinMigration());
    QVERIFY(config.getJellyfinSession().accessToken.isEmpty());
    QCOMPARE(config.getPendingLegacyJellyfinSession().accessToken,
             QStringLiteral("alice-token"));
}

void ConnectionPersistenceTest::loggingOutMatchingAccountClearsPendingLegacyMigration()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config(QStringLiteral("alice-token")));

    ConfigManager config;
    config.load();
    config.clearJellyfinSession();

    QVERIFY(!config.getActiveConnection().has_value());
    QVERIFY(!config.hasPendingLegacyJellyfinMigration());
}

void ConnectionPersistenceTest::jellyfinFacadeDoesNotOverwriteAnotherServerOrAccount()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setJellyfinSession(QStringLiteral("https://one.example.test"),
                              QStringLiteral("user-1"),
                              QString(),
                              QStringLiteral("Alice"));
    const QString firstId = config.getActiveConnection()->connectionId;

    config.setJellyfinSession(QStringLiteral("https://two.example.test"),
                              QStringLiteral("user-2"),
                              QString(),
                              QStringLiteral("Bob"));
    QCOMPARE(config.getConnections().size(), 2);
    QVERIFY(config.getConnection(firstId).has_value());
    QVERIFY(config.getActiveConnection()->connectionId != firstId);

    config.setJellyfinSession(QStringLiteral("https://two.example.test"),
                              QStringLiteral("user-3"),
                              QString(),
                              QStringLiteral("Carol"));
    QCOMPARE(config.getConnections().size(), 3);
    QCOMPARE(config.getConnection(firstId)->username, QStringLiteral("Alice"));
}

void ConnectionPersistenceTest::connectionPersistenceSupportsMultipleServers()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    ServerConnection first = jellyfinConnection();
    config.upsertConnection(first);

    ServerConnection second = first;
    second.connectionId = QStringLiteral("connection-2");
    second.baseUrl = QStringLiteral("https://other.example.test/");
    second.credentialReference = ServerConnection::createCredentialReference(second.connectionId);
    config.upsertConnection(second);

    QCOMPARE(config.getConnections().size(), 2);
    QCOMPARE(config.getActiveConnection()->connectionId, QStringLiteral("connection-2"));
    QVERIFY(config.setActiveConnection(first.connectionId));
    QCOMPARE(config.getActiveConnection()->connectionId, first.connectionId);
    config.clearActiveConnection();
    QVERIFY(!config.getActiveConnection().has_value());
    QCOMPARE(config.getConnections().size(), 2);
}

void ConnectionPersistenceTest::connectionScopedSettingsPreventRemoteIdCollisions()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    QJsonObject legacy = legacyV27Config(QString());
    QJsonObject settings = legacy.value(QStringLiteral("settings")).toObject();
    settings[QStringLiteral("library_profiles")] = QJsonObject{
        {QStringLiteral("shared-library"), QStringLiteral("High Quality")}
    };
    settings[QStringLiteral("series_profiles")] = QJsonObject{
        {QStringLiteral("shared-series"), QStringLiteral("Low Quality")}
    };
    settings[QStringLiteral("library_startup_buffering_modes")] = QJsonObject{
        {QStringLiteral("shared-library"), QStringLiteral("remote-mount")}
    };
    legacy[QStringLiteral("settings")] = settings;
    writeConfig(legacy);

    ConfigManager config;
    config.load();
    const ServerConnection first = *config.getActiveConnection();
    QCOMPARE(config.getLibraryProfile(QStringLiteral("shared-library")),
             QStringLiteral("High Quality"));
    QCOMPARE(config.getSeriesProfile(QStringLiteral("shared-series")),
             QStringLiteral("Low Quality"));
    QCOMPARE(config.getLibraryStartupBufferingMode(QStringLiteral("shared-library")),
             QStringLiteral("remote-mount"));

    ServerConnection second = first;
    second.connectionId = QStringLiteral("connection-2");
    second.baseUrl = QStringLiteral("https://other.example.test");
    second.credentialReference = ServerConnection::createCredentialReference(second.connectionId);
    config.upsertConnection(second);

    QVERIFY(config.getLibraryProfile(QStringLiteral("shared-library")).isEmpty());
    QVERIFY(config.getSeriesProfile(QStringLiteral("shared-series")).isEmpty());
    QVERIFY(config.getLibraryStartupBufferingMode(QStringLiteral("shared-library")).isEmpty());
    config.setLibraryProfile(QStringLiteral("shared-library"), QStringLiteral("Medium Quality"));

    QVERIFY(config.setActiveConnection(first.connectionId));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("shared-library")),
             QStringLiteral("High Quality"));
    QVERIFY(config.setActiveConnection(second.connectionId));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("shared-library")),
             QStringLiteral("Medium Quality"));
}

void ConnectionPersistenceTest::preActivationConnectionStateIsAdopted()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.setLibraryProfile(QStringLiteral("library-1"), QStringLiteral("High Quality"));
    config.setLibraryStartupBufferingMode(QStringLiteral("library-1"),
                                          QStringLiteral("remote-mount"));

    ServerConnection connection = jellyfinConnection();
    config.upsertConnection(connection);

    QCOMPARE(config.getLibraryProfile(QStringLiteral("library-1")),
             QStringLiteral("High Quality"));
    QCOMPARE(config.getLibraryStartupBufferingMode(QStringLiteral("library-1")),
             QStringLiteral("remote-mount"));
}

void ConnectionPersistenceTest::reservedConnectionScopeIdsAreRejected()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    ServerConnection connection = jellyfinConnection();
    connection.connectionId = QStringLiteral(" _pending ");
    connection.credentialReference = ServerConnection::createCredentialReference(
        connection.connectionId);
    config.upsertConnection(connection);

    QVERIFY(config.getConnections().isEmpty());
    QVERIFY(!config.getActiveConnection().has_value());
}

void ConnectionPersistenceTest::inactiveSoleConnectionReceivesV28StateMigration()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    const ServerConnection connection = jellyfinConnection();
    QJsonObject settings;
    settings[QStringLiteral("playback")] = QJsonObject{
        {QStringLiteral("completion_threshold"), 90}
    };
    settings[QStringLiteral("connections")] = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("active"), QString()},
        {QStringLiteral("items"), QJsonArray{connection.toJson()}}
    };
    settings[QStringLiteral("library_profiles")] = QJsonObject{
        {QStringLiteral("library-1"), QStringLiteral("High Quality")}
    };
    settings[QStringLiteral("series_profiles")] = QJsonObject{};
    settings[QStringLiteral("library_startup_buffering_modes")] = QJsonObject{};
    writeConfig(QJsonObject{
        {QStringLiteral("version"), 28},
        {QStringLiteral("settings"), settings}
    });

    ConfigManager config;
    config.load();
    QVERIFY(!config.getActiveConnection().has_value());
    QVERIFY(config.setActiveConnection(connection.connectionId));
    QCOMPARE(config.getLibraryProfile(QStringLiteral("library-1")),
             QStringLiteral("High Quality"));
}

void ConnectionPersistenceTest::credentialStoreMigratesLegacyEntryAfterVerifiedCopy()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"));
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("legacy-token"));

    CredentialStore credentials(&secretStore);
    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("legacy-token"));
    QVERIFY(result.usedLegacyEntry);
    QVERIFY(result.migratedLegacyEntry);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(credentials.read(connection, CredentialKind::AccessToken),
             QStringLiteral("legacy-token"));
    QVERIFY(secretStore.getSecret(CredentialStore::legacyJellyfinServiceName(), legacyAccount).isEmpty());
}

void ConnectionPersistenceTest::credentialStoreFindsLegacyEntryWithOriginalTrailingSlashUrl()
{
    FakeSecretStore secretStore;
    ServerConnection connection = jellyfinConnection();
    connection.username = QStringLiteral("Alice Renamed");
    const QString originalUrl = QStringLiteral("https://media.example.test/");
    const QString legacyUsername = QStringLiteral("Alice");
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"), originalUrl, legacyUsername);
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("legacy-token"));

    CredentialStore credentials(&secretStore);
    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"), originalUrl, legacyUsername);

    QCOMPARE(result.secret, QStringLiteral("legacy-token"));
    QVERIFY(result.migratedLegacyEntry);
    QVERIFY(secretStore.getSecret(CredentialStore::legacyJellyfinServiceName(), legacyAccount).isEmpty());
}

void ConnectionPersistenceTest::credentialStoreRetainsLegacyEntryWhenCopyFails()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"));
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("legacy-token"));
    secretStore.failWrites = true;

    CredentialStore credentials(&secretStore);
    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("legacy-token"));
    QVERIFY(result.usedLegacyEntry);
    QVERIFY(!result.migratedLegacyEntry);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(secretStore.getSecret(CredentialStore::legacyJellyfinServiceName(), legacyAccount),
             QStringLiteral("legacy-token"));
}

void ConnectionPersistenceTest::credentialStoreRetainsLegacyEntryWhenVerificationFails()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"));
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("legacy-token"));
    secretStore.discardWrites = true;

    CredentialStore credentials(&secretStore);
    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("legacy-token"));
    QVERIFY(!result.migratedLegacyEntry);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(secretStore.getSecret(CredentialStore::legacyJellyfinServiceName(), legacyAccount),
             QStringLiteral("legacy-token"));
}

void ConnectionPersistenceTest::credentialStoreCleansLegacyEntryWhenNewCredentialAlreadyExists()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"));
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("old-token"));

    CredentialStore credentials(&secretStore);
    QVERIFY(credentials.write(connection, CredentialKind::AccessToken,
                              QStringLiteral("current-token")));
    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("current-token"));
    QVERIFY(!result.usedLegacyEntry);
    QVERIFY(result.migratedLegacyEntry);
    QVERIFY(secretStore.getSecret(CredentialStore::legacyJellyfinServiceName(), legacyAccount).isEmpty());
}

void ConnectionPersistenceTest::credentialStoreReportsCleanupFailureSeparately()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    const QString legacyAccount = CredentialStore::legacyJellyfinAccountKey(
        connection, QStringLiteral("device-1"));
    secretStore.setSecret(CredentialStore::legacyJellyfinServiceName(),
                          legacyAccount,
                          QStringLiteral("old-token"));

    CredentialStore credentials(&secretStore);
    QVERIFY(credentials.write(connection, CredentialKind::AccessToken,
                              QStringLiteral("current-token")));
    secretStore.failDeletes = true;

    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("current-token"));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.cleanupError, QStringLiteral("delete failed"));
    QVERIFY(!result.migratedLegacyEntry);
}

void ConnectionPersistenceTest::credentialStoreTreatsLegacyProbeFailureAsCleanupOnly()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    CredentialStore credentials(&secretStore);
    QVERIFY(credentials.write(connection, CredentialKind::AccessToken,
                              QStringLiteral("current-token")));
    secretStore.failLegacyReads = true;

    const CredentialReadResult result = credentials.readAccessToken(
        connection, QStringLiteral("device-1"));

    QCOMPARE(result.secret, QStringLiteral("current-token"));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.cleanupError, QStringLiteral("read failed"));
}

void ConnectionPersistenceTest::credentialStorePrefersCurrentCredentialOverConfigFallback()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    CredentialStore credentials(&secretStore);
    QVERIFY(credentials.write(connection, CredentialKind::AccessToken,
                              QStringLiteral("current-token")));

    const CredentialReadResult result = credentials.readAccessToken(
        connection,
        QStringLiteral("device-1"),
        QString(),
        QString(),
        QStringLiteral("stale-config-token"));

    QCOMPARE(result.secret, QStringLiteral("current-token"));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(credentials.read(connection, CredentialKind::AccessToken),
             QStringLiteral("current-token"));
}

void ConnectionPersistenceTest::credentialStoreMigratesConfigFallbackWhenSecureEntriesAreEmpty()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    CredentialStore credentials(&secretStore);

    const CredentialReadResult result = credentials.readAccessToken(
        connection,
        QStringLiteral("device-1"),
        QString(),
        QString(),
        QStringLiteral("legacy-config-token"));

    QCOMPARE(result.secret, QStringLiteral("legacy-config-token"));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(credentials.read(connection, CredentialKind::AccessToken),
             QStringLiteral("legacy-config-token"));
}

void ConnectionPersistenceTest::credentialStoreDoesNotOverwriteCredentialWhenSecureReadFails()
{
    FakeSecretStore secretStore;
    const ServerConnection connection = jellyfinConnection();
    CredentialStore credentials(&secretStore);
    QVERIFY(credentials.write(connection, CredentialKind::AccessToken,
                              QStringLiteral("current-token")));
    secretStore.failReads = true;

    const CredentialReadResult result = credentials.readAccessToken(
        connection,
        QStringLiteral("device-1"),
        QString(),
        QString(),
        QStringLiteral("stale-config-token"));

    QVERIFY(result.secret.isEmpty());
    QCOMPARE(result.error, QStringLiteral("read failed"));
    const QString account = CredentialStore::accountKey(
        connection, CredentialKind::AccessToken);
    QCOMPARE(secretStore.secrets.value(FakeSecretStore::key(CredentialStore::serviceName(), account)),
             QStringLiteral("current-token"));
}

void ConnectionPersistenceTest::deviceRotationMigratesLegacyConfigFallback()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(legacyV27Config(QStringLiteral("legacy-config-token")));

    ConfigManager config;
    config.load();
    FakeSecretStore secretStore;
    SessionManager sessionManager(&config, &secretStore);

    QVERIFY(sessionManager.rotateDeviceId());
    CredentialStore credentials(&secretStore);
    QCOMPARE(credentials.read(*config.getActiveConnection(), CredentialKind::AccessToken),
             QStringLiteral("legacy-config-token"));
}

void ConnectionPersistenceTest::deviceRotationAbortsWhenActiveCredentialsCannotBePreserved()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(jellyfinConnection());
    FakeSecretStore secretStore;
    SessionManager sessionManager(&config, &secretStore);
    const QString originalDeviceId = sessionManager.deviceId();

    QVERIFY(!sessionManager.rotateDeviceId());
    QCOMPARE(sessionManager.deviceId(), originalDeviceId);
}

QTEST_MAIN(ConnectionPersistenceTest)
#include "ConnectionPersistenceTest.moc"
