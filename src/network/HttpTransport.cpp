#include "HttpTransport.h"

#include <QLoggingCategory>
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
}

HttpTransport::HttpTransport(QNetworkAccessManager *networkManager, QObject *parent)
    : QObject(parent)
    , m_networkManager(networkManager ? networkManager : new QNetworkAccessManager(this))
{
}

void HttpTransport::setUrlRedactor(UrlRedactor redactor)
{
    m_urlRedactor = std::move(redactor);
}

void HttpTransport::setUnauthorizedRecovery(UnauthorizedRecovery recovery)
{
    if (!recovery && m_unauthorizedRecoveryInProgress) {
        ++m_unauthorizedRecoveryGeneration;
        m_unauthorizedRecoveryInProgress = false;
        const auto pending = std::exchange(
            m_pendingUnauthorizedRecoveries,
            QList<std::function<void(bool)>>{});
        for (const auto &callback : pending) {
            callback(false);
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
    ++m_unauthorizedRecoveryGeneration;
    m_unauthorizedRecoveryInProgress = false;
    const auto pending = std::exchange(
        m_pendingUnauthorizedRecoveries,
        QList<std::function<void(bool)>>{});
    for (const auto &callback : pending) {
        callback(false);
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
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    qCDebug(lcHttpTransport) << "Sending request"
                             << redactedEndpoint(endpoint, reply)
                             << "attempt" << (attemptNumber + 1)
                             << "of" << options.retryPolicy.maxRetries;

    connect(reply, &QNetworkReply::finished, context,
            [this, handle, context, endpoint, requestFactory, responseHandler,
             failureHandler, options, attemptNumber, authenticationRetried,
             authenticationEpoch, reply]() {
        if (!handle || !context) {
            return;
        }

        handle->m_reply.clear();

        if (handle->isCanceled()
            || reply->error() == QNetworkReply::OperationCanceledError) {
            handle->deleteLater();
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            if (responseHandler) {
                responseHandler(reply);
            }
            handle->deleteLater();
            return;
        }

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        NetworkError error = ErrorHandler::createError(reply, endpoint);

        if (httpStatus == 401) {
            error.code = 401;
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
                     failureHandler, options, attemptNumber, error](bool recovered) {
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
                    sessionError.userMessage =
                        tr("Session expired. Please log in again.");
                    if (failureHandler) {
                        failureHandler(sessionError);
                    }
                    handle->deleteLater();
                });
                return;
            }

            if (options.unauthorizedPolicy != UnauthorizedPolicy::Ignore) {
                const bool defer =
                    options.unauthorizedPolicy == UnauthorizedPolicy::DeferSessionExpiry;
                emit unauthorized(defer);
                error.userMessage = tr("Session expired. Please log in again.");
            }
        }

        const bool shouldRetry = !handle->isCanceled()
            && options.retryEnabled
            && options.retryPolicy.retryOnTransient
            && reply->error() != QNetworkReply::OperationCanceledError
            && ErrorHandler::isTransientError(reply->error())
            && !ErrorHandler::isClientError(httpStatus)
            && attemptNumber < options.retryPolicy.maxRetries - 1;

        if (shouldRetry) {
            const int delayMs = ErrorHandler::calculateBackoffDelay(
                attemptNumber, options.retryPolicy);
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

void HttpTransport::recoverUnauthorized(std::function<void(bool)> completion)
{
    m_pendingUnauthorizedRecoveries.append(std::move(completion));
    if (m_unauthorizedRecoveryInProgress) {
        return;
    }

    if (!m_unauthorizedRecovery) {
        const auto pending = std::exchange(
            m_pendingUnauthorizedRecoveries,
            QList<std::function<void(bool)>>{});
        for (const auto &callback : pending) {
            callback(false);
        }
        return;
    }

    m_unauthorizedRecoveryInProgress = true;
    const quint64 recoveryGeneration = ++m_unauthorizedRecoveryGeneration;
    const auto completed = std::make_shared<bool>(false);
    const QPointer<HttpTransport> guardedThis(this);
    m_unauthorizedRecovery(
        [guardedThis, completed, recoveryGeneration](bool recovered) {
        if (!guardedThis || std::exchange(*completed, true)
            || recoveryGeneration != guardedThis->m_unauthorizedRecoveryGeneration) {
            return;
        }

        guardedThis->m_unauthorizedRecoveryInProgress = false;
        if (recovered) {
            ++guardedThis->m_authenticationEpoch;
        }
        const auto pending = std::exchange(
            guardedThis->m_pendingUnauthorizedRecoveries,
            QList<std::function<void(bool)>>{});
        for (const auto &callback : pending) {
            callback(recovered);
        }
    });
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
