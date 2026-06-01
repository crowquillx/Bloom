#include "SecretStoreFactory.h"

#ifdef Q_OS_LINUX
#include "SecretStoreLinux.h"
#elif defined(Q_OS_WIN)
#include "SecretStoreWindows.h"
#endif

#include <QDebug>
#include "../utils/BloomLogging.h"

std::unique_ptr<ISecretStore> SecretStoreFactory::create()
{
#ifdef Q_OS_LINUX
    qCDebug(lcAuth) << "SecretStoreFactory: Creating Linux implementation (libsecret)";
    return std::make_unique<SecretStoreLinux>();
#elif defined(Q_OS_WIN)
    qCDebug(lcAuth) << "SecretStoreFactory: Creating Windows implementation (Credential Manager)";
    return std::make_unique<SecretStoreWindows>();
#else
    qCWarning(lcAuth) << "SecretStoreFactory: No secure storage available for this platform";
    return nullptr;
#endif
}
