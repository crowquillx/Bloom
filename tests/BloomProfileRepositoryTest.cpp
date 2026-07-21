#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "profiles/BloomProfile.h"
#include "profiles/BloomProfileRepository.h"
#include "providers/ServerConnection.h"
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

ServerConnection makeConnection(const QString &connectionId,
                                const QString &baseUrl,
                                const QString &accountId,
                                const QString &username)
{
    ServerConnection connection;
    connection.connectionId = connectionId;
    connection.providerKind = ProviderKind::Jellyfin;
    connection.protocolMode = ProtocolMode::Native;
    connection.baseUrl = baseUrl;
    connection.accountId = accountId;
    connection.profileId = accountId;
    connection.username = username;
    connection.displayName = username;
    connection.credentialReference = ServerConnection::createCredentialReference(connectionId);
    return connection;
}

QJsonObject minimalV29Config()
{
    QJsonObject connections;
    connections[QStringLiteral("version")] = 1;
    connections[QStringLiteral("active")] = QString();
    connections[QStringLiteral("items")] = QJsonArray();

    QJsonObject connectionState;
    connectionState[QStringLiteral("version")] = 1;
    connectionState[QStringLiteral("scopes")] = QJsonObject();

    QJsonObject settings;
    settings[QStringLiteral("playback")] = QJsonObject{{QStringLiteral("completion_threshold"), 90}};
    settings[QStringLiteral("connections")] = connections;
    settings[QStringLiteral("connection_state")] = connectionState;

    QJsonObject config;
    config[QStringLiteral("version")] = 29;
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

QJsonObject readPersistedRoot()
{
    QFile file(ConfigManager::getConfigPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool jsonContainsForbiddenSecretFields(const QJsonValue &value)
{
    static const QSet<QString> forbiddenKeys = {
        QStringLiteral("base_url"),
        QStringLiteral("server_url"),
        QStringLiteral("server_id"),
        QStringLiteral("server_name"),
        QStringLiteral("account_id"),
        QStringLiteral("username"),
        QStringLiteral("display_name"),
        QStringLiteral("profile_id"),
        QStringLiteral("credential_reference"),
        QStringLiteral("access_token"),
        QStringLiteral("refresh_token"),
        QStringLiteral("password"),
        QStringLiteral("api_key"),
    };

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (forbiddenKeys.contains(it.key())) {
                return true;
            }
            if (jsonContainsForbiddenSecretFields(it.value())) {
                return true;
            }
        }
    } else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            if (jsonContainsForbiddenSecretFields(entry)) {
                return true;
            }
        }
    } else if (value.isString()) {
        const QString text = value.toString();
        if (text.contains(QStringLiteral("https://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("http://"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

class BloomProfileRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultConfigHasEmptyBloomProfilesBlock();
    void jsonRoundTripPreservesOrderedMembers();
    void multiServerAndTwoUsersOnSamePhysicalServer();
    void duplicateSameConnectionIsDeduped();
    void migrationSeedsOnlyActiveConnectionWithDeterministicIds();
    void migrationUsesSoleConnectionWhenNoActive();
    void migrationWritesEmptyBlockWithZeroConnections();
    void danglingMembersAreRepairedAndPersistedIdempotently();
    void whitespaceNormalizationIsPersisted();
    void malformedEntriesArePersistentlyRemoved();
    void orderingAndDefaultMemberRepair();
    void singleModeUpsertRetainsDefaultOrFirstMember();
    void crudPersistsAcrossReload();
    void requestContextGenerationStaleGuard();
    void validExternalReloadEmitsAndInvalidatesActiveContext();
    void disabledDefaultCannotBecomeRequestTarget();
    void inactiveProfileMutationDoesNotChangeActiveGeneration();
    void profileJsonExcludesCredentialAndServerIdentity();
};

void BloomProfileRepositoryTest::defaultConfigHasEmptyBloomProfilesBlock()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();

    const QJsonObject bloom = config.getBloomProfilesConfig();
    QCOMPARE(bloom.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(bloom.value(QStringLiteral("active_profile_id")).toString(), QString());
    QVERIFY(bloom.value(QStringLiteral("items")).toArray().isEmpty());

    BloomProfileRepository repo(&config);
    QVERIFY(repo.profiles().isEmpty());
    QVERIFY(!repo.activeProfile().has_value());
}

void BloomProfileRepositoryTest::jsonRoundTripPreservesOrderedMembers()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-a"),
                                           QStringLiteral("https://a.example.test"),
                                           QStringLiteral("a"),
                                           QStringLiteral("A")),
                            false);
    config.upsertConnection(makeConnection(QStringLiteral("conn-b"),
                                           QStringLiteral("https://b.example.test"),
                                           QStringLiteral("b"),
                                           QStringLiteral("B")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Merged Home");
    profile.mode = BloomProfileMode::Merged;
    BloomProfileMember first;
    first.memberId = QStringLiteral("member-1");
    first.connectionId = QStringLiteral("conn-b");
    first.enabled = true;
    first.labelOverride = QStringLiteral("Kids");
    BloomProfileMember second;
    second.memberId = QStringLiteral("member-2");
    second.connectionId = QStringLiteral("conn-a");
    second.enabled = false;
    profile.members = {first, second};
    profile.defaultMemberId = QStringLiteral("member-1");
    QVERIFY(repo.upsertProfile(profile));

    BloomProfileRepository reloaded(&config);
    QCOMPARE(reloaded.profiles().size(), 1);
    const BloomProfile stored = reloaded.profiles().first();
    QCOMPARE(stored.mode, BloomProfileMode::Merged);
    QCOMPARE(stored.members.size(), 2);
    QCOMPARE(stored.members.at(0).connectionId, QStringLiteral("conn-b"));
    QCOMPARE(stored.members.at(0).priority, 0);
    QCOMPARE(stored.members.at(0).labelOverride, QStringLiteral("Kids"));
    QCOMPARE(stored.members.at(1).connectionId, QStringLiteral("conn-a"));
    QCOMPARE(stored.members.at(1).priority, 1);
    QCOMPARE(stored.defaultMemberId, QStringLiteral("member-1"));
    QVERIFY(BloomProfile::createDeterministicMemberId(QStringLiteral("profile-a"),
                                                      QStringLiteral("conn-a"))
            != BloomProfile::createDeterministicMemberId(QStringLiteral("profile-b"),
                                                         QStringLiteral("conn-a")));
}

void BloomProfileRepositoryTest::multiServerAndTwoUsersOnSamePhysicalServer()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    const QString sharedUrl = QStringLiteral("https://shared.example.test");
    config.upsertConnection(makeConnection(QStringLiteral("conn-alice"),
                                           sharedUrl,
                                           QStringLiteral("alice"),
                                           QStringLiteral("Alice")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-bob"),
                                           sharedUrl,
                                           QStringLiteral("bob"),
                                           QStringLiteral("Bob")),
                            false);
    config.upsertConnection(makeConnection(QStringLiteral("conn-other"),
                                           QStringLiteral("https://other.example.test"),
                                           QStringLiteral("other"),
                                           QStringLiteral("Other")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Family");
    profile.mode = BloomProfileMode::Merged;
    for (const QString &connectionId : {QStringLiteral("conn-alice"),
                                        QStringLiteral("conn-bob"),
                                        QStringLiteral("conn-other")}) {
        BloomProfileMember member;
        member.connectionId = connectionId;
        profile.members.append(member);
    }
    profile.defaultMemberId = profile.members.first().memberId;
    QVERIFY(repo.upsertProfile(profile));

    const auto stored = repo.activeProfile();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->members.size(), 3);
    QCOMPARE(stored->members.at(0).connectionId, QStringLiteral("conn-alice"));
    QCOMPARE(stored->members.at(1).connectionId, QStringLiteral("conn-bob"));
    QCOMPARE(stored->members.at(2).connectionId, QStringLiteral("conn-other"));
}

void BloomProfileRepositoryTest::duplicateSameConnectionIsDeduped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://media.example.test"),
                                           QStringLiteral("user"),
                                           QStringLiteral("User")),
                            true);

    QJsonObject bloom;
    bloom[QStringLiteral("version")] = 1;
    QJsonObject memberA;
    memberA[QStringLiteral("member_id")] = QStringLiteral("m-a");
    memberA[QStringLiteral("connection_id")] = QStringLiteral("conn-1");
    memberA[QStringLiteral("enabled")] = true;
    memberA[QStringLiteral("priority")] = 0;
    QJsonObject memberB;
    memberB[QStringLiteral("member_id")] = QStringLiteral("m-b");
    memberB[QStringLiteral("connection_id")] = QStringLiteral("conn-1");
    memberB[QStringLiteral("enabled")] = true;
    memberB[QStringLiteral("priority")] = 1;
    QJsonObject profile;
    profile[QStringLiteral("id")] = QStringLiteral("profile-1");
    profile[QStringLiteral("name")] = QStringLiteral("Dup");
    profile[QStringLiteral("mode")] = QStringLiteral("merged");
    profile[QStringLiteral("members")] = QJsonArray{memberA, memberB};
    profile[QStringLiteral("default_member_id")] = QStringLiteral("m-a");
    bloom[QStringLiteral("active_profile_id")] = QStringLiteral("profile-1");
    bloom[QStringLiteral("items")] = QJsonArray{profile};
    config.setBloomProfilesConfig(bloom);

    BloomProfileRepository repo(&config);
    QCOMPARE(repo.profiles().size(), 1);
    QCOMPARE(repo.profiles().first().members.size(), 1);
    QCOMPARE(repo.profiles().first().members.first().memberId, QStringLiteral("m-a"));

    BloomProfileRepository again(&config);
    QCOMPARE(again.profiles().first().members.size(), 1);
}

void BloomProfileRepositoryTest::migrationSeedsOnlyActiveConnectionWithDeterministicIds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    QJsonObject config = minimalV29Config();
    QJsonObject settings = config.value(QStringLiteral("settings")).toObject();
    QJsonObject connections = settings.value(QStringLiteral("connections")).toObject();
    const ServerConnection active = makeConnection(QStringLiteral("conn-active"),
                                                   QStringLiteral("https://active.example.test"),
                                                   QStringLiteral("active"),
                                                   QStringLiteral("Active"));
    const ServerConnection other = makeConnection(QStringLiteral("conn-other"),
                                                  QStringLiteral("https://other.example.test"),
                                                  QStringLiteral("other"),
                                                  QStringLiteral("Other"));
    connections[QStringLiteral("active")] = active.connectionId;
    connections[QStringLiteral("items")] = QJsonArray{active.toJson(), other.toJson()};
    settings[QStringLiteral("connections")] = connections;
    config[QStringLiteral("settings")] = settings;
    writeConfig(config);

    ConfigManager first;
    first.load();
    const QJsonObject persistedAfterMigration = readPersistedRoot();
    QCOMPARE(persistedAfterMigration.value(QStringLiteral("version")).toInt(), 30);
    QCOMPARE(persistedAfterMigration.value(QStringLiteral("settings")).toObject()
                 .value(QStringLiteral("bloom_profiles")).toObject()
                 .value(QStringLiteral("items")).toArray().size(),
             1);
    ConfigManager second;
    second.load();

    const QJsonObject bloom = first.getBloomProfilesConfig();
    QCOMPARE(bloom.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(bloom.value(QStringLiteral("items")).toArray().size(), 1);
    const QJsonObject profile = bloom.value(QStringLiteral("items")).toArray().at(0).toObject();
    QCOMPARE(profile.value(QStringLiteral("mode")).toString(), QStringLiteral("single"));
    QCOMPARE(profile.value(QStringLiteral("members")).toArray().size(), 1);
    QCOMPARE(profile.value(QStringLiteral("members")).toArray().at(0).toObject()
                 .value(QStringLiteral("connection_id")).toString(),
             QStringLiteral("conn-active"));
    const QString expectedProfileId =
        BloomProfile::createDeterministicProfileId(QStringLiteral("conn-active"));
    QCOMPARE(profile.value(QStringLiteral("id")).toString(), expectedProfileId);
    QCOMPARE(profile.value(QStringLiteral("members")).toArray().at(0).toObject()
                 .value(QStringLiteral("member_id")).toString(),
             BloomProfile::createDeterministicMemberId(expectedProfileId,
                                                       QStringLiteral("conn-active")));
    QCOMPARE(second.getBloomProfilesConfig().value(QStringLiteral("items")).toArray().at(0).toObject()
                 .value(QStringLiteral("id")).toString(),
             profile.value(QStringLiteral("id")).toString());
    QCOMPARE(first.getActiveConnection()->connectionId, QStringLiteral("conn-active"));
}

void BloomProfileRepositoryTest::migrationUsesSoleConnectionWhenNoActive()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    QJsonObject config = minimalV29Config();
    QJsonObject settings = config.value(QStringLiteral("settings")).toObject();
    QJsonObject connections = settings.value(QStringLiteral("connections")).toObject();
    const ServerConnection sole = makeConnection(QStringLiteral("conn-sole"),
                                                 QStringLiteral("https://sole.example.test"),
                                                 QStringLiteral("sole"),
                                                 QStringLiteral("Sole"));
    connections[QStringLiteral("active")] = QString();
    connections[QStringLiteral("items")] = QJsonArray{sole.toJson()};
    settings[QStringLiteral("connections")] = connections;
    config[QStringLiteral("settings")] = settings;
    writeConfig(config);

    ConfigManager manager;
    manager.load();
    const QJsonObject profile = manager.getBloomProfilesConfig()
                                    .value(QStringLiteral("items")).toArray().at(0).toObject();
    QCOMPARE(profile.value(QStringLiteral("members")).toArray().at(0).toObject()
                 .value(QStringLiteral("connection_id")).toString(),
             QStringLiteral("conn-sole"));
}

void BloomProfileRepositoryTest::migrationWritesEmptyBlockWithZeroConnections()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());
    writeConfig(minimalV29Config());

    ConfigManager manager;
    manager.load();
    const QJsonObject bloom = manager.getBloomProfilesConfig();
    QCOMPARE(bloom.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(bloom.value(QStringLiteral("items")).toArray().isEmpty());
    manager.save();
    QCOMPARE(readPersistedRoot().value(QStringLiteral("version")).toInt(), 30);
}

void BloomProfileRepositoryTest::danglingMembersAreRepairedAndPersistedIdempotently()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-live"),
                                           QStringLiteral("https://live.example.test"),
                                           QStringLiteral("live"),
                                           QStringLiteral("Live")),
                            true);

    QJsonObject bloom;
    bloom[QStringLiteral("version")] = 1;
    QJsonObject live;
    live[QStringLiteral("member_id")] = QStringLiteral("m-live");
    live[QStringLiteral("connection_id")] = QStringLiteral("conn-live");
    live[QStringLiteral("enabled")] = true;
    live[QStringLiteral("priority")] = 0;
    QJsonObject dangling;
    dangling[QStringLiteral("member_id")] = QStringLiteral("m-gone");
    dangling[QStringLiteral("connection_id")] = QStringLiteral("conn-gone");
    dangling[QStringLiteral("enabled")] = true;
    dangling[QStringLiteral("priority")] = 1;
    QJsonObject profile;
    profile[QStringLiteral("id")] = QStringLiteral("profile-1");
    profile[QStringLiteral("name")] = QStringLiteral("Repair");
    profile[QStringLiteral("mode")] = QStringLiteral("merged");
    profile[QStringLiteral("members")] = QJsonArray{live, dangling};
    profile[QStringLiteral("default_member_id")] = QStringLiteral("m-gone");
    bloom[QStringLiteral("active_profile_id")] = QStringLiteral("profile-1");
    bloom[QStringLiteral("items")] = QJsonArray{profile};
    config.setBloomProfilesConfig(bloom);

    BloomProfileRepository first(&config);
    QCOMPARE(first.profiles().first().members.size(), 1);
    QCOMPARE(first.profiles().first().defaultMemberId, QStringLiteral("m-live"));

    const QJsonObject persisted = config.getBloomProfilesConfig();
    BloomProfileRepository second(&config);
    QCOMPARE(second.profiles().first().members.size(), 1);
    QCOMPARE(config.getBloomProfilesConfig(), persisted);
}

void BloomProfileRepositoryTest::whitespaceNormalizationIsPersisted()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            true);

    QJsonObject member{
        {QStringLiteral("member_id"), QStringLiteral("  member-1  ")},
        {QStringLiteral("connection_id"), QStringLiteral("  conn-1  ")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("priority"), 0}
    };
    QJsonObject profile{
        {QStringLiteral("id"), QStringLiteral("  profile-1  ")},
        {QStringLiteral("name"), QStringLiteral("  Profile One  ")},
        {QStringLiteral("mode"), QStringLiteral("single")},
        {QStringLiteral("members"), QJsonArray{member}},
        {QStringLiteral("default_member_id"), QStringLiteral("  member-1  ")}
    };
    config.setBloomProfilesConfig(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("active_profile_id"), QStringLiteral("  profile-1  ")},
        {QStringLiteral("items"), QJsonArray{profile, QStringLiteral("malformed")}}
    });

    BloomProfileRepository repo(&config);
    QCOMPARE(repo.activeProfile()->id, QStringLiteral("profile-1"));
    QCOMPARE(repo.activeProfile()->name, QStringLiteral("Profile One"));
    QCOMPARE(repo.activeProfile()->members.first().memberId, QStringLiteral("member-1"));
    QCOMPARE(repo.activeProfile()->members.first().connectionId, QStringLiteral("conn-1"));

    const QJsonObject persisted = config.getBloomProfilesConfig();
    QCOMPARE(persisted.value(QStringLiteral("active_profile_id")).toString(),
             QStringLiteral("profile-1"));
    const QJsonArray persistedItems = persisted.value(QStringLiteral("items")).toArray();
    QCOMPARE(persistedItems.size(), 1);
    const QJsonObject persistedProfile = persistedItems.first().toObject();
    QCOMPARE(persistedProfile.value(QStringLiteral("id")).toString(),
             QStringLiteral("profile-1"));
    QCOMPARE(persistedProfile.value(QStringLiteral("name")).toString(),
             QStringLiteral("Profile One"));
    const QJsonObject persistedMember = persistedProfile.value(QStringLiteral("members"))
                                            .toArray().first().toObject();
    QCOMPARE(persistedMember.value(QStringLiteral("member_id")).toString(),
             QStringLiteral("member-1"));
    QCOMPARE(persistedMember.value(QStringLiteral("connection_id")).toString(),
             QStringLiteral("conn-1"));
}

void BloomProfileRepositoryTest::malformedEntriesArePersistentlyRemoved()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            true);

    BloomProfileMember member;
    member.memberId = QStringLiteral("member-1");
    member.connectionId = QStringLiteral("conn-1");

    BloomProfile profile;
    profile.id = QStringLiteral("profile-1");
    profile.name = QStringLiteral("Profile One");
    profile.members = {member};
    profile.defaultMemberId = member.memberId;
    profile.createdAt = QDateTime::currentDateTimeUtc();
    profile.updatedAt = profile.createdAt;

    config.setBloomProfilesConfig(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("active_profile_id"), profile.id},
        {QStringLiteral("items"), QJsonArray{profile.toJson(), QStringLiteral("malformed")}}
    });

    BloomProfileRepository repo(&config);
    QCOMPARE(repo.profiles().size(), 1);
    const QJsonArray persistedItems = config.getBloomProfilesConfig()
                                          .value(QStringLiteral("items")).toArray();
    QCOMPARE(persistedItems.size(), 1);
    QCOMPARE(persistedItems.first().toObject().value(QStringLiteral("id")).toString(),
             profile.id);
}

void BloomProfileRepositoryTest::orderingAndDefaultMemberRepair()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            false);
    config.upsertConnection(makeConnection(QStringLiteral("conn-2"),
                                           QStringLiteral("https://two.example.test"),
                                           QStringLiteral("two"),
                                           QStringLiteral("Two")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Ordered");
    profile.mode = BloomProfileMode::Merged;
    BloomProfileMember disabled;
    disabled.memberId = QStringLiteral("m-1");
    disabled.connectionId = QStringLiteral("conn-1");
    disabled.enabled = false;
    disabled.priority = 99;
    BloomProfileMember enabled;
    enabled.memberId = QStringLiteral("m-2");
    enabled.connectionId = QStringLiteral("conn-2");
    enabled.enabled = true;
    enabled.priority = 5;
    profile.members = {disabled, enabled};
    profile.defaultMemberId = QStringLiteral("missing");
    QVERIFY(repo.upsertProfile(profile));

    const BloomProfile stored = repo.profiles().first();
    QCOMPARE(stored.members.at(0).priority, 0);
    QCOMPARE(stored.members.at(1).priority, 1);
    QCOMPARE(stored.defaultMemberId, QStringLiteral("m-2"));

    QVERIFY(repo.reorderMembers(stored.id, {QStringLiteral("m-2"), QStringLiteral("m-1")}));
    const BloomProfile reordered = repo.profile(stored.id).value();
    QCOMPARE(reordered.members.at(0).memberId, QStringLiteral("m-2"));
    QCOMPARE(reordered.members.at(0).priority, 0);
    QCOMPARE(reordered.members.at(1).memberId, QStringLiteral("m-1"));
    QCOMPARE(reordered.members.at(1).priority, 1);
}

void BloomProfileRepositoryTest::singleModeUpsertRetainsDefaultOrFirstMember()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            false);
    config.upsertConnection(makeConnection(QStringLiteral("conn-2"),
                                           QStringLiteral("https://two.example.test"),
                                           QStringLiteral("two"),
                                           QStringLiteral("Two")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Single");
    profile.mode = BloomProfileMode::Single;
    BloomProfileMember first;
    first.memberId = QStringLiteral("m-1");
    first.connectionId = QStringLiteral("conn-1");
    BloomProfileMember second;
    second.memberId = QStringLiteral("m-2");
    second.connectionId = QStringLiteral("conn-2");
    profile.members = {first, second};
    profile.defaultMemberId = QStringLiteral("m-2");
    QVERIFY(repo.upsertProfile(profile));

    const BloomProfile stored = repo.profiles().first();
    QCOMPARE(stored.mode, BloomProfileMode::Single);
    QCOMPARE(stored.members.size(), 1);
    QCOMPARE(stored.members.first().memberId, QStringLiteral("m-2"));
    QCOMPARE(stored.defaultMemberId, QStringLiteral("m-2"));
}

void BloomProfileRepositoryTest::crudPersistsAcrossReload()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-2"),
                                           QStringLiteral("https://two.example.test"),
                                           QStringLiteral("two"),
                                           QStringLiteral("Two")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile first;
    first.name = QStringLiteral("Alpha");
    first.mode = BloomProfileMode::Single;
    BloomProfileMember member;
    member.connectionId = QStringLiteral("conn-1");
    first.members = {member};
    QVERIFY(repo.upsertProfile(first));

    BloomProfile second;
    second.name = QStringLiteral("Beta");
    second.mode = BloomProfileMode::Merged;
    BloomProfileMember betaMember = member;
    betaMember.memberId.clear();
    BloomProfileMember extra;
    extra.connectionId = QStringLiteral("conn-2");
    second.members = {betaMember, extra};
    QVERIFY(repo.upsertProfile(second));
    QVERIFY(repo.setActiveProfile(second.id));
    QVERIFY(repo.removeProfile(first.id));

    BloomProfileRepository reloaded(&config);
    QCOMPARE(reloaded.profiles().size(), 1);
    QCOMPARE(reloaded.activeProfile()->name, QStringLiteral("Beta"));
    QCOMPARE(reloaded.activeProfile()->members.size(), 2);
}

void BloomProfileRepositoryTest::requestContextGenerationStaleGuard()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-2"),
                                           QStringLiteral("https://two.example.test"),
                                           QStringLiteral("two"),
                                           QStringLiteral("Two")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Ctx");
    profile.mode = BloomProfileMode::Merged;
    BloomProfileMember one;
    one.memberId = QStringLiteral("m-1");
    one.connectionId = QStringLiteral("conn-1");
    BloomProfileMember two;
    two.memberId = QStringLiteral("m-2");
    two.connectionId = QStringLiteral("conn-2");
    profile.members = {one, two};
    profile.defaultMemberId = QStringLiteral("m-1");
    QVERIFY(repo.upsertProfile(profile));

    const BloomProfileRequestContext before = repo.activeRequestContext();
    QVERIFY(before.isValid());
    QVERIFY(repo.isCurrent(before));
    QCOMPARE(before.connectionId, QStringLiteral("conn-1"));
    QVERIFY(!before.memberId.isEmpty());

    const quint64 generationBefore = repo.generation();
    QVERIFY(repo.setDefaultMember(profile.id, QStringLiteral("m-2")));
    QVERIFY(repo.generation() > generationBefore);
    QVERIFY(!before.isCurrent(repo.generation()));
    QVERIFY(!repo.isCurrent(before));

    const BloomProfileRequestContext after = repo.activeRequestContext();
    QVERIFY(repo.isCurrent(after));
    QCOMPARE(after.connectionId, QStringLiteral("conn-2"));
    QCOMPARE(config.getActiveConnection()->connectionId, QStringLiteral("conn-1"));
}

void BloomProfileRepositoryTest::disabledDefaultCannotBecomeRequestTarget()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-disabled"),
                                           QStringLiteral("https://shared.example.test"),
                                           QStringLiteral("disabled"),
                                           QStringLiteral("Disabled")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-enabled"),
                                           QStringLiteral("https://shared.example.test"),
                                           QStringLiteral("enabled"),
                                           QStringLiteral("Enabled")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Enabled routing");
    profile.mode = BloomProfileMode::Merged;
    BloomProfileMember disabled;
    disabled.memberId = QStringLiteral("member-disabled");
    disabled.connectionId = QStringLiteral("conn-disabled");
    disabled.enabled = false;
    BloomProfileMember enabled;
    enabled.memberId = QStringLiteral("member-enabled");
    enabled.connectionId = QStringLiteral("conn-enabled");
    enabled.enabled = true;
    profile.members = {disabled, enabled};
    profile.defaultMemberId = disabled.memberId;
    QVERIFY(repo.upsertProfile(profile));

    QCOMPARE(profile.defaultMemberId, enabled.memberId);
    QCOMPARE(repo.activeRequestContext().memberId, enabled.memberId);
    QVERIFY(!repo.requestContext(profile.id, disabled.memberId).isValid());
    QVERIFY(!repo.setDefaultMember(profile.id, disabled.memberId));
}

void BloomProfileRepositoryTest::validExternalReloadEmitsAndInvalidatesActiveContext()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://one.example.test"),
                                           QStringLiteral("one"),
                                           QStringLiteral("One")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-2"),
                                           QStringLiteral("https://two.example.test"),
                                           QStringLiteral("two"),
                                           QStringLiteral("Two")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile first;
    first.name = QStringLiteral("First");
    first.members = {BloomProfileMember{QString(), QStringLiteral("conn-1")}};
    QVERIFY(repo.upsertProfile(first));

    BloomProfile second;
    second.name = QStringLiteral("Second");
    second.members = {BloomProfileMember{QString(), QStringLiteral("conn-2")}};
    QVERIFY(repo.upsertProfile(second));
    QVERIFY(repo.setActiveProfile(first.id));

    const BloomProfileRequestContext staleContext = repo.activeRequestContext();
    QVERIFY(staleContext.isValid());
    const quint64 previousGeneration = repo.generation();
    QSignalSpy profilesChangedSpy(&repo, &BloomProfileRepository::profilesChanged);
    QSignalSpy activeChangedSpy(&repo, &BloomProfileRepository::activeProfileChanged);

    QJsonObject externalStore = config.getBloomProfilesConfig();
    externalStore[QStringLiteral("active_profile_id")] = second.id;
    config.setBloomProfilesConfig(externalStore);
    repo.reload();

    QCOMPARE(repo.activeProfile()->id, second.id);
    QVERIFY(repo.generation() > previousGeneration);
    QVERIFY(!repo.isCurrent(staleContext));
    QCOMPARE(profilesChangedSpy.count(), 1);
    QCOMPARE(activeChangedSpy.count(), 1);
}

void BloomProfileRepositoryTest::inactiveProfileMutationDoesNotChangeActiveGeneration()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-active"),
                                           QStringLiteral("https://active.example.test"),
                                           QStringLiteral("active"),
                                           QStringLiteral("Active")),
                            true);
    config.upsertConnection(makeConnection(QStringLiteral("conn-inactive"),
                                           QStringLiteral("https://inactive.example.test"),
                                           QStringLiteral("inactive"),
                                           QStringLiteral("Inactive")),
                            false);

    BloomProfileRepository repo(&config);
    BloomProfile active;
    active.name = QStringLiteral("Active");
    active.members = {BloomProfileMember{QString(), QStringLiteral("conn-active")}};
    QVERIFY(repo.upsertProfile(active));

    BloomProfile inactive;
    inactive.name = QStringLiteral("Inactive");
    inactive.members = {BloomProfileMember{QString(), QStringLiteral("conn-inactive")}};
    QVERIFY(repo.upsertProfile(inactive));

    const quint64 generationBefore = repo.generation();
    QSignalSpy activeChangedSpy(&repo, &BloomProfileRepository::activeProfileChanged);
    inactive.name = QStringLiteral("Renamed inactive");
    QVERIFY(repo.upsertProfile(inactive));
    QCOMPARE(repo.generation(), generationBefore);
    QCOMPARE(activeChangedSpy.count(), 0);
}

void BloomProfileRepositoryTest::profileJsonExcludesCredentialAndServerIdentity()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    ScopedConfigIsolation isolation(tempDir.path());

    ConfigManager config;
    config.load();
    config.upsertConnection(makeConnection(QStringLiteral("conn-1"),
                                           QStringLiteral("https://secret.example.test"),
                                           QStringLiteral("account-1"),
                                           QStringLiteral("SecretUser")),
                            true);

    BloomProfileRepository repo(&config);
    BloomProfile profile;
    profile.name = QStringLiteral("Safe");
    profile.mode = BloomProfileMode::Single;
    BloomProfileMember member;
    member.memberId = QStringLiteral("m-1");
    member.connectionId = QStringLiteral("conn-1");
    profile.members = {member};
    profile.defaultMemberId = member.memberId;
    QVERIFY(repo.upsertProfile(profile));

    const QJsonObject bloom = config.getBloomProfilesConfig();
    QVERIFY(!jsonContainsForbiddenSecretFields(bloom));
    const QByteArray serialized = QJsonDocument(bloom).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("https://"));
    QVERIFY(!serialized.contains("SecretUser"));
    QVERIFY(!serialized.contains("account-1"));
    QVERIFY(!serialized.contains("credential"));
}

QTEST_MAIN(BloomProfileRepositoryTest)
#include "BloomProfileRepositoryTest.moc"
