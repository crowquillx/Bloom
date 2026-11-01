#include <QtTest/QtTest>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/SessionService.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloProviderAdapter.h"

namespace {

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, int statusCode, QByteArray payload,
              QObject *parent)
        : QNetworkReply(parent)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (statusCode >= 400) {
            setError(statusCode == 401 ? AuthenticationRequiredError
                                       : (statusCode == 403 ? ContentAccessDenied
                                                            : UnknownServerError),
                     QStringLiteral("test network error"));
        }
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            if (!m_payload.isEmpty()) {
                emit readyRead();
            }
            emit finished();
        });
    }

    void abort() override {}
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

struct RecordedRequest {
    QNetworkAccessManager::Operation operation;
    QNetworkRequest request;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    QList<QPair<int, QByteArray>> responses;
    QList<RecordedRequest> requests;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *) override
    {
        requests.append({operation, request});
        const auto response = responses.isEmpty()
            ? qMakePair(500, QByteArray())
            : responses.takeFirst();
        return new FakeReply(request, response.first, response.second, this);
    }
};

QByteArray userDto(const char *policy)
{
    return QByteArray(R"({"Id":"user-1","Name":"Alice")") + policy + "}";
}

const QByteArray TwoDeviceSessions = QByteArrayLiteral(
    R"([{"Id":"session-own","DeviceId":"device-own","DeviceName":"htpc","Client":"Bloom"},)"
    R"({"Id":"session-other","DeviceId":"device-other","DeviceName":"Phone","Client":"Jellyfin Android"}])");

} // namespace

class SessionServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void validationRecordsJellyfinAdministratorFlag_data();
    void validationRecordsJellyfinAdministratorFlag();
    void nonAdministratorCannotRevokeOtherDevices();
    void administratorRevokesOtherDeviceThroughDeviceRoute();
    void serverRefusalExplainsAdministratorRequirement();
    void signOutClearsAdministratorFlag();
    void siloKeepsAuthenticationSessionRevocation();

private:
    static void restoreJellyfin(AuthenticationService &auth, FakeNetworkAccessManager &manager,
                                const QByteArray &user);
    static void loadSessions(SessionService &sessions, FakeNetworkAccessManager &manager);
};

void SessionServiceTest::restoreJellyfin(AuthenticationService &auth,
                                         FakeNetworkAccessManager &manager,
                                         const QByteArray &user)
{
    manager.responses.append({200, user});
    auth.restoreSession(QStringLiteral("https://jellyfin.example.test"),
                        QStringLiteral("user-1"),
                        QStringLiteral("access-token"),
                        QStringLiteral("Alice"));
    QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 1000);
    QCOMPARE(manager.requests.constLast().request.url().path(), QStringLiteral("/Users/user-1"));
}

void SessionServiceTest::loadSessions(SessionService &sessions,
                                      FakeNetworkAccessManager &manager)
{
    manager.responses.append({200, TwoDeviceSessions});
    sessions.fetchActiveSessions();
    QTRY_COMPARE_WITH_TIMEOUT(sessions.sessions().size(), 2, 1000);
    QVERIFY(!sessions.isLoading());
}

void SessionServiceTest::validationRecordsJellyfinAdministratorFlag_data()
{
    QTest::addColumn<QByteArray>("user");
    QTest::addColumn<int>("expected"); // -1 unknown, 0 false, 1 true
    QTest::addColumn<bool>("canRevoke");

    QTest::newRow("administrator")
        << userDto(R"(,"Policy":{"IsAdministrator":true})") << 1 << true;
    QTest::newRow("user")
        << userDto(R"(,"Policy":{"IsAdministrator":false})") << 0 << false;
    // Without a policy the server still enforces the route; keep the action.
    QTest::newRow("no-policy") << userDto("") << -1 << true;
}

void SessionServiceTest::validationRecordsJellyfinAdministratorFlag()
{
    QFETCH(QByteArray, user);
    QFETCH(int, expected);
    QFETCH(bool, canRevoke);

    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);

    restoreJellyfin(auth, manager, user);
    const std::optional<bool> flag = auth.accountIsAdministrator();
    QCOMPARE(flag.has_value() ? int(*flag) : -1, expected);
    QVERIFY(!sessions.authenticationSessionMode());
    QCOMPARE(sessions.canRevokeOtherSessions(), canRevoke);
}

void SessionServiceTest::nonAdministratorCannotRevokeOtherDevices()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);
    restoreJellyfin(auth, manager, userDto(R"(,"Policy":{"IsAdministrator":false})"));
    loadSessions(sessions, manager);
    const qsizetype requestCount = manager.requests.size();

    sessions.revokeSession(QStringLiteral("session-other"));
    QVERIFY(!sessions.isLoading());
    QVERIFY(sessions.errorString().contains(QStringLiteral("administrator")));

    sessions.revokeAllOtherSessions();
    QVERIFY(!sessions.isLoading());
    QVERIFY(sessions.errorString().contains(QStringLiteral("administrator")));

    // Neither a device deletion nor the bulk refresh reached the server.
    QTest::qWait(20);
    QCOMPARE(manager.requests.size(), requestCount);
    QCOMPARE(sessions.sessions().size(), 2);
}

void SessionServiceTest::administratorRevokesOtherDeviceThroughDeviceRoute()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);
    restoreJellyfin(auth, manager, userDto(R"(,"Policy":{"IsAdministrator":true})"));
    loadSessions(sessions, manager);

    manager.responses.append({204, QByteArray()});
    sessions.revokeSession(QStringLiteral("session-other"));
    QTRY_COMPARE_WITH_TIMEOUT(sessions.sessions().size(), 1, 1000);
    const RecordedRequest &revoke = manager.requests.constLast();
    QCOMPARE(revoke.operation, QNetworkAccessManager::DeleteOperation);
    QCOMPARE(revoke.request.url().path(), QStringLiteral("/Devices"));
    QCOMPARE(QUrlQuery(revoke.request.url()).queryItemValue(QStringLiteral("id")),
             QStringLiteral("device-other"));
    QVERIFY(sessions.errorString().isEmpty());
}

void SessionServiceTest::serverRefusalExplainsAdministratorRequirement()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);
    // Privileges were not reported, so the action stays and the server decides.
    restoreJellyfin(auth, manager, userDto(""));
    loadSessions(sessions, manager);

    manager.responses.append({403, QByteArray()});
    sessions.revokeSession(QStringLiteral("session-other"));
    QTRY_VERIFY_WITH_TIMEOUT(!sessions.errorString().isEmpty(), 1000);
    QVERIFY(sessions.errorString().contains(QStringLiteral("administrator")));
    QVERIFY(!sessions.isLoading());
    QVERIFY(auth.isAuthenticated());
    QCOMPARE(sessions.sessions().size(), 2);
}

void SessionServiceTest::signOutClearsAdministratorFlag()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    JellyfinProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);
    restoreJellyfin(auth, manager, userDto(R"(,"Policy":{"IsAdministrator":false})"));
    QVERIFY(!sessions.canRevokeOtherSessions());

    QSignalSpy changed(&sessions, &SessionService::canRevokeOtherSessionsChanged);
    auth.logout();
    QVERIFY(!auth.accountIsAdministrator().has_value());
    QVERIFY(changed.count() > 0);
}

void SessionServiceTest::siloKeepsAuthenticationSessionRevocation()
{
    FakeNetworkAccessManager manager;
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    AuthenticationService auth(nullptr, &transport, &adapter);
    SessionService sessions(&auth);

    // Silo lists and revokes authentication sessions; the Jellyfin
    // administrator rule does not apply.
    QVERIFY(sessions.authenticationSessionMode());
    QVERIFY(sessions.canRevokeOtherSessions());
}

QTEST_GUILESS_MAIN(SessionServiceTest)
#include "SessionServiceTest.moc"
