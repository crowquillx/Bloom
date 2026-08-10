#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTimer>
#include <cstring>

#include "network/AuthenticationService.h"
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
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    QList<QueuedResponse> responses;
    QList<RecordedRequest> requests;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        const QByteArray body = outgoingData ? outgoingData->readAll() : QByteArray{};
        requests.append({operation, request, body});
        QueuedResponse response{
            500,
            QByteArrayLiteral(R"({"error":"test_queue_empty","message":"No response queued"})"),
            QNetworkReply::UnknownServerError
        };
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
        R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":42,"username":"Alice"}})");
}

QByteArray singleUnlockedProfile()
{
    return QByteArrayLiteral(
        R"({"profiles":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true}]})");
}

} // namespace

class SiloAuthenticationTest : public QObject
{
    Q_OBJECT

private slots:
    void adapterExposesNativeAuthenticationBoundaries();
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

    const auto omitted = adapter.mapDetectionResult(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("ok")}
    });
    QVERIFY(omitted.has_value());
    QCOMPARE(omitted->providerKind, ProviderKind::Silo);
    QCOMPARE(omitted->protocolMode, ProtocolMode::Native);
    QVERIFY(omitted->serverId.isEmpty());
    QVERIFY(omitted->serverName.isEmpty());

    const auto configuredDefault = adapter.mapDetectionResult(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("server_id"),
         QStringLiteral("b2d9e6c9-1237-5add-a687-5dae547ece33")},
        {QStringLiteral("server_name"), QStringLiteral("Living Room")}
    });
    QVERIFY(configuredDefault.has_value());
    QVERIFY(configuredDefault->serverId.isEmpty());
    QCOMPARE(configuredDefault->serverName, QStringLiteral("Living Room"));

    QVERIFY(!adapter.mapDetectionResult(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("starting")},
        {QStringLiteral("server_id"), QStringLiteral("default-server-id")}
    }).has_value());
    QVERIFY(!adapter.mapDetectionResult(QJsonObject{
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("server_id"), 17}
    }).has_value());
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

    const QNetworkRequest request = factory.createRequest(context, QStringLiteral("api/v1/profiles"));
    QCOMPARE(request.url(), QUrl(QStringLiteral("https://silo.example.test/api/v1/profiles")));
    QCOMPARE(request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-secret"));
    QCOMPARE(request.rawHeader("X-Profile-Id"), QByteArrayLiteral("profile-1"));
    QCOMPARE(request.rawHeader("X-Profile-Token"), QByteArrayLiteral("profile-secret"));
    QCOMPARE(request.rawHeader("X-Silo-Client"), QByteArrayLiteral("Bloom Test"));
    QCOMPARE(request.rawHeader("X-Silo-Client-Version"), QByteArrayLiteral("1.2.3"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Id"), QByteArrayLiteral("device-1"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Name"), QByteArrayLiteral("Living Room"));
    QCOMPARE(request.rawHeader("X-Silo-Device-Platform"), QByteArrayLiteral("linux"));

    const QString redacted = factory.redactedUrl(QUrl(QStringLiteral(
        "https://user:password@silo.example.test/api/v1/items?access_token=access-secret&profile_token=profile-secret&q=title")));
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

    const QNetworkRequest login = factory.createRequest(context, QStringLiteral("/api/v1/auth/login"));
    const QNetworkRequest refresh = factory.createRequest(context, QStringLiteral("/api/v1/auth/refresh"));
    QVERIFY(!login.hasRawHeader("Authorization"));
    QVERIFY(!refresh.hasRawHeader("Authorization"));
    QVERIFY(!login.hasRawHeader("X-Profile-Id"));
    const QNetworkRequest health = factory.createRequest(
        context, QStringLiteral("/api/v1/health"));
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

    const QNetworkRequest request = factory.createRequest(
        context, QStringLiteral("/api/v1/profiles/new-profile/verify-pin"));
    QCOMPARE(request.rawHeader("Authorization"), QByteArrayLiteral("Bearer access-secret"));
    QVERIFY(!request.hasRawHeader("X-Profile-Id"));
    QVERIFY(!request.hasRawHeader("X-Profile-Token"));
}

void SiloAuthenticationTest::authenticatorBuildsAndParsesLoginAndRefreshContracts()
{
    SiloAuthenticator authenticator;
    const ProviderAuthenticationRequest login = authenticator.createLoginRequest(
        QStringLiteral("Alice"), QStringLiteral("password"));
    QCOMPARE(login.endpoint, QStringLiteral("/api/v1/auth/login"));
    const QJsonObject loginBody = QJsonDocument::fromJson(login.body).object();
    QCOMPARE(loginBody.value(QStringLiteral("username")).toString(), QStringLiteral("Alice"));
    QCOMPARE(loginBody.value(QStringLiteral("password")).toString(), QStringLiteral("password"));

    const ProviderAuthenticationResult loginResult = authenticator.parseLoginResponse(
        QByteArrayLiteral(R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":42,"username":"Alice"}})"));
    QVERIFY(loginResult.isValid());
    QCOMPARE(loginResult.accessToken, QStringLiteral("access-1"));
    QCOMPARE(loginResult.refreshToken, QStringLiteral("refresh-1"));
    QCOMPARE(loginResult.accountId, QStringLiteral("42"));
    QCOMPARE(loginResult.username, QStringLiteral("Alice"));
    QCOMPARE(loginResult.expiresInSeconds, 900);

    const auto refresh = authenticator.createRefreshRequest(QStringLiteral("refresh-1"));
    QVERIFY(refresh.has_value());
    QCOMPARE(refresh->endpoint, QStringLiteral("/api/v1/auth/refresh"));
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
    QVERIFY(!authenticator.parseLoginResponse(
                 QByteArrayLiteral(R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":0,"user":{"id":42,"username":"Alice"}})"))
                 .isValid());
    QVERIFY(!authenticator.parseLoginResponse(
                 QByteArrayLiteral(R"({"access_token":"access-1","expires_in":900,"user":{"id":42,"username":"Alice"}})"))
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

    const SiloAuthenticationError invalidCredentials =
        authenticator.parseErrorResponse(QByteArrayLiteral(
            R"({"error":"invalid_credentials","message":"Invalid username or password"})"));
    QVERIFY(invalidCredentials.isValid());
    QCOMPARE(invalidCredentials.failure,
             SiloAuthenticationFailure::InvalidCredentials);
    const SiloAuthenticationError revoked =
        authenticator.parseErrorResponse(QByteArrayLiteral(
            R"({"error":"session_revoked","message":"Session was revoked"})"));
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
    QCOMPARE(*profiles, QStringLiteral("/api/v1/profiles"));
    QCOMPARE(*pin,
             QStringLiteral("/api/v1/profiles/profile%2Fwith%20space/verify-pin"));
    QCOMPARE(*sessions, QStringLiteral("/api/v1/auth/sessions"));
    QCOMPARE(*revoke,
             QStringLiteral("/api/v1/auth/sessions/session%2Fwith%20space"));
    QCOMPARE(*itemMarkers,
             QStringLiteral("/api/v1/markers/items/content%2Fwith%20space"));
    QCOMPARE(*fileMarkers,
             QStringLiteral("/api/v1/markers/files/file%2Fwith%20space"));
    QCOMPARE(*health, QStringLiteral("/api/v1/health"));
    QCOMPARE(*logout, QStringLiteral("/api/v1/auth/logout"));
}

void SiloAuthenticationTest::authenticatorDistinguishesIncorrectAndValidProfilePins()
{
    SiloAuthenticator authenticator;
    const auto request = authenticator.createProfileLoginRequest(
        QStringLiteral("profile/one"), QStringLiteral("1234"));
    QVERIFY(request.has_value());
    QCOMPARE(request->endpoint,
             QStringLiteral("/api/v1/profiles/profile%2Fone/verify-pin"));
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
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"access_token":"access-2","refresh_token":"refresh-2","expires_in":1800})")));
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Still unauthorized"})")));

    int attempts = 0;
    int successes = 0;
    int failures = 0;
    QSignalSpy expiredSpy(&service, &AuthenticationService::sessionExpired);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/protected"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v1/protected")));
        },
        [&](QNetworkReply *) { ++successes; },
        [&](const NetworkError &) { ++failures; },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(successes, 0);
    QCOMPARE(attempts, 2);
    QCOMPARE(expiredSpy.count(), 1);
    QCOMPARE(manager.requests.size(), initialRequestCount + 3);
    QCOMPARE(manager.requests.at(initialRequestCount).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(manager.requests.at(initialRequestCount + 1).request.url().path(),
             QStringLiteral("/api/v1/auth/refresh"));
    QVERIFY(!manager.requests.at(initialRequestCount + 1)
                 .request.hasRawHeader("Authorization"));
    QCOMPARE(QJsonDocument::fromJson(manager.requests.at(initialRequestCount + 1).body)
                 .object().value(QStringLiteral("refresh_token")).toString(),
             QStringLiteral("refresh-1"));
    QCOMPARE(manager.requests.at(initialRequestCount + 2).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
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
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
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
        this,
        QStringLiteral("/api/v1/first"),
        [&]() -> QNetworkReply * {
            ++firstAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v1/first")));
        },
        success,
        failure,
        options);
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/second"),
        [&]() -> QNetworkReply * {
            ++secondAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v1/second")));
        },
        success,
        failure,
        options);

    QTRY_COMPARE_WITH_TIMEOUT(successes, 2, 1000);
    QCOMPARE(failures, 0);
    QCOMPARE(firstAttempts, 2);
    QCOMPARE(secondAttempts, 2);
    int refreshRequests = 0;
    for (qsizetype i = initialRequestCount; i < manager.requests.size(); ++i) {
        if (manager.requests.at(i).request.url().path()
            == QStringLiteral("/api/v1/auth/refresh")) {
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
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        401,
        QByteArrayLiteral(R"({"error":"invalid_token","message":"Refresh expired"})")));

    int firstAttempts = 0;
    int secondAttempts = 0;
    int failures = 0;
    QSignalSpy expiredSpy(&service, &AuthenticationService::sessionExpired);
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    const auto failure = [&](const NetworkError &) { ++failures; };

    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/first"),
        [&]() -> QNetworkReply * {
            ++firstAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v1/first")));
        },
        [](QNetworkReply *) {},
        failure,
        options);
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/second"),
        [&]() -> QNetworkReply * {
            ++secondAttempts;
            return manager.get(service.createRequest(QStringLiteral("/api/v1/second")));
        },
        [](QNetworkReply *) {},
        failure,
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 2, 1000);
    QCOMPARE(firstAttempts, 1);
    QCOMPARE(secondAttempts, 1);
    QCOMPARE(expiredSpy.count(), 1);
    int refreshRequests = 0;
    for (qsizetype i = initialRequestCount; i < manager.requests.size(); ++i) {
        if (manager.requests.at(i).request.url().path()
            == QStringLiteral("/api/v1/auth/refresh")) {
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
        response(200, QByteArrayLiteral(
            R"({"profiles":[{"id":"profile-1","name":"Alice","has_pin":true,"is_child":false,"is_primary":true}]})"))
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

    const QNetworkRequest selected =
        service.createRequest(QStringLiteral("/api/v1/catalog"));
    QCOMPARE(selected.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(selected.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QCOMPARE(selected.rawHeader("X-Profile-Token"),
             QByteArrayLiteral("profile-token-1"));

    service.clearProfileState();
    const QNetworkRequest cleared =
        service.createRequest(QStringLiteral("/api/v1/catalog"));
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
    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"sessions":[{"id":"session-current","device_name":"Living Room","ip_address":"192.0.2.1","created_at":"2026-08-01T10:00:00Z","expires_at":"2026-08-08T10:00:00Z","is_current":true},{"id":"session-revoked","device_name":"Bedroom","ip_address":"192.0.2.2","created_at":"2026-07-01T10:00:00Z","expires_at":"2026-07-08T10:00:00Z","revoked_at":"2026-07-02T10:00:00Z","is_current":false}]})")));
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
             QStringLiteral("/api/v1/auth/sessions"));
    QCOMPARE(manager.requests.at(initialRequestCount).request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));

    manager.responses.append(response(204, QByteArray{}));
    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"sessions":[{"id":"session-current","device_name":"Living Room","ip_address":"192.0.2.1","created_at":"2026-08-01T10:00:00Z","expires_at":"2026-08-08T10:00:00Z","is_current":true}]})")));
    service.revokeAuthSession(QStringLiteral("session-revoked"));
    QTRY_COMPARE_WITH_TIMEOUT(service.authSessions().size(), 1, 1000);
    QCOMPARE(manager.requests.at(initialRequestCount + 1).operation,
             QNetworkAccessManager::DeleteOperation);
    QCOMPARE(manager.requests.at(initialRequestCount + 1).request.url().path(),
             QStringLiteral("/api/v1/auth/sessions/session-revoked"));
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

    manager.responses.append(response(
        200,
        QByteArrayLiteral(
            R"({"sessions":[{"id":"session-current","device_name":"Living Room","is_current":true}]})")));
    service.loadAuthSessions();
    QTRY_COMPARE_WITH_TIMEOUT(service.authSessions().size(), 1, 1000);

    QSignalSpy errorSpy(&service, &AuthenticationService::loginError);
    manager.responses.append(response(
        400,
        QByteArrayLiteral(R"({"error":"invalid_request","message":"Request rejected"})")));
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

    manager.responses.append(response(
        401, QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        200, QByteArrayLiteral(R"({"access_token":"access-2","expires_in":1800})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    int successes = 0;
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/protected"),
        [&]() { return manager.get(service.createRequest(QStringLiteral("/api/v1/protected"))); },
        [&](QNetworkReply *) { ++successes; },
        [](const NetworkError &) {},
        options);
    QTRY_COMPARE_WITH_TIMEOUT(successes, 1, 1000);

    manager.responses.append(response(
        401, QByteArrayLiteral(R"({"error":"invalid_token","message":"Expired"})")));
    manager.responses.append(response(
        200, QByteArrayLiteral(R"({"access_token":"access-3","expires_in":1800})")));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"ok":true})")));
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/protected-again"),
        [&]() {
            return manager.get(service.createRequest(
                QStringLiteral("/api/v1/protected-again")));
        },
        [&](QNetworkReply *) { ++successes; },
        [](const NetworkError &) {},
        options);
    QTRY_COMPARE_WITH_TIMEOUT(successes, 2, 1000);

    int refreshRequests = 0;
    for (const RecordedRequest &request : manager.requests) {
        if (request.request.url().path() == QStringLiteral("/api/v1/auth/refresh")) {
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
    manager.responses = {
        response(200, QByteArrayLiteral(
            R"({"id":43,"username":"Bob","role":"user"})"))
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService service(nullptr, &transport, &adapter);

    service.restoreSession(QStringLiteral("https://silo.example.test"),
                           QStringLiteral("42"),
                           QStringLiteral("stale-access"),
                           QStringLiteral("Alice"));
    QTRY_COMPARE_WITH_TIMEOUT(service.getUserId(), QString(), 1000);
    QCOMPARE(manager.requests.size(), 1);
    QCOMPARE(manager.requests.constFirst().request.url().path(),
             QStringLiteral("/api/v1/auth/me"));
}


QTEST_MAIN(SiloAuthenticationTest)
#include "SiloAuthenticationTest.moc"
