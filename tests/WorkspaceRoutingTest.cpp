#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>

#include "TestConfigIsolation.h"
#include "models/MediaModels.h"
#include "network/AuthenticationService.h"
#include "network/ConnectionSessionPool.h"
#include "network/HttpTransport.h"
#include "profiles/BloomProfileRepository.h"
#include "profiles/MergedCatalogService.h"
#include "profiles/WorkspaceController.h"
#include "providers/ActiveArtworkProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "security/CredentialStore.h"
#include "security/ISecretStore.h"
#include "utils/ConfigManager.h"

namespace {

// Keyring stand-in. Credential reads run on worker threads.
class MemorySecretStore final : public ISecretStore
{
public:
    bool setSecret(const QString &service, const QString &account, const QString &secret) override
    {
        QMutexLocker locker(&m_mutex);
        m_secrets.insert(service + QLatin1Char('|') + account, secret);
        return true;
    }
    QString getSecret(const QString &service, const QString &account) override
    {
        QMutexLocker locker(&m_mutex);
        return m_secrets.value(service + QLatin1Char('|') + account);
    }
    bool deleteSecret(const QString &service, const QString &account) override
    {
        QMutexLocker locker(&m_mutex);
        m_secrets.remove(service + QLatin1Char('|') + account);
        return true;
    }
    QString lastError() const override { return {}; }
    QStringList listAccounts(const QString &) override { return {}; }

private:
    mutable QMutex m_mutex;
    QHash<QString, QString> m_secrets;
};

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, int statusCode, QByteArray payload, QObject *parent)
        : QNetworkReply(parent)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (statusCode == 401) {
            setError(AuthenticationRequiredError, QStringLiteral("unauthorized"));
        } else if (statusCode >= 400) {
            setError(UnknownServerError, QStringLiteral("server error"));
        }
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            if (isFinished()) {
                return;
            }
            setFinished(true);
            if (!m_payload.isEmpty()) {
                emit readyRead();
            }
            emit finished();
        });
    }

    void abort() override
    {
        if (!isFinished()) {
            setError(OperationCanceledError, QStringLiteral("canceled"));
            setFinished(true);
            emit finished();
        }
    }

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, m_payload.size() - m_offset);
        if (count <= 0) {
            return -1;
        }
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_payload;
    qint64 m_offset = 0;
};

struct Account {
    QString userId;
    QString host;
    QJsonArray views;
    bool offline = false;
};

struct RecordedRequest {
    QString host;
    QString path;
    QString token;
};

// Answers Jellyfin session validation and /UserViews per access token, so a
// request is only served when it carries its own connection's credentials.
class FakeServers final : public QNetworkAccessManager
{
public:
    QHash<QString, Account> accountsByToken;
    QList<RecordedRequest> requests;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        static const QRegularExpression tokenPattern(QStringLiteral("Token=\"([^\"]*)\""));
        const QString authorization = QString::fromUtf8(request.rawHeader("Authorization"));
        const QString token = tokenPattern.match(authorization).captured(1);
        const QUrl url = request.url();
        requests.append({url.host(), url.path(), token});

        const auto account = accountsByToken.constFind(token);
        if (account == accountsByToken.cend() || account->host != url.host()) {
            return new FakeReply(request, 401, {}, this);
        }
        if (account->offline) {
            return new FakeReply(request, 503, {}, this);
        }
        if (url.path() == QStringLiteral("/Users/") + account->userId) {
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("Id"), account->userId},
                                                           {QStringLiteral("Name"), account->userId}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        if (url.path() == QStringLiteral("/UserViews")) {
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("Items"), account->views},
                                                           {QStringLiteral("TotalRecordCount"),
                                                            account->views.size()}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        return new FakeReply(request, 404, {}, this);
    }
};

QJsonObject view(const QString &id, const QString &name)
{
    return {{QStringLiteral("Id"), id},
            {QStringLiteral("Name"), name},
            {QStringLiteral("Type"), QStringLiteral("CollectionFolder")},
            {QStringLiteral("CollectionType"), QStringLiteral("movies")}};
}

ServerConnection connection(const QString &id, const QString &host, const QString &userId)
{
    ServerConnection result;
    result.connectionId = id;
    result.providerKind = ProviderKind::Jellyfin;
    result.baseUrl = QStringLiteral("https://") + host;
    result.serverName = host;
    result.accountId = userId;
    result.profileId = userId;
    result.username = userId;
    result.displayName = userId;
    result.credentialReference = ServerConnection::createCredentialReference(id);
    return result;
}

QStringList viewRefs(const QVariantList &views)
{
    QStringList refs;
    for (const QVariant &value : views) {
        const QVariantMap map = value.toMap();
        refs.append(map.value(QStringLiteral("connectionId")).toString() + QLatin1Char('/')
                    + map.value(QStringLiteral("itemId")).toString());
    }
    return refs;
}

} // namespace

class WorkspaceRoutingTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void mergedReadsUseEachMembersOwnCredentials();
    void unavailableMemberDegradesOnlyItsSource();
    void profileSwitchDropsInFlightMergedResults();
    void routingSwitchesSessionWithoutDeletingCredentials();
    void failedRouteFallsBackToPreviousSession();
    void addedConnectionJoinsTargetWorkspace();
    void removingConnectionAffectsOnlyThatAccount();
    void pooledMemberArtworkUsesOwnServerAndToken();
    void mergedWorkspaceActiveAtStartupLoadsMemberSessions();
    void signedOutMemberLosesPooledSession();
    void routeNeedingSignInFailsWithoutSticking();
    void switchNeedingSignInFinishesAfterLogin();
    void skippedMemberKeepsRestoringUntilNextFinishes();

private:
    void seedAccounts();
    QString createMergedWorkspace();
    void waitForMembersReady();
    CredentialStore credentials() const { return CredentialStore(m_store.get()); }

    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;
    std::shared_ptr<MemorySecretStore> m_store;
    std::unique_ptr<FakeServers> m_servers;
    std::unique_ptr<HttpTransport> m_transport;
    std::unique_ptr<JellyfinProviderAdapter> m_adapter;
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<BloomProfileRepository> m_repository;
    std::unique_ptr<AuthenticationService> m_auth;
    std::unique_ptr<MergedCatalogService> m_merged;
    std::unique_ptr<WorkspaceController> m_workspaces;
};

void WorkspaceRoutingTest::init()
{
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
    m_isolation = std::make_unique<ScopedConfigIsolation>(m_tempDir->path());
    m_store = std::make_shared<MemorySecretStore>();
    m_servers = std::make_unique<FakeServers>();
    m_transport = std::make_unique<HttpTransport>(m_servers.get());
    m_adapter = std::make_unique<JellyfinProviderAdapter>();
    m_config = std::make_unique<ConfigManager>();
    m_config->load();
    seedAccounts();
    m_repository = std::make_unique<BloomProfileRepository>(m_config.get());
    m_auth = std::make_unique<AuthenticationService>(
        m_store, m_transport.get(), QList<IProviderAdapter *>{m_adapter.get()});
    m_merged = std::make_unique<MergedCatalogService>(
        m_auth.get(), m_repository.get(), m_config.get());
    m_workspaces = std::make_unique<WorkspaceController>(
        m_auth.get(), m_repository.get(), m_config.get());

    m_auth->initialize(m_config.get());
    QTRY_VERIFY(m_auth->isAuthenticated());
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("alice"));
    // A plain restore of an account outside every workspace gets its own.
    QVERIFY(m_repository->activeProfile().has_value());
}

void WorkspaceRoutingTest::cleanup()
{
    m_workspaces.reset();
    m_merged.reset();
    m_auth.reset();
    m_repository.reset();
    m_config.reset();
    m_adapter.reset();
    m_transport.reset();
    m_servers.reset();
    m_store.reset();
    m_isolation.reset();
    m_tempDir.reset();
}

void WorkspaceRoutingTest::seedAccounts()
{
    // Two users on one physical server plus a user on a second server.
    const QList<std::tuple<QString, QString, QString, QString, QJsonArray>> accounts{
        {QStringLiteral("alice"), QStringLiteral("media.test"), QStringLiteral("u-alice"),
         QStringLiteral("token-alice"),
         QJsonArray{view(QStringLiteral("lib-movies"), QStringLiteral("Movies"))}},
        {QStringLiteral("bob"), QStringLiteral("media.test"), QStringLiteral("u-bob"),
         QStringLiteral("token-bob"),
         QJsonArray{view(QStringLiteral("lib-movies"), QStringLiteral("Movies"))}},
        {QStringLiteral("carol"), QStringLiteral("other.test"), QStringLiteral("u-carol"),
         QStringLiteral("token-carol"),
         QJsonArray{view(QStringLiteral("lib-shows"), QStringLiteral("Shows"))}},
        {QStringLiteral("dave"), QStringLiteral("third.test"), QStringLiteral("u-dave"),
         QStringLiteral("token-dave"), QJsonArray{}},
    };
    bool first = true;
    for (const auto &[id, host, userId, token, views] : accounts) {
        const ServerConnection saved = connection(id, host, userId);
        m_config->upsertConnection(saved, first);
        first = false;
        QVERIFY(credentials().write(saved, CredentialKind::AccessToken, token));
        m_servers->accountsByToken.insert(token, Account{userId, host, views});
    }
}

QString WorkspaceRoutingTest::createMergedWorkspace()
{
    const QString id = m_workspaces->createProfile(
        QStringLiteral("Household"),
        {QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol")},
        true);
    if (id.isEmpty()) {
        return {};
    }
    QSignalSpy switched(m_workspaces.get(), &WorkspaceController::workspaceSwitched);
    if (!m_workspaces->switchProfile(id)) {
        return {};
    }
    // The live session (alice) is already a member: no reconnect needed.
    if (switched.count() != 1) {
        return {};
    }
    return id;
}

void WorkspaceRoutingTest::waitForMembersReady()
{
    for (const QString &id : {QStringLiteral("bob"), QStringLiteral("carol")}) {
        QTRY_VERIFY(m_auth->hasSessionForConnection(id));
    }
}

void WorkspaceRoutingTest::mergedReadsUseEachMembersOwnCredentials()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    QVERIFY(m_merged->isActive());
    waitForMembersReady();

    m_servers->requests.clear();
    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());

    // Same library ID from two users on one server stays two libraries,
    // ordered by member priority, each owned by its connection.
    QCOMPARE(viewRefs(m_merged->views()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("bob/lib-movies"),
                          QStringLiteral("carol/lib-shows")}));
    QCOMPARE(m_merged->views().at(1).toMap().value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("u-bob · media.test"));

    QHash<QString, QString> tokenByHostAndUser;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        QCOMPARE(request.path, QStringLiteral("/UserViews"));
        tokenByHostAndUser.insert(request.host + QLatin1Char('|') + request.token, request.token);
    }
    QVERIFY(tokenByHostAndUser.contains(QStringLiteral("media.test|token-alice")));
    QVERIFY(tokenByHostAndUser.contains(QStringLiteral("media.test|token-bob")));
    QVERIFY(tokenByHostAndUser.contains(QStringLiteral("other.test|token-carol")));
    QCOMPARE(m_servers->requests.size(), 3);
    // Merged reads never move the live session.
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("alice"));
}

void WorkspaceRoutingTest::unavailableMemberDegradesOnlyItsSource()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    m_servers->accountsByToken[QStringLiteral("token-carol")].offline = true;

    QSignalSpy failed(m_merged.get(), &MergedCatalogService::viewsFailed);
    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());

    QCOMPARE(viewRefs(m_merged->views()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("bob/lib-movies")}));
    QCOMPARE(failed.count(), 0);
    QString carolState;
    for (const QVariant &value : m_merged->sources()) {
        const QVariantMap source = value.toMap();
        if (source.value(QStringLiteral("connectionId")).toString() == QStringLiteral("carol")) {
            carolState = source.value(QStringLiteral("state")).toString();
        }
    }
    QCOMPARE(carolState, QStringLiteral("failed"));
    QVERIFY(m_auth->isAuthenticated());
}

void WorkspaceRoutingTest::profileSwitchDropsInFlightMergedResults()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    const QString single = m_workspaces->createProfile(
        QStringLiteral("Just Alice"), {QStringLiteral("alice")}, false);
    QVERIFY(!single.isEmpty());

    const quint64 epoch = m_merged->epoch();
    m_merged->refreshViews();
    // Switch before any reply is delivered.
    QVERIFY(m_workspaces->switchProfile(single));
    QVERIFY(m_merged->epoch() > epoch);
    QVERIFY(!m_merged->isActive());

    QTest::qWait(100);
    QVERIFY(m_merged->views().isEmpty());
    QVERIFY(!m_merged->viewsLoading());
}

void WorkspaceRoutingTest::routingSwitchesSessionWithoutDeletingCredentials()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();

    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("carol")));
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("carol"));
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("carol"));
    QVERIFY(m_auth->isAuthenticated());
    QCOMPARE(m_auth->getServerUrl(), QStringLiteral("https://other.test"));

    // The previous member's sign-in survives, and the workspace is unchanged.
    QCOMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                CredentialKind::AccessToken),
             QStringLiteral("token-alice"));
    QVERIFY(m_workspaces->isMerged());
    QVERIFY(m_merged->isActive());
    QVERIFY(m_workspaces->routeTo(QStringLiteral("carol")));

    // Items outside the workspace are refused rather than silently switched.
    QSignalSpy refused(m_workspaces.get(), &WorkspaceController::routeFailed);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("dave")));
    QCOMPARE(refused.count(), 1);
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("carol"));
}

void WorkspaceRoutingTest::failedRouteFallsBackToPreviousSession()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    m_servers->accountsByToken[QStringLiteral("token-carol")].offline = true;

    QSignalSpy failed(m_workspaces.get(), &WorkspaceController::routeFailed);
    QSignalSpy loggedOut(m_auth.get(), &AuthenticationService::loggedOut);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("carol")));
    QTRY_COMPARE(failed.count(), 1);
    QTRY_VERIFY(m_auth->isAuthenticated());
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("alice"));
    QCOMPARE(loggedOut.count(), 0);
    // An unreachable server never loses its stored sign-in.
    QCOMPARE(credentials().read(*m_config->getConnection(QStringLiteral("carol")),
                                CredentialKind::AccessToken),
             QStringLiteral("token-carol"));
}

void WorkspaceRoutingTest::addedConnectionJoinsTargetWorkspace()
{
    const QString household = createMergedWorkspace();
    QVERIFY(!household.isEmpty());

    QSignalSpy loggedOut(m_auth.get(), &AuthenticationService::loggedOut);
    m_workspaces->beginAddConnection(household);
    QVERIFY(m_workspaces->isAddingConnection());
    QCOMPARE(loggedOut.count(), 1);
    QVERIFY(!m_auth->isAuthenticated());
    // Leaving for the sign-in screen keeps every stored credential.
    QCOMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                CredentialKind::AccessToken),
             QStringLiteral("token-alice"));

    // The sign-in screen produces a session for dave.
    m_auth->activateConnection(QStringLiteral("dave"));
    QTRY_VERIFY(m_auth->isAuthenticated());
    QTRY_VERIFY(!m_workspaces->isAddingConnection());

    const auto profile = m_repository->activeProfile();
    QVERIFY(profile.has_value());
    QCOMPARE(profile->id, household);
    QCOMPARE(profile->members.size(), 4);
    QCOMPARE(profile->members.last().connectionId, QStringLiteral("dave"));
}

void WorkspaceRoutingTest::removingConnectionAffectsOnlyThatAccount()
{
    const QString household = createMergedWorkspace();
    QVERIFY(!household.isEmpty());
    waitForMembersReady();

    QVERIFY(m_workspaces->removeConnection(QStringLiteral("bob")));
    QVERIFY(!m_config->getConnection(QStringLiteral("bob")).has_value());
    QVERIFY(!m_auth->hasSessionForConnection(QStringLiteral("bob")));
    QTRY_COMPARE(credentials().read(connection(QStringLiteral("bob"), QStringLiteral("media.test"),
                                               QStringLiteral("u-bob")),
                                    CredentialKind::AccessToken),
                 QString());

    // Same-server sibling, other members, and the live session are untouched.
    QCOMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                CredentialKind::AccessToken),
             QStringLiteral("token-alice"));
    QVERIFY(m_auth->isAuthenticated());
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("alice"));
    const auto profile = m_repository->profile(household);
    QVERIFY(profile.has_value());
    QCOMPARE(profile->members.size(), 2);
}

void WorkspaceRoutingTest::pooledMemberArtworkUsesOwnServerAndToken()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();

    ActiveArtworkProvider artwork(m_auth.get());
    Bloom::ArtworkRef carolPoster;
    carolPoster.connectionId = QStringLiteral("carol");
    carolPoster.itemId = QStringLiteral("item-1");
    carolPoster.kind = Bloom::ArtworkKind::Primary;
    carolPoster.requestedWidth = 400;
    const auto request = artwork.resolveArtwork(carolPoster);
    QVERIFY(request.has_value());
    QCOMPARE(request->url().host(), QStringLiteral("other.test"));
    QVERIFY(QString::fromUtf8(request->rawHeader("Authorization"))
                .contains(QStringLiteral("Token=\"token-carol\"")));

    // Connections outside the workspace have no pooled session.
    Bloom::ArtworkRef davePoster = carolPoster;
    davePoster.connectionId = QStringLiteral("dave");
    QVERIFY(!artwork.resolveArtwork(davePoster).has_value());
}

void WorkspaceRoutingTest::mergedWorkspaceActiveAtStartupLoadsMemberSessions()
{
    QVERIFY(!createMergedWorkspace().isEmpty());

    // Relaunch in production order: services are constructed (and see the
    // merged workspace) before AuthenticationService::initialize runs.
    m_workspaces.reset();
    m_merged.reset();
    m_auth.reset();
    m_auth = std::make_unique<AuthenticationService>(
        m_store, m_transport.get(), QList<IProviderAdapter *>{m_adapter.get()});
    m_merged = std::make_unique<MergedCatalogService>(
        m_auth.get(), m_repository.get(), m_config.get());
    m_workspaces = std::make_unique<WorkspaceController>(
        m_auth.get(), m_repository.get(), m_config.get());
    QVERIFY(m_merged->isActive());

    m_auth->initialize(m_config.get());
    QTRY_VERIFY(m_auth->isAuthenticated());
    waitForMembersReady();

    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && m_merged->views().size() == 3);
}

void WorkspaceRoutingTest::signedOutMemberLosesPooledSession()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    QTRY_VERIFY(!m_auth->sessionPool()->isLoading(QStringLiteral("alice")));

    m_auth->logout();
    QVERIFY(!m_auth->isAuthenticated());
    // The pool also held the live member; signing out must not leave its
    // token reachable for merged reads once another member is live.
    QVERIFY(!m_auth->hasSessionForConnection(QStringLiteral("alice")));
    QVERIFY(m_auth->hasSessionForConnection(QStringLiteral("bob")));
}

void WorkspaceRoutingTest::routeNeedingSignInFailsWithoutSticking()
{
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();

    QSignalSpy failed(m_workspaces.get(), &WorkspaceController::routeFailed);
    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("carol")));
    QVERIFY(m_workspaces->isSwitching());
    // The restored session reached a provider profile or PIN prompt.
    emit m_auth->signInRequired(QStringLiteral("carol"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("carol"));
    QVERIFY(!m_workspaces->isSwitching());

    QTRY_VERIFY(m_auth->isAuthenticated());
    QTest::qWait(50);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!m_workspaces->isSwitching());
}

void WorkspaceRoutingTest::switchNeedingSignInFinishesAfterLogin()
{
    const QString single = m_workspaces->createProfile(
        QStringLiteral("Carol"), {QStringLiteral("carol")}, false);
    QVERIFY(!single.isEmpty());

    QSignalSpy switched(m_workspaces.get(), &WorkspaceController::workspaceSwitched);
    QSignalSpy failed(m_workspaces.get(), &WorkspaceController::switchFailed);
    QVERIFY(m_workspaces->switchProfile(single));
    QVERIFY(m_workspaces->isSwitching());
    emit m_auth->signInRequired(QStringLiteral("carol"));
    // Profile edits and repairs keep working while the login screen is up.
    QVERIFY(!m_workspaces->isSwitching());
    QCOMPARE(switched.count(), 0);

    // The login screen finishes signing in to the awaited member.
    QTRY_VERIFY(m_auth->isAuthenticated());
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("carol"));
    QTRY_COMPARE(switched.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(m_workspaces->activeProfileId(), single);
}

void WorkspaceRoutingTest::skippedMemberKeepsRestoringUntilNextFinishes()
{
    // A saved account with no stored sign-in, as the workspace default.
    m_config->upsertConnection(
        connection(QStringLiteral("erin"), QStringLiteral("media.test"), QStringLiteral("u-erin")),
        false);
    const QString id = m_workspaces->createProfile(
        QStringLiteral("Erin first"), {QStringLiteral("erin"), QStringLiteral("carol")}, true);
    QVERIFY(!id.isEmpty());

    bool erinFailed = false;
    QList<bool> restoringAfterFailure;
    connect(m_auth.get(), &AuthenticationService::connectionActivationFailed, this,
            [&](const QString &connectionId) {
        erinFailed = erinFailed || connectionId == QStringLiteral("erin");
    });
    connect(m_auth.get(), &AuthenticationService::isRestoringSessionChanged, this, [&]() {
        if (erinFailed) {
            restoringAfterFailure.append(m_auth->isRestoringSession());
        }
    });
    QSignalSpy switched(m_workspaces.get(), &WorkspaceController::workspaceSwitched);
    QVERIFY(m_workspaces->switchProfile(id));
    QTRY_COMPARE(switched.count(), 1);
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("carol"));
    // Carol's restoration starts inside erin's failure; erin's finished
    // handler must not then report restoring as done while carol's runs.
    QVERIFY(erinFailed);
    QCOMPARE(restoringAfterFailure.count(false), 1);
    QVERIFY(!m_auth->isRestoringSession());
}

QTEST_GUILESS_MAIN(WorkspaceRoutingTest)
#include "WorkspaceRoutingTest.moc"
