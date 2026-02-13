#include "PlayerBackendFactory.h"
#include "IPlayerBackend.h"
#include "ExternalMpvBackend.h"
#if defined(Q_OS_LINUX)
#include "LinuxLibmpvOpenGLBackend.h"
#endif

#include <QByteArray>
#include <QLoggingCategory>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcPlayerBackendFactory, "bloom.playback.backend.factory")

static constexpr auto kDefaultBackendName = "external-mpv-ipc";
static constexpr auto kLinuxLibmpvBackendName = "linux-libmpv-opengl";

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::create(QObject *parent)
{
    const QByteArray configuredBackend = qgetenv("BLOOM_PLAYER_BACKEND");
    if (configuredBackend.isEmpty()) {
        return createByName(QString::fromLatin1(kDefaultBackendName), parent);
    }

    return createByName(QString::fromUtf8(configuredBackend), parent);
}

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::createByName(const QString &backendName, QObject *parent)
{
#if defined(Q_OS_LINUX)
    if (backendName.compare(QString::fromLatin1(kLinuxLibmpvBackendName), Qt::CaseInsensitive) == 0) {
        if (LinuxLibmpvOpenGLBackend::isRuntimeSupported()) {
            return std::make_unique<LinuxLibmpvOpenGLBackend>(parent);
        }

        qCWarning(lcPlayerBackendFactory)
            << "Linux libmpv backend requested but OpenGL runtime requirements are not met"
            << "- falling back to" << kDefaultBackendName;
        return std::make_unique<ExternalMpvBackend>(parent);
    }
#endif

    if (backendName.compare(QString::fromLatin1(kDefaultBackendName), Qt::CaseInsensitive) == 0) {
        return std::make_unique<ExternalMpvBackend>(parent);
    }

    qCWarning(lcPlayerBackendFactory)
        << "Unknown backend requested:" << backendName
        << "- falling back to" << kDefaultBackendName;
    return std::make_unique<ExternalMpvBackend>(parent);
}
