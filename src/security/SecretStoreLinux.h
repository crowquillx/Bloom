#pragma once

#include "ISecretStore.h"

#include <QMutex>

/**
 * @brief Linux implementation using libsecret (Secret Service API)
 * 
 * Uses libsecret to store credentials in GNOME Keyring, KWallet, or any
 * Secret Service-compatible backend.
 */
class SecretStoreLinux : public ISecretStore {
public:
    SecretStoreLinux();
    ~SecretStoreLinux() override = default;

    bool setSecret(const QString &service, const QString &account, const QString &secret) override;
    QString getSecret(const QString &service, const QString &account) override;
    bool deleteSecret(const QString &service, const QString &account) override;
    QString lastError() const override;
    QStringList listAccounts(const QString &service) override;

private:
    // Keyring calls run on AuthenticationService's background queue while
    // session-restoration reads run on the global thread pool, so every
    // entry point serializes here to protect m_lastError.
    mutable QMutex m_mutex;
    QString m_lastError;
};
