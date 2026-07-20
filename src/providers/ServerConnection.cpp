#include "ServerConnection.h"

#include <QUrl>
#include <QUuid>

bool ServerConnection::isValid() const
{
    return !connectionId.trimmed().isEmpty()
        && !baseUrl.trimmed().isEmpty()
        && !credentialReference.trimmed().isEmpty();
}

QJsonObject ServerConnection::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("id")] = connectionId;
    json[QStringLiteral("provider")] = providerKindName(providerKind);
    json[QStringLiteral("protocol_mode")] = protocolModeName(protocolMode);
    json[QStringLiteral("base_url")] = normalizeBaseUrl(baseUrl);
    json[QStringLiteral("server_id")] = serverId;
    json[QStringLiteral("server_name")] = serverName;
    json[QStringLiteral("account_id")] = accountId;
    json[QStringLiteral("profile_id")] = profileId;
    json[QStringLiteral("username")] = username;
    json[QStringLiteral("display_name")] = displayName;
    json[QStringLiteral("capabilities")] = capabilities;
    json[QStringLiteral("credential_reference")] = credentialReference;
    return json;
}

ServerConnection ServerConnection::fromJson(const QJsonObject &json)
{
    ServerConnection connection;
    connection.connectionId = json.value(QStringLiteral("id")).toString().trimmed();
    connection.providerKind = providerKindFromName(json.value(QStringLiteral("provider")).toString());
    connection.protocolMode = protocolModeFromName(json.value(QStringLiteral("protocol_mode")).toString());
    connection.baseUrl = normalizeBaseUrl(json.value(QStringLiteral("base_url")).toString());
    connection.serverId = json.value(QStringLiteral("server_id")).toString();
    connection.serverName = json.value(QStringLiteral("server_name")).toString();
    connection.accountId = json.value(QStringLiteral("account_id")).toString();
    connection.profileId = json.value(QStringLiteral("profile_id")).toString();
    connection.username = json.value(QStringLiteral("username")).toString();
    connection.displayName = json.value(QStringLiteral("display_name")).toString();
    connection.capabilities = json.value(QStringLiteral("capabilities")).toObject();
    connection.credentialReference = json.value(QStringLiteral("credential_reference")).toString().trimmed();
    return connection;
}

QString ServerConnection::providerKindName(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Silo:
        return QStringLiteral("silo");
    case ProviderKind::Jellyfin:
        return QStringLiteral("jellyfin");
    }
    return QStringLiteral("jellyfin");
}

ProviderKind ServerConnection::providerKindFromName(const QString &name)
{
    return name.trimmed().compare(QStringLiteral("silo"), Qt::CaseInsensitive) == 0
        ? ProviderKind::Silo
        : ProviderKind::Jellyfin;
}

QString ServerConnection::protocolModeName(ProtocolMode mode)
{
    switch (mode) {
    case ProtocolMode::Compatibility:
        return QStringLiteral("compatibility");
    case ProtocolMode::Native:
        return QStringLiteral("native");
    }
    return QStringLiteral("native");
}

ProtocolMode ServerConnection::protocolModeFromName(const QString &name)
{
    return name.trimmed().compare(QStringLiteral("compatibility"), Qt::CaseInsensitive) == 0
        ? ProtocolMode::Compatibility
        : ProtocolMode::Native;
}

QString ServerConnection::normalizeBaseUrl(const QString &url)
{
    QString normalized = url.trimmed();
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}

QString ServerConnection::createConnectionId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ServerConnection::createDeterministicConnectionId(ProviderKind kind,
                                                          const QString &baseUrl,
                                                          const QString &accountId)
{
    static const QUuid migrationNamespace(
        QStringLiteral("{b76c9d28-e1a4-5ee8-b86d-10d9ae8fd85a}"));
    const QByteArray identity = QStringLiteral("%1\n%2\n%3")
                                    .arg(providerKindName(kind),
                                         normalizeBaseUrl(baseUrl),
                                         accountId.trimmed())
                                    .toUtf8();
    return QUuid::createUuidV5(migrationNamespace, identity).toString(QUuid::WithoutBraces);
}

QString ServerConnection::createCredentialReference(const QString &connectionId)
{
    return QStringLiteral("connection:%1").arg(connectionId.trimmed());
}
