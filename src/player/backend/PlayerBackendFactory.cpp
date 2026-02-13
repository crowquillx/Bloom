#include "PlayerBackendFactory.h"
#include "IPlayerBackend.h"
#include "ExternalMpvBackend.h"
#if defined(Q_OS_LINUX)
#include "LinuxMpvBackend.h"
#endif
#if defined(Q_OS_WIN)
#include "WindowsMpvBackend.h"
#endif

#include <QByteArray>
#include <QLoggingCategory>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcPlayerBackendFactory, "bloom.playback.backend.factory")

static constexpr auto kExternalBackendName = "external-mpv-ipc";
static constexpr auto kLinuxLibmpvBackendName = "linux-libmpv-opengl";
static constexpr auto kWinLibmpvBackendName = "win-libmpv";

#if defined(Q_OS_LINUX)
static constexpr auto kDefaultBackendName = kLinuxLibmpvBackendName;
#elif defined(Q_OS_WIN)
static constexpr auto kDefaultBackendName = kWinLibmpvBackendName;
#else
static constexpr auto kDefaultBackendName = kExternalBackendName;
#endif

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::create(QObject *parent)
{
    return create(QString(), parent);
}

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::create(const QString &configuredBackendName, QObject *parent)
{
    const QByteArray configuredBackend = qgetenv("BLOOM_PLAYER_BACKEND");
    if (!configuredBackend.isEmpty()) {
        return createByName(QString::fromUtf8(configuredBackend), parent);
    }

    const QString normalizedConfiguredBackend = configuredBackendName.trimmed();
    if (!normalizedConfiguredBackend.isEmpty()) {
        return createByName(normalizedConfiguredBackend, parent);
    }

    return createByName(QString::fromLatin1(kDefaultBackendName), parent);
}

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::createByName(const QString &backendName, QObject *parent)
{
#if defined(Q_OS_LINUX)
    if (backendName.compare(QString::fromLatin1(kLinuxLibmpvBackendName), Qt::CaseInsensitive) == 0) {
        if (LinuxMpvBackend::isRuntimeSupported()) {
            return std::make_unique<LinuxMpvBackend>(parent);
        }

        qCWarning(lcPlayerBackendFactory)
            << "Linux libmpv backend requested but OpenGL runtime requirements are not met"
            << "- falling back to" << kExternalBackendName;
        return std::make_unique<ExternalMpvBackend>(parent);
    }
#endif

    if (backendName.compare(QString::fromLatin1(kWinLibmpvBackendName), Qt::CaseInsensitive) == 0) {
#if defined(Q_OS_WIN)
    return std::make_unique<WindowsMpvBackend>(parent);
#else
        qCWarning(lcPlayerBackendFactory)
            << "Windows libmpv backend requested on unsupported platform"
            << "- falling back to" << kExternalBackendName;
        return std::make_unique<ExternalMpvBackend>(parent);
#endif
    }

    if (backendName.compare(QString::fromLatin1(kExternalBackendName), Qt::CaseInsensitive) == 0) {
        return std::make_unique<ExternalMpvBackend>(parent);
    }

    qCWarning(lcPlayerBackendFactory)
        << "Unknown backend requested:" << backendName
        << "- falling back to" << kExternalBackendName;
    return std::make_unique<ExternalMpvBackend>(parent);
}
