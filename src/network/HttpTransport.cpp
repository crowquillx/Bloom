#include "HttpTransport.h"

#include <QLoggingCategory>
#include <QNetworkRequest>
#include <QTimer>
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

void HttpTransport::cancelAll()
{
    const auto handles = findChildren<HttpRequestHandle *>(QString(), Qt::FindDirectChildrenOnly);
    for (HttpRequestHandle *handle : handles) {
        handle->cancel();
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
                 0);
    return handle;
}

void HttpTransport::startAttempt(const QPointer<HttpRequestHandle> &handle,
                                 const QPointer<QObject> &context,
                                 const QString &endpoint,
                                 const RequestFactory &requestFactory,
                                 const ResponseHandler &responseHandler,
                                 const FailureHandler &failureHandler,
                                 const HttpRequestOptions &options,
                                 int attemptNumber)
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
             failureHandler, options, attemptNumber, reply]() {
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
            if (options.unauthorizedPolicy != UnauthorizedPolicy::Ignore) {
                const bool defer = options.unauthorizedPolicy == UnauthorizedPolicy::DeferSessionExpiry;
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
                                responseHandler, failureHandler, options, attemptNumber]() {
                startAttempt(handle,
                             context,
                             endpoint,
                             requestFactory,
                             responseHandler,
                             failureHandler,
                             options,
                             attemptNumber + 1);
            });
            return;
        }

        if (failureHandler) {
            failureHandler(error);
        }
        handle->deleteLater();
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
