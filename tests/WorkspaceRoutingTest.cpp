#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

#include "TestConfigIsolation.h"
#include "models/MediaModels.h"
#include "network/AuthenticationService.h"
#include "network/ConnectionSessionPool.h"
#include "network/HttpTransport.h"
#include "network/LibrarySearchScope.h"
#include "network/LibraryService.h"
#include "profiles/BloomProfileRepository.h"
#include "profiles/MergedCatalogService.h"
#include "profiles/PosterBadgeService.h"
#include "profiles/WorkspaceController.h"
#include "providers/ActiveArtworkProvider.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloProviderAdapter.h"
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
    // held: the reply waits for deliver(), so a test can act while it is in flight.
    FakeReply(const QNetworkRequest &request, int statusCode, QByteArray payload, QObject *parent,
              bool held = false)
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
        if (!held) {
            QTimer::singleShot(0, this, [this]() { deliver(); });
        }
    }

    void deliver()
    {
        if (isFinished()) {
            return;
        }
        setFinished(true);
        if (!m_payload.isEmpty()) {
            emit readyRead();
        }
        emit finished();
    }

    // Reports an abort (a canceled request) to the test.
    std::function<void()> onAbort;

    void abort() override
    {
        if (!isFinished()) {
            if (onAbort) {
                onAbort();
            }
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
    // Search results per library ID; an unscoped search returns them all.
    QHash<QString, QJsonArray> itemsByLibrary;
    bool offline = false;
    // Home reads: Next Up, the resume list (with its HTTP status) and
    // favorites.
    QJsonArray nextUp;
    QJsonArray resume;
    int resumeStatus = 200;
    QJsonArray favorites;
    int favoritesStatus = 200;
    // GetMovieRecommendations per library (ParentId), with its HTTP status.
    QHash<QString, QJsonArray> recommendationsByLibrary;
    QHash<QString, int> recommendationsStatusByLibrary;
    // People search answers in the server's own shape (Jellyfin /Persons
    // items or Silo /api/v2/catalog/people items).
    QJsonArray people;
    // Silo getCatalogSearchCapabilities; status 404 when unset.
    QByteArray searchCapabilities;
    int searchCapabilitiesStatus = 200;
};

struct RecordedRequest {
    QString host;
    QString path;
    QString token;
    QString parentId;
    QNetworkAccessManager::Operation operation = QNetworkAccessManager::GetOperation;
    QUrlQuery query;
};

// Answers Jellyfin session validation and /UserViews per access token, so a
// request is only served when it carries its own connection's credentials.
class FakeServers final : public QNetworkAccessManager
{
public:
    QHash<QString, Account> accountsByToken;
    QList<RecordedRequest> requests;

    // Silo refresh tokens: refresh token -> access token it renews. The
    // refresh endpoint rotates both, like the server.
    enum class RefreshMode { Renew, Reject, Offline };
    QHash<QString, QString> accessTokenByRefreshToken;
    RefreshMode refreshMode = RefreshMode::Renew;
    int refreshRequests = 0;
    int renewalCounter = 0;
    // Holds refresh replies until a test delivers them.
    bool holdRefresh = false;
    QList<QPointer<FakeReply>> heldRefreshes;
    // Holds Next Up replies until a test delivers them.
    bool holdNextUp = false;
    QList<QPointer<FakeReply>> heldNextUps;
    // Holds movie recommendation replies until a test delivers them.
    bool holdRecommendations = false;
    QList<QPointer<FakeReply>> heldRecommendations;
    // Silo poster badge preferences: effective settings and overlay config.
    QByteArray overlaySettings = QByteArrayLiteral(R"({"revision":15,"items":[]})");
    int overlaySettingsStatus = 200;
    QByteArray overlayConfig = QByteArrayLiteral(
        R"({"enabled":true,"quick_actions_enabled":false,"quick_actions_default":"both"})");
    // Server logouts (Jellyfin and Silo) that were canceled before an answer.
    int abortedLogouts = 0;

    QList<RecordedRequest> logouts() const
    {
        QList<RecordedRequest> result;
        for (const RecordedRequest &request : requests) {
            if (request.path == QStringLiteral("/Sessions/Logout")
                || request.path == QStringLiteral("/api/v2/auth/logout")) {
                result.append(request);
            }
        }
        return result;
    }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        static const QRegularExpression tokenPattern(
            QStringLiteral("(?:Token=\"([^\"]*)\"|^Bearer (\\S+))"));
        const QString authorization = QString::fromUtf8(request.rawHeader("Authorization"));
        const QRegularExpressionMatch match = tokenPattern.match(authorization);
        const QString token = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        const QUrl url = request.url();
        const QUrlQuery query(url);
        requests.append({url.host(), url.path(), token,
                         query.queryItemValue(QStringLiteral("ParentId")), operation, query});

        if (url.path() == QStringLiteral("/api/v2/auth/refresh")) {
            ++refreshRequests;
            const QString refreshToken = QJsonDocument::fromJson(
                outgoingData ? outgoingData->readAll() : QByteArray())
                .object().value(QStringLiteral("refresh_token")).toString();
            int status = 200;
            QByteArray payload;
            if (refreshMode == RefreshMode::Offline) {
                status = 503;
            } else if (refreshMode == RefreshMode::Reject
                       || !accessTokenByRefreshToken.contains(refreshToken)) {
                status = 401;
            } else {
                const QString oldAccess = accessTokenByRefreshToken.take(refreshToken);
                ++renewalCounter;
                const QString access = QStringLiteral("access-r%1").arg(renewalCounter);
                const QString refresh = QStringLiteral("refresh-r%1").arg(renewalCounter);
                Account account = accountsByToken.take(oldAccess);
                accountsByToken.insert(access, account);
                accessTokenByRefreshToken.insert(refresh, access);
                payload = QJsonDocument(QJsonObject{
                                            {QStringLiteral("access_token"), access},
                                            {QStringLiteral("refresh_token"), refresh},
                                            {QStringLiteral("expires_in"), 900}})
                              .toJson(QJsonDocument::Compact);
            }
            auto *reply = new FakeReply(request, status, payload, this, holdRefresh);
            if (holdRefresh) {
                heldRefreshes.append(reply);
            }
            return reply;
        }

        const auto account = accountsByToken.constFind(token);
        if (account == accountsByToken.cend() || account->host != url.host()) {
            return new FakeReply(request, 401, {}, this);
        }
        if (account->offline) {
            return new FakeReply(request, 503, {}, this);
        }
        if (operation == PostOperation
            && (url.path() == QStringLiteral("/Sessions/Logout")
                || url.path() == QStringLiteral("/api/v2/auth/logout"))) {
            // The server ends the session: its token stops working.
            accountsByToken.remove(token);
            auto *reply = new FakeReply(request, 204, {}, this);
            reply->onAbort = [this]() { ++abortedLogouts; };
            return reply;
        }
        if (url.path() == QStringLiteral("/api/v2/account/me")) {
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("id"), account->userId},
                                                           {QStringLiteral("username"), account->userId},
                                                           {QStringLiteral("role"), QStringLiteral("user")},
                                                           {QStringLiteral("permissions"), QJsonArray{}},
                                                           {QStringLiteral("download_allowed"), false}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        if (url.path() == QStringLiteral("/api/v2/profiles")) {
            return new FakeReply(request, 200,
                                 QByteArrayLiteral(R"({"items":[{"id":"p-1","name":"Main","has_pin":false,"is_child":false,"is_primary":true}]})"),
                                 this);
        }
        if (url.path() == QStringLiteral("/api/v2/user/libraries")) {
            QJsonArray libraries;
            for (const QJsonValue &value : account->views) {
                libraries.append(QJsonObject{
                    {QStringLiteral("id"), value.toObject().value(QStringLiteral("Id"))},
                    {QStringLiteral("name"), value.toObject().value(QStringLiteral("Name"))},
                    {QStringLiteral("type"), QStringLiteral("movies")}});
            }
            return new FakeReply(request, 200, QJsonDocument(libraries).toJson(QJsonDocument::Compact),
                                 this);
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
        if (url.path() == QStringLiteral("/Items") && query.hasQueryItem(QStringLiteral("SearchTerm"))) {
            const QString parentId = query.queryItemValue(QStringLiteral("ParentId"));
            QJsonArray items;
            for (auto it = account->itemsByLibrary.cbegin(); it != account->itemsByLibrary.cend(); ++it) {
                if (parentId.isEmpty() || it.key() == parentId) {
                    for (const QJsonValue &item : it.value()) {
                        items.append(item);
                    }
                }
            }
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                                           {QStringLiteral("TotalRecordCount"),
                                                            items.size()}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        const auto itemsEnvelope = [](const QJsonArray &items) {
            return QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                             {QStringLiteral("TotalRecordCount"), items.size()},
                                             {QStringLiteral("StartIndex"), 0}})
                .toJson(QJsonDocument::Compact);
        };
        if (url.path() == QStringLiteral("/Shows/NextUp")) {
            auto *reply = new FakeReply(request, 200, itemsEnvelope(account->nextUp), this,
                                        holdNextUp);
            if (holdNextUp) {
                heldNextUps.append(reply);
            }
            return reply;
        }
        if (url.path() == QStringLiteral("/UserItems/Resume")) {
            return new FakeReply(request, account->resumeStatus,
                                 account->resumeStatus == 200 ? itemsEnvelope(account->resume)
                                                              : QByteArray(),
                                 this);
        }
        if (url.path() == QStringLiteral("/Items")
            && query.queryItemValue(QStringLiteral("IsFavorite")) == QStringLiteral("true")) {
            return new FakeReply(request, account->favoritesStatus,
                                 account->favoritesStatus == 200 ? itemsEnvelope(account->favorites)
                                                                 : QByteArray(),
                                 this);
        }
        if (url.path() == QStringLiteral("/Movies/Recommendations")) {
            const QString parentId = query.queryItemValue(QStringLiteral("ParentId"));
            const int status = account->recommendationsStatusByLibrary.value(parentId, 200);
            auto *reply = new FakeReply(
                request, status,
                status == 200 ? QJsonDocument(account->recommendationsByLibrary.value(parentId))
                                    .toJson(QJsonDocument::Compact)
                              : QByteArray(),
                this, holdRecommendations);
            if (holdRecommendations) {
                heldRecommendations.append(reply);
            }
            return reply;
        }
        if (url.path() == QStringLiteral("/Persons")) {
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("Items"), account->people},
                                                           {QStringLiteral("TotalRecordCount"),
                                                            account->people.size()}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        if (url.path() == QStringLiteral("/api/v2/catalog/search/capabilities")) {
            const int status = account->searchCapabilities.isEmpty()
                ? 404 : account->searchCapabilitiesStatus;
            return new FakeReply(request, status,
                                 status == 200 ? account->searchCapabilities : QByteArray(), this);
        }
        if (url.path() == QStringLiteral("/api/v2/catalog/people")) {
            return new FakeReply(request, 200,
                                 QJsonDocument(QJsonObject{{QStringLiteral("items"), account->people}})
                                     .toJson(QJsonDocument::Compact),
                                 this);
        }
        if (url.path() == QStringLiteral("/api/v2/settings/values/effective")) {
            return new FakeReply(request, overlaySettingsStatus,
                                 overlaySettingsStatus == 200 ? overlaySettings : QByteArray(), this);
        }
        if (url.path() == QStringLiteral("/api/v2/settings/overlay-config")) {
            return new FakeReply(request, 200, overlayConfig, this);
        }
        return new FakeReply(request, 404, {}, this);
    }
};

QJsonObject view(const QString &id, const QString &name,
                 const QString &collectionType = QStringLiteral("movies"))
{
    return {{QStringLiteral("Id"), id},
            {QStringLiteral("Name"), name},
            {QStringLiteral("Type"), QStringLiteral("CollectionFolder")},
            {QStringLiteral("CollectionType"), collectionType}};
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

QJsonObject movie(const QString &id, const QString &name)
{
    return {{QStringLiteral("Id"), id},
            {QStringLiteral("Name"), name},
            {QStringLiteral("Type"), QStringLiteral("Movie")}};
}

// A Jellyfin episode or movie card with its watch state.
QJsonObject playable(const QString &id, const QString &type, qint64 positionTicks,
                     const QString &lastPlayed = QString())
{
    QJsonObject userData{{QStringLiteral("PlaybackPositionTicks"), positionTicks}};
    if (!lastPlayed.isEmpty()) {
        userData.insert(QStringLiteral("LastPlayedDate"), lastPlayed);
    }
    return {{QStringLiteral("Id"), id},
            {QStringLiteral("Name"), id},
            {QStringLiteral("Type"), type},
            {QStringLiteral("UserData"), userData}};
}

// A Jellyfin RecommendationDto.
QJsonObject recommendation(const QString &type, const QString &baseline, const QJsonArray &items)
{
    return {{QStringLiteral("RecommendationType"), type},
            {QStringLiteral("BaselineItemName"), baseline},
            {QStringLiteral("CategoryId"), type + baseline},
            {QStringLiteral("Items"), items}};
}

// The title of each Home row, in order.
QStringList rowTitles(const QVariantList &rows)
{
    QStringList titles;
    for (const QVariant &row : rows) {
        titles.append(row.toMap().value(QStringLiteral("title")).toString());
    }
    return titles;
}

// connectionId/itemId of each item, in order.
QStringList orderedRefs(const QVariantList &items)
{
    QStringList refs;
    for (const QVariant &value : items) {
        const QVariantMap map = value.toMap();
        refs.append(map.value(QStringLiteral("connectionId")).toString() + QLatin1Char('/')
                    + map.value(QStringLiteral("itemId")).toString());
    }
    return refs;
}

// One recovery ping; nullopt when none was sent or it never finished.
std::optional<ServerReachability> pingOnce(LibraryService &library, QObject *context)
{
    const auto result = std::make_shared<std::optional<ServerReachability>>();
    if (!library.pingServer(context, [result](ServerReachability reachability) {
            *result = reachability;
        })) {
        return std::nullopt;
    }
    QElapsedTimer clock;
    clock.start();
    while (!result->has_value() && clock.elapsed() < 5000) {
        QTest::qWait(5);
    }
    return *result;
}

QStringList itemIds(const QVariantList &items)
{
    QStringList ids;
    for (const QVariant &value : items) {
        ids.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    ids.sort();
    return ids;
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
    void removingToOneSourceMakesSpaceSingle();
    void mergedSpaceHidesAndUnsearchesChosenLibraries();
    void singleSpaceHidesAndUnsearchesChosenLibraries();
    void scopedSearchInterleavesLibrariesByRank();
    void unsupportedLibrariesLeaveViewsAndSearch();
    void pooledMemberRenewsExpiredTokenOnceAndSavesIt();
    void rejectedRenewalNeedsSignInButKeepsCredentials();
    void unreachableRenewalKeepsMemberUntilServerReturns();
    void unreachableServerAtStartupKeepsCredentials();
    void renewalFinishingAfterSpaceSwitchIsSaved();
    void activationAdoptsRenewalAlreadyInFlight();
    void renewalFinishingAfterRemovalDoesNotReviveCredentials();
    void replacedSearchDoesNotPublishResults();
    void mergedPeopleSearchReadsEachMembersCapability();
    void mergedPeopleSearchSkipsMembersThatCannotScope();
    void mergedPeopleSearchKeepsWorkingMembers();
    void posterBadgesFollowEachSiloMembersPreferences();
    void failedPosterBadgeReadLeavesSourceAlone();
    void recoveryPingChecksJellyfinSessionRoute();
    void recoveryPingRenewsSiloSessionBeforeReportingExpiry();
    void signOutEndsServerSessionWithoutWaiting();
    void signOutFinishesWhenServerIsDown();
    void expiredSessionSignOutSkipsServerLogout();
    void siloSignOutUsesNativeLogoutRoute();
    void siloSignOutRefreshesAnExpiredTokenFirst();
    void removingSiloMemberEndsItsSession();
    void switchingSpacesNeverEndsServerSessions();
    void jellyfinNextUpAddsResumeListAndOptions();
    void jellyfinHomeShowsFavoritesRowBehindItsSetting();
    void jellyfinHomeAddsRecommendationRowsPerMovieLibrary();
    void jellyfinHomeRecommendationReadsFailAlone();
    void jellyfinHomeDropsStaleRecommendationReplies();
    void mergedNextUpAddsEachJellyfinMembersResumeList();
    void olderNextUpRefreshNeverPublishesAfterNewerOne();

private:
    void seedAccounts();
    // Alice (Jellyfin) and erin (Silo) know an "Al Pacino"; erin's search
    // capabilities say whether her people search can be scoped.
    void seedPeople(const QByteArray &erinCapabilities, int erinCapabilitiesStatus = 200);
    QStringList pathsSince(qsizetype from, const QString &path) const;
    QString createMergedWorkspace();
    // Adds a Silo account (access "erin-a1", refresh "erin-r1") and a merged
    // space of alice + erin, then switches to it.
    QString createSiloMergedWorkspace();
    QString sourceState(const QString &connectionId) const;
    // Expires erin's access token server-side and starts a held renewal
    // through a merged read.
    void startHeldErinRenewal();
    void waitForMembersReady();
    CredentialStore credentials() const { return CredentialStore(m_store.get()); }

    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;
    std::shared_ptr<MemorySecretStore> m_store;
    std::unique_ptr<FakeServers> m_servers;
    std::unique_ptr<HttpTransport> m_transport;
    std::unique_ptr<JellyfinProviderAdapter> m_adapter;
    std::unique_ptr<SiloProviderAdapter> m_siloAdapter;
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
    m_siloAdapter = std::make_unique<SiloProviderAdapter>();
    m_config = std::make_unique<ConfigManager>();
    m_config->load();
    seedAccounts();
    m_repository = std::make_unique<BloomProfileRepository>(m_config.get());
    m_auth = std::make_unique<AuthenticationService>(
        m_store, m_transport.get(), QList<IProviderAdapter *>{m_adapter.get(), m_siloAdapter.get()});
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
    m_siloAdapter.reset();
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

    m_servers->requests.clear();
    QVERIFY(m_workspaces->removeConnection(QStringLiteral("bob")));
    QVERIFY(!m_config->getConnection(QStringLiteral("bob")).has_value());
    // bob's pooled session is ended on the server; alice's token on the
    // same server keeps working.
    QCOMPARE(m_servers->logouts().size(), 1);
    QCOMPARE(m_servers->logouts().constFirst().token, QStringLiteral("token-bob"));
    QCOMPARE(m_servers->logouts().constFirst().operation, QNetworkAccessManager::PostOperation);
    QVERIFY(!m_servers->accountsByToken.contains(QStringLiteral("token-bob")));
    QVERIFY(m_servers->accountsByToken.contains(QStringLiteral("token-alice")));
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

    // dave is in no space, so no tokens are loaded to end its session with.
    QVERIFY(m_workspaces->removeConnection(QStringLiteral("dave")));
    QCOMPARE(m_servers->logouts().size(), 1);

    // carol, a pooled member on another server, is logged out with her token.
    QVERIFY(m_workspaces->removeConnection(QStringLiteral("carol")));
    QTRY_COMPARE(m_servers->logouts().size(), 2);
    QCOMPARE(m_servers->logouts().at(1).host, QStringLiteral("other.test"));
    QCOMPARE(m_servers->logouts().at(1).token, QStringLiteral("token-carol"));
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
        m_store, m_transport.get(), QList<IProviderAdapter *>{m_adapter.get(), m_siloAdapter.get()});
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

void WorkspaceRoutingTest::removingToOneSourceMakesSpaceSingle()
{
    const QString id = m_workspaces->createProfile(
        QStringLiteral("Pair"), {QStringLiteral("alice"), QStringLiteral("carol")}, true);
    QVERIFY(!id.isEmpty());
    QSignalSpy switched(m_workspaces.get(), &WorkspaceController::workspaceSwitched);
    QVERIFY(m_workspaces->switchProfile(id));
    QTRY_COMPARE(switched.count(), 1);

    auto memberId = [&](const QString &connectionId) {
        for (const BloomProfileMember &member : m_repository->profile(id)->members) {
            if (member.connectionId == connectionId) {
                return member.memberId;
            }
        }
        return QString();
    };
    // Carol is hidden, then alice (the live default) is removed: the space
    // is left with carol alone, so it turns single and shows carol again.
    QVERIFY(m_workspaces->setMemberEnabled(id, memberId(QStringLiteral("carol")), false));
    QVERIFY(m_workspaces->removeMember(id, memberId(QStringLiteral("alice"))));

    const auto profile = m_repository->profile(id);
    QVERIFY(profile.has_value());
    QCOMPARE(profile->mode, BloomProfileMode::Single);
    QCOMPARE(profile->members.size(), 1);
    QCOMPARE(profile->members.first().connectionId, QStringLiteral("carol"));
    QVERIFY(profile->members.first().enabled);
    QCOMPARE(profile->defaultMemberId, profile->members.first().memberId);
    QTRY_COMPARE(m_auth->activeConnectionId(), QStringLiteral("carol"));
    // Alice keeps her saved sign-in.
    QCOMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                CredentialKind::AccessToken),
             QStringLiteral("token-alice"));
}

void WorkspaceRoutingTest::mergedSpaceHidesAndUnsearchesChosenLibraries()
{
    const QString profileId = createMergedWorkspace();
    QVERIFY(!profileId.isEmpty());
    waitForMembersReady();
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.views.append(view(QStringLiteral("lib-kids"), QStringLiteral("Kids")));
    alice.itemsByLibrary.insert(QStringLiteral("lib-movies"),
                                QJsonArray{movie(QStringLiteral("m-alice"), QStringLiteral("Heist"))});
    alice.itemsByLibrary.insert(QStringLiteral("lib-kids"),
                                QJsonArray{movie(QStringLiteral("k-alice"), QStringLiteral("Heist Jr"))});
    m_servers->accountsByToken[QStringLiteral("token-bob")].itemsByLibrary.insert(
        QStringLiteral("lib-movies"),
        QJsonArray{movie(QStringLiteral("m-bob"), QStringLiteral("Heist Again"))});

    QString aliceMemberId;
    for (const BloomProfileMember &member : m_repository->profile(profileId)->members) {
        if (member.connectionId == QStringLiteral("alice")) {
            aliceMemberId = member.memberId;
        }
    }
    // Alice browses only Movies and searches only Kids; bob is untouched.
    QVERIFY(m_workspaces->setMemberLibraries(profileId, aliceMemberId,
                                             {QStringLiteral("lib-kids")},
                                             {QStringLiteral("lib-movies")}));
    QVERIFY(m_merged->isActive());
    waitForMembersReady();

    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());
    QCOMPARE(viewRefs(m_merged->views()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("bob/lib-movies"),
                          QStringLiteral("carol/lib-shows")}));

    // Settings still lists every library so a hidden one can be shown again.
    QSignalSpy libraries(m_merged.get(), &MergedCatalogService::sourceLibrariesLoaded);
    m_merged->loadSourceLibraries(QStringLiteral("alice"));
    QTRY_COMPARE(libraries.count(), 1);
    QCOMPARE(viewRefs(libraries.first().at(1).toList()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("alice/lib-kids")}));

    m_servers->requests.clear();
    QSignalSpy results(m_merged.get(), &MergedCatalogService::searchResultsLoaded);
    m_merged->search(QStringLiteral("Heist"));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(itemIds(results.first().at(1).toList()),
             QStringList({QStringLiteral("k-alice"), QStringLiteral("m-bob")}));

    QStringList aliceSearchScopes;
    QStringList bobSearchScopes;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path != QStringLiteral("/Items")) {
            continue;
        }
        if (request.token == QStringLiteral("token-alice")) {
            aliceSearchScopes.append(request.parentId);
        } else if (request.token == QStringLiteral("token-bob")) {
            bobSearchScopes.append(request.parentId);
        }
    }
    QCOMPARE(aliceSearchScopes, QStringList({QStringLiteral("lib-kids")}));
    QCOMPARE(bobSearchScopes, QStringList({QString()}));
}

void WorkspaceRoutingTest::singleSpaceHidesAndUnsearchesChosenLibraries()
{
    const auto profile = m_repository->activeProfile();
    QVERIFY(profile.has_value());
    QCOMPARE(profile->members.size(), 1);
    const QString memberId = profile->members.first().memberId;
    QVERIFY(!m_merged->isActive());

    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.views.append(view(QStringLiteral("lib-kids"), QStringLiteral("Kids")));
    alice.itemsByLibrary.insert(QStringLiteral("lib-movies"),
                                QJsonArray{movie(QStringLiteral("m-alice"), QStringLiteral("Heist"))});
    alice.itemsByLibrary.insert(QStringLiteral("lib-kids"),
                                QJsonArray{movie(QStringLiteral("k-alice"), QStringLiteral("Heist Jr"))});

    QSignalSpy visibility(m_workspaces.get(), &WorkspaceController::libraryVisibilityChanged);
    QVERIFY(m_workspaces->setMemberLibraries(profile->id, memberId,
                                             {QStringLiteral(" lib-kids "), QStringLiteral("lib-kids")},
                                             {QStringLiteral("lib-kids")}));
    QCOMPARE(visibility.count(), 1);
    QCOMPARE(m_repository->activeProfile()->members.first().hiddenLibraryIds,
             QStringList({QStringLiteral("lib-kids")}));
    // Saving the same choices again is a no-op.
    QVERIFY(m_workspaces->setMemberLibraries(profile->id, memberId,
                                             {QStringLiteral("lib-kids")},
                                             {QStringLiteral("lib-kids")}));
    QCOMPARE(visibility.count(), 1);

    // A library ID is only unique per connection: another account's
    // library with the same ID stays visible.
    const QVariantList views{
        QVariantMap{{QStringLiteral("connectionId"), QStringLiteral("alice")},
                    {QStringLiteral("itemId"), QStringLiteral("lib-movies")}},
        QVariantMap{{QStringLiteral("connectionId"), QStringLiteral("alice")},
                    {QStringLiteral("itemId"), QStringLiteral("lib-kids")}},
        QVariantMap{{QStringLiteral("connectionId"), QStringLiteral("bob")},
                    {QStringLiteral("itemId"), QStringLiteral("lib-kids")}},
    };
    QCOMPARE(viewRefs(m_workspaces->visibleLibraries(views)),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("bob/lib-kids")}));
    QCOMPARE(m_workspaces->searchHiddenLibraryIds(QStringLiteral("alice")),
             QStringList({QStringLiteral("lib-kids")}));
    QVERIFY(m_workspaces->searchHiddenLibraryIds(QStringLiteral("bob")).isEmpty());

    // The live connection lists its libraries without merged membership.
    QSignalSpy libraries(m_merged.get(), &MergedCatalogService::sourceLibrariesLoaded);
    m_merged->loadSourceLibraries(QStringLiteral("alice"));
    QTRY_COMPARE(libraries.count(), 1);
    QCOMPARE(viewRefs(libraries.first().at(1).toList()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("alice/lib-kids")}));
    // A connection outside the space has no session to list with.
    QSignalSpy failed(m_merged.get(), &MergedCatalogService::sourceLibrariesFailed);
    m_merged->loadSourceLibraries(QStringLiteral("carol"));
    QCOMPARE(failed.count(), 1);

    LibraryService library(m_auth.get());
    m_servers->requests.clear();
    QSignalSpy results(&library, &LibraryService::canonicalSearchResultsLoaded);
    library.search(QStringLiteral("Heist"), 50,
                   m_workspaces->searchHiddenLibraryIds(QStringLiteral("alice")));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(itemIds(results.first().at(2).toList()), QStringList({QStringLiteral("m-alice")}));
    QStringList scopes;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path == QStringLiteral("/Items")) {
            scopes.append(request.parentId);
        }
    }
    QCOMPARE(scopes, QStringList({QStringLiteral("lib-movies")}));
}

void WorkspaceRoutingTest::unsupportedLibrariesLeaveViewsAndSearch()
{
    // Only movie and series libraries exist in the app: a music library (or
    // any other type) never reaches views, and search goes around it.
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.views.append(view(QStringLiteral("lib-music"), QStringLiteral("Music"),
                            QStringLiteral("music")));
    alice.itemsByLibrary.insert(QStringLiteral("lib-movies"),
                                QJsonArray{movie(QStringLiteral("m-alice"), QStringLiteral("Heist"))});
    alice.itemsByLibrary.insert(QStringLiteral("lib-music"),
                                QJsonArray{movie(QStringLiteral("x-alice"), QStringLiteral("Heist Live"))});

    // Searching before the library list is known lists it first instead of
    // assuming no library is of an unsupported type.
    {
        LibraryService fresh(m_auth.get());
        m_servers->requests.clear();
        QSignalSpy freshResults(&fresh, &LibraryService::canonicalSearchResultsLoaded);
        fresh.search(QStringLiteral("Heist"), 50, {});
        QTRY_COMPARE(freshResults.count(), 1);
        QCOMPARE(itemIds(freshResults.first().at(2).toList()),
                 QStringList({QStringLiteral("m-alice")}));
    }

    LibraryService library(m_auth.get());
    QSignalSpy views(&library, &LibraryService::canonicalViewsLoadedForConnection);
    library.getViews();
    QTRY_COMPARE(views.count(), 1);
    const QVariantList listed = views.first().at(1).toList();
    QCOMPARE(viewRefs(listed), QStringList({QStringLiteral("alice/lib-movies")}));
    QCOMPARE(listed.first().toMap().value(QStringLiteral("libraryKind")).toString(),
             QStringLiteral("movies"));

    m_servers->requests.clear();
    QSignalSpy results(&library, &LibraryService::canonicalSearchResultsLoaded);
    library.search(QStringLiteral("Heist"), 50, {});
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(itemIds(results.first().at(2).toList()), QStringList({QStringLiteral("m-alice")}));
    QStringList scopes;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path == QStringLiteral("/Items")) {
            scopes.append(request.parentId);
        }
    }
    QCOMPARE(scopes, QStringList({QStringLiteral("lib-movies")}));
}

void WorkspaceRoutingTest::scopedSearchInterleavesLibrariesByRank()
{
    const auto item = [](const QString &id) {
        return QVariant(QVariantMap{{QStringLiteral("itemId"), id}});
    };
    const QVariantList views{item(QStringLiteral("a")), item(QStringLiteral("b")),
                             item(QStringLiteral("c"))};
    QCOMPARE(LibrarySearchScope::searchableLibraryIds(views, {QStringLiteral("b")}),
             QStringList({QStringLiteral("a"), QStringLiteral("c")}));

    // Rank by rank across libraries; an item found in two libraries once.
    const QVariantList merged = LibrarySearchScope::interleave(
        {{item(QStringLiteral("a1")), item(QStringLiteral("a2")), item(QStringLiteral("a3"))},
         {item(QStringLiteral("c1")), item(QStringLiteral("a2"))}},
        4);
    QStringList order;
    for (const QVariant &value : merged) {
        order.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    QCOMPARE(order, QStringList({QStringLiteral("a1"), QStringLiteral("c1"),
                                 QStringLiteral("a2"), QStringLiteral("a3")}));
}

QString WorkspaceRoutingTest::createSiloMergedWorkspace()
{
    ServerConnection erin = connection(QStringLiteral("erin"), QStringLiteral("silo.test"),
                                       QStringLiteral("u-erin"));
    erin.providerKind = ProviderKind::Silo;
    erin.profileId.clear();
    m_config->upsertConnection(erin, false);
    if (!credentials().write(erin, CredentialKind::AccessToken, QStringLiteral("erin-a1"))
        || !credentials().write(erin, CredentialKind::RefreshToken, QStringLiteral("erin-r1"))) {
        return {};
    }
    m_servers->accountsByToken.insert(
        QStringLiteral("erin-a1"),
        Account{QStringLiteral("u-erin"), QStringLiteral("silo.test"),
                QJsonArray{view(QStringLiteral("s1"), QStringLiteral("Shows"))}});
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-a1"));
    const QString id = m_workspaces->createProfile(
        QStringLiteral("Mixed"), {QStringLiteral("alice"), QStringLiteral("erin")}, true);
    if (id.isEmpty() || !m_workspaces->switchProfile(id)) {
        return {};
    }
    return id;
}

QString WorkspaceRoutingTest::sourceState(const QString &connectionId) const
{
    for (const QVariant &value : m_merged->sources()) {
        const QVariantMap source = value.toMap();
        if (source.value(QStringLiteral("connectionId")).toString() == connectionId) {
            return source.value(QStringLiteral("state")).toString();
        }
    }
    return {};
}

void WorkspaceRoutingTest::pooledMemberRenewsExpiredTokenOnceAndSavesIt()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    // The access token expires; its refresh token is still good.
    const Account erin = m_servers->accountsByToken.take(QStringLiteral("erin-a1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-expired"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-expired"));

    // Two reads hit the expiry at once and share one renewal.
    m_servers->requests.clear();
    QSignalSpy libraries(m_merged.get(), &MergedCatalogService::sourceLibrariesLoaded);
    m_merged->refreshViews();
    m_merged->loadSourceLibraries(QStringLiteral("erin"));
    QTRY_VERIFY(!m_merged->viewsLoading() && libraries.count() == 1);
    QCOMPARE(m_servers->refreshRequests, 1);
    int expiredReads = 0;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.token == QStringLiteral("erin-a1")) {
            ++expiredReads;
        }
    }
    QCOMPARE(expiredReads, 2);
    QCOMPARE(viewRefs(m_merged->views()),
             QStringList({QStringLiteral("alice/lib-movies"), QStringLiteral("erin/s1")}));
    QCOMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("ready"));

    // The rotated tokens are saved, so the next session starts from them.
    const ServerConnection saved = *m_config->getConnection(QStringLiteral("erin"));
    QTRY_COMPARE(credentials().read(saved, CredentialKind::AccessToken),
                 QStringLiteral("access-r1"));
    QTRY_COMPARE(credentials().read(saved, CredentialKind::RefreshToken),
                 QStringLiteral("refresh-r1"));
    QCOMPARE(m_auth->sessionPool()->credentials(QStringLiteral("erin"))->accessToken,
             QStringLiteral("access-r1"));
    // A merged read never moves the live session.
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("alice"));
}

void WorkspaceRoutingTest::rejectedRenewalNeedsSignInButKeepsCredentials()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    m_servers->accountsByToken.remove(QStringLiteral("erin-a1"));
    m_servers->refreshMode = FakeServers::RefreshMode::Reject;

    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());
    QCOMPARE(viewRefs(m_merged->views()), QStringList({QStringLiteral("alice/lib-movies")}));
    QCOMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("signInRequired"));
    QVERIFY(m_auth->sessionPool()->isRejected(QStringLiteral("erin")));
    // Rejection is reported, not acted on: the saved sign-in stays.
    const ServerConnection saved = *m_config->getConnection(QStringLiteral("erin"));
    QCOMPARE(credentials().read(saved, CredentialKind::AccessToken), QStringLiteral("erin-a1"));
    QCOMPARE(credentials().read(saved, CredentialKind::RefreshToken), QStringLiteral("erin-r1"));
    QVERIFY(m_auth->isAuthenticated());

    // A rejected member does not retry the refresh on every read.
    const int refreshes = m_servers->refreshRequests;
    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());
    QCOMPARE(m_servers->refreshRequests, refreshes);
}

void WorkspaceRoutingTest::unreachableRenewalKeepsMemberUntilServerReturns()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    const Account erin = m_servers->accountsByToken.take(QStringLiteral("erin-a1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-expired"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-expired"));
    m_servers->refreshMode = FakeServers::RefreshMode::Offline;

    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading() && !m_merged->views().isEmpty());
    QCOMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("failed"));
    QVERIFY(!m_auth->sessionPool()->isRejected(QStringLiteral("erin")));
    QVERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));

    // The server is back: the next read renews and includes the source.
    m_servers->refreshMode = FakeServers::RefreshMode::Renew;
    m_merged->refreshViews();
    QTRY_VERIFY(!m_merged->viewsLoading()
                && viewRefs(m_merged->views()).contains(QStringLiteral("erin/s1")));
    QCOMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("ready"));
}

void WorkspaceRoutingTest::unreachableServerAtStartupKeepsCredentials()
{
    // Restart against a server that does not answer: the restore fails,
    // but the saved sign-in and the account selection survive.
    m_workspaces.reset();
    m_merged.reset();
    m_auth.reset();
    m_servers->accountsByToken[QStringLiteral("token-alice")].offline = true;

    AuthenticationService restarted(
        m_store, m_transport.get(),
        QList<IProviderAdapter *>{m_adapter.get(), m_siloAdapter.get()});
    QSignalSpy loginError(&restarted, &AuthenticationService::loginError);
    restarted.initialize(m_config.get());
    // isRestoringSession clears once the saved sign-in is read; the failed
    // validation is reported later, so wait for the error itself.
    QTRY_COMPARE(loginError.count(), 1);
    QVERIFY(!restarted.isRestoringSession());
    QVERIFY(!restarted.isAuthenticated());

    const ServerConnection alice = *m_config->getConnection(QStringLiteral("alice"));
    QTest::qWait(50);
    QCOMPARE(loginError.count(), 1);
    QCOMPARE(credentials().read(alice, CredentialKind::AccessToken),
             QStringLiteral("token-alice"));
    QCOMPARE(m_config->getActiveConnection()->connectionId, QStringLiteral("alice"));

    // Once the server answers again, the next start signs straight in.
    m_servers->accountsByToken[QStringLiteral("token-alice")].offline = false;
    AuthenticationService again(
        m_store, m_transport.get(),
        QList<IProviderAdapter *>{m_adapter.get(), m_siloAdapter.get()});
    again.initialize(m_config.get());
    QTRY_VERIFY(again.isAuthenticated());
}

void WorkspaceRoutingTest::startHeldErinRenewal()
{
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    const Account erin = m_servers->accountsByToken.take(QStringLiteral("erin-a1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-expired"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-expired"));
    m_servers->holdRefresh = true;
    m_merged->refreshViews();
    QTRY_COMPARE(m_servers->heldRefreshes.size(), 1);
}

void WorkspaceRoutingTest::renewalFinishingAfterSpaceSwitchIsSaved()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    startHeldErinRenewal();

    // Switching to a space without erin drops it from the pool while the
    // server has already rotated its refresh token.
    const QString single = m_workspaces->createProfile(
        QStringLiteral("Just Alice"), {QStringLiteral("alice")}, false);
    QVERIFY(m_workspaces->switchProfile(single));
    QVERIFY(!m_auth->sessionPool()->isTracked(QStringLiteral("erin")));
    m_servers->heldRefreshes.constFirst()->deliver();

    const ServerConnection saved = *m_config->getConnection(QStringLiteral("erin"));
    QTRY_COMPARE(credentials().read(saved, CredentialKind::RefreshToken),
                 QStringLiteral("refresh-r1"));
    QCOMPARE(credentials().read(saved, CredentialKind::AccessToken), QStringLiteral("access-r1"));
}

void WorkspaceRoutingTest::activationAdoptsRenewalAlreadyInFlight()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    startHeldErinRenewal();

    // Routing to erin restores its stored (expired) tokens; validation fails
    // and the restore joins the renewal already in flight.
    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("erin")));
    QTRY_VERIFY(m_servers->requests.constLast().path == QStringLiteral("/api/v2/account/me"));
    QTest::qWait(50);
    m_servers->heldRefreshes.constFirst()->deliver();

    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(m_auth->activeConnectionId(), QStringLiteral("erin"));
    QCOMPARE(m_auth->getAccessToken(), QStringLiteral("access-r1"));
    QCOMPARE(m_servers->refreshRequests, 1);
    const ServerConnection saved = *m_config->getConnection(QStringLiteral("erin"));
    QTRY_COMPARE(credentials().read(saved, CredentialKind::RefreshToken),
                 QStringLiteral("refresh-r1"));
}

void WorkspaceRoutingTest::renewalFinishingAfterRemovalDoesNotReviveCredentials()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    startHeldErinRenewal();

    const ServerConnection erin = *m_config->getConnection(QStringLiteral("erin"));
    QVERIFY(m_workspaces->removeConnection(QStringLiteral("erin")));
    QTRY_VERIFY(credentials().read(erin, CredentialKind::RefreshToken).isEmpty());
    m_servers->heldRefreshes.constFirst()->deliver();

    QTest::qWait(100);
    QVERIFY(credentials().read(erin, CredentialKind::AccessToken).isEmpty());
    QVERIFY(credentials().read(erin, CredentialKind::RefreshToken).isEmpty());
}

void WorkspaceRoutingTest::replacedSearchDoesNotPublishResults()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.views.append(view(QStringLiteral("lib-kids"), QStringLiteral("Kids")));
    alice.itemsByLibrary.insert(QStringLiteral("lib-movies"),
                                QJsonArray{movie(QStringLiteral("m-alice"), QStringLiteral("Heist"))});
    alice.itemsByLibrary.insert(QStringLiteral("lib-kids"),
                                QJsonArray{movie(QStringLiteral("k-alice"), QStringLiteral("Heist Jr"))});

    // Exclusions change while the first, unscoped search is in flight; its
    // reply arrives first and must not be shown.
    LibraryService library(m_auth.get());
    QSignalSpy results(&library, &LibraryService::canonicalSearchResultsLoaded);
    library.search(QStringLiteral("Heist"));
    library.search(QStringLiteral("Heist"), 50, {QStringLiteral("lib-kids")});
    QTRY_COMPARE(results.count(), 1);
    QTest::qWait(50);
    QCOMPARE(results.count(), 1);
    QCOMPARE(itemIds(results.first().at(2).toList()), QStringList({QStringLiteral("m-alice")}));
}

void WorkspaceRoutingTest::seedPeople(const QByteArray &erinCapabilities,
                                      int erinCapabilitiesStatus)
{
    m_servers->accountsByToken[QStringLiteral("token-alice")].people = QJsonArray{
        QJsonObject{{QStringLiteral("Id"), QStringLiteral("p-alice")},
                    {QStringLiteral("Name"), QStringLiteral("Al Pacino")},
                    {QStringLiteral("Type"), QStringLiteral("Person")}}};
    Account &erin = m_servers->accountsByToken[QStringLiteral("erin-a1")];
    erin.people = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("7")},
                                         {QStringLiteral("name"), QStringLiteral("Al Pacino")}}};
    erin.searchCapabilities = erinCapabilities;
    erin.searchCapabilitiesStatus = erinCapabilitiesStatus;
}

QStringList WorkspaceRoutingTest::pathsSince(qsizetype from, const QString &path) const
{
    QStringList hosts;
    for (qsizetype index = from; index < m_servers->requests.size(); ++index) {
        if (m_servers->requests.at(index).path == path) {
            hosts.append(m_servers->requests.at(index).host);
        }
    }
    return hosts;
}

namespace {

QStringList peopleRefs(const QVariantList &people)
{
    QStringList refs;
    for (const QVariant &value : people) {
        const QVariantMap person = value.toMap();
        refs.append(person.value(QStringLiteral("connectionId")).toString() + QLatin1Char('/')
                    + person.value(QStringLiteral("personId")).toString());
    }
    return refs;
}

const QString kSearchCapabilities = QStringLiteral("/api/v2/catalog/search/capabilities");
const QString kSiloPeople = QStringLiteral("/api/v2/catalog/people");

} // namespace

// Each member decides for itself: Jellyfin searches GetPersons with film and
// TV credit kinds, Silo only once its capability allows a video scope. The
// capability is read once per member and read again in a new epoch.
void WorkspaceRoutingTest::mergedPeopleSearchReadsEachMembersCapability()
{
    const QString mixed = createSiloMergedWorkspace();
    QVERIFY(!mixed.isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    seedPeople(QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true,"people_media_scope":true})"));

    QSignalSpy loaded(m_merged.get(), &MergedCatalogService::peopleSearchLoaded);
    qsizetype first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral(" pacino "));
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QStringLiteral("pacino"));
    // One name on two servers stays two people, each owned by its source.
    QCOMPARE(peopleRefs(loaded.first().at(1).toList()),
             QStringList({QStringLiteral("alice/p-alice"), QStringLiteral("erin/7")}));
    QCOMPARE(pathsSince(first, kSearchCapabilities), QStringList{QStringLiteral("silo.test")});
    QCOMPARE(pathsSince(first, kSiloPeople), QStringList{QStringLiteral("silo.test")});
    QCOMPARE(pathsSince(first, QStringLiteral("/Persons")), QStringList{QStringLiteral("media.test")});
    for (qsizetype index = first; index < m_servers->requests.size(); ++index) {
        const RecordedRequest &request = m_servers->requests.at(index);
        const QUrlQuery query(request.query);
        if (request.path == QStringLiteral("/Persons")) {
            QCOMPARE(request.token, QStringLiteral("token-alice"));
            QCOMPARE(query.queryItemValue(QStringLiteral("personTypes")),
                     QStringLiteral("Actor,Director,Writer,Producer,GuestStar"));
        } else if (request.path == kSiloPeople) {
            QCOMPARE(request.token, QStringLiteral("erin-a1"));
            QCOMPARE(query.queryItemValue(QStringLiteral("media_scope")), QStringLiteral("video"));
        }
    }

    // Known now: the next search reads no capability.
    first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count(), 2);
    QVERIFY(pathsSince(first, kSearchCapabilities).isEmpty());
    QCOMPARE(pathsSince(first, kSiloPeople), QStringList{QStringLiteral("silo.test")});

    // Leaving the space and coming back starts a new epoch: the member may
    // be another server version by then, so its capability is read again.
    const QString single = m_workspaces->createProfile(
        QStringLiteral("Just Alice"), {QStringLiteral("alice")}, false);
    QVERIFY(!single.isEmpty());
    QVERIFY(m_workspaces->switchProfile(single));
    QVERIFY(!m_merged->isActive());
    QVERIFY(m_workspaces->switchProfile(mixed));
    QTRY_VERIFY(m_merged->isActive());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count(), 3);
    QCOMPARE(pathsSince(first, kSearchCapabilities), QStringList{QStringLiteral("silo.test")});
}

// A Silo member whose people search cannot be limited to movie and series
// credits adds no people and is never asked for any.
void WorkspaceRoutingTest::mergedPeopleSearchSkipsMembersThatCannotScope()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    seedPeople(QByteArrayLiteral(R"({"revision":"r1","state":"available","allowed":true})"));

    QSignalSpy loaded(m_merged.get(), &MergedCatalogService::peopleSearchLoaded);
    QSignalSpy failed(m_merged.get(), &MergedCatalogService::peopleSearchFailed);
    const qsizetype first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count(), 1);
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count(), 2);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(peopleRefs(loaded.last().at(1).toList()), QStringList{QStringLiteral("alice/p-alice")});
    QCOMPARE(pathsSince(first, kSearchCapabilities), QStringList{QStringLiteral("silo.test")});
    QVERIFY(pathsSince(first, kSiloPeople).isEmpty());
    // People search never changes a source's state.
    QCOMPARE(sourceState(QStringLiteral("erin")), sourceState(QStringLiteral("alice")));
}

// A member whose capability read or search fails leaves the others' people.
// The failed read is not repeated for every keystroke: meanwhile that member
// adds no people, like one that cannot scope.
void WorkspaceRoutingTest::mergedPeopleSearchKeepsWorkingMembers()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    seedPeople(QByteArrayLiteral(R"({"type":"about:blank","title":"Forbidden","status":403})"), 403);

    QSignalSpy loaded(m_merged.get(), &MergedCatalogService::peopleSearchLoaded);
    QSignalSpy failed(m_merged.get(), &MergedCatalogService::peopleSearchFailed);
    qsizetype first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(peopleRefs(loaded.first().at(1).toList()), QStringList{QStringLiteral("alice/p-alice")});
    // A 403 renews erin's token and tries once more before failing.
    QVERIFY(!pathsSince(first, kSearchCapabilities).isEmpty());
    QVERIFY(pathsSince(first, kSiloPeople).isEmpty());

    first = m_servers->requests.size();
    m_merged->searchPeople(QStringLiteral("pacin"));
    QTRY_COMPARE(loaded.count(), 2);
    QVERIFY(pathsSince(first, kSearchCapabilities).isEmpty());
    QVERIFY(pathsSince(first, kSiloPeople).isEmpty());
    QCOMPARE(failed.count(), 0);

    // Alice's search fails too: no people, and Search shows none.
    m_servers->accountsByToken[QStringLiteral("token-alice")].offline = true;
    m_merged->searchPeople(QStringLiteral("pacino"));
    QTRY_COMPARE(loaded.count() + failed.count(), 3);
    QCOMPARE(failed.count(), 0);
    QVERIFY(loaded.last().at(1).toList().isEmpty());
}

void WorkspaceRoutingTest::posterBadgesFollowEachSiloMembersPreferences()
{
    m_servers->overlaySettings = QJsonDocument(QJsonObject{
        {QStringLiteral("revision"), 15},
        {QStringLiteral("items"), QJsonArray{QJsonObject{
             {QStringLiteral("key"), QStringLiteral("ui.card_overlays")},
             {QStringLiteral("value"), QJsonObject{
                  {QStringLiteral("version"), 2},
                  {QStringLiteral("preset"), QStringLiteral("classic")},
                  {QStringLiteral("order"), QJsonArray{}},
                  {QStringLiteral("items"), QJsonObject{
                       {QStringLiteral("rating_imdb"),
                        QJsonObject{{QStringLiteral("enabled"), true},
                                    {QStringLiteral("position"), QStringLiteral("top-right")}}}}}}}}}}})
                                     .toJson(QJsonDocument::Compact);
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    const QString sourceBefore = sourceState(QStringLiteral("erin"));
    m_servers->requests.clear();

    PosterBadgeService badges(m_auth.get(), m_repository.get(), m_config.get(), m_merged.get());
    QVERIFY(badges.hasBadgeConnections());
    QTRY_VERIFY(badges.layoutFor(QStringLiteral("erin")).enabled);

    // Only the Silo member is asked, with its own server and token.
    int settingsReads = 0;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path.startsWith(QStringLiteral("/api/v2/settings/"))) {
            ++settingsReads;
            QCOMPARE(request.host, QStringLiteral("silo.test"));
            QCOMPARE(request.token, QStringLiteral("erin-a1"));
        }
    }
    QCOMPARE(settingsReads, 2);
    QVERIFY(!badges.layoutFor(QStringLiteral("alice")).enabled);
    // Preference reads never report on the source.
    QCOMPARE(sourceState(QStringLiteral("erin")), sourceBefore);

    QVariantMap item{
        {QStringLiteral("connectionId"), QStringLiteral("erin")},
        {QStringLiteral("mediaSummary"), QVariantMap{{QStringLiteral("resolution"), QStringLiteral("2160p")}}},
        {QStringLiteral("ratings"), QVariantMap{{QStringLiteral("imdb"), 7.0}}},
    };
    QStringList texts;
    for (const QVariant &badge : badges.badgesFor(item)) {
        texts.append(badge.toMap().value(QStringLiteral("text")).toString());
    }
    QCOMPARE(texts, QStringList({QStringLiteral("4K"), QStringLiteral("IMDb 7.0")}));
    // The same data on a Jellyfin member's item shows nothing.
    QVariantMap jellyfinItem = item;
    jellyfinItem[QStringLiteral("connectionId")] = QStringLiteral("alice");
    QVERIFY(badges.badgesFor(jellyfinItem).isEmpty());

    // The Bloom setting hides badges without forgetting the layout.
    QSignalSpy changed(&badges, &PosterBadgeService::badgesChanged);
    m_config->setServerPosterBadgesEnabled(false);
    QCOMPARE(changed.count(), 1);
    QVERIFY(badges.badgesFor(item).isEmpty());
    m_config->setServerPosterBadgesEnabled(true);
    QCOMPARE(badges.badgesFor(item).size(), 2);
}

void WorkspaceRoutingTest::failedPosterBadgeReadLeavesSourceAlone()
{
    m_servers->overlaySettingsStatus = 503;
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    m_merged->refreshViews();
    QTRY_COMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("ready"));
    m_servers->requests.clear();

    PosterBadgeService badges(m_auth.get(), m_repository.get(), m_config.get(), m_merged.get());
    const auto settingsReads = [this]() {
        int count = 0;
        for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
            count += request.path == QStringLiteral("/api/v2/settings/values/effective") ? 1 : 0;
        }
        return count;
    };
    // A safe read: the server error is retried once, then given up.
    QTRY_COMPARE_WITH_TIMEOUT(settingsReads(), 2, 5000);
    QTest::qWait(100);
    QVERIFY(!badges.layoutFor(QStringLiteral("erin")).enabled);
    QCOMPARE(sourceState(QStringLiteral("erin")), QStringLiteral("ready"));
}

void WorkspaceRoutingTest::recoveryPingChecksJellyfinSessionRoute()
{
    LibraryService library(m_auth.get());
    m_servers->requests.clear();
    QVERIFY(pingOnce(library, this) == ServerReachability::Reachable);
    QCOMPARE(m_servers->requests.size(), 1);
    QCOMPARE(m_servers->requests.constFirst().path, QStringLiteral("/Users/u-alice"));
    QCOMPARE(m_servers->requests.constFirst().token, QStringLiteral("token-alice"));

    m_servers->accountsByToken[QStringLiteral("token-alice")].offline = true;
    QVERIFY(pingOnce(library, this) == ServerReachability::Unreachable);

    // The server refuses the token, and Jellyfin has no refresh token.
    QSignalSpy expired(m_auth.get(), &AuthenticationService::sessionExpired);
    m_servers->accountsByToken.remove(QStringLiteral("token-alice"));
    QVERIFY(pingOnce(library, this) == ServerReachability::SessionExpired);
    // The sign-out waits until playback stops.
    QCOMPARE(expired.count(), 0);
    QVERIFY(m_auth->isAuthenticated());
}

void WorkspaceRoutingTest::recoveryPingRenewsSiloSessionBeforeReportingExpiry()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("erin")));
    QTRY_COMPARE(ready.count(), 1);
    LibraryService library(m_auth.get());

    // Native Silo has no /System/Info; the native identity route answers.
    m_servers->requests.clear();
    QVERIFY(pingOnce(library, this) == ServerReachability::Reachable);
    QCOMPARE(m_servers->requests.size(), 1);
    QCOMPARE(m_servers->requests.constFirst().host, QStringLiteral("silo.test"));
    QCOMPARE(m_servers->requests.constFirst().path, QStringLiteral("/api/v2/account/me"));
    QCOMPARE(m_servers->requests.constFirst().token, QStringLiteral("erin-a1"));

    // The access token expired during the outage; its refresh token is good.
    Account erin = m_servers->accountsByToken.take(QStringLiteral("erin-a1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-expired"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-expired"));
    QVERIFY(pingOnce(library, this) == ServerReachability::Reachable);
    QCOMPARE(m_servers->refreshRequests, 1);
    QCOMPARE(m_auth->getAccessToken(), QStringLiteral("access-r1"));

    // Expired again, and the refresh endpoint is down: only unreachable.
    QSignalSpy expired(m_auth.get(), &AuthenticationService::sessionExpired);
    erin = m_servers->accountsByToken.take(QStringLiteral("access-r1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-expired-2"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("refresh-r1"),
                                                QStringLiteral("erin-expired-2"));
    m_servers->refreshMode = FakeServers::RefreshMode::Offline;
    QVERIFY(pingOnce(library, this) == ServerReachability::Unreachable);

    // The server refuses the refresh token: the session has expired.
    m_servers->refreshMode = FakeServers::RefreshMode::Reject;
    QVERIFY(pingOnce(library, this) == ServerReachability::SessionExpired);
    QCOMPARE(expired.count(), 0);
    QVERIFY(m_auth->isAuthenticated());
}

void WorkspaceRoutingTest::signOutEndsServerSessionWithoutWaiting()
{
    QSignalSpy loggedOut(m_auth.get(), &AuthenticationService::loggedOut);
    m_servers->requests.clear();
    m_auth->logout();

    // The local sign-out is done before the server answers.
    QVERIFY(!m_auth->isAuthenticated());
    QCOMPARE(loggedOut.count(), 1);
    QCOMPARE(m_servers->logouts().size(), 1);
    const RecordedRequest request = m_servers->logouts().constFirst();
    QCOMPARE(request.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(request.host, QStringLiteral("media.test"));
    QCOMPARE(request.path, QStringLiteral("/Sessions/Logout"));
    QCOMPARE(request.token, QStringLiteral("token-alice"));
    // Clearing the session cancels its transport, but not the logout.
    QCOMPARE(m_servers->abortedLogouts, 0);
    QTest::qWait(20);
    QCOMPARE(m_servers->abortedLogouts, 0);
    QVERIFY(!m_servers->accountsByToken.contains(QStringLiteral("token-alice")));
    QVERIFY(m_servers->accountsByToken.contains(QStringLiteral("token-bob")));
    QTRY_COMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                    CredentialKind::AccessToken),
                 QString());
}

void WorkspaceRoutingTest::signOutFinishesWhenServerIsDown()
{
    m_servers->accountsByToken[QStringLiteral("token-alice")].offline = true;
    QSignalSpy errors(m_auth.get(), &AuthenticationService::loginError);
    QSignalSpy loggedOut(m_auth.get(), &AuthenticationService::loggedOut);
    m_servers->requests.clear();
    m_auth->logout();

    QVERIFY(!m_auth->isAuthenticated());
    QCOMPARE(loggedOut.count(), 1);
    QCOMPARE(m_servers->logouts().size(), 1);
    QTRY_COMPARE(credentials().read(*m_config->getConnection(QStringLiteral("alice")),
                                    CredentialKind::AccessToken),
                 QString());
    // A failed logout is never replayed and never reaches the user.
    QTest::qWait(50);
    QCOMPARE(m_servers->logouts().size(), 1);
    QCOMPARE(errors.count(), 0);
}

void WorkspaceRoutingTest::expiredSessionSignOutSkipsServerLogout()
{
    QSignalSpy expired(m_auth.get(), &AuthenticationService::sessionExpired);
    emit m_transport->unauthorized(false);
    QCOMPARE(expired.count(), 1);

    m_servers->requests.clear();
    m_auth->logout();
    QVERIFY(!m_auth->isAuthenticated());
    QVERIFY(m_servers->logouts().isEmpty());
}

void WorkspaceRoutingTest::siloSignOutUsesNativeLogoutRoute()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("erin")));
    QTRY_COMPARE(ready.count(), 1);

    m_servers->requests.clear();
    m_auth->logout();
    QCOMPARE(m_servers->logouts().size(), 1);
    const RecordedRequest request = m_servers->logouts().constFirst();
    QCOMPARE(request.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(request.host, QStringLiteral("silo.test"));
    QCOMPARE(request.path, QStringLiteral("/api/v2/auth/logout"));
    QCOMPARE(request.token, QStringLiteral("erin-a1"));
}

void WorkspaceRoutingTest::siloSignOutRefreshesAnExpiredTokenFirst()
{
    QVERIFY(!createSiloMergedWorkspace().isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));
    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("erin")));
    QTRY_COMPARE(ready.count(), 1);

    // The access token expired while Bloom sat idle; the refresh token works.
    const Account erin = m_servers->accountsByToken.take(QStringLiteral("erin-a1"));
    m_servers->accountsByToken.insert(QStringLiteral("erin-old"), erin);
    m_servers->accessTokenByRefreshToken.insert(QStringLiteral("erin-r1"),
                                                QStringLiteral("erin-old"));

    m_servers->requests.clear();
    m_auth->logout();
    QVERIFY(!m_auth->isAuthenticated());
    // Refused, refreshed once, then sent once more with the new token, which
    // ends the server session.
    QTRY_COMPARE(m_servers->logouts().size(), 2);
    QCOMPARE(m_servers->logouts().at(0).token, QStringLiteral("erin-a1"));
    QVERIFY(m_servers->logouts().at(1).token.startsWith(QStringLiteral("access-r")));
    QVERIFY(!m_servers->accountsByToken.contains(m_servers->logouts().at(1).token));
    QTest::qWait(50);
    QCOMPARE(m_servers->logouts().size(), 2);
    // The rotated tokens are never stored.
    QTRY_COMPARE(credentials().read(*m_config->getConnection(QStringLiteral("erin")),
                                    CredentialKind::AccessToken),
                 QString());
}

void WorkspaceRoutingTest::removingSiloMemberEndsItsSession()
{
    const QString household = createSiloMergedWorkspace();
    QVERIFY(!household.isEmpty());
    QTRY_VERIFY(m_auth->hasSessionForConnection(QStringLiteral("erin")));

    m_servers->requests.clear();
    QVERIFY(m_workspaces->removeConnection(QStringLiteral("erin")));
    QTRY_COMPARE(m_servers->logouts().size(), 1);
    QCOMPARE(m_servers->logouts().constFirst().path, QStringLiteral("/api/v2/auth/logout"));
    QCOMPARE(m_servers->logouts().constFirst().token, QStringLiteral("erin-a1"));
    QVERIFY(!m_servers->accountsByToken.contains(QStringLiteral("erin-a1")));
    QVERIFY(m_servers->accountsByToken.contains(QStringLiteral("token-alice")));
}

void WorkspaceRoutingTest::switchingSpacesNeverEndsServerSessions()
{
    const QString household = createMergedWorkspace();
    QVERIFY(!household.isEmpty());
    waitForMembersReady();
    m_servers->requests.clear();

    QSignalSpy ready(m_workspaces.get(), &WorkspaceController::routeReady);
    QVERIFY(!m_workspaces->routeTo(QStringLiteral("carol")));
    QTRY_COMPARE(ready.count(), 1);

    const QString single = m_workspaces->createProfile(
        QStringLiteral("Just Dave"), {QStringLiteral("dave")}, false);
    QVERIFY(!single.isEmpty());
    QVERIFY(m_workspaces->switchProfile(single));
    QTRY_VERIFY(m_auth->isAuthenticated()
                && m_auth->activeConnectionId() == QStringLiteral("dave"));

    // Adding a server leaves the live session without deleting it.
    m_workspaces->beginAddConnection(household);
    QVERIFY(!m_auth->isAuthenticated());
    m_workspaces->cancelAddConnection();
    QTRY_VERIFY(m_auth->isAuthenticated());

    QVERIFY(m_servers->logouts().isEmpty());
}

void WorkspaceRoutingTest::jellyfinNextUpAddsResumeListAndOptions()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.resume = {
        playable(QStringLiteral("movie-half"), QStringLiteral("Movie"), 6000000000,
                 QStringLiteral("2026-10-30T20:15:00.1234567Z")),
        playable(QStringLiteral("episode-part"), QStringLiteral("Episode"), 3000000000,
                 QStringLiteral("2026-10-31T21:00:00.0000000Z")),
    };
    alice.nextUp = {
        playable(QStringLiteral("episode-next"), QStringLiteral("Episode"), 0),
        // Listed by both reads: shown once, as in progress.
        playable(QStringLiteral("episode-part"), QStringLiteral("Episode"), 3000000000,
                 QStringLiteral("2026-10-31T21:00:00.0000000Z")),
    };

    LibraryService library(m_auth.get());
    QVERIFY(library.supportsNextUpOptions());
    QSignalSpy loaded(&library, &LibraryService::canonicalNextUpLoaded);
    m_servers->requests.clear();
    library.getNextUp();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QStringLiteral("alice"));
    // In-progress movies join Continue Watching, most recently played first.
    QCOMPARE(orderedRefs(loaded.first().at(1).toList()),
             (QStringList{QStringLiteral("alice/episode-part"), QStringLiteral("alice/movie-half"),
                          QStringLiteral("alice/episode-next")}));

    QUrlQuery nextUpQuery;
    QUrlQuery resumeQuery;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        QCOMPARE(request.token, QStringLiteral("token-alice"));
        if (request.path == QStringLiteral("/Shows/NextUp")) {
            nextUpQuery = request.query;
        } else if (request.path == QStringLiteral("/UserItems/Resume")) {
            resumeQuery = request.query;
        }
    }
    QCOMPARE(m_servers->requests.size(), 2);
    QCOMPARE(nextUpQuery.queryItemValue(QStringLiteral("EnableResumable")), QStringLiteral("true"));
    QVERIFY(!nextUpQuery.hasQueryItem(QStringLiteral("EnableRewatching")));
    QVERIFY(!nextUpQuery.hasQueryItem(QStringLiteral("NextUpDateCutoff")));
    QCOMPARE(resumeQuery.queryItemValue(QStringLiteral("UserId")), QStringLiteral("u-alice"));
    QCOMPARE(resumeQuery.queryItemValue(QStringLiteral("Limit")), QStringLiteral("20"));
    QCOMPARE(resumeQuery.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Episode"));

    // The saved Next Up options reach the request.
    m_config->setNextUpRewatching(true);
    m_config->setNextUpCutoffDays(90);
    m_servers->requests.clear();
    library.getNextUp();
    QTRY_COMPARE(loaded.count(), 2);
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path == QStringLiteral("/Shows/NextUp")) {
            nextUpQuery = request.query;
        }
    }
    QCOMPARE(nextUpQuery.queryItemValue(QStringLiteral("EnableRewatching")), QStringLiteral("true"));
    QCOMPARE(nextUpQuery.queryItemValue(QStringLiteral("NextUpDateCutoff")),
             QDate::currentDate().addDays(-90).startOfDay(QTimeZone::UTC).toString(Qt::ISODate));

    // A failed resume list leaves Next Up.
    alice.resumeStatus = 404;
    library.getNextUp();
    QTRY_COMPARE(loaded.count(), 3);
    QCOMPARE(orderedRefs(loaded.at(2).at(1).toList()),
             (QStringList{QStringLiteral("alice/episode-part"), QStringLiteral("alice/episode-next")}));
}

void WorkspaceRoutingTest::jellyfinHomeShowsFavoritesRowBehindItsSetting()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.favorites = {
        playable(QStringLiteral("series-fav"), QStringLiteral("Series"), 0,
                 QStringLiteral("2026-10-31T10:00:00Z")),
        movie(QStringLiteral("movie-fav"), QStringLiteral("Loved")),
    };

    LibraryService library(m_auth.get());
    QVERIFY(!library.supportsWatchlist());
    QVERIFY(library.supportsHomeFavoritesRow());
    QSignalSpy loaded(&library, &LibraryService::canonicalHomeSectionsLoaded);
    m_servers->requests.clear();
    library.getHomeSections();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QStringLiteral("alice"));
    const QVariantList rows = loaded.first().at(1).toList();
    QCOMPARE(rows.size(), 1);
    const QVariantMap favorites = rows.first().toMap();
    QCOMPARE(favorites.value(QStringLiteral("sectionId")).toString(),
             QStringLiteral("bloom:favorites"));
    QCOMPARE(favorites.value(QStringLiteral("title")).toString(), QStringLiteral("Favorites"));
    QCOMPARE(orderedRefs(favorites.value(QStringLiteral("items")).toList()),
             (QStringList{QStringLiteral("alice/series-fav"), QStringLiteral("alice/movie-fav")}));

    // One /Items read: favorite movies and series, most recently played first.
    QCOMPARE(m_servers->requests.size(), 1);
    const RecordedRequest request = m_servers->requests.constFirst();
    QCOMPARE(request.path, QStringLiteral("/Items"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("UserId")), QStringLiteral("u-alice"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("IsFavorite")), QStringLiteral("true"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("IncludeItemTypes")),
             QStringLiteral("Movie,Series"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("SortBy")),
             QStringLiteral("DatePlayed,SortName"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("SortOrder")),
             QStringLiteral("Descending,Ascending"));
    QCOMPARE(request.query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("20"));

    // No favorites: no row.
    alice.favorites = {};
    library.getHomeSections();
    QTRY_COMPARE(loaded.count(), 2);
    QVERIFY(loaded.at(1).at(1).toList().isEmpty());

    // Turned off: no row and no read.
    alice.favorites = {movie(QStringLiteral("movie-fav"), QStringLiteral("Loved"))};
    m_config->setHomeFavoritesRowEnabled(false);
    m_servers->requests.clear();
    library.getHomeSections();
    QCOMPARE(loaded.count(), 3);
    QVERIFY(loaded.at(2).at(1).toList().isEmpty());
    QVERIFY(m_servers->requests.isEmpty());
}

void WorkspaceRoutingTest::jellyfinHomeAddsRecommendationRowsPerMovieLibrary()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.favorites = {movie(QStringLiteral("movie-fav"), QStringLiteral("Loved"))};
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-movies"), QJsonArray{
        recommendation(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                       {movie(QStringLiteral("m1"), QStringLiteral("Thief")),
                        playable(QStringLiteral("series-1"), QStringLiteral("Series"), 0)}),
        recommendation(QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"), {}),
    });
    // The second library recommends from the same title: one row.
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-films"), QJsonArray{
        recommendation(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                       {movie(QStringLiteral("m1"), QStringLiteral("Thief")),
                        movie(QStringLiteral("m2"), QStringLiteral("Collateral"))}),
        recommendation(QStringLiteral("HasLikedDirector"), QStringLiteral("Michael Mann"),
                       {movie(QStringLiteral("m3"), QStringLiteral("Ali"))}),
    });
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-hidden"), QJsonArray{
        recommendation(QStringLiteral("SimilarToLikedItem"), QStringLiteral("Alien"),
                       {movie(QStringLiteral("m4"), QStringLiteral("Aliens"))})});

    LibraryService library(m_auth.get());
    QVERIFY(library.supportsHomeRecommendations());
    QSignalSpy loaded(&library, &LibraryService::canonicalHomeSectionsLoaded);
    m_servers->requests.clear();
    // Home passes the movie libraries it shows; a hidden one is not among them.
    library.getHomeSections({QStringLiteral("lib-movies"), QStringLiteral("lib-films")});
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QStringLiteral("alice"));
    QVariantList rows = loaded.first().at(1).toList();
    // Favorites first, recommendations after, movies only, each once.
    QCOMPARE(rowTitles(rows), (QStringList{QStringLiteral("Favorites"),
                                           QStringLiteral("Because you watched Heat"),
                                           QStringLiteral("Directed by Michael Mann")}));
    QCOMPARE(orderedRefs(rows.at(1).toMap().value(QStringLiteral("items")).toList()),
             (QStringList{QStringLiteral("alice/m1"), QStringLiteral("alice/m2")}));
    QCOMPARE(orderedRefs(rows.at(2).toMap().value(QStringLiteral("items")).toList()),
             QStringList{QStringLiteral("alice/m3")});

    QStringList recommendationLibraries;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path != QStringLiteral("/Movies/Recommendations")) {
            continue;
        }
        recommendationLibraries.append(request.parentId);
        QCOMPARE(request.query.queryItemValue(QStringLiteral("UserId")), QStringLiteral("u-alice"));
        QCOMPARE(request.query.queryItemValue(QStringLiteral("CategoryLimit")), QStringLiteral("4"));
        QCOMPARE(request.query.queryItemValue(QStringLiteral("ItemLimit")), QStringLiteral("15"));
    }
    QCOMPARE(recommendationLibraries,
             (QStringList{QStringLiteral("lib-movies"), QStringLiteral("lib-films")}));

    // Without movie libraries there is nothing to read.
    m_servers->requests.clear();
    library.getHomeSections();
    QTRY_COMPARE(loaded.count(), 2);
    QCOMPARE(rowTitles(loaded.at(1).at(1).toList()), QStringList{QStringLiteral("Favorites")});
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        QVERIFY(request.path != QStringLiteral("/Movies/Recommendations"));
    }

    // Turned off: no recommendation reads; the Favorites row stays.
    m_config->setHomeRecommendationsEnabled(false);
    m_servers->requests.clear();
    library.getHomeSections({QStringLiteral("lib-movies")});
    QTRY_COMPARE(loaded.count(), 3);
    QCOMPARE(rowTitles(loaded.at(2).at(1).toList()), QStringList{QStringLiteral("Favorites")});
    QCOMPARE(m_servers->requests.size(), 1);
    QCOMPARE(m_servers->requests.constFirst().path, QStringLiteral("/Items"));

    // Both off: no reads at all.
    m_config->setHomeFavoritesRowEnabled(false);
    m_servers->requests.clear();
    library.getHomeSections({QStringLiteral("lib-movies")});
    QCOMPARE(loaded.count(), 4);
    QVERIFY(loaded.at(3).at(1).toList().isEmpty());
    QVERIFY(m_servers->requests.isEmpty());
}

void WorkspaceRoutingTest::jellyfinHomeRecommendationReadsFailAlone()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.favorites = {movie(QStringLiteral("movie-fav"), QStringLiteral("Loved"))};
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-movies"), QJsonArray{
        recommendation(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                       {movie(QStringLiteral("m1"), QStringLiteral("Thief"))})});
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-films"), QJsonArray{
        recommendation(QStringLiteral("HasLikedActor"), QStringLiteral("Al Pacino"),
                       {movie(QStringLiteral("m2"), QStringLiteral("Serpico"))})});
    alice.recommendationsStatusByLibrary.insert(QStringLiteral("lib-films"), 404);

    LibraryService library(m_auth.get());
    QSignalSpy loaded(&library, &LibraryService::canonicalHomeSectionsLoaded);
    QSignalSpy failed(&library, &LibraryService::canonicalHomeSectionsFailed);
    const QStringList libraries{QStringLiteral("lib-movies"), QStringLiteral("lib-films")};

    // One library fails: the other's rows and the Favorites row stay.
    library.getHomeSections(libraries);
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(rowTitles(loaded.first().at(1).toList()),
             (QStringList{QStringLiteral("Favorites"), QStringLiteral("Because you watched Heat")}));

    // Favorites fail: the recommendations still show.
    alice.favoritesStatus = 404;
    library.getHomeSections(libraries);
    QTRY_COMPARE(loaded.count(), 2);
    QCOMPARE(rowTitles(loaded.at(1).at(1).toList()),
             QStringList{QStringLiteral("Because you watched Heat")});
    QCOMPARE(failed.count(), 0);

    // Every read fails: the read fails, so Home keeps the rows it shows.
    alice.recommendationsStatusByLibrary.insert(QStringLiteral("lib-movies"), 404);
    library.getHomeSections(libraries);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("alice"));
    QCOMPARE(loaded.count(), 2);
}

void WorkspaceRoutingTest::jellyfinHomeDropsStaleRecommendationReplies()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.recommendationsByLibrary.insert(QStringLiteral("lib-movies"), QJsonArray{
        recommendation(QStringLiteral("SimilarToRecentlyPlayed"), QStringLiteral("Heat"),
                       {movie(QStringLiteral("m1"), QStringLiteral("Thief"))})});
    Account &bob = m_servers->accountsByToken[QStringLiteral("token-bob")];
    bob.recommendationsByLibrary.insert(QStringLiteral("lib-movies"), QJsonArray{
        recommendation(QStringLiteral("SimilarToLikedItem"), QStringLiteral("Alien"),
                       {movie(QStringLiteral("m9"), QStringLiteral("Aliens"))})});
    m_config->setHomeFavoritesRowEnabled(false);

    LibraryService library(m_auth.get());
    QSignalSpy loaded(&library, &LibraryService::canonicalHomeSectionsLoaded);
    QSignalSpy failed(&library, &LibraryService::canonicalHomeSectionsFailed);
    const QStringList libraries{QStringLiteral("lib-movies")};

    // A newer read supersedes one in flight.
    m_servers->holdRecommendations = true;
    library.getHomeSections(libraries);
    QTRY_COMPARE(m_servers->heldRecommendations.size(), 1);
    library.getHomeSections(libraries);
    QTRY_COMPARE(m_servers->heldRecommendations.size(), 2);
    m_servers->heldRecommendations.at(0)->deliver();
    QTest::qWait(20);
    QCOMPARE(loaded.count(), 0);
    m_servers->heldRecommendations.at(1)->deliver();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(rowTitles(loaded.first().at(1).toList()),
             QStringList{QStringLiteral("Because you watched Heat")});

    // A reply for the account Home left is dropped after the switch.
    m_servers->heldRecommendations.clear();
    library.getHomeSections(libraries);
    QTRY_COMPARE(m_servers->heldRecommendations.size(), 1);
    m_auth->activateConnection(QStringLiteral("bob"));
    QTRY_COMPARE(m_auth->activeConnectionId(), QStringLiteral("bob"));
    QTRY_VERIFY(m_auth->isAuthenticated());
    library.getHomeSections(libraries);
    QTRY_COMPARE(m_servers->heldRecommendations.size(), 2);
    // The transport may already have dropped the old account's request.
    if (m_servers->heldRecommendations.at(0)) {
        m_servers->heldRecommendations.at(0)->deliver();
    }
    m_servers->heldRecommendations.at(1)->deliver();
    QTRY_COMPARE(loaded.count(), 2);
    QTest::qWait(20);
    QCOMPARE(loaded.count(), 2);
    QCOMPARE(loaded.at(1).at(0).toString(), QStringLiteral("bob"));
    QCOMPARE(rowTitles(loaded.at(1).at(1).toList()),
             QStringList{QStringLiteral("More like Alien")});
    QCOMPARE(failed.count(), 0);
}

void WorkspaceRoutingTest::mergedNextUpAddsEachJellyfinMembersResumeList()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    alice.resume = {playable(QStringLiteral("movie-a"), QStringLiteral("Movie"), 1000000000,
                             QStringLiteral("2026-10-29T18:00:00Z"))};
    alice.nextUp = {playable(QStringLiteral("episode-a"), QStringLiteral("Episode"), 0)};
    Account &carol = m_servers->accountsByToken[QStringLiteral("token-carol")];
    carol.resume = {playable(QStringLiteral("episode-c"), QStringLiteral("Episode"), 2000000000,
                             QStringLiteral("2026-10-31T18:00:00Z"))};
    carol.nextUp = {playable(QStringLiteral("episode-c2"), QStringLiteral("Episode"), 0)};

    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    m_config->setNextUpRewatching(true);

    QSignalSpy loaded(m_merged.get(), &MergedCatalogService::nextUpLoaded);
    m_servers->requests.clear();
    m_merged->getNextUp();
    QTRY_COMPARE(loaded.count(), 1);
    // Each member's in-progress items, most recently played first across
    // members; each stays owned by its connection.
    const QStringList refs = orderedRefs(loaded.first().at(0).toList());
    QCOMPARE(refs.mid(0, 2),
             (QStringList{QStringLiteral("carol/episode-c"), QStringLiteral("alice/movie-a")}));
    QVERIFY(refs.contains(QStringLiteral("alice/episode-a")));
    QVERIFY(refs.contains(QStringLiteral("carol/episode-c2")));
    QCOMPARE(refs.size(), 4);

    int resumeReads = 0;
    for (const RecordedRequest &request : std::as_const(m_servers->requests)) {
        if (request.path == QStringLiteral("/UserItems/Resume")) {
            ++resumeReads;
        } else if (request.path == QStringLiteral("/Shows/NextUp")) {
            QCOMPARE(request.query.queryItemValue(QStringLiteral("EnableResumable")),
                     QStringLiteral("true"));
            QCOMPARE(request.query.queryItemValue(QStringLiteral("EnableRewatching")),
                     QStringLiteral("true"));
        }
    }
    QCOMPARE(resumeReads, 3);

    // A member whose resume list fails keeps its Next Up and stays ready.
    carol.resumeStatus = 404;
    m_merged->getNextUp();
    QTRY_COMPARE(loaded.count(), 2);
    const QStringList degraded = orderedRefs(loaded.at(1).at(0).toList());
    QVERIFY(!degraded.contains(QStringLiteral("carol/episode-c")));
    QVERIFY(degraded.contains(QStringLiteral("carol/episode-c2")));
    QVERIFY(degraded.contains(QStringLiteral("alice/movie-a")));
    QCOMPARE(sourceState(QStringLiteral("carol")), QStringLiteral("ready"));
}

void WorkspaceRoutingTest::olderNextUpRefreshNeverPublishesAfterNewerOne()
{
    Account &alice = m_servers->accountsByToken[QStringLiteral("token-alice")];
    m_servers->holdNextUp = true;

    // Single space: the first refresh answers last and is dropped.
    LibraryService library(m_auth.get());
    QSignalSpy loaded(&library, &LibraryService::canonicalNextUpLoaded);
    alice.nextUp = {playable(QStringLiteral("episode-old"), QStringLiteral("Episode"), 0)};
    library.getNextUp();
    QTRY_COMPARE(m_servers->heldNextUps.size(), 1);
    alice.nextUp = {playable(QStringLiteral("episode-new"), QStringLiteral("Episode"), 0)};
    library.getNextUp();
    QTRY_COMPARE(m_servers->heldNextUps.size(), 2);
    m_servers->heldNextUps.at(1)->deliver();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(orderedRefs(loaded.first().at(1).toList()),
             QStringList{QStringLiteral("alice/episode-new")});
    m_servers->heldNextUps.at(0)->deliver();
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 1);

    // Merged space: the same for the fan-out.
    m_servers->holdNextUp = false;
    QVERIFY(!createMergedWorkspace().isEmpty());
    waitForMembersReady();
    m_servers->holdNextUp = true;
    m_servers->heldNextUps.clear();
    QSignalSpy merged(m_merged.get(), &MergedCatalogService::nextUpLoaded);
    alice.nextUp = {playable(QStringLiteral("episode-old"), QStringLiteral("Episode"), 0)};
    m_merged->getNextUp();
    // One Next Up read per member (alice, bob and carol).
    constexpr qsizetype firstFanOut = 3;
    QTRY_COMPARE(m_servers->heldNextUps.size(), firstFanOut);
    alice.nextUp = {playable(QStringLiteral("episode-new"), QStringLiteral("Episode"), 0)};
    m_merged->getNextUp();
    QTRY_COMPARE(m_servers->heldNextUps.size(), 2 * firstFanOut);
    for (qsizetype i = firstFanOut; i < m_servers->heldNextUps.size(); ++i) {
        m_servers->heldNextUps.at(i)->deliver();
    }
    QTRY_COMPARE(merged.count(), 1);
    QStringList refs = orderedRefs(merged.first().at(0).toList());
    QVERIFY(refs.contains(QStringLiteral("alice/episode-new")));
    QVERIFY(!refs.contains(QStringLiteral("alice/episode-old")));
    for (qsizetype i = 0; i < firstFanOut; ++i) {
        m_servers->heldNextUps.at(i)->deliver();
    }
    QTest::qWait(50);
    QCOMPARE(merged.count(), 1);
}

QTEST_GUILESS_MAIN(WorkspaceRoutingTest)
#include "WorkspaceRoutingTest.moc"
