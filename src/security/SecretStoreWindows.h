#pragma once

#include "ISecretStore.h"

/**
 * @brief Windows implementation using Windows Credential Manager
 * 
 * Uses CredWrite/CredRead/CredDelete APIs to store credentials
 * in the Windows Credential Manager.
 */
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
};
