#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * @brief Server implementation selected for a Bloom connection.
 */
enum class ProviderKind {
    Jellyfin,
    Silo
};

/**
 * @brief Protocol surface used for a server connection.
 *
 * Native means the provider's primary API. Compatibility identifies an
 * emulated protocol surface, such as Silo's optional MediaBrowser listener.
 */
enum class ProtocolMode {
    Native,
    Compatibility
};

/**
 * @brief Provider-neutral persisted identity for one server/account/profile.
 *
 * Secrets are never serialized here. credentialReference is an opaque key used
 * by CredentialStore to locate them in the platform secret store.
 */
struct ServerConnection {
    QString connectionId;
    ProviderKind providerKind = ProviderKind::Jellyfin;
    ProtocolMode protocolMode = ProtocolMode::Native;
    QString baseUrl;
    QString serverId;
    QString serverName;
    QString accountId;
    QString profileId;
    QString username;
    QString displayName;
    QJsonObject capabilities;
    QString credentialReference;

    bool isValid() const;
    QJsonObject toJson() const;

    static ServerConnection fromJson(const QJsonObject &json);
    static QString providerKindName(ProviderKind kind);
    static ProviderKind providerKindFromName(const QString &name);
    static QString protocolModeName(ProtocolMode mode);
    static ProtocolMode protocolModeFromName(const QString &name);
    static QString normalizeBaseUrl(const QString &url);
    static QString createConnectionId();
    static QString createDeterministicConnectionId(ProviderKind kind,
                                                   const QString &baseUrl,
                                                   const QString &accountId);
    static QString createCredentialReference(const QString &connectionId);
};

Q_DECLARE_METATYPE(ServerConnection)
Q_DECLARE_METATYPE(QList<ServerConnection>)
