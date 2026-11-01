#pragma once

#include "providers/IProviderAuthenticator.h"
#include "providers/IProviderRequestFactory.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>

class HttpRequestHandle;
class HttpTransport;
class QNetworkAccessManager;
class QNetworkReply;
struct HttpRequestOptions;
struct NetworkError;

/**
 * @brief One device authorization ("sign in with a code") for AuthenticationService.
 *
 * Starts the provider's device authorization, polls at the server's pace
 * until the request is decided or its deadline passes, and withdraws a live
 * request when abandoned. When the provider authorizes without tokens
 * (Jellyfin Quick Connect), the device code is exchanged once for a login
 * response. Approved tokens go to the owner, which signs in exactly as after
 * a password login. The device code stays in memory only.
 * Requests run on a private transport so a session change on the shared
 * transport cannot silently drop a poll.
 */
class DeviceSignInSession final : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Starting,
        Waiting,
        SigningIn,
        Expired,
        Denied,
        Failed
    };

    struct Target {
        QNetworkAccessManager *networkManager = nullptr;
        const IProviderRequestFactory *requestFactory = nullptr;
        const IProviderAuthenticator *authenticator = nullptr;
        // Unauthenticated context: base URL and device headers only.
        ProviderRequestContext context;
        bool canWithdraw = false;
        // Flow shown to QML: "code" (Silo device login) or "quickConnect".
        QString method = QStringLiteral("code");
    };
    using AuthorizedHandler = std::function<void(const ProviderAuthenticationResult &)>;

    explicit DeviceSignInSession(QObject *parent = nullptr);
    ~DeviceSignInSession() override;

    /// Replaces any current flow (withdrawing its code) and requests a new code.
    void start(const Target &target, AuthorizedHandler onAuthorized);
    /// Abandons the flow; a live code is withdrawn on the server, best effort.
    void cancel();
    /// Forgets a finished flow without contacting the server.
    void reset();
    /// The sign-in that followed an approval failed.
    void fail(const QString &message);

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }
    int secondsRemaining() const;
    /// QML projection: state, method, userCode, verificationUrl,
    /// verificationPage, opened, secondsRemaining, errorMessage.
    QVariantMap toVariantMap() const;

    /// Length of one server "second" in milliseconds; tests shorten it.
    void setSecondDurationForTesting(int milliseconds);

signals:
    void changed();

private:
    void ensureTransport();
    void sendStart();
    void handleStarted(const QByteArray &body);
    void schedulePoll(qint64 seconds);
    void poll();
    void handlePoll(const QByteArray &body);
    void handlePollError(const NetworkError &error);
    void exchange();
    void handleExchanged(const QByteArray &body);
    void signIn(const ProviderAuthenticationResult &authentication);
    void moveDeadline(const QDateTime &serverExpiry);
    void tick();
    void finish(State state, const QString &message = {});
    void withdraw();
    void abortRequest();
    HttpRequestHandle *send(const ProviderAuthenticationRequest &request,
                            const HttpRequestOptions &options,
                            std::function<void(QNetworkReply *)> onReply,
                            std::function<void(const NetworkError &)> onError);
    void clear();
    void setState(State state, const QString &message = {});
    qint64 durationMs(qint64 seconds) const;

    std::unique_ptr<HttpTransport> m_transport;
    QNetworkAccessManager *m_transportManager = nullptr;
    Target m_target;
    AuthorizedHandler m_onAuthorized;
    QPointer<HttpRequestHandle> m_request;
    // Bumped on every start/stop so stale replies are ignored.
    quint64 m_attempt = 0;

    State m_state = State::Idle;
    QString m_errorMessage;
    QString m_deviceCode;
    QString m_userCode;
    QString m_verificationUri;
    QString m_verificationUriComplete;
    bool m_opened = false;
    qint64 m_intervalSeconds = 0;
    int m_consecutiveFailures = 0;

    // Deadline on a monotonic clock; the server's absolute expiry is mapped
    // onto it through the skew measured at start.
    QElapsedTimer m_clock;
    qint64 m_deadlineMs = 0;
    qint64 m_lifetimeCapMs = 0;
    qint64 m_serverSkewMs = 0;
    QTimer m_pollTimer;
    QTimer m_tickTimer;
    int m_secondMs = 1000;
};
