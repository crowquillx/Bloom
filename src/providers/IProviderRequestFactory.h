#pragma once

#include <QNetworkRequest>
#include <QString>
#include <QUrl>

struct ProviderRequestContext {
    QString baseUrl;
    QString accessToken;
    QString deviceId;
};

/**
 * @brief Provider-owned HTTP request construction boundary.
 *
 * Implementations own provider-specific authorization headers, URL assembly,
 * and redaction rules. Callers provide only connection/authentication state and
 * a provider endpoint.
 */
class IProviderRequestFactory
{
public:
    virtual ~IProviderRequestFactory() = default;

    virtual QNetworkRequest createRequest(const ProviderRequestContext &context,
                                          const QString &endpoint) const = 0;
    virtual QString redactedUrl(const QUrl &url) const = 0;
};
