#pragma once

#include "providers/IProviderAuthenticator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include <algorithm>
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

/** Native Silo /api/v2 authentication wire contract. */
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
                                                      const QString &provider) const override
    {
        QJsonObject body{
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), password}
        };
        if (!provider.trimmed().isEmpty()) {
            body[QStringLiteral("provider")] = provider.trimmed();
        }
        return {
            QStringLiteral("/api/v2/auth/login"),
            QJsonDocument(body).toJson(QJsonDocument::Compact)
        };
    }

    QString sessionValidationEndpoint(const QString &accountId) const override
    {
        Q_UNUSED(accountId)
        return QStringLiteral("/api/v2/account/me");
    }

    ProviderAuthenticationResult parseLoginResponse(const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }
        return tokenPair(*object);
    }

    std::optional<ProviderAuthenticationRequest> createRefreshRequest(
        const QString &refreshToken) const override
    {
        if (refreshToken.isEmpty()) {
            return std::nullopt;
        }
        return ProviderAuthenticationRequest{
            QStringLiteral("/api/v2/auth/refresh"),
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
            || expiresIn <= 0
            || (!refreshToken.isUndefined()
                && (!refreshToken.isString() || refreshToken.toString().isEmpty()))) {
            return {};
        }

        ProviderAuthenticationResult result;
        result.accessToken = accessToken.toString();
        if (!refreshToken.isUndefined()) {
            result.refreshToken = refreshToken.toString();
        }
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
        request.endpoint = QStringLiteral("/api/v2/profiles/%1/verify-pin")
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
        if (!profileToken.isString() || profileToken.toString().isEmpty()) {
            return {};
        }

        result.profileToken = profileToken.toString();
        return result;
    }

    ProviderAuthenticationResult parseSessionValidationResponse(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const SiloAccountIdentity identity = accountIdentity(*object, true);
        if (!identity.isValid()) {
            return {};
        }

        ProviderAuthenticationResult result;
        result.accountId = identity.accountId;
        result.username = identity.username;
        return result;
    }

    std::optional<QString> signInMethodsEndpoint() const override
    {
        return QStringLiteral("/api/v2/auth/providers");
    }

    std::optional<ProviderSignInMethods> parseSignInMethods(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object || !object->value(QStringLiteral("items")).isArray()) {
            return std::nullopt;
        }
        ProviderSignInMethods result;
        const QJsonValue passwordLogin = object->value(QStringLiteral("password_login"));
        if (passwordLogin.isBool()) {
            result.passwordSignIn = passwordLogin.toBool();
        }
        return result;
    }

    std::optional<QString> deviceAuthorizationCapabilityEndpoint() const override
    {
        return QStringLiteral("/api/v2/auth/device/capability");
    }

    std::optional<ProviderDeviceAuthorizationCapability> parseDeviceAuthorizationCapability(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return std::nullopt;
        }
        const QJsonValue state = object->value(QStringLiteral("state"));
        const QJsonValue versions = object->value(QStringLiteral("protocol_versions"));
        if (!state.isString() || !versions.isArray()) {
            return std::nullopt;
        }
        const QJsonArray versionList = versions.toArray();
        const bool speaksVersion2 = std::any_of(
            versionList.begin(), versionList.end(),
            [](const QJsonValue &version) { return positiveInteger(version) == 2; });
        ProviderDeviceAuthorizationCapability result;
        // "allowed" is optional; only an explicit false withholds the flow.
        const QJsonValue allowed = object->value(QStringLiteral("allowed"));
        result.available = state.toString() == QStringLiteral("available")
            && speaksVersion2
            && !(allowed.isBool() && !allowed.toBool());
        result.cancel = object->value(QStringLiteral("cancel")).toBool(false);
        return result;
    }

    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationRequest(
        const QString &deviceName, const QString &devicePlatform) const override
    {
        QJsonObject body;
        // Shown to the approver; the schema bounds both fields.
        const QString name = deviceName.trimmed().left(128);
        const QString platform = devicePlatform.trimmed().left(64);
        if (!name.isEmpty()) {
            body[QStringLiteral("device_name")] = name;
        }
        if (!platform.isEmpty()) {
            body[QStringLiteral("device_platform")] = platform;
        }
        return ProviderAuthenticationRequest{
            QStringLiteral("/api/v2/auth/device/start"),
            QJsonDocument(body).toJson(QJsonDocument::Compact)
        };
    }

    std::optional<ProviderDeviceAuthorization> parseDeviceAuthorizationResponse(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return std::nullopt;
        }
        ProviderDeviceAuthorization result;
        result.deviceCode = object->value(QStringLiteral("device_code")).toString();
        result.userCode = object->value(QStringLiteral("user_code")).toString().trimmed();
        result.verificationUri =
            webUrl(object->value(QStringLiteral("verification_uri")).toString());
        result.verificationUriComplete =
            webUrl(object->value(QStringLiteral("verification_uri_complete")).toString());
        result.expiresInSeconds = positiveInteger(object->value(QStringLiteral("expires_in")));
        result.intervalSeconds = positiveInteger(object->value(QStringLiteral("interval")));
        result.expiresAt = instant(object->value(QStringLiteral("expires_at")));
        if (!result.isValid()) {
            return std::nullopt;
        }
        return result;
    }

    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationPollRequest(
        const QString &deviceCode) const override
    {
        return deviceCodeRequest(QStringLiteral("/api/v2/auth/device/poll"), deviceCode);
    }

    ProviderDeviceAuthorizationPoll parseDeviceAuthorizationPollResponse(
        const QByteArray &response) const override
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }
        using Status = ProviderDeviceAuthorizationPoll::Status;
        const QString status = object->value(QStringLiteral("status")).toString();
        ProviderDeviceAuthorizationPoll result;
        if (status == QStringLiteral("pending")) {
            result.status = Status::Pending;
        } else if (status == QStringLiteral("approved")) {
            result.status = Status::Approved;
        } else if (status == QStringLiteral("denied")) {
            result.status = Status::Denied;
        } else if (status == QStringLiteral("expired")) {
            result.status = Status::Expired;
        } else if (status == QStringLiteral("canceled")) {
            result.status = Status::Canceled;
        } else if (status == QStringLiteral("consumed")) {
            result.status = Status::Consumed;
        } else {
            return {};
        }
        result.opened = object->value(QStringLiteral("opened")).toBool(false);
        result.pollAfterSeconds = positiveInteger(object->value(QStringLiteral("poll_after")));
        result.expiresAt = instant(object->value(QStringLiteral("expires_at")));
        if (result.status != Status::Approved) {
            return result;
        }

        // Tokens arrive exactly once, on the poll that collects the approval.
        const QJsonValue tokens = object->value(QStringLiteral("tokens"));
        if (!tokens.isObject()) {
            return {};
        }
        result.authentication = tokenPair(tokens.toObject());
        if (!result.authentication.isValid()) {
            return {};
        }
        // A temporary (profile-bound) session names its profile; a full
        // sign-in leaves both empty and continues to profile selection.
        if (object->value(QStringLiteral("temporary")).toBool(false)) {
            result.authentication.profileId =
                object->value(QStringLiteral("profile_id")).toString();
            result.authentication.profileToken =
                object->value(QStringLiteral("profile_token")).toString();
        }
        return result;
    }

    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationCancelRequest(
        const QString &deviceCode) const override
    {
        return deviceCodeRequest(QStringLiteral("/api/v2/auth/device/cancel"), deviceCode);
    }

    SiloAuthenticationError parseErrorResponse(const QByteArray &response) const
    {
        const auto object = parseObject(response);
        if (!object) {
            return {};
        }

        const QUrl problemType(object->value(QStringLiteral("type")).toString());
        const QString code = problemType.path().section(QLatin1Char('/'), -1);
        const QJsonValue codeValue(code);
        const QJsonValue messageValue = object->value(QStringLiteral("detail"));
        if (!problemType.isValid() || problemType.scheme() != QStringLiteral("https")
            || !object->value(QStringLiteral("status")).isDouble()
            || !object->value(QStringLiteral("title")).isString()
            || !codeValue.isString() || codeValue.toString().isEmpty()
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
    static ProviderAuthenticationResult tokenPair(const QJsonObject &object)
    {
        const QJsonValue accessToken = object.value(QStringLiteral("access_token"));
        const QJsonValue refreshToken = object.value(QStringLiteral("refresh_token"));
        const qint64 expiresIn = positiveInteger(object.value(QStringLiteral("expires_in")));
        const QJsonValue user = object.value(QStringLiteral("user"));
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

    static std::optional<ProviderAuthenticationRequest> deviceCodeRequest(
        const QString &endpoint, const QString &deviceCode)
    {
        if (deviceCode.isEmpty()) {
            return std::nullopt;
        }
        return ProviderAuthenticationRequest{
            endpoint,
            QJsonDocument(QJsonObject{
                {QStringLiteral("device_code"), deviceCode}
            }).toJson(QJsonDocument::Compact)
        };
    }

    // Only absolute web links are shown to a person.
    static QString webUrl(const QString &value)
    {
        const QUrl url(value.trimmed(), QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty()
            || (url.scheme() != QStringLiteral("https")
                && url.scheme() != QStringLiteral("http"))) {
            return {};
        }
        return url.toString();
    }

    static QDateTime instant(const QJsonValue &value)
    {
        if (!value.isString()) {
            return {};
        }
        const QDateTime parsed = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
        return parsed.isValid() ? parsed.toUTC() : QDateTime();
    }

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
        const QString id = user.value(QStringLiteral("id")).toString();
        const QJsonValue username = user.value(QStringLiteral("username"));
        if (id.isEmpty() || !username.isString() || username.toString().isEmpty()) {
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
        result.accountId = id;
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
