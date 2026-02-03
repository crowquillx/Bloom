#pragma once

#include <QString>

/**
 * @brief Platform-agnostic interface for secure credential storage
 * 
 * Implementations use native platform keychains:
 * - Linux: libsecret (GNOME Keyring / KWallet via Secret Service API)
 * - Windows: Windows Credential Manager
 * 
 * Key schema: service = "Bloom/Jellyfin", account = serverUrl + username
 */
class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    /**
     * @brief Store a secret in the platform keychain
     * @param service Service identifier (e.g., "Bloom/Jellyfin")
     * @param account Account identifier (e.g., "https://server.com|username")
     * @param secret The secret to store (e.g., access token)
     * @return true if stored successfully, false on error
     */
    virtual bool setSecret(const QString &service, const QString &account, const QString &secret) = 0;

    /**
     * @brief Retrieve a secret from the platform keychain
     * @param service Service identifier
     * @param account Account identifier
     * @return The secret, or empty string if not found or on error
     */
    virtual QString getSecret(const QString &service, const QString &account) = 0;

    /**
     * @brief Delete a secret from the platform keychain
     * @param service Service identifier
     * @param account Account identifier
     * @return true if deleted successfully or not found, false on error
     */
    virtual bool deleteSecret(const QString &service, const QString &account) = 0;

    /**
     * @brief Get the last error message (if any)
     * @return Human-readable error description
     */
    virtual QString lastError() const = 0;

    /**
     * @brief List all account keys for a service
     * @param service Service identifier
     * @return List of account keys, or empty list if none/error
     */
    virtual QStringList listAccounts(const QString &service) = 0;
};
