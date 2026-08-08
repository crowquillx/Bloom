#pragma once

#include <QJsonObject>
#include <QFlags>
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

/** Provider-advertised operations. Absence means unsupported, not unprobed. */
enum class ProviderCapability {
    RefreshAuthentication = 1 << 0,
    Profiles = 1 << 1,
    ProfilePin = 1 << 2,
    AuthSessions = 1 << 3,
    Catalog = 1 << 4,
    NativeState = 1 << 5,
    MediaSegments = 1 << 6,
    Playback = 1 << 7,
    PlaybackReporting = 1 << 8
};
Q_DECLARE_FLAGS(ProviderCapabilities, ProviderCapability)

struct ProviderDetectionResult {
    ProviderKind providerKind = ProviderKind::Jellyfin;
    ProtocolMode protocolMode = ProtocolMode::Native;
    QString serverId;
    QString serverName;
    ProviderCapabilities capabilities;

    [[nodiscard]] bool isValid() const { return !serverId.isEmpty(); }
};

struct ProviderProfile {
    QString id;
    QString name;
    QString avatarUrl;
    bool hasPin = false;
    bool isChild = false;
    bool isPrimary = false;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

struct ProviderAuthSession {
    QString id;
    QString deviceName;
    QString ipAddress;
    // Absolute Unix timestamps in milliseconds; negative means unavailable.
    qint64 createdAt = -1;
    qint64 expiresAt = -1;
    qint64 revokedAt = -1;
    bool isCurrent = false;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
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
Q_DECLARE_METATYPE(ProviderDetectionResult)
Q_DECLARE_METATYPE(ProviderProfile)
Q_DECLARE_METATYPE(QList<ProviderProfile>)
Q_DECLARE_METATYPE(ProviderAuthSession)
Q_DECLARE_METATYPE(QList<ProviderAuthSession>)
Q_DECLARE_OPERATORS_FOR_FLAGS(ProviderCapabilities)
