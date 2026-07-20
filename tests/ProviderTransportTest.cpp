#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <cstring>

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
    void transportDoesNotRetryClientErrors();
    void transportSuppressesCancellationCallbacks();
    void transportEmitsUnauthorizedPolicy();
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
    const QNetworkRequest authenticated = factory.createRequest(context, QStringLiteral("/Users/user-1"));
    QCOMPARE(authenticated.url(), QUrl(QStringLiteral("https://media.example.test/Users/user-1")));
    QVERIFY(authenticated.rawHeader("Authorization").contains("Token=\"secret-token\""));
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
        QStringLiteral("https://user:password@media.example.test/Items/item-1?api_key=secret&width=480")));
    QVERIFY(!redacted.contains(QStringLiteral("secret")));
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
}

void ProviderTransportTest::transportRetriesTransientFailures()
{
    HttpTransport transport;
    int attempts = 0;
    int successes = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};

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

void ProviderTransportTest::transportDoesNotRetryClientErrors()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};

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

void ProviderTransportTest::transportSuppressesCancellationCallbacks()
{
    HttpTransport transport;
    int attempts = 0;
    int failures = 0;
    HttpRequestOptions options;
    options.retryPolicy = RetryPolicy{3, 0, true};

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

void ProviderTransportTest::transportEmitsUnauthorizedPolicy()
{
    HttpTransport transport;
    QSignalSpy unauthorizedSpy(&transport, &HttpTransport::unauthorized);
    int failures = 0;
    HttpRequestOptions options;
    options.retryEnabled = false;
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

void ProviderTransportTest::cancellationIsNotClassifiedAsTransient()
{
    QVERIFY(!ErrorHandler::isTransientError(QNetworkReply::OperationCanceledError));
}

QTEST_MAIN(ProviderTransportTest)
#include "ProviderTransportTest.moc"
