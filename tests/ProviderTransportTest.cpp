#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <cstring>
#include <functional>
#include <utility>

#include "network/HttpTransport.h"
#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"
#include "providers/jellyfin/JellyfinAuthenticator.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/jellyfin/JellyfinRequestFactory.h"

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

    void setResponseHeader(const QByteArray &name, const QByteArray &value)
    {
        setRawHeader(name, value);
    }

    void abort() override
    {
        if (isFinished()) {
            return;
        }
        setError(OperationCanceledError, QStringLiteral("canceled"));
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

class HangingReply final : public QNetworkReply
{
public:
    explicit HangingReply(const QNetworkRequest &request,
                          QObject *parent,
                          int statusCode = 0)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        if (statusCode > 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        }
        open(QIODevice::ReadOnly);
    }

    void abort() override
    {
        if (isFinished()) {
            return;
        }
        setError(OperationCanceledError, QStringLiteral("canceled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64) override
    {
        return -1;
    }
};

QNetworkRequest requestFor(const QString &path)
{
    return QNetworkRequest(QUrl(QStringLiteral("https://media.example.test") + path));
}

} // namespace

class ProviderTransportTest : public QObject
{
    Q_OBJECT

private slots:
    void jellyfinAdapterExposesProviderBoundaries();
    void jellyfinRequestFactoryBuildsHeaderWithAndWithoutToken();
    void jellyfinRequestFactoryNormalizesUrlAndRedactsSecrets();
    void jellyfinAuthenticatorOwnsLoginAndValidationWireContract();
    void transportRetriesTransientFailures();
    void transportRetriesRetryableHttpStatuses_data();
    void transportRetriesRetryableHttpStatuses();
    void transportDoesNotRetryClientErrors();
    void transportDoesNotReplayUnsafeMutation();
    void transportSuppressesCancellationCallbacks();
    void transportEnforcesAttemptDeadline();
    void transportRetriesExpiredIdempotentAttempt();
    void transportTreatsTimedOut401AsTimeout();
    void transportEmitsUnauthorizedPolicy();
    void transportCoalescesUnauthorizedRecovery();
    void transportCapsUnauthorizedRetryAtOne();
    void transportBoundsUnauthorizedRecovery();
    void transportTreatsRefreshFailureAsTerminal();
    void transportSeparatesNetworkAndHttpErrors();
    void retryAfterIsBounded();
    void cancellationIsNotClassifiedAsTransient();
};

void ProviderTransportTest::jellyfinAdapterExposesProviderBoundaries()
{
    JellyfinProviderAdapter adapter;
    QCOMPARE(adapter.providerKind(), ProviderKind::Jellyfin);
    QCOMPARE(adapter.protocolMode(), ProtocolMode::Native);
    QVERIFY(adapter.authenticator());
    QVERIFY(adapter.requestFactory());
}

void ProviderTransportTest::jellyfinRequestFactoryBuildsHeaderWithAndWithoutToken()
{
    JellyfinRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = QStringLiteral("https://media.example.test/");
    context.deviceId = QStringLiteral("device-1");

    const QNetworkRequest anonymous = factory.createRequest(context, QStringLiteral("/System/Info"));
    const QByteArray anonymousHeader = anonymous.rawHeader("Authorization");
    QVERIFY(anonymousHeader.startsWith("MediaBrowser "));
    QVERIFY(anonymousHeader.contains("DeviceId=\"device-1\""));
    QVERIFY(!anonymousHeader.contains("Token="));

    context.accessToken = QStringLiteral("secret-token");
    context.profileId = QStringLiteral("silo-profile-must-not-leak");
    context.profileToken = QStringLiteral("silo-profile-token-must-not-leak");
    const QNetworkRequest authenticated = factory.createRequest(context, QStringLiteral("/Users/user-1"));
    QCOMPARE(authenticated.url(), QUrl(QStringLiteral("https://media.example.test/Users/user-1")));
    QVERIFY(authenticated.rawHeader("Authorization").contains("Token=\"secret-token\""));
    QVERIFY(authenticated.rawHeader("Authorization").startsWith("MediaBrowser "));
    QVERIFY(!authenticated.hasRawHeader("X-Profile-Id"));
    QVERIFY(!authenticated.hasRawHeader("X-Profile-Token"));
}

void ProviderTransportTest::jellyfinRequestFactoryNormalizesUrlAndRedactsSecrets()
{
    JellyfinRequestFactory factory;
    ProviderRequestContext context;
    context.baseUrl = QStringLiteral("https://media.example.test///");
    context.deviceId = QStringLiteral("device-1");

    const QNetworkRequest request = factory.createRequest(context, QStringLiteral("Items/item-1"));
    QCOMPARE(request.url(), QUrl(QStringLiteral("https://media.example.test/Items/item-1")));

    const QString redacted = factory.redactedUrl(QUrl(
        QStringLiteral("https://user:password@media.example.test/Items/item-1?ApiKey=secret&api_key=legacy&width=480")));
    QVERIFY(!redacted.contains(QStringLiteral("secret")));
    QVERIFY(!redacted.contains(QStringLiteral("legacy")));
    QVERIFY(!redacted.contains(QStringLiteral("user")));
    QVERIFY(!redacted.contains(QStringLiteral("password")));
    QVERIFY(redacted.contains(QStringLiteral("%5BREDACTED%5D"))
            || redacted.contains(QStringLiteral("[REDACTED]")));
    QVERIFY(redacted.contains(QStringLiteral("width=480")));
}

void ProviderTransportTest::jellyfinAuthenticatorOwnsLoginAndValidationWireContract()
{
    JellyfinAuthenticator authenticator;
    const ProviderAuthenticationRequest request = authenticator.createLoginRequest(
        QStringLiteral("Alice"), QStringLiteral("password"));
    QCOMPARE(request.endpoint, QStringLiteral("/Users/AuthenticateByName"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("Username")).toString(), QStringLiteral("Alice"));
    QCOMPARE(body.value(QStringLiteral("Pw")).toString(), QStringLiteral("password"));
    QCOMPARE(authenticator.sessionValidationEndpoint(QStringLiteral("user-1")),
             QStringLiteral("/Users/user-1"));

    const QByteArray response = R"({"AccessToken":"token-1","User":{"Id":"user-1","Name":"Alice"}})";
    const ProviderAuthenticationResult result = authenticator.parseLoginResponse(response);
    QVERIFY(result.isValid());
    QCOMPARE(result.accessToken, QStringLiteral("token-1"));
    QCOMPARE(result.accountId, QStringLiteral("user-1"));
    QCOMPARE(result.username, QStringLiteral("Alice"));

    QVERIFY(!authenticator.createRefreshRequest(QStringLiteral("refresh-token")).has_value());
    QVERIFY(!authenticator.parseLoginResponse(QByteArrayLiteral("not-json")).isValid());
    QVERIFY(!authenticator.parseLoginResponse(
                 QByteArrayLiteral(R"({"AccessToken":"token-only","User":{}})"))
                 .isValid());
}

void ProviderTransportTest::transportRetriesTransientFailures()
{
    HttpTransport transport;
    int attempts = 0;
    int successes = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};
    options.retrySafety = RetrySafety::Idempotent;

    transport.sendWithRetry(
        this,
        QStringLiteral("/Items"),
        [&]() -> QNetworkReply * {
            ++attempts;
            const auto error = attempts < 3 ? QNetworkReply::TimeoutError
                                            : QNetworkReply::NoError;
            return new FakeReply(requestFor(QStringLiteral("/Items")), error,
                                 error == QNetworkReply::NoError ? 200 : 0,
                                 QByteArray(), &transport);
        },
        [&](QNetworkReply *) { ++successes; },
        [&](const NetworkError &) { ++failures; },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(successes, 1, 1000);
    QCOMPARE(attempts, 3);
    QCOMPARE(failures, 0);
}

void ProviderTransportTest::transportRetriesRetryableHttpStatuses_data()
{
    QTest::addColumn<int>("status");
    QTest::newRow("request-timeout") << 408;
    QTest::newRow("too-many-requests") << 429;
    QTest::newRow("internal-server-error") << 500;
    QTest::newRow("bad-gateway") << 502;
    QTest::newRow("service-unavailable") << 503;
    QTest::newRow("gateway-timeout") << 504;
}

void ProviderTransportTest::transportRetriesRetryableHttpStatuses()
{
    QFETCH(int, status);

    HttpTransport transport;
    int attempts = 0;
    int successes = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{2, 0, true, 0, 0.0};
    options.retrySafety = RetrySafety::Idempotent;

    transport.sendWithRetry(
        this,
        QStringLiteral("/retryable-status"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(
                requestFor(QStringLiteral("/retryable-status")),
                QNetworkReply::NoError,
                attempts == 1 ? status : 200,
                QByteArray(),
                &transport);
        },
        [&](QNetworkReply *) { ++successes; },
        [&](const NetworkError &) { ++failures; },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(successes, 1, 1000);
    QCOMPARE(attempts, 2);
    QCOMPARE(failures, 0);
}

void ProviderTransportTest::transportDoesNotRetryClientErrors()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};
    options.retrySafety = RetrySafety::Idempotent;

    transport.sendWithRetry(
        this,
        QStringLiteral("/missing"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(requestFor(QStringLiteral("/missing")),
                                 QNetworkReply::ContentNotFoundError,
                                 404,
                                 QByteArray(),
                                 &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &) { ++failures; },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(attempts, 1);
}

void ProviderTransportTest::transportDoesNotReplayUnsafeMutation()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    NetworkError received;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true, 0, 0.0};
    options.retrySafety = RetrySafety::Never;

    transport.sendWithRetry(
        this,
        QStringLiteral("/mutation"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(
                requestFor(QStringLiteral("/mutation")),
                QNetworkReply::NoError,
                503,
                QByteArray(),
                &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(attempts, 1);
    QCOMPARE(received.httpStatus, 503);
}

void ProviderTransportTest::transportSuppressesCancellationCallbacks()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};
    options.retrySafety = RetrySafety::Idempotent;

    QPointer<HttpRequestHandle> handle = transport.sendWithRetry(
        this,
        QStringLiteral("/cancel"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(requestFor(QStringLiteral("/cancel")),
                                 QNetworkReply::NoError,
                                 200,
                                 QByteArray(),
                                 &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &) { ++failures; },
        options);
    handle->cancel();

    QTRY_VERIFY_WITH_TIMEOUT(handle.isNull(), 1000);
    QCOMPARE(failures, 0);
    QCOMPARE(attempts, 1);
}

void ProviderTransportTest::transportEnforcesAttemptDeadline()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    NetworkError received;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{1, 0, true, 0, 0.0};
    options.retrySafety = RetrySafety::Idempotent;
    options.attemptTimeoutMs = 20;

    transport.sendWithRetry(
        this,
        QStringLiteral("/hang"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new HangingReply(
                requestFor(QStringLiteral("/hang")), &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(attempts, 1);
    QCOMPARE(received.networkErrorCode,
             static_cast<int>(QNetworkReply::TimeoutError));
    QCOMPARE(received.httpStatus, 0);
    QCOMPARE(received.code,
             static_cast<int>(QNetworkReply::TimeoutError));
    QVERIFY(received.userMessage.contains(
        QStringLiteral("timed out"), Qt::CaseInsensitive));
}

void ProviderTransportTest::transportRetriesExpiredIdempotentAttempt()
{
    HttpTransport transport;
    int attempts = 0;
    int successes = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{2, 0, true, 0, 0.0};
    options.retrySafety = RetrySafety::Idempotent;
    options.attemptTimeoutMs = 20;

    transport.sendWithRetry(
        this,
        QStringLiteral("/retry-timeout"),
        [&]() -> QNetworkReply * {
            ++attempts;
            if (attempts == 1) {
                return new HangingReply(
                    requestFor(QStringLiteral("/retry-timeout")), &transport);
            }
            return new FakeReply(
                requestFor(QStringLiteral("/retry-timeout")),
                QNetworkReply::NoError,
                200,
                QByteArray(),
                &transport);
        },
        [&](QNetworkReply *) { ++successes; },
        [&](const NetworkError &) { ++failures; },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(successes, 1, 1000);
    QCOMPARE(attempts, 2);
    QCOMPARE(failures, 0);
}

void ProviderTransportTest::transportTreatsTimedOut401AsTimeout()
{
    HttpTransport transport;
    QSignalSpy unauthorizedSpy(&transport, &HttpTransport::unauthorized);
    int refreshes = 0;
    int failures = 0;
    NetworkError received;
    transport.setUnauthorizedRecovery(
        [&](std::function<void(bool)>) { ++refreshes; });

    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{1, 0, true, 0, 0.0};
    options.retrySafety = RetrySafety::Idempotent;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    options.attemptTimeoutMs = 20;
    transport.sendWithRetry(
        this,
        QStringLiteral("/stalled-unauthorized"),
        [&]() -> QNetworkReply * {
            return new HangingReply(
                requestFor(QStringLiteral("/stalled-unauthorized")),
                &transport,
                401);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(refreshes, 0);
    QCOMPARE(unauthorizedSpy.count(), 0);
    QCOMPARE(received.code, static_cast<int>(QNetworkReply::TimeoutError));
    QCOMPARE(received.networkErrorCode,
             static_cast<int>(QNetworkReply::TimeoutError));
    QCOMPARE(received.httpStatus, 401);
}

void ProviderTransportTest::transportEmitsUnauthorizedPolicy()
{
    HttpTransport transport;
    QSignalSpy unauthorizedSpy(&transport, &HttpTransport::unauthorized);
    int failures = 0;
    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::DeferSessionExpiry;

    transport.sendWithRetry(
        this,
        QStringLiteral("/Sessions"),
        [&]() -> QNetworkReply * {
            return new FakeReply(requestFor(QStringLiteral("/Sessions")),
                                 QNetworkReply::AuthenticationRequiredError,
                                 401,
                                 QByteArray(),
                                 &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            QCOMPARE(error.code, 401);
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(unauthorizedSpy.count(), 1);
    QCOMPARE(unauthorizedSpy.first().first().toBool(), true);
}

void ProviderTransportTest::transportCoalescesUnauthorizedRecovery()
{
    HttpTransport transport;
    int refreshes = 0;
    int firstAttempts = 0;
    int secondAttempts = 0;
    int successes = 0;
    int failures = 0;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;
    std::function<void(bool)> finishRefresh;

    transport.setUnauthorizedRecovery([&](std::function<void(bool)> completion) {
        ++refreshes;
        finishRefresh = std::move(completion);
    });

    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    const auto factory = [&](int &attempts, const QString &path) -> QNetworkReply * {
        ++attempts;
        QNetworkRequest request = requestFor(path);
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer access-token"));
        request.setRawHeader("X-Test-Method", QByteArrayLiteral("POST"));
        request.setRawHeader("X-Profile-Id", QByteArrayLiteral("profile-1"));
        request.setRawHeader("X-Profile-Token", QByteArrayLiteral("profile-token"));
        const QByteArray body = QByteArrayLiteral(R"({"operation":"replay"})");
        requests.append(request);
        bodies.append(body);
        const bool unauthorized = attempts == 1;
        return new FakeReply(request,
                             unauthorized ? QNetworkReply::AuthenticationRequiredError
                                          : QNetworkReply::NoError,
                             unauthorized ? 401 : 200,
                             QByteArray(),
                             &transport);
    };
    const auto success = [&](QNetworkReply *) { ++successes; };
    const auto failure = [&](const NetworkError &) { ++failures; };

    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/first"),
        [&]() { return factory(firstAttempts, QStringLiteral("/api/v1/first")); },
        success,
        failure,
        options);
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/second"),
        [&]() { return factory(secondAttempts, QStringLiteral("/api/v1/second")); },
        success,
        failure,
        options);

    QTRY_COMPARE_WITH_TIMEOUT(refreshes, 1, 1000);
    QVERIFY(finishRefresh);
    finishRefresh(true);
    QTRY_COMPARE_WITH_TIMEOUT(successes, 2, 1000);
    QCOMPARE(failures, 0);
    QCOMPARE(firstAttempts, 2);
    QCOMPARE(requests.size(), 4);
    QCOMPARE(bodies.at(0), bodies.at(2));
    QCOMPARE(bodies.at(1), bodies.at(3));
    QCOMPARE(secondAttempts, 2);
    QCOMPARE(requests.at(0).rawHeader("Authorization"),
             requests.at(2).rawHeader("Authorization"));
    QCOMPARE(requests.at(0).rawHeader("X-Test-Method"),
             requests.at(2).rawHeader("X-Test-Method"));
    QCOMPARE(requests.at(1).rawHeader("X-Profile-Id"),
             requests.at(3).rawHeader("X-Profile-Id"));
    QCOMPARE(requests.at(1).rawHeader("X-Profile-Token"),
             requests.at(3).rawHeader("X-Profile-Token"));
}

void ProviderTransportTest::transportCapsUnauthorizedRetryAtOne()
{
    HttpTransport transport;
    int refreshes = 0;
    int attempts = 0;
    int failures = 0;
    std::function<void(bool)> finishRefresh;
    transport.setUnauthorizedRecovery([&](std::function<void(bool)> completion) {
        ++refreshes;
        finishRefresh = std::move(completion);
    });

    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Idempotent;
    options.retryPolicy = RetryPolicy{5, 0, true};
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/protected"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(
                requestFor(QStringLiteral("/api/v1/protected")),
                QNetworkReply::AuthenticationRequiredError,
                401,
                QByteArray(),
                &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            QCOMPARE(error.code, 401);
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(refreshes, 1, 1000);
    QVERIFY(finishRefresh);
    finishRefresh(true);
    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(attempts, 2);
    QCOMPARE(refreshes, 1);
}

void ProviderTransportTest::transportBoundsUnauthorizedRecovery()
{
    HttpTransport transport;
    QSignalSpy unauthorizedSpy(&transport, &HttpTransport::unauthorized);
    int refreshes = 0;
    int failures = 0;
    int successes = 0;
    NetworkError received;
    std::function<void(bool)> finishRefresh;
    transport.setUnauthorizedRecovery(
        [&](std::function<void(bool)> completion) {
            ++refreshes;
            finishRefresh = std::move(completion);
        });

    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    options.unauthorizedRecoveryTimeoutMs = 20;
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/recovery-timeout"),
        [&]() -> QNetworkReply * {
            return new FakeReply(
                requestFor(QStringLiteral("/api/v1/recovery-timeout")),
                QNetworkReply::AuthenticationRequiredError,
                401,
                QByteArray(),
                &transport);
        },
        [&](QNetworkReply *) { ++successes; },
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(refreshes, 1);
    QCOMPARE(successes, 0);
    QCOMPARE(unauthorizedSpy.count(), 1);
    QCOMPARE(received.networkErrorCode,
             static_cast<int>(QNetworkReply::TimeoutError));
    QVERIFY(received.userMessage.contains(
        QStringLiteral("recovery timed out"), Qt::CaseInsensitive));

    QVERIFY(finishRefresh);
    finishRefresh(true);
    QTest::qWait(30);
    QCOMPARE(failures, 1);
    QCOMPARE(successes, 0);
}

void ProviderTransportTest::transportTreatsRefreshFailureAsTerminal()
{
    HttpTransport transport;
    int refreshes = 0;
    int attempts = 0;
    int failures = 0;
    std::function<void(bool)> finishRefresh;
    transport.setUnauthorizedRecovery([&](std::function<void(bool)> completion) {
        ++refreshes;
        finishRefresh = std::move(completion);
    });

    HttpRequestOptions options;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;
    transport.sendWithRetry(
        this,
        QStringLiteral("/api/v1/protected"),
        [&]() -> QNetworkReply * {
            ++attempts;
            return new FakeReply(
                requestFor(QStringLiteral("/api/v1/protected")),
                QNetworkReply::NoError,
                401,
                QByteArray(),
                &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            QCOMPARE(error.code, 401);
            ++failures;
        },
        options);

    QTRY_COMPARE_WITH_TIMEOUT(refreshes, 1, 1000);
    QVERIFY(finishRefresh);
    finishRefresh(false);
    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(attempts, 1);
    QCOMPARE(refreshes, 1);
}

void ProviderTransportTest::transportSeparatesNetworkAndHttpErrors()
{
    HttpTransport transport;
    int failures = 0;
    NetworkError received;

    transport.sendWithRetry(
        this,
        QStringLiteral("/provider-error"),
        [&]() -> QNetworkReply * {
            return new FakeReply(
                requestFor(QStringLiteral("/provider-error")),
                QNetworkReply::UnknownServerError,
                503,
                QByteArrayLiteral(
                    R"({"Message":"Provider failure","ErrorCode":"ProviderBusy"})"),
                &transport);
        },
        [](QNetworkReply *) {},
        [&](const NetworkError &error) {
            received = error;
            ++failures;
        });

    QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 1000);
    QCOMPARE(received.code, 503);
    QCOMPARE(received.httpStatus, 503);
    QCOMPARE(received.networkErrorCode,
             static_cast<int>(QNetworkReply::UnknownServerError));
    QCOMPARE(received.providerErrorCode, QStringLiteral("ProviderBusy"));
    QCOMPARE(received.userMessage, QStringLiteral("Provider failure"));
    QVERIFY(!received.responseBody.isEmpty());
}

void ProviderTransportTest::retryAfterIsBounded()
{
    HttpTransport transport;
    auto *reply = new FakeReply(
        requestFor(QStringLiteral("/retry-after")),
        QNetworkReply::NoError,
        429,
        QByteArray(),
        &transport);
    reply->setResponseHeader(
        QByteArrayLiteral("Retry-After"), QByteArrayLiteral("7"));

    QCOMPARE(ErrorHandler::retryAfterDelayMs(reply, 250), 250);
    const QByteArray futureHttpDate = QLocale::c()
        .toString(QDateTime::currentDateTimeUtc().addSecs(60),
                  QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"))
        .toLatin1();
    reply->setResponseHeader(
        QByteArrayLiteral("Retry-After"), futureHttpDate);
    QCOMPARE(ErrorHandler::retryAfterDelayMs(reply, 250), 250);
    reply->setResponseHeader(
        QByteArrayLiteral("Retry-After"), QByteArrayLiteral("invalid"));
    QCOMPARE(ErrorHandler::retryAfterDelayMs(reply, 250), -1);
}


void ProviderTransportTest::cancellationIsNotClassifiedAsTransient()
{
    QVERIFY(!ErrorHandler::isTransientError(QNetworkReply::OperationCanceledError));
}

QTEST_MAIN(ProviderTransportTest)
#include "ProviderTransportTest.moc"
