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
    return name.compare(QStringLiteral("ApiKey"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("api_key"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("X-Emby-Token"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("X-MediaBrowser-Token"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("access_token"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("token"), Qt::CaseInsensitive) == 0;
}

// Jellyfin splits the MediaBrowser header on commas outside quotes and
// URL-decodes each value, so a value is percent-encoded rather than quoted
// raw. Control characters are dropped and the name is bounded so a hostile
// or unusual hostname cannot break or bloat the header.
QString headerDeviceName(const QString &deviceName)
{
    constexpr qsizetype MaxDeviceNameLength = 64;
    QString cleaned;
    cleaned.reserve(deviceName.size());
    for (const QChar character : deviceName) {
        if (character.category() != QChar::Other_Control
            && character.category() != QChar::Other_Format) {
            cleaned.append(character);
        }
    }
    cleaned = cleaned.simplified();
    if (cleaned.size() > MaxDeviceNameLength) {
        // Never split a surrogate pair (an emoji, for example).
        qsizetype length = MaxDeviceNameLength;
        if (cleaned.at(length - 1).isHighSurrogate()) {
            --length;
        }
        cleaned.truncate(length);
    }
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) {
        cleaned = QStringLiteral("Bloom Device");
    }
    return QString::fromLatin1(QUrl::toPercentEncoding(cleaned));
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
        "MediaBrowser Client=\"Bloom\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                                .arg(headerDeviceName(context.deviceName), context.deviceId,
                                     QString::fromUtf8(BLOOM_VERSION));
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
