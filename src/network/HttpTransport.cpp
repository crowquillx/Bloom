#include "HttpTransport.h"

#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <memory>
#include <utility>
#include <QUrl>

Q_LOGGING_CATEGORY(lcHttpTransport, "bloom.network.transport")

HttpRequestHandle::HttpRequestHandle(QObject *parent)
    : QObject(parent)
{
}

void HttpRequestHandle::cancel()
{
    if (m_canceled) {
        return;
    }
    m_canceled = true;
    if (m_reply) {
        m_reply->abort();
    } else {
        deleteLater();
    }
}

HttpTransport::HttpTransport(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    initializeRecoveryTimer();
}

HttpTransport::HttpTransport(QNetworkAccessManager *networkManager, QObject *parent)
    : QObject(parent)
    , m_networkManager(networkManager ? networkManager : new QNetworkAccessManager(this))
{
    initializeRecoveryTimer();
}

void HttpTransport::initializeRecoveryTimer()
{
    m_unauthorizedRecoveryTimer.setSingleShot(true);
    connect(&m_unauthorizedRecoveryTimer, &QTimer::timeout, this, [this]() {
        finishUnauthorizedRecovery(
            m_unauthorizedRecoveryGeneration, false, true);
    });
}

void HttpTransport::setUrlRedactor(UrlRedactor redactor)
{
    m_urlRedactor = std::move(redactor);
}

void HttpTransport::setUnauthorizedRecovery(UnauthorizedRecovery recovery)
{
    // A provider/session switch must not let an old refresh replay requests
    // with credentials belonging to the previous provider.
    if (m_unauthorizedRecoveryInProgress) {
        m_unauthorizedRecoveryTimer.stop();
        ++m_unauthorizedRecoveryGeneration;
        m_unauthorizedRecoveryInProgress = false;
        const auto pending = std::exchange(
            m_pendingUnauthorizedRecoveries,
            QList<std::function<void(bool, bool)>>{});
        for (const auto &callback : pending) {
            callback(false, false);
        }
    }
    m_unauthorizedRecovery = std::move(recovery);
}

void HttpTransport::cancelAll()
{
    const auto handles = findChildren<HttpRequestHandle *>(QString(), Qt::FindDirectChildrenOnly);
    for (HttpRequestHandle *handle : handles) {
        handle->cancel();
    }
    m_unauthorizedRecoveryTimer.stop();
    ++m_unauthorizedRecoveryGeneration;
    m_unauthorizedRecoveryInProgress = false;
    const auto pending = std::exchange(
        m_pendingUnauthorizedRecoveries,
        QList<std::function<void(bool, bool)>>{});
    for (const auto &callback : pending) {
        callback(false, false);
    }
}

HttpRequestHandle *HttpTransport::sendWithRetry(QObject *context,
                                                const QString &endpoint,
                                                RequestFactory requestFactory,
                                                ResponseHandler responseHandler,
                                                FailureHandler failureHandler,
                                                HttpRequestOptions options)
{
    auto *handle = new HttpRequestHandle(this);
    const QPointer<QObject> guardedContext(context ? context : this);
    if (context) {
        connect(context, &QObject::destroyed, handle, [handle]() {
            handle->cancel();
            handle->deleteLater();
        });
    }

    startAttempt(handle,
                 guardedContext,
                 endpoint,
                 requestFactory,
                 responseHandler,
                 failureHandler,
                 options,
                 0,
                 false,
                 m_authenticationEpoch);
    return handle;
}

void HttpTransport::startAttempt(const QPointer<HttpRequestHandle> &handle,
                                 const QPointer<QObject> &context,
                                 const QString &endpoint,
                                 const RequestFactory &requestFactory,
                                 const ResponseHandler &responseHandler,
                                 const FailureHandler &failureHandler,
                                 const HttpRequestOptions &options,
                                 int attemptNumber,
                                 bool authenticationRetried,
                                 quint64 authenticationEpoch)
{
    if (!handle || !context || handle->isCanceled()) {
        return;
    }

    QNetworkReply *reply = requestFactory ? requestFactory() : nullptr;
    if (!reply) {
        NetworkError error;
        error.code = -1;
        error.networkErrorCode = -1;
        error.endpoint = endpoint;
        error.userMessage = tr("Unable to create network request.");
        error.technicalDetails = QStringLiteral("Request factory returned no reply");
        if (failureHandler) {
            failureHandler(error);
        }
        handle->deleteLater();
        return;
    }

    handle->m_reply = reply;
    const auto attemptTimedOut = std::make_shared<bool>(false);
    auto *attemptTimer = new QTimer(reply);
    attemptTimer->setSingleShot(true);
    if (options.attemptTimeoutMs > 0) {
        attemptTimer->start(options.attemptTimeoutMs);
        connect(attemptTimer, &QTimer::timeout, reply,
                [attemptTimedOut, reply]() {
            if (reply->isFinished()) {
                return;
            }
            *attemptTimedOut = true;
            reply->abort();
        });
    }
    connect(reply, &QNetworkReply::finished, attemptTimer, &QTimer::stop);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    const int maxAttempts = qMax(1, options.retryPolicy.maxAttempts);
    qCDebug(lcHttpTransport) << "Sending request"
                             << redactedEndpoint(endpoint, reply)
                             << "attempt" << (attemptNumber + 1)
                             << "of" << maxAttempts;

    connect(reply, &QNetworkReply::finished, context,
            [this, handle, context, endpoint, requestFactory, responseHandler,
             failureHandler, options, attemptNumber, authenticationRetried,
             authenticationEpoch, reply, attemptTimedOut, maxAttempts]() {
        if (!handle || !context) {
            return;
        }

        handle->m_reply.clear();

        if (handle->isCanceled()
            || (reply->error() == QNetworkReply::OperationCanceledError
                && !*attemptTimedOut)) {
            handle->deleteLater();
            return;
        }

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool isUnauthorized = httpStatus == 401;
        const bool isHttpFailure = httpStatus >= 400;

        // HTTP failures take precedence even when a custom network manager or
        // test double reports QNetworkReply::NoError.
        if (reply->error() == QNetworkReply::NoError && !isHttpFailure) {
            if (responseHandler) {
                responseHandler(reply);
            }
            handle->deleteLater();
            return;
        }

        NetworkError error = ErrorHandler::createError(reply, endpoint);
        if (*attemptTimedOut) {
            error.networkErrorCode =
                static_cast<int>(QNetworkReply::TimeoutError);
            if (error.httpStatus <= 0) {
                error.code = error.networkErrorCode;
            }
            error.userMessage = tr("Request timed out. Please try again.");
            error.technicalDetails =
                QStringLiteral("Per-attempt deadline exceeded (%1 ms)")
                    .arg(options.attemptTimeoutMs);
        }

        if (isUnauthorized) {
            if (options.unauthorizedPolicy != UnauthorizedPolicy::Ignore
                && !authenticationRetried
                && authenticationEpoch < m_authenticationEpoch) {
                startAttempt(handle,
                             context,
                             endpoint,
                             requestFactory,
                             responseHandler,
                             failureHandler,
                             options,
                             attemptNumber,
                             true,
                             m_authenticationEpoch);
                return;
            }
            if (options.unauthorizedPolicy != UnauthorizedPolicy::Ignore
                && !authenticationRetried && m_unauthorizedRecovery) {
                recoverUnauthorized(
                    [this, handle, context, endpoint, requestFactory, responseHandler,
                     failureHandler, options, attemptNumber, error](
                        bool recovered, bool recoveryTimedOut) {
                    if (!handle || !context || handle->isCanceled()) {
                        return;
                    }
                    if (recovered) {
                        startAttempt(handle,
                                     context,
                                     endpoint,
                                     requestFactory,
                                     responseHandler,
                                     failureHandler,
                                     options,
                                     attemptNumber,
                                     true,
                                     m_authenticationEpoch);
                        return;
                    }

                    const bool defer =
                        options.unauthorizedPolicy == UnauthorizedPolicy::DeferSessionExpiry;
                    emit unauthorized(defer);
                    NetworkError sessionError = error;
                    if (recoveryTimedOut) {
                        sessionError.networkErrorCode =
                            static_cast<int>(QNetworkReply::TimeoutError);
                        sessionError.userMessage =
                            tr("Session recovery timed out. Please log in again.");
                        sessionError.technicalDetails =
                            QStringLiteral(
                                "Unauthorized recovery deadline exceeded");
                    } else {
                        sessionError.userMessage =
                            tr("Session expired. Please log in again.");
                    }
                    if (failureHandler) {
                        failureHandler(sessionError);
                    }
                    handle->deleteLater();
                },
                options.unauthorizedRecoveryTimeoutMs);
                return;
            }

            if (options.unauthorizedPolicy != UnauthorizedPolicy::Ignore) {
                const bool defer =
                    options.unauthorizedPolicy == UnauthorizedPolicy::DeferSessionExpiry;
                emit unauthorized(defer);
                error.userMessage = tr("Session expired. Please log in again.");
            }
        }

        const bool retryIsSafe =
            options.retrySafety == RetrySafety::Idempotent
            || options.retrySafety
                == RetrySafety::ReplayableWithIdempotencyMechanism;
        const bool retryableFailure =
            ErrorHandler::isRetryableHttpStatus(httpStatus)
            || ErrorHandler::isTransientError(
                static_cast<QNetworkReply::NetworkError>(
                    error.networkErrorCode));
        const bool shouldRetry = !handle->isCanceled()
            && retryIsSafe
            && options.retryPolicy.retryOnRetryableFailure
            && (!isUnauthorized)
            && retryableFailure
            && attemptNumber + 1 < maxAttempts;

        if (shouldRetry) {
            int delayMs = ErrorHandler::calculateBackoffDelay(
                attemptNumber, options.retryPolicy);
            const int retryAfterMs = ErrorHandler::retryAfterDelayMs(
                reply, options.retryPolicy.maxDelayMs);
            if (retryAfterMs >= 0) {
                delayMs = qMax(delayMs, retryAfterMs);
            }
            qCInfo(lcHttpTransport) << "Retrying request"
                                    << redactedEndpoint(endpoint, reply)
                                    << "in" << delayMs << "ms";
            QTimer::singleShot(delayMs, this,
                               [this, handle, context, endpoint, requestFactory,
                                responseHandler, failureHandler, options, attemptNumber,
                                authenticationRetried, authenticationEpoch]() {
                startAttempt(handle,
                             context,
                             endpoint,
                             requestFactory,
                             responseHandler,
                             failureHandler,
                             options,
                             attemptNumber + 1,
                             authenticationRetried,
                             authenticationEpoch);
            });
            return;
        }

        if (failureHandler) {
            failureHandler(error);
        }
        handle->deleteLater();
    });
}

void HttpTransport::recoverUnauthorized(
    std::function<void(bool, bool)> completion,
    int timeoutMs)
{
    m_pendingUnauthorizedRecoveries.append(std::move(completion));
    if (m_unauthorizedRecoveryInProgress) {
        const int remainingMs = m_unauthorizedRecoveryTimer.remainingTime();
        if (timeoutMs > 0
            && (remainingMs < 0 || timeoutMs < remainingMs)) {
            m_unauthorizedRecoveryTimer.start(timeoutMs);
        }
        return;
    }

    if (!m_unauthorizedRecovery) {
        const auto pending = std::exchange(
            m_pendingUnauthorizedRecoveries,
            QList<std::function<void(bool, bool)>>{});
        for (const auto &callback : pending) {
            callback(false, false);
        }
        return;
    }

    m_unauthorizedRecoveryInProgress = true;
    const quint64 recoveryGeneration = ++m_unauthorizedRecoveryGeneration;
    if (timeoutMs > 0) {
        m_unauthorizedRecoveryTimer.start(timeoutMs);
    }
    const QPointer<HttpTransport> guardedThis(this);
    m_unauthorizedRecovery(
        [guardedThis, recoveryGeneration](bool recovered) {
        if (!guardedThis) {
            return;
        }
        guardedThis->finishUnauthorizedRecovery(
            recoveryGeneration, recovered, false);
    });
}

void HttpTransport::finishUnauthorizedRecovery(quint64 generation,
                                               bool recovered,
                                               bool timedOut)
{
    if (!m_unauthorizedRecoveryInProgress
        || generation != m_unauthorizedRecoveryGeneration) {
        return;
    }

    m_unauthorizedRecoveryTimer.stop();
    m_unauthorizedRecoveryInProgress = false;
    if (recovered) {
        ++m_authenticationEpoch;
    }
    const auto pending = std::exchange(
        m_pendingUnauthorizedRecoveries,
        QList<std::function<void(bool, bool)>>{});
    for (const auto &callback : pending) {
        callback(recovered, timedOut);
    }
}

QString HttpTransport::redactedEndpoint(const QString &endpoint, const QNetworkReply *reply) const
{
    const QUrl url = reply ? reply->request().url() : QUrl(endpoint);
    if (m_urlRedactor) {
        return m_urlRedactor(url);
    }

    QUrl redacted = url;
    redacted.setUserInfo(QString());
    return redacted.toString(QUrl::FullyEncoded);
}
