#include "SecretStoreFactory.h"

#ifdef Q_OS_LINUX
#include "SecretStoreLinux.h"
#elif defined(Q_OS_WIN)
#include "SecretStoreWindows.h"
#endif

#include <QDebug>

std::unique_ptr<ISecretStore> SecretStoreFactory::create()
{
#ifdef Q_OS_LINUX
    qDebug() << "SecretStoreFactory: Creating Linux implementation (libsecret)";
    return std::make_unique<SecretStoreLinux>();
#elif defined(Q_OS_WIN)
    qDebug() << "SecretStoreFactory: Creating Windows implementation (Credential Manager)";
    return std::make_unique<SecretStoreWindows>();
#else
    qWarning() << "SecretStoreFactory: No secure storage available for this platform";
    return nullptr;
#endif
}
