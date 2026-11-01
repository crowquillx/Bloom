#include "JellyfinAuthenticator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {
// UserDto.Policy.IsAdministrator gates admin-only routes such as
// DELETE /Devices. A missing policy leaves the capability unknown.
std::optional<bool> administratorFlag(const QJsonObject &user)
{
    const QJsonValue value = user.value(QStringLiteral("Policy")).toObject()
                                 .value(QStringLiteral("IsAdministrator"));
    return value.isBool() ? std::optional<bool>(value.toBool()) : std::nullopt;
}
}

ProviderAuthenticationRequest JellyfinAuthenticator::createLoginRequest(
    const QString &username,
    const QString &password) const
{
    QJsonObject body;
    body[QStringLiteral("Username")] = username;
    body[QStringLiteral("Pw")] = password;
    return {
        QStringLiteral("/Users/AuthenticateByName"),
        QJsonDocument(body).toJson()
    };
}

QString JellyfinAuthenticator::sessionValidationEndpoint(const QString &accountId) const
{
    return QStringLiteral("/Users/%1").arg(accountId);
}

ProviderAuthenticationResult JellyfinAuthenticator::parseLoginResponse(
    const QByteArray &response) const
{
    const QJsonObject root = QJsonDocument::fromJson(response).object();
    const QJsonObject user = root.value(QStringLiteral("User")).toObject();

    ProviderAuthenticationResult result;
    result.accessToken = root.value(QStringLiteral("AccessToken")).toString();
    result.accountId = user.value(QStringLiteral("Id")).toString();
    result.username = user.value(QStringLiteral("Name")).toString();
    result.isAdministrator = administratorFlag(user);
    return result;
}

ProviderAuthenticationResult JellyfinAuthenticator::parseSessionValidationResponse(
    const QByteArray &response) const
{
    // GET /Users/{userId} returns the bare UserDto, not the login envelope.
    const QJsonObject user = QJsonDocument::fromJson(response).object();

    ProviderAuthenticationResult result;
    result.accountId = user.value(QStringLiteral("Id")).toString();
    result.username = user.value(QStringLiteral("Name")).toString();
    result.isAdministrator = administratorFlag(user);
    return result;
}

// Quick Connect -------------------------------------------------------------

std::optional<QString> JellyfinAuthenticator::deviceAuthorizationCapabilityEndpoint() const
{
    return QStringLiteral("/QuickConnect/Enabled");
}

std::optional<ProviderDeviceAuthorizationCapability>
JellyfinAuthenticator::parseDeviceAuthorizationCapability(const QByteArray &response) const
{
    // The answer is a bare JSON boolean. Jellyfin has no cancel operation.
    const QByteArray value = response.trimmed();
    if (value != "true" && value != "false") {
        return std::nullopt;
    }
    ProviderDeviceAuthorizationCapability result;
    result.available = value == "true";
    return result;
}

std::optional<ProviderAuthenticationRequest> JellyfinAuthenticator::createDeviceAuthorizationRequest(
    const QString &deviceName, const QString &devicePlatform) const
{
    // The device is named by the MediaBrowser authorization header.
    Q_UNUSED(deviceName)
    Q_UNUSED(devicePlatform)
    return ProviderAuthenticationRequest{QStringLiteral("/QuickConnect/Initiate"), QByteArray()};
}

std::optional<ProviderDeviceAuthorization> JellyfinAuthenticator::parseDeviceAuthorizationResponse(
    const QByteArray &response) const
{
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    ProviderDeviceAuthorization result;
    result.deviceCode = root.value(QStringLiteral("Secret")).toString();
    result.userCode = root.value(QStringLiteral("Code")).toString().trimmed();
    // The result has no lifetime or interval; the server's fixed timeout
    // becomes the local deadline.
    result.expiresInSeconds = kQuickConnectLifetimeSeconds;
    result.intervalSeconds = kQuickConnectPollSeconds;
    if (!result.isValid()) {
        return std::nullopt;
    }
    return result;
}

std::optional<ProviderAuthenticationRequest>
JellyfinAuthenticator::createDeviceAuthorizationPollRequest(const QString &deviceCode) const
{
    if (deviceCode.isEmpty()) {
        return std::nullopt;
    }
    ProviderAuthenticationRequest request;
    request.endpoint = QStringLiteral("/QuickConnect/Connect?secret=")
        + QString::fromLatin1(QUrl::toPercentEncoding(deviceCode));
    request.method = ProviderAuthenticationRequest::Method::Get;
    return request;
}

ProviderDeviceAuthorizationPoll JellyfinAuthenticator::parseDeviceAuthorizationPollResponse(
    const QByteArray &response) const
{
    using Status = ProviderDeviceAuthorizationPoll::Status;
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject()) {
        return {};
    }
    const QJsonObject root = document.object();
    ProviderDeviceAuthorizationPoll result;
    if (root.value(QStringLiteral("Secret")).toString().isEmpty()) {
        // The server no longer knows this request.
        result.status = Status::Expired;
        return result;
    }
    const QJsonValue authenticated = root.value(QStringLiteral("Authenticated"));
    if (!authenticated.isBool()) {
        return {};
    }
    result.status = authenticated.toBool() ? Status::Authorized : Status::Pending;
    return result;
}

std::optional<ProviderAuthenticationRequest>
JellyfinAuthenticator::createDeviceAuthorizationExchangeRequest(const QString &deviceCode) const
{
    if (deviceCode.isEmpty()) {
        return std::nullopt;
    }
    return ProviderAuthenticationRequest{
        QStringLiteral("/Users/AuthenticateWithQuickConnect"),
        QJsonDocument(QJsonObject{{QStringLiteral("Secret"), deviceCode}})
            .toJson(QJsonDocument::Compact)
    };
}
