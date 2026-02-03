#include "SecretStoreWindows.h"

#ifdef Q_OS_WIN

#include <windows.h>
#include <wincred.h>
#include <QDebug>

SecretStoreWindows::SecretStoreWindows()
{
    qDebug() << "SecretStoreWindows: Initialized (using Windows Credential Manager)";
}

QString SecretStoreWindows::makeTargetName(const QString &service, const QString &account) const
{
    // Format: "Bloom:service:account"
    return QString("Bloom:%1:%2").arg(service, account);
}

bool SecretStoreWindows::setSecret(const QString &service, const QString &account, const QString &secret)
{
    m_lastError.clear();
    
    QString targetName = makeTargetName(service, account);
    QByteArray secretBytes = secret.toUtf8();
    
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = (LPWSTR)targetName.utf16();
    cred.CredentialBlobSize = secretBytes.size();
    cred.CredentialBlob = (LPBYTE)secretBytes.data();
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = (LPWSTR)account.utf16();
    cred.Comment = (LPWSTR)L"Bloom HTPC Client Credentials";
    
    if (!CredWriteW(&cred, 0)) {
        DWORD errorCode = GetLastError();
        m_lastError = QString("Failed to store credential (error %1)").arg(errorCode);
        qWarning() << "SecretStoreWindows::setSecret:" << m_lastError;
        return false;
    }
    
    qDebug() << "SecretStoreWindows: Stored secret for service=" << service << "account=" << account;
    return true;
}

QString SecretStoreWindows::getSecret(const QString &service, const QString &account)
{
    m_lastError.clear();
    
    QString targetName = makeTargetName(service, account);
    PCREDENTIALW pcred = nullptr;
    
    if (!CredReadW((LPCWSTR)targetName.utf16(), CRED_TYPE_GENERIC, 0, &pcred)) {
        DWORD errorCode = GetLastError();
        if (errorCode == ERROR_NOT_FOUND) {
            qDebug() << "SecretStoreWindows: No secret found for service=" << service << "account=" << account;
        } else {
            m_lastError = QString("Failed to retrieve credential (error %1)").arg(errorCode);
            qWarning() << "SecretStoreWindows::getSecret:" << m_lastError;
        }
        return QString();
    }
    
    QString result = QString::fromUtf8(
        reinterpret_cast<const char*>(pcred->CredentialBlob),
        pcred->CredentialBlobSize
    );
    
    CredFree(pcred);
    
    qDebug() << "SecretStoreWindows: Retrieved secret for service=" << service << "account=" << account;
    return result;
}

bool SecretStoreWindows::deleteSecret(const QString &service, const QString &account)
{
    m_lastError.clear();
    
    QString targetName = makeTargetName(service, account);
    
    if (!CredDeleteW((LPCWSTR)targetName.utf16(), CRED_TYPE_GENERIC, 0)) {
        DWORD errorCode = GetLastError();
        if (errorCode == ERROR_NOT_FOUND) {
            qDebug() << "SecretStoreWindows: No secret to delete for service=" << service << "account=" << account;
            return true;  // Not found is success
        }
        
        m_lastError = QString("Failed to delete credential (error %1)").arg(errorCode);
        qWarning() << "SecretStoreWindows::deleteSecret:" << m_lastError;
        return false;
    }
    
    qDebug() << "SecretStoreWindows: Deleted secret for service=" << service << "account=" << account;
    return true;
}

QString SecretStoreWindows::lastError() const
{
    return m_lastError;
}

QStringList SecretStoreWindows::listAccounts(const QString &service)
{
    m_lastError.clear();
    QStringList accounts;

    // Use CredEnumerate to list all credentials with our prefix
    QString filter = QString("Bloom:%1:*").arg(service);
    PCREDENTIALW *pcreds = nullptr;
    DWORD count = 0;

    if (!CredEnumerateW((LPCWSTR)filter.utf16(), CRED_ENUMERATE_ALL_CREDENTIALS, &count, &pcreds)) {
        DWORD errorCode = GetLastError();
        if (errorCode == ERROR_NOT_FOUND) {
            // No credentials found - not an error
            return accounts;
        }
        m_lastError = QString("Failed to enumerate credentials (error %1)").arg(errorCode);
        qWarning() << "SecretStoreWindows::listAccounts:" << m_lastError;
        return accounts;
    }

    // Extract account names from matching credentials
    QString prefix = QString("Bloom:%1:").arg(service);
    for (DWORD i = 0; i < count; ++i) {
        QString targetName = QString::fromWCharArray(pcreds[i]->TargetName);
        if (targetName.startsWith(prefix)) {
            // Extract account part after the prefix
            QString account = targetName.mid(prefix.length());
            if (!account.isEmpty()) {
                accounts.append(account);
            }
        }
    }

    CredFree(pcreds);

    qDebug() << "SecretStoreWindows: Listed" << accounts.size() << "accounts for service=" << service;
    return accounts;
}

#endif // Q_OS_WIN
