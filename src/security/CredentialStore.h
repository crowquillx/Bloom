#pragma once

#include "providers/ServerConnection.h"

#include <QString>

class ISecretStore;

enum class CredentialKind {
    AccessToken,
    RefreshToken,
    ProfileToken
};

struct CredentialReadResult {
    QString secret;
    bool usedLegacyEntry = false;
    bool migratedLegacyEntry = false;
    QString error;
    QString cleanupError;
};

/**
 * @brief Provider-neutral credential naming and legacy Jellyfin migration.
 *
 * This class stores only opaque credential references in account keys. It
 * prefers provider-neutral and legacy keychain credentials over a plaintext
 * config fallback, verifies a provider-neutral copy, and only then removes the
 * old keychain entry.
 */
class CredentialStore
{
public:
    explicit CredentialStore(ISecretStore *secretStore);

    CredentialReadResult readAccessToken(const ServerConnection &connection,
                                         const QString &deviceId,
                                         const QString &legacyServerUrl = QString(),
                                         const QString &legacyUsername = QString(),
                                         const QString &configFallbackToken = QString()) const;
    QString read(const ServerConnection &connection, CredentialKind kind) const;
    bool write(const ServerConnection &connection, CredentialKind kind, const QString &secret) const;
    bool remove(const ServerConnection &connection, CredentialKind kind) const;
    bool removeAll(const ServerConnection &connection,
                   const QString &deviceId,
                   const QString &legacyServerUrl = QString(),
                   const QString &legacyUsername = QString()) const;

    static QString serviceName();
    static QString credentialKindName(CredentialKind kind);
    static QString accountKey(const ServerConnection &connection, CredentialKind kind);
    static QString legacyJellyfinServiceName();
    static QString legacyJellyfinAccountKey(const ServerConnection &connection,
                                            const QString &deviceId,
                                            const QString &serverUrlOverride = QString(),
                                            const QString &usernameOverride = QString());

private:
    ISecretStore *m_secretStore = nullptr;
};
