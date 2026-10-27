#include "DeviceSignInSession.h"

#include "HttpTransport.h"
#include "../utils/BloomLogging.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <utility>

namespace {
// Every device request has its own deadline; the poll loop is bounded by the
// code's lifetime.
constexpr int kRequestTimeoutMs = 15000;
constexpr int kWithdrawTimeoutMs = 10000;
constexpr qint64 kMinimumIntervalSeconds = 1;
constexpr qint64 kMaximumIntervalSeconds = 60;
// RFC 8628 slow_down: add five seconds to the interval for every later poll.
constexpr qint64 kSlowDownSeconds = 5;
// Silo never holds a request past 30 minutes after start.
constexpr qint64 kMaximumLifetimeSeconds = 31 * 60;
constexpr int kMaximumConsecutiveFailures = 3;

QString stateName(DeviceSignInSession::State state)
{
    switch (state) {
    case DeviceSignInSession::State::Idle:
        return QStringLiteral("idle");
    case DeviceSignInSession::State::Starting:
        return QStringLiteral("starting");
    case DeviceSignInSession::State::Waiting:
        return QStringLiteral("waiting");
    case DeviceSignInSession::State::SigningIn:
        return QStringLiteral("signingIn");
    case DeviceSignInSession::State::Expired:
        return QStringLiteral("expired");
    case DeviceSignInSession::State::Denied:
        return QStringLiteral("denied");
    case DeviceSignInSession::State::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

bool isTransient(const NetworkError &error)
{
    return error.httpStatus == 0 || error.httpStatus == 408 || error.httpStatus >= 500;
}
} // namespace

DeviceSignInSession::DeviceSignInSession(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceSignInSession::poll);
    connect(&m_tickTimer, &QTimer::timeout, this, &DeviceSignInSession::tick);
}

DeviceSignInSession::~DeviceSignInSession()
{
    ++m_attempt;
    abortRequest();
}

void DeviceSignInSession::setSecondDurationForTesting(int milliseconds)
{
    m_secondMs = qMax(1, milliseconds);
}

qint64 DeviceSignInSession::durationMs(qint64 seconds) const
{
    return seconds * m_secondMs;
}

void DeviceSignInSession::ensureTransport()
{
    if (m_transport && m_transportManager == m_target.networkManager) {
        return;
    }
    m_transport = std::make_unique<HttpTransport>(m_target.networkManager);
    m_transportManager = m_target.networkManager;
    const IProviderRequestFactory *factory = m_target.requestFactory;
    m_transport->setUrlRedactor([factory](const QUrl &url) {
        return factory ? factory->redactedUrl(url) : url.toString(QUrl::RemoveQuery);
    });
}

void DeviceSignInSession::start(const Target &target, AuthorizedHandler onAuthorized)
{
    cancel();
    m_target = target;
    m_onAuthorized = std::move(onAuthorized);
    if (!m_target.networkManager || !m_target.requestFactory || !m_target.authenticator) {
        setState(State::Failed, tr("Sign-in with a code is not available."));
        return;
    }
    ensureTransport();
    setState(State::Starting);
    sendStart();
}

void DeviceSignInSession::sendStart()
{
    const auto request = m_target.authenticator->createDeviceAuthorizationRequest(
        m_target.context.deviceName, m_target.context.devicePlatform);
    if (!request || !request->isValid()) {
        finish(State::Failed, tr("This server can't sign in with a code."));
        return;
    }

    const quint64 attempt = ++m_attempt;
    // Each start opens a new request on the server, so it is never replayed.
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Never;
    options.retryPolicy.maxAttempts = 1;
    options.attemptTimeoutMs = kRequestTimeoutMs;
    const Target target = m_target;
    m_request = m_transport->sendWithRetry(
        this,
        request->endpoint,
        [target, endpoint = request->endpoint, body = request->body]() {
            return target.networkManager->post(
                target.requestFactory->createRequest(target.context, endpoint), body);
        },
        [this, attempt](QNetworkReply *reply) {
            if (attempt == m_attempt) {
                handleStarted(reply->readAll());
            }
        },
        [this, attempt](const NetworkError &error) {
            if (attempt != m_attempt) {
                return;
            }
            if (error.httpStatus == 429) {
                finish(State::Failed,
                       tr("Too many sign-in attempts. Wait a minute, then get a new code."));
            } else if (error.httpStatus == 404 || error.httpStatus == 503) {
                finish(State::Failed, tr("This server isn't offering sign-in with a code right now."));
            } else {
                finish(State::Failed,
                       error.userMessage.isEmpty()
                           ? tr("Could not reach the server.")
                           : error.userMessage);
            }
        },
        options);
}

void DeviceSignInSession::handleStarted(const QByteArray &body)
{
    m_request.clear();
    const auto authorization = m_target.authenticator->parseDeviceAuthorizationResponse(body);
    if (!authorization) {
        finish(State::Failed, tr("The server sent an unexpected reply. Try again."));
        return;
    }

    m_deviceCode = authorization->deviceCode;
    m_userCode = authorization->userCode;
    m_verificationUri = authorization->verificationUri;
    m_verificationUriComplete = authorization->verificationUriComplete;
    m_opened = false;
    m_consecutiveFailures = 0;
    m_intervalSeconds = qBound(kMinimumIntervalSeconds, authorization->intervalSeconds,
                               kMaximumIntervalSeconds);
    const qint64 lifetime = qMin(authorization->expiresInSeconds, kMaximumLifetimeSeconds);
    m_clock.start();
    m_deadlineMs = durationMs(lifetime);
    m_lifetimeCapMs = durationMs(kMaximumLifetimeSeconds);
    // Relative expires_in is authoritative; the server's absolute expiry only
    // tells us how far its clock is from ours, for later expiry moves.
    m_serverSkewMs = authorization->expiresAt.isValid()
        ? authorization->expiresAt.toMSecsSinceEpoch()
            - (QDateTime::currentMSecsSinceEpoch() + authorization->expiresInSeconds * 1000)
        : 0;

    qCInfo(lcAuth) << "Device sign-in code issued; polling every" << m_intervalSeconds << "s";
    setState(State::Waiting);
    m_tickTimer.start(m_secondMs);
    schedulePoll(m_intervalSeconds);
}

void DeviceSignInSession::schedulePoll(qint64 seconds)
{
    m_pollTimer.start(static_cast<int>(durationMs(qBound(kMinimumIntervalSeconds, seconds,
                                                         kMaximumIntervalSeconds))));
}

void DeviceSignInSession::poll()
{
    if (m_state != State::Waiting || m_deviceCode.isEmpty()) {
        return;
    }
    const auto request =
        m_target.authenticator->createDeviceAuthorizationPollRequest(m_deviceCode);
    if (!request || !request->isValid()) {
        finish(State::Failed, tr("This server can't sign in with a code."));
        return;
    }

    const quint64 attempt = ++m_attempt;
    // An approved answer carries the tokens exactly once, so a lost reply must
    // not be replayed by the transport; the next scheduled poll asks again.
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Never;
    options.retryPolicy.maxAttempts = 1;
    options.attemptTimeoutMs = kRequestTimeoutMs;
    const Target target = m_target;
    m_request = m_transport->sendWithRetry(
        this,
        request->endpoint,
        [target, endpoint = request->endpoint, body = request->body]() {
            return target.networkManager->post(
                target.requestFactory->createRequest(target.context, endpoint), body);
        },
        [this, attempt](QNetworkReply *reply) {
            if (attempt == m_attempt) {
                handlePoll(reply->readAll());
            }
        },
        [this, attempt](const NetworkError &error) {
            if (attempt == m_attempt) {
                handlePollError(error);
            }
        },
        options);
}

void DeviceSignInSession::handlePoll(const QByteArray &body)
{
    m_request.clear();
    if (m_state != State::Waiting) {
        return;
    }
    using Status = ProviderDeviceAuthorizationPoll::Status;
    const ProviderDeviceAuthorizationPoll result =
        m_target.authenticator->parseDeviceAuthorizationPollResponse(body);
    switch (result.status) {
    case Status::Pending: {
        m_consecutiveFailures = 0;
        const bool openedChanged = m_opened != result.opened;
        m_opened = result.opened;
        if (result.expiresAt.isValid()) {
            moveDeadline(result.expiresAt);
        }
        if (openedChanged) {
            emit changed();
        }
        schedulePoll(qMax(m_intervalSeconds, result.pollAfterSeconds));
        return;
    }
    case Status::Approved: {
        qCInfo(lcAuth) << "Device sign-in approved";
        m_pollTimer.stop();
        m_tickTimer.stop();
        m_deviceCode.clear();
        setState(State::SigningIn);
        // The owner may reset or restart this session from the handler.
        const AuthorizedHandler handler = m_onAuthorized;
        if (handler) {
            handler(result.authentication);
        }
        return;
    }
    case Status::Denied:
        finish(State::Denied);
        return;
    case Status::Expired:
    case Status::Canceled:
        finish(State::Expired);
        return;
    case Status::Consumed:
        finish(State::Failed, tr("This code was already used. Get a new code."));
        return;
    case Status::Invalid:
        withdraw();
        finish(State::Failed, tr("The server sent an unexpected reply. Try again."));
        return;
    }
}

void DeviceSignInSession::handlePollError(const NetworkError &error)
{
    m_request.clear();
    if (m_state != State::Waiting) {
        return;
    }
    if (error.httpStatus == 429) {
        // Polling too fast: slow down for the rest of this code's life.
        m_intervalSeconds = qMin(m_intervalSeconds + kSlowDownSeconds, kMaximumIntervalSeconds);
        qCInfo(lcAuth) << "Device sign-in poll rate limited; polling every"
                       << m_intervalSeconds << "s";
        schedulePoll(m_intervalSeconds);
        return;
    }
    if (error.httpStatus == 404) {
        // The server no longer knows this code.
        finish(State::Expired);
        return;
    }
    if (isTransient(error)) {
        ++m_consecutiveFailures;
        qCWarning(lcAuth) << "Device sign-in poll failed:" << error.code
                          << "attempt" << m_consecutiveFailures;
        if (m_consecutiveFailures >= kMaximumConsecutiveFailures) {
            withdraw();
            finish(State::Failed, tr("Lost contact with the server. Get a new code to try again."));
            return;
        }
        schedulePoll(m_intervalSeconds * (m_consecutiveFailures + 1));
        return;
    }
    withdraw();
    finish(State::Failed,
           error.userMessage.isEmpty() ? tr("Sign-in with a code failed.") : error.userMessage);
}

void DeviceSignInSession::moveDeadline(const QDateTime &serverExpiry)
{
    if (!m_clock.isValid()) {
        return;
    }
    const qint64 localExpiryMs = serverExpiry.toMSecsSinceEpoch() - m_serverSkewMs;
    const qint64 remainingRealMs = localExpiryMs - QDateTime::currentMSecsSinceEpoch();
    if (remainingRealMs <= 0) {
        return;
    }
    const qint64 remainingMs = remainingRealMs * m_secondMs / 1000;
    m_deadlineMs = qMin(m_clock.elapsed() + remainingMs, m_lifetimeCapMs);
    emit changed();
}

int DeviceSignInSession::secondsRemaining() const
{
    if (m_state != State::Waiting || !m_clock.isValid()) {
        return 0;
    }
    const qint64 remainingMs = m_deadlineMs - m_clock.elapsed();
    if (remainingMs <= 0) {
        return 0;
    }
    return static_cast<int>((remainingMs + m_secondMs - 1) / m_secondMs);
}

void DeviceSignInSession::tick()
{
    if (m_state != State::Waiting) {
        m_tickTimer.stop();
        return;
    }
    if (m_clock.elapsed() >= m_deadlineMs) {
        // Expired requests need no withdrawal.
        finish(State::Expired);
        return;
    }
    emit changed();
}

void DeviceSignInSession::finish(State state, const QString &message)
{
    ++m_attempt;
    abortRequest();
    m_pollTimer.stop();
    m_tickTimer.stop();
    m_deviceCode.clear();
    if (state == State::Expired || state == State::Denied || state == State::Failed) {
        // The code is no longer usable; don't keep showing it.
        m_userCode.clear();
        m_verificationUriComplete.clear();
        m_opened = false;
    }
    qCInfo(lcAuth) << "Device sign-in ended:" << stateName(state);
    setState(state, message);
}

void DeviceSignInSession::withdraw()
{
    if (m_deviceCode.isEmpty() || !m_target.canWithdraw || !m_transport
        || !m_target.authenticator) {
        return;
    }
    const auto request =
        m_target.authenticator->createDeviceAuthorizationCancelRequest(m_deviceCode);
    if (!request || !request->isValid()) {
        return;
    }
    // Withdrawing converges on repeat, so one transient retry is safe. The
    // reply is not needed: an unwithdrawn code still expires on its own.
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Idempotent;
    options.retryPolicy.maxAttempts = 2;
    options.attemptTimeoutMs = kWithdrawTimeoutMs;
    const Target target = m_target;
    m_transport->sendWithRetry(
        this,
        request->endpoint,
        [target, endpoint = request->endpoint, body = request->body]() {
            return target.networkManager->post(
                target.requestFactory->createRequest(target.context, endpoint), body);
        },
        [](QNetworkReply *) {},
        [](const NetworkError &) {},
        options);
}

void DeviceSignInSession::abortRequest()
{
    if (m_request) {
        m_request->cancel();
    }
    m_request.clear();
}

void DeviceSignInSession::clear()
{
    ++m_attempt;
    abortRequest();
    m_pollTimer.stop();
    m_tickTimer.stop();
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_verificationUriComplete.clear();
    m_opened = false;
    m_consecutiveFailures = 0;
    m_intervalSeconds = 0;
    m_onAuthorized = {};
}

void DeviceSignInSession::cancel()
{
    if (m_state == State::Idle) {
        return;
    }
    if (m_state == State::Waiting) {
        withdraw();
    }
    qCInfo(lcAuth) << "Device sign-in canceled";
    clear();
    setState(State::Idle);
}

void DeviceSignInSession::reset()
{
    if (m_state == State::Idle) {
        return;
    }
    clear();
    setState(State::Idle);
}

void DeviceSignInSession::fail(const QString &message)
{
    if (m_state != State::SigningIn) {
        return;
    }
    clear();
    setState(State::Failed, message);
}

void DeviceSignInSession::setState(State state, const QString &message)
{
    m_state = state;
    m_errorMessage = message;
    emit changed();
}

QVariantMap DeviceSignInSession::toVariantMap() const
{
    QString displayCode = m_userCode;
    displayCode.replace(QLatin1Char('-'), QLatin1Char(' '));
    return {
        {QStringLiteral("state"), stateName(m_state)},
        {QStringLiteral("userCode"), displayCode},
        // The prefilled link spares typing the code; fall back to the page.
        {QStringLiteral("verificationUrl"),
         m_verificationUriComplete.isEmpty() ? m_verificationUri : m_verificationUriComplete},
        {QStringLiteral("verificationPage"), m_verificationUri},
        {QStringLiteral("opened"), m_opened},
        {QStringLiteral("secondsRemaining"), secondsRemaining()},
        {QStringLiteral("errorMessage"), m_errorMessage},
    };
}
