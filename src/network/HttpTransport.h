#pragma once

#include "network/Types.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <functional>

class HttpRequestHandle final : public QObject
{
public:
    explicit HttpRequestHandle(QObject *parent = nullptr);

    void cancel();
    bool isCanceled() const { return m_canceled; }

private:
    friend class HttpTransport;

    bool m_canceled = false;
    QPointer<QNetworkReply> m_reply;
};

enum class UnauthorizedPolicy {
    Ignore,
    ExpireSession,
    DeferSessionExpiry
};

struct HttpRequestOptions {
    RetryPolicy retryPolicy;
    bool retryEnabled = true;
    UnauthorizedPolicy unauthorizedPolicy = UnauthorizedPolicy::Ignore;
};

/**
 * @brief Shared HTTP execution, retry, cancellation, and error-policy boundary.
 *
 * Request factories remain provider-owned. HttpTransport owns request execution
 * policy and emits a provider-neutral unauthorized signal for session handling.
 */
class HttpTransport final : public QObject
{
    Q_OBJECT

public:
    using RequestFactory = std::function<QNetworkReply *()>;
    using ResponseHandler = std::function<void(QNetworkReply *)>;
    using FailureHandler = std::function<void(const NetworkError &)>;
    using UrlRedactor = std::function<QString(const QUrl &)>;

    explicit HttpTransport(QObject *parent = nullptr);
    explicit HttpTransport(QNetworkAccessManager *networkManager, QObject *parent = nullptr);

    QNetworkAccessManager *networkManager() const { return m_networkManager; }
    void setUrlRedactor(UrlRedactor redactor);

    HttpRequestHandle *sendWithRetry(QObject *context,
                                     const QString &endpoint,
                                     RequestFactory requestFactory,
                                     ResponseHandler responseHandler,
                                     FailureHandler failureHandler = FailureHandler(),
                                     HttpRequestOptions options = HttpRequestOptions());

signals:
    void unauthorized(bool deferSessionExpiry);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    UrlRedactor m_urlRedactor;

    void startAttempt(const QPointer<HttpRequestHandle> &handle,
                      const QPointer<QObject> &context,
                      const QString &endpoint,
                      const RequestFactory &requestFactory,
                      const ResponseHandler &responseHandler,
                      const FailureHandler &failureHandler,
                      const HttpRequestOptions &options,
                      int attemptNumber);
    QString redactedEndpoint(const QString &endpoint, const QNetworkReply *reply = nullptr) const;
};
