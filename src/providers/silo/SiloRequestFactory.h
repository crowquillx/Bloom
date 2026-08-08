#pragma once

#include "providers/IProviderRequestFactory.h"

#include <QByteArray>
#include <QUrl>

class SiloRequestFactory final : public IProviderRequestFactory
{
public:
    QNetworkRequest createRequest(const ProviderRequestContext &context,
                                  const QString &endpoint) const override
    {
        QNetworkRequest request(resolveEndpoint(context.baseUrl, endpoint));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Accept", "application/json");

        const QString path = request.url().path();
        const bool authenticationRoute =
            path.contains(QStringLiteral("/api/v1/auth/"));
        const bool publicAuthenticationRoute =
            path.endsWith(QStringLiteral("/api/v1/auth/login"))
            || path.endsWith(QStringLiteral("/api/v1/auth/refresh"))
            || path.endsWith(QStringLiteral("/api/v1/auth/providers"))
            || path.endsWith(QStringLiteral("/api/v1/health"));
        const bool profileVerificationRoute =
            path.contains(QStringLiteral("/api/v1/profiles/"))
            && path.endsWith(QStringLiteral("/verify-pin"));
        if (sameOrigin(context.baseUrl, request.url())) {
            if (!publicAuthenticationRoute && !context.accessToken.isEmpty()) {
                request.setRawHeader("Authorization",
                                     QByteArrayLiteral("Bearer ") + context.accessToken.toUtf8());
            }
            if (!authenticationRoute && !profileVerificationRoute
                && !publicAuthenticationRoute && !context.profileId.isEmpty()) {
                request.setRawHeader("X-Profile-Id", context.profileId.toUtf8());
            }
            if (!authenticationRoute && !profileVerificationRoute
                && !publicAuthenticationRoute && !context.profileToken.isEmpty()) {
                request.setRawHeader("X-Profile-Token", context.profileToken.toUtf8());
            }

            setHeaderIfPresent(request, "X-Silo-Client", context.clientName);
            setHeaderIfPresent(request, "X-Silo-Client-Version", context.clientVersion);
            setHeaderIfPresent(request, "X-Silo-Device-Id", context.deviceId);
            setHeaderIfPresent(request, "X-Silo-Device-Name", context.deviceName);
            setHeaderIfPresent(request, "X-Silo-Device-Platform", context.devicePlatform);

            // Silo records the login request's User-Agent as the auth-session device name.
            if (!context.deviceName.isEmpty()) {
                request.setRawHeader("User-Agent", context.deviceName.toUtf8());
            }
        }
        return request;
    }

    QString redactedUrl(const QUrl &url) const override
    {
        QUrl withoutCredentials = url;
        withoutCredentials.setUserName({});
        withoutCredentials.setPassword({});

        QByteArray encoded = withoutCredentials.toEncoded(QUrl::FullyEncoded);
        const qsizetype queryStart = encoded.indexOf('?');
        if (queryStart < 0) {
            return QString::fromUtf8(encoded);
        }

        const qsizetype fragmentStart = encoded.indexOf('#', queryStart + 1);
        const qsizetype queryEnd = fragmentStart < 0 ? encoded.size() : fragmentStart;
        QByteArray query = encoded.mid(queryStart + 1, queryEnd - queryStart - 1);

        qsizetype itemStart = 0;
        while (itemStart <= query.size()) {
            qsizetype itemEnd = query.indexOf('&', itemStart);
            if (itemEnd < 0) {
                itemEnd = query.size();
            }
            const qsizetype equals = query.indexOf('=', itemStart);
            const qsizetype nameEnd = equals >= itemStart && equals < itemEnd ? equals : itemEnd;
            const QByteArray encodedName = query.mid(itemStart, nameEnd - itemStart);
            const QString decodedName = QUrl::fromPercentEncoding(encodedName);
            if (isSensitiveQueryItem(decodedName) && equals >= itemStart && equals < itemEnd) {
                static const QByteArray redaction = QByteArrayLiteral("%5BREDACTED%5D");
                query.replace(equals + 1, itemEnd - equals - 1, redaction);
                itemEnd = equals + 1 + redaction.size();
            }
            if (itemEnd >= query.size()) {
                break;
            }
            itemStart = itemEnd + 1;
        }

        encoded.replace(queryStart + 1, queryEnd - queryStart - 1, query);
        return QString::fromUtf8(encoded);
    }

private:
    static QUrl resolveEndpoint(const QString &baseUrl, const QString &endpoint)
    {
        const QUrl direct(endpoint);
        if (!direct.isRelative() && !direct.scheme().isEmpty()) {
            return direct;
        }

        QUrl base(baseUrl.trimmed());
        QString path = base.path();
        while (path.endsWith(QLatin1Char('/'))) {
            path.chop(1);
        }
        path += QLatin1Char('/');
        base.setPath(path);

        QString relative = endpoint;
        while (relative.startsWith(QLatin1Char('/'))) {
            relative.remove(0, 1);
        }
        return base.resolved(QUrl(relative));
    }

    static bool sameOrigin(const QString &baseUrl, const QUrl &resolved)
    {
        const QUrl base(baseUrl.trimmed());
        if (!base.isValid() || !resolved.isValid()
            || base.scheme().compare(resolved.scheme(), Qt::CaseInsensitive) != 0
            || base.host().compare(resolved.host(), Qt::CaseInsensitive) != 0) {
            return false;
        }
        const auto effectivePort = [](const QUrl &url) {
            if (url.port() >= 0) {
                return url.port();
            }
            return url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                ? 443 : 80;
        };
        return effectivePort(base) == effectivePort(resolved);
    }

    static void setHeaderIfPresent(QNetworkRequest &request,
                                   const QByteArray &name,
                                   const QString &value)
    {
        if (!value.isEmpty()) {
            request.setRawHeader(name, value.toUtf8());
        }
    }

    static bool isSensitiveQueryItem(const QString &name)
    {
        const QString normalized = name.trimmed().toLower();
        return normalized == QStringLiteral("st")
            || normalized == QStringLiteral("sig")
            || normalized == QStringLiteral("signature")
            || normalized == QStringLiteral("x-amz-signature")
            || normalized == QStringLiteral("x-goog-signature")
            || normalized.contains(QStringLiteral("token"));
    }
};
