#pragma once

#include "providers/IProviderAuthenticator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include <cmath>
#include <limits>

/** Provider-specific authentication failure codes carried by Silo error envelopes. */
enum class SiloAuthenticationFailure {
    None,
    InvalidCredentials,
    InvalidToken,
    SessionRevoked,
    Unauthorized,
    Forbidden,
    NotFound,
    BadRequest,
    Other
};

struct SiloAuthenticationError {
    SiloAuthenticationFailure failure = SiloAuthenticationFailure::None;
    QString code;
    QString message;

    [[nodiscard]] bool isValid() const
    {
        return failure != SiloAuthenticationFailure::None && !code.isEmpty();
    }

    [[nodiscard]] bool isSessionRevoked() const
    {
        return failure == SiloAuthenticationFailure::SessionRevoked;
    }
};

struct SiloAccountIdentity {
    QString accountId;
    QString username;
    QString role;
    QStringList permissions;
    bool downloadAllowed = false;

    [[nodiscard]] bool isValid() const
    {
        return !accountId.isEmpty() && !username.isEmpty();
    }
};

/** Native Silo /api/v1 authentication wire contract. */
class SiloAuthenticator final : public IProviderAuthenticator
{
public:
    ProviderAuthenticationRequest createLoginRequest(const QString &username,
                                                      const QString &password) const override
    {
        return createLoginRequest(username, password, {});
    }

    ProviderAuthenticationRequest createLoginRequest(const QString &username,
                                                      const QString &password,
                                                      const QString &provider) const
    {
        QJsonObject body{
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), password}
        };
        if (!provider.trimmed().isEmpty()) {
            body[QStringLiteral("provider")] = provider.trimmed();
        }
        return {
            QStringLiteral("/api/v1/auth/login"),
            QJsonDocument(body).toJson(QJsonDocument::Compact)
        };
    }

    QString sessionValidationEndpoint(const QString &accountId) const override
    {
        Q_UNUSED(accountId)
        return QStringLiteral("/api/v1/auth/me");
    }

    ProviderAuthenticationResult parseLoginResponse(const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const QJsonValue accessToken = object->value(QStringLiteral("access_token"));
        const QJsonValue refreshToken = object->value(QStringLiteral("refresh_token"));
        const qint64 expiresIn = positiveInteger(object->value(QStringLiteral("expires_in")));
        const QJsonValue user = object->value(QStringLiteral("user"));
        if (!accessToken.isString() || accessToken.toString().isEmpty()
            || !refreshToken.isString() || refreshToken.toString().isEmpty()
            || expiresIn <= 0 || !user.isObject()) {
            return {};
        }

        const SiloAccountIdentity identity = accountIdentity(user.toObject(), false);
        if (!identity.isValid()) {
            return {};
        }

        ProviderAuthenticationResult result;
        result.accessToken = accessToken.toString();
        result.refreshToken = refreshToken.toString();
        result.accountId = identity.accountId;
        result.username = identity.username;
        result.expiresInSeconds = expiresIn;
        return result;
    }

    std::optional<ProviderAuthenticationRequest> createRefreshRequest(
        const QString &refreshToken) const override
    {
        if (refreshToken.isEmpty()) {
            return std::nullopt;
        }
        return ProviderAuthenticationRequest{
            QStringLiteral("/api/v1/auth/refresh"),
            QJsonDocument(QJsonObject{
                {QStringLiteral("refresh_token"), refreshToken}
            }).toJson(QJsonDocument::Compact),
            refreshToken
        };
    }

    ProviderAuthenticationResult parseRefreshResponse(const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const QJsonValue accessToken = object->value(QStringLiteral("access_token"));
        const QJsonValue refreshToken = object->value(QStringLiteral("refresh_token"));
        const qint64 expiresIn = positiveInteger(object->value(QStringLiteral("expires_in")));
        if (!accessToken.isString() || accessToken.toString().isEmpty()
            || !refreshToken.isString() || refreshToken.toString().isEmpty()
            || expiresIn <= 0) {
            return {};
        }

        ProviderAuthenticationResult result;
        result.accessToken = accessToken.toString();
        // Silo returns a replacement refresh token. Callers must persist this value.
        result.refreshToken = refreshToken.toString();
        result.expiresInSeconds = expiresIn;
        return result;
    }

    std::optional<ProviderAuthenticationRequest> createProfileLoginRequest(
        const QString &profileId, const QString &pin) const override
    {
        if (profileId.trimmed().isEmpty() || pin.isEmpty()) {
            return std::nullopt;
        }

        const QString encodedProfileId = QString::fromLatin1(
            QUrl::toPercentEncoding(profileId.trimmed()));
        ProviderAuthenticationRequest request;
        request.endpoint = QStringLiteral("/api/v1/profiles/%1/verify-pin")
                               .arg(encodedProfileId);
        request.body = QJsonDocument(QJsonObject{
            {QStringLiteral("pin"), pin}
        }).toJson(QJsonDocument::Compact);
        request.profileId = profileId.trimmed();
        return request;
    }

    ProviderProfileAuthenticationResult parseProfileLoginResponse(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const QJsonValue valid = object->value(QStringLiteral("valid"));
        if (!valid.isBool()) {
            return {};
        }

        ProviderProfileAuthenticationResult result;
        result.responseParsed = true;
        result.valid = valid.toBool();
        if (!result.valid) {
            return result;
        }

        const QJsonValue profileToken = object->value(QStringLiteral("profile_token"));
        const QJsonValue expiresAt = object->value(QStringLiteral("expires_at"));
        if (!profileToken.isString() || profileToken.toString().isEmpty()
            || !expiresAt.isString() || expiresAt.toString().trimmed().isEmpty()) {
            return {};
        }

        result.profileToken = profileToken.toString();
        return result;
    }

    SiloAccountIdentity parseSessionValidationResponse(const QByteArray &response) const
    {
        const auto object = parseObject(response);
        return object ? accountIdentity(*object, true) : SiloAccountIdentity{};
    }

    SiloAuthenticationError parseErrorResponse(const QByteArray &response) const
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const QJsonValue codeValue = object->value(QStringLiteral("error"));
        const QJsonValue messageValue = object->value(QStringLiteral("message"));
        if (!codeValue.isString() || codeValue.toString().isEmpty()
            || !messageValue.isString()) {
            return {};
        }

        SiloAuthenticationError result;
        result.code = codeValue.toString();
        result.message = messageValue.toString();
        if (result.code == QStringLiteral("invalid_credentials")) {
            result.failure = SiloAuthenticationFailure::InvalidCredentials;
        } else if (result.code == QStringLiteral("invalid_token")) {
            result.failure = SiloAuthenticationFailure::InvalidToken;
        } else if (result.code == QStringLiteral("session_revoked")) {
            result.failure = SiloAuthenticationFailure::SessionRevoked;
        } else if (result.code == QStringLiteral("unauthorized")) {
            result.failure = SiloAuthenticationFailure::Unauthorized;
        } else if (result.code == QStringLiteral("forbidden")
                   || result.code == QStringLiteral("user_disabled")) {
            result.failure = SiloAuthenticationFailure::Forbidden;
        } else if (result.code == QStringLiteral("not_found")) {
            result.failure = SiloAuthenticationFailure::NotFound;
        } else if (result.code == QStringLiteral("bad_request")) {
            result.failure = SiloAuthenticationFailure::BadRequest;
        } else {
            result.failure = SiloAuthenticationFailure::Other;
        }
        return result;
    }

private:
    static std::optional<QJsonObject> parseObject(const QByteArray &response)
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(response, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return std::nullopt;
        }
        return document.object();
    }

    static qint64 positiveInteger(const QJsonValue &value)
    {
        if (!value.isDouble()) {
            return -1;
        }
        const double number = value.toDouble();
        constexpr double maximumExactJsonInteger = 9007199254740991.0;
        if (!std::isfinite(number) || number <= 0.0
            || number > maximumExactJsonInteger || std::trunc(number) != number) {
            return -1;
        }
        return static_cast<qint64>(number);
    }

    static SiloAccountIdentity accountIdentity(const QJsonObject &user,
                                               bool requireAuthorizationFields)
    {
        const qint64 id = positiveInteger(user.value(QStringLiteral("id")));
        const QJsonValue username = user.value(QStringLiteral("username"));
        if (id <= 0 || !username.isString() || username.toString().isEmpty()) {
            return {};
        }

        const QJsonValue role = user.value(QStringLiteral("role"));
        const QJsonValue permissions = user.value(QStringLiteral("permissions"));
        const QJsonValue downloadAllowed = user.value(QStringLiteral("download_allowed"));
        if (requireAuthorizationFields
            && (!role.isString() || !permissions.isArray() || !downloadAllowed.isBool())) {
            return {};
        }

        SiloAccountIdentity result;
        result.accountId = QString::number(id);
        result.username = username.toString();
        if (role.isString()) {
            result.role = role.toString();
        }
        if (permissions.isArray()) {
            for (const QJsonValue &permission : permissions.toArray()) {
                if (!permission.isString()) {
                    if (requireAuthorizationFields) {
                        return {};
                    }
                    continue;
                }
                result.permissions.append(permission.toString());
            }
        }
        if (downloadAllowed.isBool()) {
            result.downloadAllowed = downloadAllowed.toBool();
        }
        return result;
    }
};
