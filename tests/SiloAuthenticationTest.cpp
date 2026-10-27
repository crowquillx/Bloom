#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <algorithm>
#include <cstring>

#include "network/AuthenticationService.h"
#include "network/DeviceSignInSession.h"
#include "network/HttpTransport.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderAdapter.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/silo/SiloAuthenticator.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "providers/silo/SiloRequestFactory.h"

namespace {

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request,
              NetworkError error,
              int statusCode,
              QByteArray payload,
              QObject *parent)
        : QNetworkReply(parent)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (error != NoError) {
            setError(error, QStringLiteral("test network error"));
        }
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() { finish(); });
    }

    void abort() override
    {
        if (!isFinished()) {
            setError(OperationCanceledError, QStringLiteral("canceled"));
        }
    }

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_payload.size() - m_offset;
        const qint64 count = qMin(maxSize, remaining);
        if (count <= 0) {
            return -1;
        }
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    void finish()
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

    QByteArray m_payload;
    qint64 m_offset = 0;
};

struct QueuedResponse {
    int statusCode;
    QByteArray payload;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

struct RecordedRequest {
    QNetworkAccessManager::Operation operation;
    QNetworkRequest request;
    QByteArray body;
    qint64 elapsedMs = 0;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    QList<QueuedResponse> responses;
    QList<RecordedRequest> requests;
    QElapsedTimer clock;

    FakeNetworkAccessManager() { clock.start(); }

    qsizetype countPath(const QString &path) const
    {
        return std::count_if(requests.cbegin(), requests.cend(),
                             [&path](const RecordedRequest &recorded) {
            return recorded.request.url().path() == path;
        });
    }

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        const QByteArray body = outgoingData ? outgoingData->readAll() : QByteArray{};
        requests.append({operation, request, body, clock.elapsed()});
        QueuedResponse response { 500,
            QByteArrayLiteral(
                R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/test_queue_empty","title":"test_queue_empty","status":400,"detail":"No response queued","instance":"urn:silo:request:test"})"),
            QNetworkReply::UnknownServerError };
        if (!responses.isEmpty()) {
            response = responses.takeFirst();
        }
        return new FakeReply(request,
                             response.error,
                             response.statusCode,
                             response.payload,
                             this);
    }
};

QueuedResponse response(int statusCode, const QByteArray &payload)
{
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    if (statusCode == 401) {
        error = QNetworkReply::AuthenticationRequiredError;
    } else if (statusCode == 404) {
        error = QNetworkReply::ContentNotFoundError;
    } else if (statusCode >= 400) {
        error = QNetworkReply::UnknownServerError;
    }
    return {statusCode, payload, error};
}

QByteArray successfulLogin()
{
    return QByteArrayLiteral(
        R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})");
}

QByteArray singleUnlockedProfile()
{
    return QByteArrayLiteral(
        R"({"items":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true}]})");
}

QByteArray siloSystemInfo()
{
    return QByteArrayLiteral(R"({"api_major":2,"links":{}})");
}

QByteArray deviceCapability(const char *state = "available")
{
    return QByteArray(R"({"state":")") + state
        + QByteArray(R"(","protocol_versions":[2],"cancel":true,"opened_signal":true,"remote_playback_handoff":true,"jellyfin_quick_connect":true,"revision":"r1"})");
}

QByteArray authProviders(bool passwordLogin)
{
    return QByteArray(R"({"password_login":)") + (passwordLogin ? "true" : "false")
        + QByteArray(R"(,"items":[{"id":"sso","display_name":"Company SSO","mode":"oauth","default":true}]})");
}

QByteArray deviceStart(int expiresIn = 900)
{
    return QByteArray(
        R"({"device_code":"device-secret","user_code":"4821-7730","match_code":"warm pony","verification_uri":"https://silo.example.test/activate","verification_uri_complete":"https://silo.example.test/activate?code=48217730","expires_at":"2026-01-02T03:14:05.678Z","expires_in":)")
        + QByteArray::number(expiresIn)
        + QByteArray(R"(,"interval":1,"device_name":"tv","device_platform":"linux","client_purpose":"device_login","temporary":false})");
}

QByteArray devicePoll(const char *status, bool opened = false)
{
    return QByteArray(R"({"status":")") + status + QByteArray(R"(","poll_after":1,"opened":)")
        + (opened ? "true" : "false")
        + QByteArray(R"(,"profile_id":"","profile_token":"","temporary":false})");
}

QByteArray deviceApproved()
{
    return QByteArrayLiteral(
        R"({"status":"approved","poll_after":1,"opened":true,"profile_id":"","profile_token":"","temporary":false,"tokens":{"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}}})");
}

constexpr int kTestSecondMs = 20;
const QString kSiloAddress = QStringLiteral("https://silo.example.test");
const QString kDevicePoll = QStringLiteral("/api/v2/auth/device/poll");

// Discovery (system info, device capability, providers), then a started code.
void beginDeviceSignIn(AuthenticationService &service,
                       FakeNetworkAccessManager &manager,
                       int expiresIn = 900,
                       int secondMs = kTestSecondMs)
{
    service.deviceSignInSession()->setSecondDurationForTesting(secondMs);
    const QVariantMap options = service.signInOptions();
    if (options.value(QStringLiteral("server")).toString() != kSiloAddress
        || options.value(QStringLiteral("state")).toString() != QStringLiteral("ready")) {
        manager.responses.append(response(200, siloSystemInfo()));
        manager.responses.append(response(200, deviceCapability()));
        manager.responses.append(response(200, authProviders(true)));
        service.checkSignInOptions(kSiloAddress);
    }
    QTRY_COMPARE_WITH_TIMEOUT(service.signInOptions().value(QStringLiteral("state")).toString(),
                              QStringLiteral("ready"), 1000);
    QVERIFY(service.signInOptions().value(QStringLiteral("deviceSignIn")).toBool());

    manager.responses.append(response(201, deviceStart(expiresIn)));
    service.startDeviceSignIn(kSiloAddress);
    QTRY_COMPARE_WITH_TIMEOUT(service.deviceSignIn().value(QStringLiteral("state")).toString(),
                              QStringLiteral("waiting"), 1000);
}

QString deviceState(const AuthenticationService &service)
{
    return service.deviceSignIn().value(QStringLiteral("state")).toString();
}

} // namespace

class SiloAuthenticationTest : public QObject
{
    Q_OBJECT

private slots:
    void adapterExposesNativeAuthenticationBoundaries();
    void problemEnvelopesIgnoreExtensionsAndUnknownTypes();
    void healthDetectionAcceptsOmittedIdentityWithoutFabricatingOne();
    void requestFactoryBuildsNativeHeadersAndRedactsSecrets();
    void authenticationEndpointsNeverReceiveBearerAuthentication();
    void authenticatorBuildsAndParsesLoginAndRefreshContracts();
    void explicitSiloSelectionDoesNotBecomeAuthProvider();
    void completionPublishesIdentityBeforeAuthenticatedState();
    void authenticatorRejectsMalformedAndIncompleteResponses();
    void adapterMapsProfilesAndRevokedAuthenticationSessions();
    void adapterExposesProfileAndSessionRoutes();
    void authenticatorDistinguishesIncorrectAndValidProfilePins();
    void unauthorizedRequestRefreshesAndRetriesExactlyOnce();
    void unreachableRefreshKeepsLiveSession();
    void concurrentUnauthorizedRequestsShareOneRefresh();
    void failedSharedRefreshExpiresSessionOnce();
    void profileTokenIsBoundToSelectionAndClearedOnSwitch();
    void serviceLoadsRevokedSessionsAndCallsRevokeEndpoint();
    void failedSessionLoadPreservesKnownSessions();
    void profileVerificationNeverReceivesStaleProfileHeaders();
    void nullTransportFailsAuthenticationWithoutCrashing();
    void emptyProfilesAfterSwitchLeaveVisibleErrorAndAuthenticatedStep();
    void restoreRejectsValidationForAnotherAccount();
    void refreshWithoutRotationRetainsPreviousToken();
    void schemelessAddressFallsBackToHttpWhenHttpsIsUnreachable();
    void schemelessAddressStaysOnHttpsOnceTheServerAnswers();
    void explicitHttpsIsNeverDowngraded();
    void authenticatorBuildsAndParsesDeviceAuthorizationContracts();
    void signInOptionsOfferCodesOnlyOnSupportingSiloServers();
    void deviceSignInStartsAndShowsCode();
    void deviceSignInKeepsPollingWhilePendingThenSignsInLikePassword();
    void deviceSignInSlowsDownWhenRateLimited();
    void deviceSignInExpiresWhenServerOrDeadlineSaysSo();
    void deviceSignInStopsWhenDenied();
    void cancelingDeviceSignInWithdrawsCodeAndStopsPolling();
};

void SiloAuthenticationTest::adapterExposesNativeAuthenticationBoundaries()
{
    SiloProviderAdapter adapter;
    QCOMPARE(adapter.providerKind(), ProviderKind::Silo);
    QCOMPARE(adapter.protocolMode(), ProtocolMode::Native);
    QVERIFY(adapter.authenticator());
    QVERIFY(adapter.requestFactory());
    QVERIFY(adapter.supportsCapability(ProviderCapability::RefreshAuthentication));
    QVERIFY(adapter.supportsCapability(ProviderCapability::Profiles));
    QVERIFY(adapter.supportsCapability(ProviderCapability::ProfilePin));
    QVERIFY(adapter.supportsCapability(ProviderCapability::AuthSessions));
}

void SiloAuthenticationTest::healthDetectionAcceptsOmittedIdentityWithoutFabricatingOne()
{
    SiloProviderAdapter adapter;

    const auto detected = adapter.mapDetectionResult(QJsonObject {
        { QStringLiteral("api_major"), 2 }, { QStringLiteral("links"), QJsonObject {} },
        { QStringLiteral("server_version"), QStringLiteral("future-version") },
        { QStringLiteral("contract_digest"), QStringLiteral("unknown-digest") },
        { QStringLiteral("unknown_field"), true } });
    QVERIFY(detected.has_value());
    QCOMPARE(detected->providerKind, ProviderKind::Silo);
    QCOMPARE(detected->protocolMode, ProtocolMode::Native);
    QVERIFY(detected->serverId.isEmpty());
    QVERIFY(detected->serverName.isEmpty());
    QVERIFY(!adapter
            .mapDetectionResult(QJsonObject { { QStringLiteral("status"), QStringLiteral("ok") } })
            .has_value());
    QVERIFY(!adapter
            .mapDetectionResult(QJsonObject {
                { QStringLiteral("api_major"), 1 }, { QStringLiteral("links"), QJsonObject {} } })
            .has_value());
}

void SiloAuthenticationTest::requestFactoryBuildsNativeHeadersAndRedactsSecrets()
{
    SiloRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = QStringLiteral("https://silo.example.test///");
    context.accessToken = QStringLiteral("access-secret");
    context.profileId = QStringLiteral("profile-1");
    context.profileToken = QStringLiteral("profile-secret");
    context.clientName = QStringLiteral("Bloom Test");
    context.clientVersion = QStringLiteral("1.2.3");
    context.deviceId = QStringLiteral("device-1");
    context.deviceName = QStringLiteral("Living Room");
    context.devicePlatform = QStringLiteral("linux");

    const QNetworkRequest request
        = factory.createRequest(context, QStringLiteral("api/v2/profiles"));
    QCOMPARE(request.url(), QUrl(QStringLiteral("https://silo.example.test/api/v2/profiles")));
    QCOMPARE(request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-secret"));
    QCOMPARE(request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(request.rawHeader("X-Profile-Token"), QByteArrayLiteral("profile-secret"));
    QCOMPARE(request.rawHeader("X-Silo-Client"), QByteArrayLiteral("Bloom Test"));
    QCOMPARE(request.rawHeader("X-Silo-Client-Version"), QByteArrayLiteral("1.2.3"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Id"), QByteArrayLiteral("device-1"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Name"), QByteArrayLiteral("Living Room"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Platform"), QByteArrayLiteral("linux"));

    const QString redacted = factory.redactedUrl(QUrl(
        QStringLiteral("https://user:password@silo.example.test/api/v2/"
                       "items?access_token=access-secret&profile_token=profile-secret&q=title")));
    QVERIFY(!redacted.contains(QStringLiteral("access-secret")));
    QVERIFY(!redacted.contains(QStringLiteral("profile-secret")));
    QVERIFY(!redacted.contains(QStringLiteral("user")));
    QVERIFY(!redacted.contains(QStringLiteral("password")));
    QVERIFY(redacted.contains(QStringLiteral("q=title")));
}

void SiloAuthenticationTest::authenticationEndpointsNeverReceiveBearerAuthentication()
{
    SiloRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = QStringLiteral("https://silo.example.test");
    context.accessToken = QStringLiteral("stale-access-token");
    context.profileId = QStringLiteral("profile-1");
    context.profileToken = QStringLiteral("stale-profile-token");

    const QNetworkRequest login
        = factory.createRequest(context, QStringLiteral("/api/v2/auth/login"));
    const QNetworkRequest refresh
        = factory.createRequest(context, QStringLiteral("/api/v2/auth/refresh"));
    QVERIFY(!login.hasRawHeader("Authorization"));
    QVERIFY(!refresh.hasRawHeader("Authorization"));
    QVERIFY(!login.hasRawHeader("X-Profile-Id"));
    const QNetworkRequest health = factory.createRequest(context, QStringLiteral("/health"));
    QVERIFY(!health.hasRawHeader("Authorization"));
    QVERIFY(!health.hasRawHeader("X-Profile-Id"));
    QVERIFY(!health.hasRawHeader("X-Profile-Token"));
    QVERIFY(!refresh.hasRawHeader("X-Profile-Token"));
}

void SiloAuthenticationTest::profileVerificationNeverReceivesStaleProfileHeaders()
{
    SiloRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = QStringLiteral("https://silo.example.test");
    context.accessToken = QStringLiteral("access-secret");
    context.profileId = QStringLiteral("new-profile");
    context.profileToken = QStringLiteral("stale-profile-token");

    const QNetworkRequest request
        = factory.createRequest(context, QStringLiteral("/api/v2/profiles/new-profile/verify-pin"));
    QCOMPARE(request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-secret"));
    QVERIFY(!request.hasRawHeader("X-Profile-Id"));
    QVERIFY(!request.hasRawHeader("X-Profile-Token"));
}

void SiloAuthenticationTest::authenticatorBuildsAndParsesLoginAndRefreshContracts()
{
    SiloAuthenticator authenticator;
    const ProviderAuthenticationRequest login = authenticator.createLoginRequest(
        QStringLiteral("Alice"), QStringLiteral("password"));
    QCOMPARE(login.endpoint, QStringLiteral("/api/v2/auth/login"));
    const QJsonObject loginBody = QJsonDocument::fromJson(login.body).object();
    QCOMPARE(loginBody.value(QStringLiteral("username")).toString(), QStringLiteral("Alice"));
    QCOMPARE(loginBody.value(QStringLiteral("password")).toString(), QStringLiteral("password"));

    const ProviderAuthenticationResult loginResult = authenticator.parseLoginResponse(QByteArrayLiteral(
        R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":"42","username":"Alice"}})"));
    QVERIFY(loginResult.isValid());
    QCOMPARE(loginResult.accessToken, QStringLiteral("access-1"));
    QCOMPARE(loginResult.refreshToken, QStringLiteral("refresh-1"));
    QCOMPARE(loginResult.accountId, QStringLiteral("42"));
    QCOMPARE(loginResult.username, QStringLiteral("Alice"));
    QCOMPARE(loginResult.expiresInSeconds, 900);

    const auto refresh = authenticator.createRefreshRequest(QStringLiteral("refresh-1"));
    QVERIFY(refresh.has_value());
    QCOMPARE(refresh->endpoint, QStringLiteral("/api/v2/auth/refresh"));
    QCOMPARE(QJsonDocument::fromJson(refresh->body).object()
                 .value(QStringLiteral("refresh_token")).toString(),
             QStringLiteral("refresh-1"));

    const ProviderAuthenticationResult refreshResult = authenticator.parseRefreshResponse(
        QByteArrayLiteral(R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":1800})"));
    QCOMPARE(refreshResult.accessToken, QStringLiteral("access-2"));
    QCOMPARE(refreshResult.refreshToken, QStringLiteral("refresh-2"));
    QCOMPARE(refreshResult.expiresInSeconds, 1800);
}

void SiloAuthenticationTest::explicitSiloSelectionDoesNotBecomeAuthProvider()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));

    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);
    QVERIFY(!manager.requests.isEmpty());
    const QJsonObject body =
        QJsonDocument::fromJson(manager.requests.constFirst().body).object();
    QCOMPARE(body.value(QStringLiteral("username")).toString(),
             QStringLiteral("Alice"));
    QCOMPARE(body.value(QStringLiteral("password")).toString(),
             QStringLiteral("password"));
    QVERIFY(!body.contains(QStringLiteral("provider")));
}

void SiloAuthenticationTest::completionPublishesIdentityBeforeAuthenticatedState()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    bool identityPublished = false;
    bool authenticatedBeforeIdentity = false;
    connect(&service, &AuthenticationService::userIdChanged,
            this, [&identityPublished]() { identityPublished = true; });
    connect(&service, &AuthenticationService::authenticatedChanged,
            this, [&]() {
        if (service.isAuthenticated() && !identityPublished) {
            authenticatedBeforeIdentity = true;
        }
    });

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));

    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);
    QVERIFY(identityPublished);
    QVERIFY(!authenticatedBeforeIdentity);
}

void SiloAuthenticationTest::authenticatorRejectsMalformedAndIncompleteResponses()
{
    SiloAuthenticator authenticator;
    QVERIFY(!authenticator.parseLoginResponse(QByteArrayLiteral("not-json")).isValid());
    QVERIFY(!authenticator
            .parseLoginResponse(QByteArrayLiteral(
                R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":0,"user":{"id":"42","username":"Alice"}})"))
            .isValid());
    QVERIFY(!authenticator
            .parseLoginResponse(QByteArrayLiteral(
                R"({"access_token":"access-1","expires_in":900,"user":{"id":"42","username":"Alice"}})"))
            .isValid());
    QVERIFY(!authenticator.parseProfileLoginResponse(
                 QByteArrayLiteral(R"({"valid":true})"))
                 .isValid());

    const ProviderAuthenticationResult retainedRefresh = authenticator.parseRefreshResponse(
        QByteArrayLiteral(R"({"access_token":"access-2","expires_in":1800})"));
    QCOMPARE(retainedRefresh.accessToken, QStringLiteral("access-2"));
    QVERIFY(retainedRefresh.refreshToken.isEmpty());
    QVERIFY(retainedRefresh.isValidRefresh());
    QVERIFY(!authenticator.createRefreshRequest(QString()).has_value());

    const SiloAuthenticationError invalidCredentials = authenticator.parseErrorResponse(QByteArrayLiteral(
        R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_credentials","title":"invalid_credentials","status":401,"detail":"Invalid username or password","instance":"urn:silo:request:test"})"));
    QVERIFY(invalidCredentials.isValid());
    QCOMPARE(invalidCredentials.failure,
             SiloAuthenticationFailure::InvalidCredentials);
    const SiloAuthenticationError revoked = authenticator.parseErrorResponse(QByteArrayLiteral(
        R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/session_revoked","title":"session_revoked","status":401,"detail":"Session was revoked","instance":"urn:silo:request:test"})"));
    QVERIFY(revoked.isValid());
    QVERIFY(revoked.isSessionRevoked());
}

void SiloAuthenticationTest::adapterMapsProfilesAndRevokedAuthenticationSessions()
{
    SiloProviderAdapter adapter;
    const auto profiles = adapter.mapProfiles(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("profile-1")},
            {QStringLiteral("name"), QStringLiteral("Alice")},
            {QStringLiteral("avatar_url"), QStringLiteral("https://images.example.test/alice")},
            {QStringLiteral("has_pin"), true},
            {QStringLiteral("is_child"), false},
            {QStringLiteral("is_primary"), true}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("profile-2")},
            {QStringLiteral("name"), QStringLiteral("Child")},
            {QStringLiteral("has_pin"), false},
            {QStringLiteral("is_child"), true},
            {QStringLiteral("is_primary"), false}
        }
    });
    QVERIFY(profiles.has_value());
    QCOMPARE(profiles->size(), 2);
    QCOMPARE(profiles->at(0).id, QStringLiteral("profile-1"));
    QCOMPARE(profiles->at(0).avatarUrl, QStringLiteral("https://images.example.test/alice"));
    QVERIFY(profiles->at(0).hasPin);
    QVERIFY(profiles->at(0).isPrimary);
    QVERIFY(profiles->at(1).isChild);

    const auto sessions = adapter.mapAuthSessions(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("session-current")},
            {QStringLiteral("device_name"), QStringLiteral("Living Room")},
            {QStringLiteral("ip_address"), QStringLiteral("192.0.2.1")},
            {QStringLiteral("created_at"), QStringLiteral("2026-08-01T10:00:00Z")},
            {QStringLiteral("expires_at"), QStringLiteral("2026-08-08T10:00:00Z")}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("session-revoked")},
            {QStringLiteral("device_name"), QStringLiteral("Bedroom")},
            {QStringLiteral("ip_address"), QStringLiteral("192.0.2.2")},
            {QStringLiteral("created_at"), QStringLiteral("2026-07-01T10:00:00Z")},
            {QStringLiteral("expires_at"), QStringLiteral("2026-07-08T10:00:00Z")},
            {QStringLiteral("revoked_at"), QStringLiteral("2026-07-02T10:00:00Z")}
        }
    });
    QVERIFY(sessions.has_value());
    QCOMPARE(sessions->size(), 2);
    QCOMPARE(sessions->at(0).id, QStringLiteral("session-current"));
    QCOMPARE(sessions->at(0).deviceName, QStringLiteral("Living Room"));
    QVERIFY(sessions->at(0).createdAt > 0);
    QCOMPARE(sessions->at(0).revokedAt, -1);
    QCOMPARE(sessions->at(1).id, QStringLiteral("session-revoked"));
    QVERIFY(sessions->at(1).revokedAt > sessions->at(1).createdAt);
}

void SiloAuthenticationTest::adapterExposesProfileAndSessionRoutes()
{
    SiloProviderAdapter adapter;
    ProviderRouteContext context;
    context.profileId = QStringLiteral("profile/with space");
    context.sessionId = QStringLiteral("session/with space");
    context.itemId = QStringLiteral("content/with space");
    const auto itemMarkers =
        adapter.endpointFor(ProviderRoute::MediaSegments, context);
    context.fileId = QStringLiteral("file/with space");
    const auto fileMarkers =
        adapter.endpointFor(ProviderRoute::MediaSegments, context);

    const auto profiles = adapter.endpointFor(ProviderRoute::Profiles);
    const auto pin = adapter.endpointFor(ProviderRoute::VerifyProfilePin, context);
    const auto sessions = adapter.endpointFor(ProviderRoute::AuthSessions);
    const auto revoke = adapter.endpointFor(ProviderRoute::RevokeAuthSession, context);
    const auto health = adapter.endpointFor(ProviderRoute::Health);
    const auto logout = adapter.endpointFor(ProviderRoute::CallerLogout);
    QVERIFY(profiles.has_value());
    QVERIFY(pin.has_value());
    QVERIFY(sessions.has_value());
    QVERIFY(revoke.has_value());
    QVERIFY(health.has_value());
    QVERIFY(logout.has_value());
    QVERIFY(itemMarkers.has_value());
    QVERIFY(fileMarkers.has_value());
    QCOMPARE(*profiles, QStringLiteral("/api/v2/profiles"));
    QCOMPARE(*pin, QStringLiteral("/api/v2/profiles/profile%2Fwith%20space/verify-pin"));
    QCOMPARE(*sessions, QStringLiteral("/api/v2/auth/sessions"));
    QCOMPARE(*revoke, QStringLiteral("/api/v2/auth/sessions/session%2Fwith%20space"));
    QCOMPARE(*itemMarkers, QStringLiteral("/api/v2/markers/items/content%2Fwith%20space"));
    QCOMPARE(*fileMarkers, QStringLiteral("/api/v2/markers/files/file%2Fwith%20space"));
    QCOMPARE(*health, QStringLiteral("/health"));
    QCOMPARE(*logout, QStringLiteral("/api/v2/auth/logout"));
}

void SiloAuthenticationTest::authenticatorDistinguishesIncorrectAndValidProfilePins()
{
    SiloAuthenticator authenticator;
    const auto request = authenticator.createProfileLoginRequest(
        QStringLiteral("profile/one"), QStringLiteral("1234"));
    QVERIFY(request.has_value());
    QCOMPARE(request->endpoint, QStringLiteral("/api/v2/profiles/profile%2Fone/verify-pin"));
    QCOMPARE(QJsonDocument::fromJson(request->body).object()
                 .value(QStringLiteral("pin")).toString(),
             QStringLiteral("1234"));

    const ProviderProfileAuthenticationResult incorrect =
        authenticator.parseProfileLoginResponse(QByteArrayLiteral(R"({"valid":false})"));
    QVERIFY(incorrect.responseParsed);
    QVERIFY(incorrect.isIncorrectPin());
    QVERIFY(!incorrect.isValid());
    QVERIFY(incorrect.profileToken.isEmpty());

    const ProviderProfileAuthenticationResult valid =
        authenticator.parseProfileLoginResponse(QByteArrayLiteral(
            R"({"valid":true,"profile_token":"profile-token-1"})"));
    QVERIFY(valid.responseParsed);
    QVERIFY(valid.valid);
    QVERIFY(valid.isValid());
    QCOMPARE(valid.profileToken, QStringLiteral("profile-token-1"));
}

void SiloAuthenticationTest::unauthorizedRequestRefreshesAndRetriesExactlyOnce()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    const qsizetype initialRequestCount = manager.requests.size();
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":1800})")));
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Still unauthorized","instance":"urn:silo:request:test"})")));

    int attempts = 0;
    int successes = 0;
    int failures = 0;
    QSignalSpy expiredSpy(&service, &AuthenticationService::sessionExpired);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/protected"),
        [&]() -> QNetworkReply *
        {
            ++attempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v2/protected")));
        },
        [&](QNetworkReply *) { ++successes; }, [&](const NetworkError &) { ++failures; }, options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(successes, 0);
    QCOMPARE(attempts, 2);
    QCOMPARE(expiredSpy.count(), 1);
    QCOMPARE(manager.requests.size(), initialRequestCount + 3);
    QCOMPARE(manager.requests.at(initialRequestCount).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(manager.requests.at(initialRequestCount + 1).request.url().path(),
        QStringLiteral("/api/v2/auth/refresh"));
    QVERIFY(!manager.requests.at(initialRequestCount + 1)
                 .request.hasRawHeader("Authorization"));
    QCOMPARE(QJsonDocument::fromJson(manager.requests.at(initialRequestCount + 1).body)
                 .object().value(QStringLiteral("refresh_token")).toString(),
             QStringLiteral("refresh-1"));
    QCOMPARE(manager.requests.at(initialRequestCount + 2).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
}

void SiloAuthenticationTest::unreachableRefreshKeepsLiveSession()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    // The refresh endpoint is down; the refresh token was never refused.
    manager.responses.append(response(503, QByteArray()));

    int failures = 0;
    NetworkError received;
    QSignalSpy expiredSpy(&service, &AuthenticationService::sessionExpired);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/protected"),
        [&]() -> QNetworkReply * {
            return manager.get(service.createRequest(QStringLiteral("/api/v2/protected")));
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(expiredSpy.count(), 0);
    QVERIFY(service.isAuthenticated());
    QCOMPARE(received.httpStatus, 0);
    QCOMPARE(manager.requests.constLast().request.url().path(),
             QStringLiteral("/api/v2/auth/refresh"));
}

void SiloAuthenticationTest::concurrentUnauthorizedRequestsShareOneRefresh()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    const qsizetype initialRequestCount = manager.requests.size();
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":1800})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));

    int firstAttempts = 0;
    int secondAttempts = 0;
    int successes = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    const auto success = [&](QNetworkReply *) { ++successes; };
    const auto failure = [&](const NetworkError &) { ++failures; };

    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/first"),
        [&]() -> QNetworkReply *
        {
            ++firstAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v2/first")));
        },
        success, failure, options);
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/second"),
        [&]() -> QNetworkReply *
        {
            ++secondAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v2/second")));
        },
        success, failure, options);

    QTRY_COMPARE_WITH_TIMEOUT(successes, 2, 1000);
    QCOMPARE(failures, 0);
    QCOMPARE(firstAttempts, 2);
    QCOMPARE(secondAttempts, 2);
    int refreshRequests = 0;
    for (qsizetype i = initialRequestCount; i < manager.requests.size(); ++i) {
        if (manager.requests.at(i).request.url().path() == QStringLiteral("/api/v2/auth/refresh"))
        {
            ++refreshRequests;
        }
    }
    QCOMPARE(refreshRequests, 1);
    QCOMPARE(manager.requests.constLast().request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
}

void SiloAuthenticationTest::failedSharedRefreshExpiresSessionOnce()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    const qsizetype initialRequestCount = manager.requests.size();
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Refresh expired","instance":"urn:silo:request:test"})")));

    int firstAttempts = 0;
    int secondAttempts = 0;
    int failures = 0;
    QSignalSpy expiredSpy(&service, &AuthenticationService::sessionExpired);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    const auto failure = [&](const NetworkError &) { ++failures; };

    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/first"),
        [&]() -> QNetworkReply *
        {
            ++firstAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v2/first")));
        },
        [](QNetworkReply *) {}, failure, options);
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/second"),
        [&]() -> QNetworkReply *
        {
            ++secondAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v2/second")));
        },
        [](QNetworkReply *) {}, failure, options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 2, 1000);
    QCOMPARE(firstAttempts, 1);
    QCOMPARE(secondAttempts, 1);
    QCOMPARE(expiredSpy.count(), 1);
    int refreshRequests = 0;
    for (qsizetype i = initialRequestCount; i < manager.requests.size(); ++i) {
        if (manager.requests.at(i).request.url().path() == QStringLiteral("/api/v2/auth/refresh"))
        {
            ++refreshRequests;
        }
    }
    QCOMPARE(refreshRequests, 1);
}

void SiloAuthenticationTest::profileTokenIsBoundToSelectionAndClearedOnSwitch()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200,
            QByteArrayLiteral(
                R"({"items":[{"id":"profile-1","name":"Alice","has_pin":true,"is_child":false,"is_primary":true}]})"))
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_COMPARE_WITH_TIMEOUT(service.authenticationStep(), QStringLiteral("pin"), 1000);

    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"valid":true,"profile_token":"profile-token-1"})")));
    service.verifyProfilePin(QStringLiteral("profile-1"), QStringLiteral("1234"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    const QNetworkRequest selected = service.createRequest(QStringLiteral("/api/v2/catalog"));
    QCOMPARE(selected.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(selected.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QCOMPARE(selected.rawHeader("X-Profile-Token"),
             QByteArrayLiteral("profile-token-1"));

    service.clearProfileState();
    const QNetworkRequest cleared = service.createRequest(QStringLiteral("/api/v2/catalog"));
    QCOMPARE(cleared.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QVERIFY(!cleared.hasRawHeader("X-Profile-Id"));
    QVERIFY(!cleared.hasRawHeader("X-Profile-Token"));
}

void SiloAuthenticationTest::serviceLoadsRevokedSessionsAndCallsRevokeEndpoint()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    const qsizetype initialRequestCount = manager.requests.size();
    manager.responses.append(response(200,
        QByteArrayLiteral(
            R"({"items":[{"id":"session-current","device_name":"Living Room","ip_address":"192.0.2.1","created_at":"2026-08-01T10:00:00Z","expires_at":"2026-08-08T10:00:00Z","is_current":true},{"id":"session-revoked","device_name":"Bedroom","ip_address":"192.0.2.2","created_at":"2026-07-01T10:00:00Z","expires_at":"2026-07-08T10:00:00Z","revoked_at":"2026-07-02T10:00:00Z","is_current":false}]})")));
    service.loadAuthSessions();
    QTRY_COMPARE_WITH_TIMEOUT(service.authSessions().size(), 2, 1000);
    QCOMPARE(service.authSessions().at(1).toMap()
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("session-revoked"));
    QVERIFY(service.authSessions().at(1).toMap()
                .value(QStringLiteral("revokedAt")).toLongLong() > 0);
    QCOMPARE(manager.requests.at(initialRequestCount).operation,
             QNetworkAccessManager::GetOperation);
    QCOMPARE(manager.requests.at(initialRequestCount).request.url().path(),
        QStringLiteral("/api/v2/auth/sessions"));
    QCOMPARE(manager.requests.at(initialRequestCount).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));

    manager.responses.append(response(204, QByteArray{}));
    manager.responses.append(response(200,
        QByteArrayLiteral(
            R"({"items":[{"id":"session-current","device_name":"Living Room","ip_address":"192.0.2.1","created_at":"2026-08-01T10:00:00Z","expires_at":"2026-08-08T10:00:00Z","is_current":true}]})")));
    service.revokeAuthSession(QStringLiteral("session-revoked"));
    QTRY_COMPARE_WITH_TIMEOUT(service.authSessions().size(), 1, 1000);
    QCOMPARE(manager.requests.at(initialRequestCount + 1).operation,
             QNetworkAccessManager::DeleteOperation);
    QCOMPARE(manager.requests.at(initialRequestCount + 1).request.url().path(),
        QStringLiteral("/api/v2/auth/sessions/session-revoked"));
    QCOMPARE(manager.requests.at(initialRequestCount + 1)
                 .request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
}

void SiloAuthenticationTest::failedSessionLoadPreservesKnownSessions()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    manager.responses.append(response(200,
        QByteArrayLiteral(
            R"({"items":[{"id":"session-current","device_name":"Living Room","is_current":true}]})")));
    service.loadAuthSessions();
    QTRY_COMPARE_WITH_TIMEOUT(service.authSessions().size(), 1, 1000);

    QSignalSpy errorSpy(&service, &AuthenticationService::loginError);
    manager.responses.append(response(400,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_request","title":"invalid_request","status":400,"detail":"Request rejected","instance":"urn:silo:request:test"})")));
    service.loadAuthSessions();
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.size(), 1, 1000);
    QCOMPARE(service.authSessions().size(), 1);
    QCOMPARE(service.authSessions().first().toMap()
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("session-current"));
}
void SiloAuthenticationTest::refreshWithoutRotationRetainsPreviousToken()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(
        200, QByteArrayLiteral(R"({"access_token":"access-2","expires_in":1800})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    int successes = 0;
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/protected"),
        [&]() { return manager.get(service.createRequest(QStringLiteral("/api/v2/protected"))); },
        [&](QNetworkReply *) { ++successes; }, [](const NetworkError &) {}, options);
    QTRY_COMPARE_WITH_TIMEOUT(successes, 1, 1000);

    manager.responses.append(response(401,
        QByteArrayLiteral(
            R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/invalid_token","title":"invalid_token","status":401,"detail":"Expired","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(
        200, QByteArrayLiteral(R"({"access_token":"access-3","expires_in":1800})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));
    transport.sendWithRetry(
        this, QStringLiteral("/api/v2/protected-again"), [&]()
        { return manager.get(service.createRequest(QStringLiteral("/api/v2/protected-again"))); },
        [&](QNetworkReply *) { ++successes; }, [](const NetworkError &) {}, options);
    QTRY_COMPARE_WITH_TIMEOUT(successes, 2, 1000);

    int refreshRequests = 0;
    for (const RecordedRequest &request : manager.requests) {
        if (request.request.url().path() == QStringLiteral("/api/v2/auth/refresh"))
        {
            ++refreshRequests;
            if (refreshRequests == 2) {
                QCOMPARE(QJsonDocument::fromJson(request.body).object()
                             .value(QStringLiteral("refresh_token")).toString(),
                         QStringLiteral("refresh-1"));
            }
        }
    }
    QCOMPARE(refreshRequests, 2);
}

void SiloAuthenticationTest::nullTransportFailsAuthenticationWithoutCrashing()
{
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, nullptr, &adapter);
    QSignalSpy errorSpy(&service, &AuthenticationService::loginError);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!service.isAuthenticated());
}

void SiloAuthenticationTest::emptyProfilesAfterSwitchLeaveVisibleErrorAndAuthenticatedStep()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    QSignalSpy errorSpy(&service, &AuthenticationService::loginError);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);

    manager.responses.append(response(200, QByteArrayLiteral(R"({"profiles": []})")));
    service.switchProfile();
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 1000);
    QCOMPARE(service.authenticationStep(), QStringLiteral("authenticated"));
}

void SiloAuthenticationTest::restoreRejectsValidationForAnotherAccount()
{
    FakeNetworkAccessManager manager;
    manager.responses
        = { response(200, QByteArrayLiteral(R"({"id":"43","username":"Bob","role":"user"})")) };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    service.restoreSession(QStringLiteral("https://silo.example.test"),
                           QStringLiteral("42"),
                           QStringLiteral("stale-access"),
                           QStringLiteral("Alice"));
    QTRY_COMPARE_WITH_TIMEOUT(service.getUserId(), QString(), 1000);
    QCOMPARE(manager.requests.size(), 1);
    QCOMPARE(
        manager.requests.constFirst().request.url().path(), QStringLiteral("/api/v2/account/me"));
}

void SiloAuthenticationTest::problemEnvelopesIgnoreExtensionsAndUnknownTypes()
{
    SiloAuthenticator authenticator;
    const auto problem
        = QJsonObject { { "type", "https://siloserver.org/docs/api/v2/problems/future_problem" },
              { "title", "Future problem" }, { "status", 429 },
              { "detail", "Wait before trying again" }, { "instance", "urn:silo:request:test" },
              { "future_extension", QJsonArray { 1, 2 } } };
    const auto parsed = authenticator.parseErrorResponse(QJsonDocument(problem).toJson());
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.failure, SiloAuthenticationFailure::Other);
    QCOMPARE(parsed.code, QStringLiteral("future_problem"));
    QCOMPARE(parsed.message, QStringLiteral("Wait before trying again"));
    QVERIFY(!authenticator
            .parseErrorResponse(
                QByteArrayLiteral(R"({"error":"invalid_token","message":"old envelope"})"))
            .isValid());
    auto malformed = problem;
    malformed["type"] = 42;
    QVERIFY(!authenticator.parseErrorResponse(QJsonDocument(malformed).toJson()).isValid());
}

void SiloAuthenticationTest::schemelessAddressFallsBackToHttpWhenHttpsIsUnreachable()
{
    FakeNetworkAccessManager manager;
    manager.responses = {
        {0, {}, QNetworkReply::ConnectionRefusedError},
        response(200, successfulLogin()),
        response(200, singleUnlockedProfile())
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("silo.example.test:8096/"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));

    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);
    QCOMPARE(service.getServerUrl(), QStringLiteral("http://silo.example.test:8096"));
    QVERIFY(manager.requests.size() >= 2);
    QCOMPARE(manager.requests.at(0).request.url().scheme(), QStringLiteral("https"));
    QCOMPARE(manager.requests.at(1).request.url().scheme(), QStringLiteral("http"));
}

void SiloAuthenticationTest::schemelessAddressStaysOnHttpsOnceTheServerAnswers()
{
    FakeNetworkAccessManager manager;
    manager.responses = { response(401, QByteArray()) };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    QSignalSpy errors(&service, &AuthenticationService::loginError);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("wrong"));

    QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 1000);
    QCOMPARE(manager.requests.size(), 1);
    QCOMPARE(service.getServerUrl(), QStringLiteral("https://silo.example.test"));
}

void SiloAuthenticationTest::explicitHttpsIsNeverDowngraded()
{
    FakeNetworkAccessManager manager;
    manager.responses = { {0, {}, QNetworkReply::ConnectionRefusedError} };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    QSignalSpy errors(&service, &AuthenticationService::loginError);

    service.setProviderSelection(QStringLiteral("silo"));
    service.authenticate(QStringLiteral("https://silo.example.test"),
                         QStringLiteral("Alice"),
                         QStringLiteral("password"));

    QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 1000);
    QCOMPARE(manager.requests.size(), 1);
    QCOMPARE(service.getServerUrl(), QStringLiteral("https://silo.example.test"));
}

void SiloAuthenticationTest::authenticatorBuildsAndParsesDeviceAuthorizationContracts()
{
    SiloAuthenticator authenticator;
    QCOMPARE(authenticator.deviceAuthorizationCapabilityEndpoint(),
             std::optional<QString>(QStringLiteral("/api/v2/auth/device/capability")));
    const auto capability = authenticator.parseDeviceAuthorizationCapability(deviceCapability());
    QVERIFY(capability.has_value());
    QVERIFY(capability->available);
    QVERIFY(capability->cancel);
    QVERIFY(!authenticator.parseDeviceAuthorizationCapability(deviceCapability("disabled"))
                 ->available);
    QVERIFY(!authenticator.parseDeviceAuthorizationCapability(QByteArrayLiteral("{}")));

    QCOMPARE(authenticator.signInMethodsEndpoint(),
             std::optional<QString>(QStringLiteral("/api/v2/auth/providers")));
    QCOMPARE(authenticator.parseSignInMethods(authProviders(false))->passwordSignIn,
             std::optional<bool>(false));
    QVERIFY(!authenticator.parseSignInMethods(QByteArrayLiteral(R"({"password_login":true})")));

    const auto start = authenticator.createDeviceAuthorizationRequest(
        QStringLiteral("Living room"), QStringLiteral("linux"));
    QVERIFY(start.has_value());
    QCOMPARE(start->endpoint, QStringLiteral("/api/v2/auth/device/start"));
    const QJsonObject startBody = QJsonDocument::fromJson(start->body).object();
    QCOMPARE(startBody.value(QStringLiteral("device_name")).toString(), QStringLiteral("Living room"));
    QCOMPARE(startBody.value(QStringLiteral("device_platform")).toString(), QStringLiteral("linux"));
    QVERIFY(!startBody.contains(QStringLiteral("client_purpose")));
    QVERIFY(!startBody.contains(QStringLiteral("temporary")));

    const auto started = authenticator.parseDeviceAuthorizationResponse(deviceStart());
    QVERIFY(started.has_value());
    QCOMPARE(started->deviceCode, QStringLiteral("device-secret"));
    QCOMPARE(started->userCode, QStringLiteral("4821-7730"));
    QCOMPARE(started->verificationUriComplete,
             QStringLiteral("https://silo.example.test/activate?code=48217730"));
    QCOMPARE(started->expiresInSeconds, 900);
    QCOMPARE(started->intervalSeconds, 1);
    QVERIFY(started->expiresAt.isValid());
    QVERIFY(!authenticator.parseDeviceAuthorizationResponse(
        QByteArrayLiteral(R"({"device_code":"d","user_code":"1","verification_uri":"javascript:void","expires_in":900,"interval":5})")));

    const auto poll = authenticator.createDeviceAuthorizationPollRequest(QStringLiteral("device-secret"));
    QCOMPARE(poll->endpoint, kDevicePoll);
    QCOMPARE(QJsonDocument::fromJson(poll->body).object().value(QStringLiteral("device_code")).toString(),
             QStringLiteral("device-secret"));
    QVERIFY(!authenticator.createDeviceAuthorizationPollRequest({}));
    QCOMPARE(authenticator.createDeviceAuthorizationCancelRequest(QStringLiteral("device-secret"))->endpoint,
             QStringLiteral("/api/v2/auth/device/cancel"));

    using Status = ProviderDeviceAuthorizationPoll::Status;
    const auto pending = authenticator.parseDeviceAuthorizationPollResponse(devicePoll("pending", true));
    QCOMPARE(pending.status, Status::Pending);
    QVERIFY(pending.opened);
    QCOMPARE(pending.pollAfterSeconds, 1);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(devicePoll("denied")).status,
             Status::Denied);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(devicePoll("expired")).status,
             Status::Expired);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(devicePoll("consumed")).status,
             Status::Consumed);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(devicePoll("unknown")).status,
             Status::Invalid);
    // Approval without a valid token pair is malformed, never a success.
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(devicePoll("approved")).status,
             Status::Invalid);
    const auto approved = authenticator.parseDeviceAuthorizationPollResponse(deviceApproved());
    QCOMPARE(approved.status, Status::Approved);
    QVERIFY(approved.authentication.isValid());
    QCOMPARE(approved.authentication.refreshToken, QStringLiteral("refresh-1"));
    QCOMPARE(approved.authentication.expiresInSeconds, 900);

    // The device's own pairing operations never carry stale bearer or profile headers.
    SiloRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = kSiloAddress;
    context.accessToken = QStringLiteral("stale-access-token");
    context.profileId = QStringLiteral("profile-1");
    context.profileToken = QStringLiteral("stale-profile-token");
    context.deviceName = QStringLiteral("Living room");
    for (const QString &path : {QStringLiteral("/api/v2/auth/device/start"), kDevicePoll,
                                QStringLiteral("/api/v2/auth/device/cancel"),
                                QStringLiteral("/api/v2/auth/device/capability")}) {
        const QNetworkRequest request = factory.createRequest(context, path);
        QVERIFY2(!request.hasRawHeader("Authorization"), qPrintable(path));
        QVERIFY2(!request.hasRawHeader("X-Profile-Id"), qPrintable(path));
        QVERIFY2(!request.hasRawHeader("X-Profile-Token"), qPrintable(path));
        QCOMPARE(request.rawHeader("X-Silo-Device-Name"), QByteArrayLiteral("Living room"));
    }
    // Approving stays an authenticated operation.
    QCOMPARE(factory.createRequest(context, QStringLiteral("/api/v2/auth/device/approve"))
                 .rawHeader("Authorization"),
             QByteArrayLiteral("Bearer stale-access-token"));
}

void SiloAuthenticationTest::signInOptionsOfferCodesOnlyOnSupportingSiloServers()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    QSignalSpy errors(&service, &AuthenticationService::loginError);

    // An SSO-only Silo server: codes are the way in, the password form hides.
    manager.responses = {response(200, siloSystemInfo()), response(200, deviceCapability()),
                         response(200, authProviders(false))};
    service.checkSignInOptions(QStringLiteral("  https://silo.example.test  "));
    QCOMPARE(service.signInOptions().value(QStringLiteral("state")).toString(),
             QStringLiteral("checking"));
    QTRY_COMPARE_WITH_TIMEOUT(service.signInOptions().value(QStringLiteral("state")).toString(),
                              QStringLiteral("ready"), 1000);
    QVariantMap options = service.signInOptions();
    QCOMPARE(options.value(QStringLiteral("server")).toString(), kSiloAddress);
    QVERIFY(options.value(QStringLiteral("silo")).toBool());
    QVERIFY(options.value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(!options.value(QStringLiteral("passwordSignIn")).toBool());
    QCOMPARE(manager.requests.size(), 3);
    QCOMPARE(manager.requests.at(0).request.url().path(), QStringLiteral("/api/v2/system/info"));
    QCOMPARE(manager.requests.at(1).request.url().path(),
             QStringLiteral("/api/v2/auth/device/capability"));
    QCOMPARE(manager.requests.at(2).request.url().path(), QStringLiteral("/api/v2/auth/providers"));
    for (const RecordedRequest &request : manager.requests) {
        QCOMPARE(request.operation, QNetworkAccessManager::GetOperation);
        QVERIFY(!request.request.hasRawHeader("Authorization"));
    }

    // Repeating the same address does not probe again.
    service.checkSignInOptions(kSiloAddress);
    QCOMPARE(manager.requests.size(), 3);

    // Silo with device sign-in turned off keeps the password form.
    manager.responses = {response(200, siloSystemInfo()), response(200, deviceCapability("disabled")),
                         response(200, authProviders(false))};
    service.checkSignInOptions(QStringLiteral("https://silo-two.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(service.signInOptions().value(QStringLiteral("state")).toString(),
                              QStringLiteral("ready"), 1000);
    options = service.signInOptions();
    QVERIFY(options.value(QStringLiteral("silo")).toBool());
    QVERIFY(!options.value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(options.value(QStringLiteral("passwordSignIn")).toBool());

    // A Jellyfin server answers system info with 404: password only, no code option.
    manager.responses = {response(404, QByteArrayLiteral("Not Found"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(service.signInOptions().value(QStringLiteral("state")).toString(),
                              QStringLiteral("ready"), 1000);
    options = service.signInOptions();
    QVERIFY(!options.value(QStringLiteral("silo")).toBool());
    QVERIFY(!options.value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(options.value(QStringLiteral("passwordSignIn")).toBool());

    const qsizetype before = manager.requests.size();
    service.startDeviceSignIn(QStringLiteral("https://jellyfin.example.test"));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(manager.requests.size(), before);
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
}

void SiloAuthenticationTest::deviceSignInStartsAndShowsCode()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    beginDeviceSignIn(service, manager);

    const RecordedRequest &start = manager.requests.last();
    QCOMPARE(start.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(start.request.url().toString(), QStringLiteral("https://silo.example.test/api/v2/auth/device/start"));
    QVERIFY(!start.request.hasRawHeader("Authorization"));
    QVERIFY(QJsonDocument::fromJson(start.body).object().contains(QStringLiteral("device_name")));

    const QVariantMap state = service.deviceSignIn();
    QCOMPARE(state.value(QStringLiteral("userCode")).toString(), QStringLiteral("4821 7730"));
    QCOMPARE(state.value(QStringLiteral("verificationUrl")).toString(),
             QStringLiteral("https://silo.example.test/activate?code=48217730"));
    QCOMPARE(state.value(QStringLiteral("verificationPage")).toString(),
             QStringLiteral("https://silo.example.test/activate"));
    QVERIFY(!state.value(QStringLiteral("opened")).toBool());
    QVERIFY(state.value(QStringLiteral("secondsRemaining")).toInt() > 890);
    QVERIFY(!state.contains(QStringLiteral("deviceCode")));
    QCOMPARE(service.authenticationStep(), QStringLiteral("credentials"));
    QCOMPARE(service.activeProviderKind(), ProviderKind::Silo);
    service.cancelDeviceSignIn();
}

void SiloAuthenticationTest::deviceSignInKeepsPollingWhilePendingThenSignsInLikePassword()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    beginDeviceSignIn(service, manager);

    manager.responses.append(response(200, devicePoll("pending")));
    manager.responses.append(response(200, devicePoll("pending", true)));
    QTRY_VERIFY_WITH_TIMEOUT(service.deviceSignIn().value(QStringLiteral("opened")).toBool(), 1000);
    QCOMPARE(deviceState(service), QStringLiteral("waiting"));
    QCOMPARE(manager.countPath(kDevicePoll), 2);
    const RecordedRequest &poll = manager.requests.last();
    QCOMPARE(QJsonDocument::fromJson(poll.body).object().value(QStringLiteral("device_code")).toString(),
             QStringLiteral("device-secret"));
    QVERIFY(!poll.request.hasRawHeader("Authorization"));

    manager.responses.append(response(200, deviceApproved()));
    manager.responses.append(response(200, singleUnlockedProfile()));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 1000);
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
    QCOMPARE(manager.countPath(kDevicePoll), 3);

    // The same session a password login produces.
    FakeNetworkAccessManager passwordManager;
    passwordManager.responses = {response(200, successfulLogin()),
                                 response(200, singleUnlockedProfile())};
    HttpTransport passwordTransport(&passwordManager);
    SiloProviderAdapter passwordAdapter;
    AuthenticationService password(nullptr, &passwordTransport, &passwordAdapter);
    password.setProviderSelection(QStringLiteral("silo"));
    password.authenticate(kSiloAddress, QStringLiteral("Alice"), QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(password.isAuthenticated(), 1000);

    QCOMPARE(service.getServerUrl(), password.getServerUrl());
    QCOMPARE(service.getUserId(), password.getUserId());
    QCOMPARE(service.getUsername(), password.getUsername());
    QCOMPARE(service.getAccessToken(), password.getAccessToken());
    QCOMPARE(service.authenticationStep(), password.authenticationStep());
    QCOMPARE(service.activeConnectionId(), password.activeConnectionId());
    const QNetworkRequest deviceRequest = service.createRequest(QStringLiteral("/api/v2/catalog"));
    const QNetworkRequest passwordRequest = password.createRequest(QStringLiteral("/api/v2/catalog"));
    QCOMPARE(deviceRequest.rawHeader("Authorization"), passwordRequest.rawHeader("Authorization"));
    QCOMPARE(deviceRequest.rawHeader("X-Profile-Id"), passwordRequest.rawHeader("X-Profile-Id"));

    // The refresh token from the approval renews the session.
    manager.responses.append(response(
        200, QByteArrayLiteral(R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":900})")));
    TokenRenewal renewal = TokenRenewal::Unavailable;
    bool renewed = false;
    service.renewConnection(service.activeConnectionId(), [&](TokenRenewal outcome) {
        renewal = outcome;
        renewed = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(renewed, 1000);
    QCOMPARE(renewal, TokenRenewal::Renewed);
    QCOMPARE(QJsonDocument::fromJson(manager.requests.last().body).object()
                 .value(QStringLiteral("refresh_token")).toString(),
             QStringLiteral("refresh-1"));
    QCOMPARE(service.getAccessToken(), QStringLiteral("access-2"));
}

void SiloAuthenticationTest::deviceSignInSlowsDownWhenRateLimited()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    beginDeviceSignIn(service, manager);

    manager.responses.append(response(429,
        QByteArrayLiteral(R"({"type":"https:\/\/siloserver.org/docs/api/v2/problems/rate_limited","title":"rate_limited","status":429,"detail":"Slow down","instance":"urn:silo:request:test"})")));
    manager.responses.append(response(200, devicePoll("pending")));
    manager.responses.append(response(200, devicePoll("pending")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.countPath(kDevicePoll), 3, 2000);
    QCOMPARE(deviceState(service), QStringLiteral("waiting"));

    QList<qint64> polls;
    for (const RecordedRequest &request : manager.requests) {
        if (request.request.url().path() == kDevicePoll) {
            polls.append(request.elapsedMs);
        }
    }
    // The interval grows from 1 to 6 server seconds and stays there.
    const qint64 slowed = 6 * kTestSecondMs;
    QVERIFY2(polls.at(1) - polls.at(0) >= slowed - 5,
             qPrintable(QStringLiteral("gap %1").arg(polls.at(1) - polls.at(0))));
    QVERIFY2(polls.at(2) - polls.at(1) >= slowed - 5,
             qPrintable(QStringLiteral("gap %1").arg(polls.at(2) - polls.at(1))));
    service.cancelDeviceSignIn();
}

void SiloAuthenticationTest::deviceSignInExpiresWhenServerOrDeadlineSaysSo()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    // The server reports the request expired.
    beginDeviceSignIn(service, manager);
    manager.responses.append(response(200, devicePoll("expired")));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("expired"), 1000);
    QVERIFY(service.deviceSignIn().value(QStringLiteral("userCode")).toString().isEmpty());
    QVERIFY(!service.isAuthenticated());

    // The local deadline passes while the server still says pending.
    beginDeviceSignIn(service, manager, 3);
    for (int i = 0; i < 10; ++i) {
        manager.responses.append(response(200, devicePoll("pending")));
    }
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("expired"), 1000);
    const qsizetype polls = manager.countPath(kDevicePoll);
    QTest::qWait(5 * kTestSecondMs);
    QCOMPARE(manager.countPath(kDevicePoll), polls);

    // A new code can be requested after expiry.
    manager.responses = {response(201, deviceStart())};
    service.startDeviceSignIn(kSiloAddress);
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("waiting"), 1000);
    service.cancelDeviceSignIn();
}

void SiloAuthenticationTest::deviceSignInStopsWhenDenied()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    beginDeviceSignIn(service, manager);

    manager.responses.append(response(200, devicePoll("denied")));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("denied"), 1000);
    const qsizetype requests = manager.requests.size();
    QTest::qWait(3 * kTestSecondMs);
    QCOMPARE(manager.requests.size(), requests);
    QVERIFY(!service.isAuthenticated());
    QCOMPARE(service.authenticationStep(), QStringLiteral("credentials"));
}

void SiloAuthenticationTest::cancelingDeviceSignInWithdrawsCodeAndStopsPolling()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);
    // Slow server seconds leave room to cancel between two polls.
    constexpr int secondMs = 250;
    beginDeviceSignIn(service, manager, 900, secondMs);
    manager.responses.append(response(200, devicePoll("pending")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.countPath(kDevicePoll), 1, 1000);

    manager.responses.append(response(200, QByteArrayLiteral(R"({"status":"canceled"})")));
    service.cancelDeviceSignIn();
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
    QVERIFY(service.deviceSignIn().value(QStringLiteral("userCode")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(manager.countPath(QStringLiteral("/api/v2/auth/device/cancel")), 1, 1000);
    const RecordedRequest &cancel = manager.requests.last();
    QCOMPARE(QJsonDocument::fromJson(cancel.body).object().value(QStringLiteral("device_code")).toString(),
             QStringLiteral("device-secret"));
    QVERIFY(!cancel.request.hasRawHeader("Authorization"));
    QTest::qWait(2 * secondMs);
    QCOMPARE(manager.countPath(kDevicePoll), 1);

    // Leaving the signed-out state (another space, a password sign-in)
    // abandons a running code the same way.
    beginDeviceSignIn(service, manager);
    manager.responses.append(response(200, QByteArrayLiteral(R"({"status":"canceled"})")));
    service.clearAccountState();
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
    QTRY_COMPARE_WITH_TIMEOUT(manager.countPath(QStringLiteral("/api/v2/auth/device/cancel")), 2, 1000);
}

QTEST_MAIN(SiloAuthenticationTest)
#include "SiloAuthenticationTest.moc"
