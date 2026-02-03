#pragma once

#include "ISecretStore.h"

/**
 * Provides a Windows-backed secret store using the Windows Credential Manager.
 *
 * Stores, retrieves, deletes, and enumerates credentials via the Windows Credential
 * Manager APIs (CredWrite / CredRead / CredDelete).
 */

/**
 * Construct a SecretStoreWindows instance.
 */

/**
 * Store a secret for the given service and account in the Windows Credential Manager.
 *
 * @param service Identifier for the service the secret belongs to.
 * @param account Account name associated with the secret.
 * @param secret Secret data to store.
 * @returns `true` if the secret was stored successfully, `false` otherwise.
 */

/**
 * Retrieve the secret for the given service and account from the Windows Credential Manager.
 *
 * @param service Identifier for the service the secret belongs to.
 * @param account Account name associated with the secret.
 * @returns The stored secret as a QString, or an empty QString if no secret is found or an error occurs.
 */

/**
 * Delete the secret for the given service and account from the Windows Credential Manager.
 *
 * @param service Identifier for the service the secret belongs to.
 * @param account Account name associated with the secret.
 * @returns `true` if the secret was deleted successfully, `false` otherwise.
 */

/**
 * Get a human-readable description of the last error that occurred within this store.
 *
 * @returns A QString describing the most recent error condition, or an empty QString if none.
 */

/**
 * List account names that have stored credentials for the specified service.
 *
 * @param service Identifier for the service whose accounts should be listed.
 * @returns A QStringList containing account names associated with the service; empty if none found.
 */

/**
 * Generate a target name used to identify a credential in the Windows Credential Manager.
 *
 * @param service Identifier for the service.
 * @param account Account name.
 * @returns The composed target name used as the credential identifier.
 */

#ifdef Q_OS_WIN
/**
 * Callback used when enumerating Windows credentials to collect matching entries.
 *
 * @param pcred Pointer to a credential entry provided by the enumerator.
 * @param context User-defined context pointer passed to the enumerator.
 * @returns `TRUE` to continue enumeration, `FALSE` to stop.
 */
#endif
class SecretStoreWindows : public ISecretStore {
public:
    SecretStoreWindows();
    ~SecretStoreWindows() override = default;

    bool setSecret(const QString &service, const QString &account, const QString &secret) override;
    QString getSecret(const QString &service, const QString &account) override;
    bool deleteSecret(const QString &service, const QString &account) override;
    QString lastError() const override;
    QStringList listAccounts(const QString &service) override;

private:
    QString m_lastError;
    QString makeTargetName(const QString &service, const QString &account) const;
#ifdef Q_OS_WIN
    static BOOL CALLBACK enumCredentialsCallback(PCREDENTIALW pcred, PVOID context);
#endif
};