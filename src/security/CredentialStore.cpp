#include "CredentialStore.h"

#include "ISecretStore.h"

CredentialStore::CredentialStore(ISecretStore *secretStore)
    : m_secretStore(secretStore)
{
}

CredentialReadResult CredentialStore::readAccessToken(const ServerConnection &connection,
                                                       const QString &deviceId,
                                                       const QString &legacyServerUrl,
                                                       const QString &legacyUsername,
                                                       const QString &configFallbackToken) const
{
    CredentialReadResult result;
    if (!m_secretStore || !connection.isValid()) {
        result.error = QStringLiteral("Credential store or connection is unavailable");
        return result;
    }

    result.secret = read(connection, CredentialKind::AccessToken);
    if (result.secret.isEmpty() && !m_secretStore->lastError().isEmpty()) {
        result.error = m_secretStore->lastError();
        return result;
    }
    if (connection.providerKind != ProviderKind::Jellyfin) {
        return result;
    }

    QStringList legacyAccounts{
        legacyJellyfinAccountKey(connection, deviceId),
        legacyJellyfinAccountKey(connection, deviceId, legacyServerUrl),
        legacyJellyfinAccountKey(connection, deviceId, QString(), legacyUsername),
        legacyJellyfinAccountKey(connection, deviceId, legacyServerUrl, legacyUsername)
    };
    legacyAccounts.removeAll(QString());
    legacyAccounts.removeDuplicates();

    const auto removeLegacyEntries = [this, &legacyAccounts, &result]() {
        bool removedAny = false;
        for (const QString &account : legacyAccounts) {
            const QString secret = m_secretStore->getSecret(legacyJellyfinServiceName(), account);
            if (secret.isEmpty() && !m_secretStore->lastError().isEmpty()) {
                result.cleanupError = m_secretStore->lastError();
                return false;
            }
            if (secret.isEmpty()) {
                continue;
            }
            removedAny = true;
            if (!m_secretStore->deleteSecret(legacyJellyfinServiceName(), account)) {
                result.cleanupError = m_secretStore->lastError();
                return false;
            }
        }
        result.migratedLegacyEntry = removedAny;
        return true;
    };

    if (!result.secret.isEmpty()) {
        removeLegacyEntries();
        return result;
    }

    QString legacyToken;
    for (const QString &account : legacyAccounts) {
        const QString candidate = m_secretStore->getSecret(legacyJellyfinServiceName(), account);
        if (candidate.isEmpty() && !m_secretStore->lastError().isEmpty()) {
            result.error = m_secretStore->lastError();
            return result;
        }
        if (legacyToken.isEmpty() && !candidate.isEmpty()) {
            legacyToken = candidate;
        }
    }

    const QString migrationToken = legacyToken.isEmpty() ? configFallbackToken : legacyToken;
    if (migrationToken.isEmpty()) {
        return result;
    }

    result.secret = migrationToken;
    result.usedLegacyEntry = !legacyToken.isEmpty();

    if (!write(connection, CredentialKind::AccessToken, migrationToken)) {
        result.error = m_secretStore->lastError();
        return result;
    }

    const QString verifiedToken = read(connection, CredentialKind::AccessToken);
    if (verifiedToken != migrationToken) {
        remove(connection, CredentialKind::AccessToken);
        result.error = QStringLiteral("Credential migration verification failed");
        return result;
    }

    removeLegacyEntries();
    return result;
}

QString CredentialStore::read(const ServerConnection &connection, CredentialKind kind) const
{
    if (!m_secretStore || !connection.isValid()) {
        return {};
    }
    return m_secretStore->getSecret(serviceName(), accountKey(connection, kind));
}

bool CredentialStore::write(const ServerConnection &connection,
                            CredentialKind kind,
                            const QString &secret) const
{
    if (!m_secretStore || !connection.isValid() || secret.isEmpty()) {
        return false;
    }
    return m_secretStore->setSecret(serviceName(), accountKey(connection, kind), secret);
}

bool CredentialStore::remove(const ServerConnection &connection, CredentialKind kind) const
{
    if (!m_secretStore || !connection.isValid()) {
        return false;
    }
    return m_secretStore->deleteSecret(serviceName(), accountKey(connection, kind));
}

bool CredentialStore::removeAll(const ServerConnection &connection,
                                const QString &deviceId,
                                const QString &legacyServerUrl,
                                const QString &legacyUsername) const
{
    if (!m_secretStore || !connection.isValid()) {
        return false;
    }

    bool success = true;
    success = remove(connection, CredentialKind::AccessToken) && success;
    success = remove(connection, CredentialKind::RefreshToken) && success;
    success = remove(connection, CredentialKind::ProfileToken) && success;

    if (connection.providerKind == ProviderKind::Jellyfin) {
        QStringList legacyAccounts{
            legacyJellyfinAccountKey(connection, deviceId),
            legacyJellyfinAccountKey(connection, deviceId, legacyServerUrl),
            legacyJellyfinAccountKey(connection, deviceId, QString(), legacyUsername),
            legacyJellyfinAccountKey(connection, deviceId, legacyServerUrl, legacyUsername)
        };
        legacyAccounts.removeAll(QString());
        legacyAccounts.removeDuplicates();
        for (const QString &legacyAccount : legacyAccounts) {
            success = m_secretStore->deleteSecret(legacyJellyfinServiceName(), legacyAccount) && success;
        }
    }
    return success;
}

QString CredentialStore::serviceName()
{
    return QStringLiteral("Bloom/Connections");
}

QString CredentialStore::credentialKindName(CredentialKind kind)
{
    switch (kind) {
    case CredentialKind::RefreshToken:
        return QStringLiteral("refresh-token");
    case CredentialKind::ProfileToken:
        return QStringLiteral("profile-token");
    case CredentialKind::AccessToken:
        return QStringLiteral("access-token");
    }
    return QStringLiteral("access-token");
}

QString CredentialStore::accountKey(const ServerConnection &connection, CredentialKind kind)
{
    return QStringLiteral("%1/%2")
        .arg(connection.credentialReference, credentialKindName(kind));
}

QString CredentialStore::legacyJellyfinServiceName()
{
    return QStringLiteral("Bloom/Jellyfin");
}

QString CredentialStore::legacyJellyfinAccountKey(const ServerConnection &connection,
                                                   const QString &deviceId,
                                                   const QString &serverUrlOverride,
                                                   const QString &usernameOverride)
{
    const QString serverUrl = serverUrlOverride.isEmpty()
        ? ServerConnection::normalizeBaseUrl(connection.baseUrl)
        : serverUrlOverride.trimmed();
    const QString username = usernameOverride.isEmpty()
        ? connection.username
        : usernameOverride;
    if (serverUrl.isEmpty() || username.isEmpty() || deviceId.isEmpty()) {
        return {};
    }
    return QStringLiteral("%1|%2|%3").arg(serverUrl, username, deviceId);
}
