#include "SecretStoreLinux.h"

#ifdef Q_OS_LINUX

// Undefine Qt's signals macro to avoid conflict with libsecret's GIO headers
#undef signals
#include <libsecret/secret.h>
#define signals Q_SIGNALS

#include <QDebug>

// Define the secret schema for Bloom credentials
static const SecretSchema bloom_schema = {
    "com.github.bloom.Credentials",
    SECRET_SCHEMA_NONE,
    {
        { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { "NULL", SECRET_SCHEMA_ATTRIBUTE_STRING }
    }
};

SecretStoreLinux::SecretStoreLinux()
{
    qDebug() << "SecretStoreLinux: Initialized (using libsecret)";
}

bool SecretStoreLinux::setSecret(const QString &service, const QString &account, const QString &secret)
{
    m_lastError.clear();
    
    GError *error = nullptr;
    const QString label = QString("%1 (%2)").arg(service, account);
    
    gboolean result = secret_password_store_sync(
        &bloom_schema,
        SECRET_COLLECTION_DEFAULT,
        label.toUtf8().constData(),
        secret.toUtf8().constData(),
        nullptr,  // cancellable
        &error,
        "service", service.toUtf8().constData(),
        "account", account.toUtf8().constData(),
        nullptr
    );
    
    if (error) {
        m_lastError = QString("Failed to store secret: %1").arg(error->message);
        qWarning() << "SecretStoreLinux::setSecret:" << m_lastError;
        g_error_free(error);
        return false;
    }
    
    if (!result) {
        m_lastError = "Failed to store secret (unknown error)";
        qWarning() << "SecretStoreLinux::setSecret:" << m_lastError;
        return false;
    }
    
    qDebug() << "SecretStoreLinux: Stored secret for service=" << service << "account=" << account;
    return true;
}

QString SecretStoreLinux::getSecret(const QString &service, const QString &account)
{
    m_lastError.clear();
    
    GError *error = nullptr;
    gchar *password = secret_password_lookup_sync(
        &bloom_schema,
        nullptr,  // cancellable
        &error,
        "service", service.toUtf8().constData(),
        "account", account.toUtf8().constData(),
        nullptr
    );
    
    if (error) {
        m_lastError = QString("Failed to retrieve secret: %1").arg(error->message);
        qWarning() << "SecretStoreLinux::getSecret:" << m_lastError;
        g_error_free(error);
        return QString();
    }
    
    if (!password) {
        qDebug() << "SecretStoreLinux: No secret found for service=" << service << "account=" << account;
        return QString();
    }
    
    QString result = QString::fromUtf8(password);
    secret_password_free(password);
    
    qDebug() << "SecretStoreLinux: Retrieved secret for service=" << service << "account=" << account;
    return result;
}

bool SecretStoreLinux::deleteSecret(const QString &service, const QString &account)
{
    m_lastError.clear();
    
    GError *error = nullptr;
    gboolean result = secret_password_clear_sync(
        &bloom_schema,
        nullptr,  // cancellable
        &error,
        "service", service.toUtf8().constData(),
        "account", account.toUtf8().constData(),
        nullptr
    );
    
    if (error) {
        m_lastError = QString("Failed to delete secret: %1").arg(error->message);
        qWarning() << "SecretStoreLinux::deleteSecret:" << m_lastError;
        g_error_free(error);
        return false;
    }
    
    if (result) {
        qDebug() << "SecretStoreLinux: Deleted secret for service=" << service << "account=" << account;
    } else {
        qDebug() << "SecretStoreLinux: No secret to delete for service=" << service << "account=" << account;
    }
    
    return true;
}

/**
 * @brief Retrieve the last error message produced by the secret store.
 *
 * @return QString The last error message recorded by this instance, or an empty QString if no error has been recorded.
 */
QString SecretStoreLinux::lastError() const
{
    return m_lastError;
}

/**
 * @brief Retrieve all account names that have stored secrets for a given service.
 *
 * Searches the secret store for entries matching the provided service and returns
 * the associated account names as a QStringList. If an error occurs or no matches
 * are found, an empty list is returned.
 *
 * @param service Service identifier used to filter stored secrets.
 * @return QStringList Account names (UTF-8 converted) associated with the service, or an empty list if none or on error.
 */
QStringList SecretStoreLinux::listAccounts(const QString &service)
{
    m_lastError.clear();
    QStringList accounts;

    GError *error = nullptr;
    GList *items = secret_password_search_sync(
        &bloom_schema,
        static_cast<SecretSearchFlags>(SECRET_SEARCH_ALL | SECRET_SEARCH_UNLOCK | SECRET_SEARCH_LOAD_SECRETS),
        nullptr,  // cancellable
        &error,
        "service", service.toUtf8().constData(),
        nullptr
    );

    if (error) {
        m_lastError = QString("Failed to list accounts: %1").arg(error->message);
        qWarning() << "SecretStoreLinux::listAccounts:" << m_lastError;
        g_error_free(error);
        return accounts;
    }

    for (GList *l = items; l != nullptr; l = l->next) {
        SecretRetrievable *item = SECRET_RETRIEVABLE(l->data);
        GHashTable *attributes = secret_retrievable_get_attributes(item);

        const gchar *account = static_cast<const gchar *>(g_hash_table_lookup(attributes, "account"));
        if (account) {
            accounts.append(QString::fromUtf8(account));
        }

        g_hash_table_unref(attributes);
        g_object_unref(item);
    }

    g_list_free(items);

    qDebug() << "SecretStoreLinux: Listed" << accounts.size() << "accounts for service=" << service;
    return accounts;
}

#endif // Q_OS_LINUX