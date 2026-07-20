#include "JellyfinAuthenticator.h"

#include <QJsonDocument>
#include <QJsonObject>

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
    return result;
}
