#include "JellyfinRequestFactory.h"

#include "config/version.h"

#include <QUrlQuery>

namespace {
QString normalizedBaseUrl(const QString &baseUrl)
{
    QString normalized = baseUrl.trimmed();
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}

bool isSensitiveQueryItem(const QString &name)
{
    return name.compare(QStringLiteral("api_key"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("X-Emby-Token"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("access_token"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("token"), Qt::CaseInsensitive) == 0;
}
}

QNetworkRequest JellyfinRequestFactory::createRequest(const ProviderRequestContext &context,
                                                       const QString &endpoint) const
{
    const QUrl url(normalizedBaseUrl(context.baseUrl)
                   + (endpoint.startsWith(QLatin1Char('/')) ? endpoint
                                                           : QLatin1Char('/') + endpoint));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QString authorization = QStringLiteral(
        "MediaBrowser Client=\"Bloom\", Device=\"Desktop\", DeviceId=\"%1\", Version=\"%2\"")
                                .arg(context.deviceId, QString::fromUtf8(BLOOM_VERSION));
    if (!context.accessToken.isEmpty()) {
        authorization += QStringLiteral(", Token=\"%1\"").arg(context.accessToken);
    }
    request.setRawHeader("Authorization", authorization.toUtf8());
    return request;
}

QString JellyfinRequestFactory::redactedUrl(const QUrl &url) const
{
    QUrl redacted = url;
    QUrlQuery source(url);
    QUrlQuery filtered;
    for (const auto &[name, value] : source.queryItems(QUrl::FullyDecoded)) {
        filtered.addQueryItem(name,
                              isSensitiveQueryItem(name) ? QStringLiteral("[REDACTED]") : value);
    }
    redacted.setQuery(filtered);
    return redacted.toString(QUrl::FullyEncoded | QUrl::RemoveUserInfo);
}
