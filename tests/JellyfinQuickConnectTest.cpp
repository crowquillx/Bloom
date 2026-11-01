#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cstring>

#include "network/AuthenticationService.h"
#include "network/DeviceSignInSession.h"
#include "network/HttpTransport.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/jellyfin/JellyfinAuthenticator.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/jellyfin/JellyfinRequestFactory.h"
#include "providers/silo/SiloProviderAdapter.h"

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
        requests.append({operation, request, body});
        QueuedResponse response{500, QByteArrayLiteral("No response queued"),
                                QNetworkReply::UnknownServerError};
        if (!responses.isEmpty()) {
            response = responses.takeFirst();
        }
        return new FakeReply(request, response.error, response.statusCode, response.payload,
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

QByteArray quickConnectResult(bool authenticated, const char *secret = "qc-secret")
{
    return QByteArray(R"({"Secret":")") + secret
        + QByteArray(R"(","Code":"482173","DeviceId":"bloom-device","DeviceName":"Desktop","AppName":"Bloom","AppVersion":"1.0","DateAdded":"2026-01-02T03:04:05.0000000Z","Authenticated":)")
        + (authenticated ? "true" : "false") + QByteArray("}");
}

QByteArray jellyfinLogin()
{
    return QByteArrayLiteral(
        R"({"AccessToken":"jf-access","ServerId":"server-1","User":{"Id":"user-1","Name":"Alice"},"SessionInfo":{"DeviceId":"bloom-device"}})");
}

constexpr int kTestSecondMs = 20;
const QString kJellyfinAddress = QStringLiteral("https://jellyfin.example.test");
const QString kEnabled = QStringLiteral("/QuickConnect/Enabled");
const QString kInitiate = QStringLiteral("/QuickConnect/Initiate");
const QString kConnect = QStringLiteral("/QuickConnect/Connect");
const QString kExchange = QStringLiteral("/Users/AuthenticateWithQuickConnect");

// Both providers, as in the app.
struct Fixture {
    FakeNetworkAccessManager manager;
    HttpTransport transport{&manager};
    JellyfinProviderAdapter jellyfin;
    SiloProviderAdapter silo;
    AuthenticationService service{nullptr, &transport,
                                  QList<IProviderAdapter *>{&jellyfin, &silo}};
};

QString deviceState(const AuthenticationService &service)
{
    return service.deviceSignIn().value(QStringLiteral("state")).toString();
}

QString signInState(const AuthenticationService &service)
{
    return service.signInOptions().value(QStringLiteral("state")).toString();
}

// Discovery (Silo system info answers 404, Quick Connect is on), then a code.
void beginQuickConnect(Fixture &fixture, int secondMs = kTestSecondMs)
{
    AuthenticationService &service = fixture.service;
    service.deviceSignInSession()->setSecondDurationForTesting(secondMs);
    if (service.signInOptions().value(QStringLiteral("server")).toString() != kJellyfinAddress
        || signInState(service) != QStringLiteral("ready")) {
        fixture.manager.responses.append(response(404, QByteArrayLiteral("Not Found")));
        fixture.manager.responses.append(response(200, QByteArrayLiteral("true")));
        service.checkSignInOptions(kJellyfinAddress);
    }
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QVERIFY(service.signInOptions().value(QStringLiteral("quickConnect")).toBool());

    fixture.manager.responses.append(response(200, quickConnectResult(false)));
    service.startDeviceSignIn(kJellyfinAddress);
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("waiting"), 1000);
}

} // namespace

class JellyfinQuickConnectTest : public QObject
{
    Q_OBJECT

private slots:
    void authenticatorBuildsAndParsesQuickConnectContracts();
    void requestFactoryRedactsTheSecretAndKeepsTheDeviceId();
    void signInOptionsOfferQuickConnectOnlyWhenEnabled();
    void quickConnectShowsTheCodeWithoutALink();
    void authorizedCodeIsExchangedOnceThenSignsInLikePassword();
    void quickConnectExpiresWhenServerOrDeadlineSaysSo();
    void turningQuickConnectOffWithdrawsTheOffer();
    void failedExchangeEndsTheCode();
    void backingOutStopsPollingWithoutACancelRequest();
};

void JellyfinQuickConnectTest::authenticatorBuildsAndParsesQuickConnectContracts()
{
    JellyfinAuthenticator authenticator;
    QCOMPARE(authenticator.deviceAuthorizationCapabilityEndpoint(),
             std::optional<QString>(kEnabled));
    const auto enabled = authenticator.parseDeviceAuthorizationCapability(QByteArrayLiteral("true\n"));
    QVERIFY(enabled.has_value());
    QVERIFY(enabled->available);
    QVERIFY(!enabled->cancel);
    QVERIFY(!authenticator.parseDeviceAuthorizationCapability(QByteArrayLiteral("false"))->available);
    QVERIFY(!authenticator.parseDeviceAuthorizationCapability(QByteArrayLiteral("{}")));
    QVERIFY(!authenticator.parseDeviceAuthorizationCapability(QByteArrayLiteral("<html>")));
    // Jellyfin keeps the password form; it reports no sign-in methods.
    QVERIFY(!authenticator.signInMethodsEndpoint());

    const auto initiate = authenticator.createDeviceAuthorizationRequest(
        QStringLiteral("Living room"), QStringLiteral("linux"));
    QVERIFY(initiate.has_value());
    QCOMPARE(initiate->endpoint, kInitiate);
    QCOMPARE(initiate->method, ProviderAuthenticationRequest::Method::Post);
    QVERIFY(initiate->body.isEmpty());

    const auto started = authenticator.parseDeviceAuthorizationResponse(quickConnectResult(false));
    QVERIFY(started.has_value());
    QCOMPARE(started->deviceCode, QStringLiteral("qc-secret"));
    QCOMPARE(started->userCode, QStringLiteral("482173"));
    QVERIFY(started->verificationUri.isEmpty());
    QVERIFY(started->verificationUriComplete.isEmpty());
    QCOMPARE(started->expiresInSeconds, JellyfinAuthenticator::kQuickConnectLifetimeSeconds);
    QCOMPARE(started->intervalSeconds, JellyfinAuthenticator::kQuickConnectPollSeconds);
    QVERIFY(!started->expiresAt.isValid());
    QVERIFY(!authenticator.parseDeviceAuthorizationResponse(
        QByteArrayLiteral(R"({"Code":"482173","Authenticated":false})")));
    QVERIFY(!authenticator.parseDeviceAuthorizationResponse(
        QByteArrayLiteral(R"({"Secret":"s","Code":" ","Authenticated":false})")));
    QVERIFY(!authenticator.parseDeviceAuthorizationResponse(QByteArrayLiteral("true")));

    const auto poll = authenticator.createDeviceAuthorizationPollRequest(QStringLiteral("a+b/c"));
    QVERIFY(poll.has_value());
    QCOMPARE(poll->method, ProviderAuthenticationRequest::Method::Get);
    QVERIFY(poll->body.isEmpty());
    const QUrl pollUrl(QStringLiteral("https://jf.test") + poll->endpoint);
    QCOMPARE(pollUrl.path(), kConnect);
    QCOMPARE(QUrlQuery(pollUrl).queryItemValue(QStringLiteral("secret"), QUrl::FullyDecoded),
             QStringLiteral("a+b/c"));
    QVERIFY(!authenticator.createDeviceAuthorizationPollRequest({}));

    using Status = ProviderDeviceAuthorizationPoll::Status;
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(quickConnectResult(false)).status,
             Status::Pending);
    const auto authorized = authenticator.parseDeviceAuthorizationPollResponse(
        quickConnectResult(true));
    QCOMPARE(authorized.status, Status::Authorized);
    // The poll never carries tokens; they come from the exchange.
    QVERIFY(!authorized.authentication.isValid());
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(quickConnectResult(true, "")).status,
             Status::Expired);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(
                 QByteArrayLiteral(R"({"Secret":"s"})")).status,
             Status::Invalid);
    QCOMPARE(authenticator.parseDeviceAuthorizationPollResponse(QByteArrayLiteral("nope")).status,
             Status::Invalid);

    // Jellyfin has no cancel operation.
    QVERIFY(!authenticator.createDeviceAuthorizationCancelRequest(QStringLiteral("qc-secret")));

    const auto exchange =
        authenticator.createDeviceAuthorizationExchangeRequest(QStringLiteral("qc-secret"));
    QVERIFY(exchange.has_value());
    QCOMPARE(exchange->endpoint, kExchange);
    QCOMPARE(exchange->method, ProviderAuthenticationRequest::Method::Post);
    const QJsonObject exchangeBody = QJsonDocument::fromJson(exchange->body).object();
    QCOMPARE(exchangeBody.value(QStringLiteral("Secret")).toString(), QStringLiteral("qc-secret"));
    QCOMPARE(exchangeBody.size(), 1);
    QVERIFY(!authenticator.createDeviceAuthorizationExchangeRequest({}));
    // The exchange answers with the AuthenticateByName shape.
    const ProviderAuthenticationResult login = authenticator.parseLoginResponse(jellyfinLogin());
    QVERIFY(login.isValid());
    QCOMPARE(login.accountId, QStringLiteral("user-1"));
}

void JellyfinQuickConnectTest::requestFactoryRedactsTheSecretAndKeepsTheDeviceId()
{
    JellyfinRequestFactory factory;
    const QString redacted = factory.redactedUrl(
        QUrl(QStringLiteral("https://jf.test/QuickConnect/Connect?secret=qc-secret")));
    QVERIFY2(!redacted.contains(QStringLiteral("qc-secret")), qPrintable(redacted));
    QVERIFY(redacted.contains(QStringLiteral("secret=")));

    ProviderRequestContext context;
    context.baseUrl = kJellyfinAddress;
    context.deviceId = QStringLiteral("bloom-device");
    for (const QString &endpoint : {kInitiate, QStringLiteral("/QuickConnect/Connect?secret=s"),
                                    kExchange}) {
        const QByteArray authorization =
            factory.createRequest(context, endpoint).rawHeader("Authorization");
        QVERIFY2(authorization.contains(R"(DeviceId="bloom-device")"), qPrintable(endpoint));
        QVERIFY2(authorization.contains(R"(Client="Bloom")"), qPrintable(endpoint));
        QVERIFY2(!authorization.contains("Token="), qPrintable(endpoint));
    }
}

void JellyfinQuickConnectTest::signInOptionsOfferQuickConnectOnlyWhenEnabled()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;
    QSignalSpy errors(&service, &AuthenticationService::loginError);

    // Not native Silo, then Quick Connect is on: offered next to the password form.
    manager.responses = {response(404, QByteArrayLiteral("Not Found")),
                         response(200, QByteArrayLiteral("true"))};
    service.checkSignInOptions(kJellyfinAddress);
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QVariantMap options = service.signInOptions();
    QVERIFY(!options.value(QStringLiteral("silo")).toBool());
    QVERIFY(options.value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(options.value(QStringLiteral("quickConnect")).toBool());
    QVERIFY(options.value(QStringLiteral("passwordSignIn")).toBool());
    QCOMPARE(manager.requests.size(), 2);
    QCOMPARE(manager.requests.at(0).request.url().path(), QStringLiteral("/api/v2/system/info"));
    QCOMPARE(manager.requests.at(1).request.url().toString(), kJellyfinAddress + kEnabled);
    QCOMPARE(manager.requests.at(1).operation, QNetworkAccessManager::GetOperation);
    QVERIFY(!manager.requests.at(1).request.rawHeader("Authorization").contains("Token="));

    // Quick Connect turned off: password only, and starting is refused.
    manager.responses = {response(404, QByteArrayLiteral("Not Found")),
                         response(200, QByteArrayLiteral("false"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin-two.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    options = service.signInOptions();
    QVERIFY(!options.value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(!options.value(QStringLiteral("quickConnect")).toBool());
    QVERIFY(options.value(QStringLiteral("passwordSignIn")).toBool());
    qsizetype before = manager.requests.size();
    service.startDeviceSignIn(QStringLiteral("https://jellyfin-two.example.test"));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(manager.requests.size(), before);
    QCOMPARE(deviceState(service), QStringLiteral("idle"));

    // A failing Enabled read (an older server, a proxy error) hides it too.
    manager.responses = {response(404, QByteArrayLiteral("Not Found")),
                         response(404, QByteArrayLiteral("Not Found"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin-three.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QVERIFY(!service.signInOptions().value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(service.signInOptions().value(QStringLiteral("passwordSignIn")).toBool());

    manager.responses = {response(404, QByteArrayLiteral("Not Found")),
                         response(500, QByteArrayLiteral("oops")),
                         response(500, QByteArrayLiteral("oops"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin-four.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 2000);
    QVERIFY(!service.signInOptions().value(QStringLiteral("deviceSignIn")).toBool());
    QVERIFY(!service.signInOptions().value(QStringLiteral("quickConnect")).toBool());

    // An explicit Jellyfin selection skips the Silo probe.
    service.setProviderSelection(QStringLiteral("jellyfin"));
    before = manager.requests.size();
    manager.responses = {response(200, QByteArrayLiteral("true"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin-five.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QVERIFY(service.signInOptions().value(QStringLiteral("quickConnect")).toBool());
    QCOMPARE(manager.requests.size(), before + 1);
    QCOMPARE(manager.requests.last().request.url().path(), kEnabled);

    // An explicit Silo selection never asks Jellyfin.
    const qsizetype enabledReads = manager.countPath(kEnabled);
    service.setProviderSelection(QStringLiteral("silo"));
    manager.responses = {response(404, QByteArrayLiteral("Not Found"))};
    service.checkSignInOptions(QStringLiteral("https://jellyfin-six.example.test"));
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QVERIFY(!service.signInOptions().value(QStringLiteral("deviceSignIn")).toBool());
    QCOMPARE(manager.countPath(kEnabled), enabledReads);
}

void JellyfinQuickConnectTest::quickConnectShowsTheCodeWithoutALink()
{
    Fixture fixture;
    beginQuickConnect(fixture);
    AuthenticationService &service = fixture.service;

    const RecordedRequest &initiate = fixture.manager.requests.last();
    QCOMPARE(initiate.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(initiate.request.url().toString(), kJellyfinAddress + kInitiate);
    const QByteArray authorization = initiate.request.rawHeader("Authorization");
    QVERIFY(authorization.startsWith("MediaBrowser "));
    QVERIFY(authorization.contains("DeviceId=\""));
    QVERIFY(!authorization.contains("Token="));

    const QVariantMap state = service.deviceSignIn();
    QCOMPARE(state.value(QStringLiteral("method")).toString(), QStringLiteral("quickConnect"));
    QCOMPARE(state.value(QStringLiteral("userCode")).toString(), QStringLiteral("482173"));
    QVERIFY(state.value(QStringLiteral("verificationUrl")).toString().isEmpty());
    QVERIFY(state.value(QStringLiteral("verificationPage")).toString().isEmpty());
    QVERIFY(state.value(QStringLiteral("secondsRemaining")).toInt() > 590);
    QVERIFY(!state.contains(QStringLiteral("deviceCode")));
    QVERIFY(!state.values().contains(QVariant(QStringLiteral("qc-secret"))));
    QCOMPARE(service.activeProviderKind(), ProviderKind::Jellyfin);
    QCOMPARE(service.authenticationStep(), QStringLiteral("credentials"));
    service.cancelDeviceSignIn();
}

void JellyfinQuickConnectTest::authorizedCodeIsExchangedOnceThenSignsInLikePassword()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;
    beginQuickConnect(fixture);

    manager.responses.append(response(200, quickConnectResult(false)));
    manager.responses.append(response(200, quickConnectResult(true)));
    manager.responses.append(response(200, jellyfinLogin()));
    QTRY_VERIFY_WITH_TIMEOUT(service.isAuthenticated(), 3000);
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
    QCOMPARE(manager.countPath(kConnect), 2);
    QCOMPARE(manager.countPath(kExchange), 1);

    QByteArray initiateAuthorization;
    for (const RecordedRequest &request : manager.requests) {
        const QString path = request.request.url().path();
        if (path == kInitiate) {
            initiateAuthorization = request.request.rawHeader("Authorization");
        } else if (path == kConnect) {
            QCOMPARE(request.operation, QNetworkAccessManager::GetOperation);
            QCOMPARE(QUrlQuery(request.request.url()).queryItemValue(QStringLiteral("secret")),
                     QStringLiteral("qc-secret"));
            // The same device identity for every step.
            QCOMPARE(request.request.rawHeader("Authorization"), initiateAuthorization);
        } else if (path == kExchange) {
            QCOMPARE(request.operation, QNetworkAccessManager::PostOperation);
            QCOMPARE(QJsonDocument::fromJson(request.body).object()
                         .value(QStringLiteral("Secret")).toString(),
                     QStringLiteral("qc-secret"));
            QCOMPARE(request.request.rawHeader("Authorization"), initiateAuthorization);
        }
    }
    // Polling stopped with the sign-in.
    const qsizetype requests = manager.requests.size();
    QTest::qWait(12 * kTestSecondMs);
    QCOMPARE(manager.requests.size(), requests);

    // The same session a password login produces.
    FakeNetworkAccessManager passwordManager;
    passwordManager.responses = {response(200, jellyfinLogin())};
    HttpTransport passwordTransport(&passwordManager);
    JellyfinProviderAdapter passwordAdapter;
    AuthenticationService password(nullptr, &passwordTransport, &passwordAdapter);
    password.setProviderSelection(QStringLiteral("jellyfin"));
    password.authenticate(kJellyfinAddress, QStringLiteral("Alice"), QStringLiteral("password"));
    QTRY_VERIFY_WITH_TIMEOUT(password.isAuthenticated(), 1000);
    QCOMPARE(passwordManager.requests.first().request.rawHeader("Authorization"),
             initiateAuthorization);

    QCOMPARE(service.getServerUrl(), password.getServerUrl());
    QCOMPARE(service.getUserId(), password.getUserId());
    QCOMPARE(service.getUsername(), password.getUsername());
    QCOMPARE(service.getAccessToken(), QStringLiteral("jf-access"));
    QCOMPARE(service.getAccessToken(), password.getAccessToken());
    QCOMPARE(service.authenticationStep(), password.authenticationStep());
    QCOMPARE(service.activeConnectionId(), password.activeConnectionId());
    QCOMPARE(service.activeProviderKind(), ProviderKind::Jellyfin);
    QCOMPARE(service.createRequest(QStringLiteral("/Items")).rawHeader("Authorization"),
             password.createRequest(QStringLiteral("/Items")).rawHeader("Authorization"));
}

void JellyfinQuickConnectTest::quickConnectExpiresWhenServerOrDeadlineSaysSo()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;

    // The server forgot the request.
    beginQuickConnect(fixture);
    manager.responses.append(response(404, QByteArrayLiteral("Not Found")));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("expired"), 1000);
    QVERIFY(service.deviceSignIn().value(QStringLiteral("userCode")).toString().isEmpty());
    QVERIFY(!service.isAuthenticated());

    // The ten-minute local deadline passes while the server still says pending.
    beginQuickConnect(fixture, 1);
    for (int i = 0; i < 400; ++i) {
        manager.responses.append(response(200, quickConnectResult(false)));
    }
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("expired"), 3000);
    const qsizetype polls = manager.countPath(kConnect);
    QTest::qWait(30);
    QCOMPARE(manager.countPath(kConnect), polls);
    QCOMPARE(manager.countPath(kExchange), 0);

    // A new code can be requested after expiry.
    manager.responses = {response(200, quickConnectResult(false))};
    service.startDeviceSignIn(kJellyfinAddress);
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("waiting"), 1000);
    service.cancelDeviceSignIn();
}

void JellyfinQuickConnectTest::turningQuickConnectOffWithdrawsTheOffer()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;

    // Quick Connect is turned off while the code is shown: the code ends,
    // and the address is checked again, which now withdraws the offer.
    beginQuickConnect(fixture);
    const qsizetype enabledReads = manager.countPath(QStringLiteral("/QuickConnect/Enabled"));
    manager.responses.append(response(401, QByteArrayLiteral("Quick connect is disabled")));
    manager.responses.append(response(404, QByteArrayLiteral("Not Found")));
    manager.responses.append(response(200, QByteArrayLiteral("false")));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("expired"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(signInState(service), QStringLiteral("ready"), 1000);
    QCOMPARE(manager.countPath(QStringLiteral("/QuickConnect/Enabled")), enabledReads + 1);
    QVERIFY(!service.signInOptions().value(QStringLiteral("quickConnect")).toBool());
}

void JellyfinQuickConnectTest::failedExchangeEndsTheCode()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;

    // The exchange is refused: never retried, never signed in.
    beginQuickConnect(fixture);
    manager.responses.append(response(200, quickConnectResult(true)));
    manager.responses.append(response(401, QByteArray()));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("failed"), 1000);
    QCOMPARE(manager.countPath(kExchange), 1);
    QVERIFY(!service.deviceSignIn().value(QStringLiteral("errorMessage")).toString().isEmpty());
    QVERIFY(service.deviceSignIn().value(QStringLiteral("userCode")).toString().isEmpty());
    QVERIFY(!service.isAuthenticated());
    QVERIFY(service.getAccessToken().isEmpty());
    const qsizetype requests = manager.requests.size();
    QTest::qWait(12 * kTestSecondMs);
    QCOMPARE(manager.requests.size(), requests);

    // A transient failure is not replayed either: the secret is spent.
    beginQuickConnect(fixture);
    manager.responses.append(response(200, quickConnectResult(true)));
    manager.responses.append(response(503, QByteArray()));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("failed"), 1000);
    QCOMPARE(manager.countPath(kExchange), 2);

    // A success-shaped answer without a token is a failure, not a sign-in.
    beginQuickConnect(fixture);
    manager.responses.append(response(200, quickConnectResult(true)));
    manager.responses.append(response(200, QByteArrayLiteral(R"({"User":{"Id":"user-1"}})")));
    QTRY_COMPARE_WITH_TIMEOUT(deviceState(service), QStringLiteral("failed"), 1000);
    QVERIFY(!service.isAuthenticated());
    QCOMPARE(service.authenticationStep(), QStringLiteral("credentials"));
}

void JellyfinQuickConnectTest::backingOutStopsPollingWithoutACancelRequest()
{
    Fixture fixture;
    AuthenticationService &service = fixture.service;
    FakeNetworkAccessManager &manager = fixture.manager;
    // Slow server seconds leave room to back out between two polls.
    constexpr int secondMs = 50;
    beginQuickConnect(fixture, secondMs);
    manager.responses.append(response(200, quickConnectResult(false)));
    QTRY_COMPARE_WITH_TIMEOUT(manager.countPath(kConnect), 1, 1000);

    const qsizetype requests = manager.requests.size();
    service.cancelDeviceSignIn();
    QCOMPARE(deviceState(service), QStringLiteral("idle"));
    QVERIFY(service.deviceSignIn().value(QStringLiteral("userCode")).toString().isEmpty());
    QTest::qWait(12 * secondMs);
    // Jellyfin has no cancel endpoint: nothing is sent at all.
    QCOMPARE(manager.requests.size(), requests);
}

QTEST_MAIN(JellyfinQuickConnectTest)
#include "JellyfinQuickConnectTest.moc"
