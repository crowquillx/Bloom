#pragma once

#include "network/Types.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

class HttpRequestHandle final : public QObject
{
    Q_OBJECT

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
    RetrySafety retrySafety = RetrySafety::Never;
    UnauthorizedPolicy unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    int attemptTimeoutMs = 30000;
    int unauthorizedRecoveryTimeoutMs = 15000;
};

/**
 * @brief Shared HTTP execution, retry, cancellation, and error-policy boundary.
 *
 * Request factories remain provider-owned. The factory is invoked again for
 * every replay, so each invocation must construct the same logical request
 * (HTTP operation, URL, body, and headers) from its captured request state.
 * Callers must opt into transient replay with RetrySafety; maxAttempts includes
 * the initial request. HttpTransport owns request execution policy and emits a
 * provider-neutral unauthorized signal for session handling.
 */
class HttpTransport final : public QObject
{
    Q_OBJECT

public:
    using RequestFactory = std::function<QNetworkReply *()>;
    using ResponseHandler = std::function<void(QNetworkReply *)>;
    using FailureHandler = std::function<void(const NetworkError &)>;
    using UrlRedactor = std::function<QString(const QUrl &)>;
    using UnauthorizedRecovery = std::function<void(std::function<void(bool)>)>;

    explicit HttpTransport(QObject *parent = nullptr);
    explicit HttpTransport(QNetworkAccessManager *networkManager, QObject *parent = nullptr);

    QNetworkAccessManager *networkManager() const { return m_networkManager; }
    void setUrlRedactor(UrlRedactor redactor);
    void setUnauthorizedRecovery(UnauthorizedRecovery recovery);
    void cancelAll();

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
    UnauthorizedRecovery m_unauthorizedRecovery;
    bool m_unauthorizedRecoveryInProgress = false;
    quint64 m_unauthorizedRecoveryGeneration = 0;
    quint64 m_authenticationEpoch = 0;
    QTimer m_unauthorizedRecoveryTimer;
    QList<std::function<void(bool, bool)>> m_pendingUnauthorizedRecoveries;

    void initializeRecoveryTimer();
    void startAttempt(const QPointer<HttpRequestHandle> &handle,
                      const QPointer<QObject> &context,
                      const QString &endpoint,
                      const RequestFactory &requestFactory,
                      const ResponseHandler &responseHandler,
                      const FailureHandler &failureHandler,
                      const HttpRequestOptions &options,
                      int attemptNumber,
                      bool authenticationRetried,
                      quint64 authenticationEpoch);
    void recoverUnauthorized(std::function<void(bool, bool)> completion,
                             int timeoutMs);
    void finishUnauthorizedRecovery(quint64 generation,
                                    bool recovered,
                                    bool timedOut);
    QString redactedEndpoint(const QString &endpoint, const QNetworkReply *reply = nullptr) const;
};
