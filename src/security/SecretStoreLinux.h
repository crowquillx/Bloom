#pragma once

#include "ISecretStore.h"

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

private:
    QString m_lastError;
};
