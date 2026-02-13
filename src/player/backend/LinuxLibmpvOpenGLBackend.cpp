#include "LinuxLibmpvOpenGLBackend.h"

#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcLinuxLibmpvBackend, "bloom.playback.backend.linux.libmpv")

LinuxLibmpvOpenGLBackend::LinuxLibmpvOpenGLBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_runtimeSupported(isRuntimeSupported())
{
}

bool LinuxLibmpvOpenGLBackend::isRuntimeSupported()
{
#if defined(Q_OS_LINUX)
    const QString rhiBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    return rhiBackend.isEmpty() || rhiBackend == QStringLiteral("opengl");
#else
    return false;
#endif
}

QString LinuxLibmpvOpenGLBackend::backendName() const
{
    return QStringLiteral("linux-libmpv-opengl");
}

void LinuxLibmpvOpenGLBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    Q_UNUSED(mpvBin);
    Q_UNUSED(args);
    Q_UNUSED(mediaUrl);

    if (!m_runtimeSupported) {
        emit errorOccurred(QStringLiteral("Linux libmpv backend requires OpenGL scenegraph backend"));
        return;
    }

    if (m_running) {
        return;
    }

    m_running = true;
    emit stateChanged(true);
}

void LinuxLibmpvOpenGLBackend::stopMpv()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    emit stateChanged(false);
}

bool LinuxLibmpvOpenGLBackend::isRunning() const
{
    return m_running;
}

void LinuxLibmpvOpenGLBackend::sendCommand(const QStringList &command)
{
    Q_UNUSED(command);
    qCWarning(lcLinuxLibmpvBackend) << "sendCommand is not implemented yet";
}

void LinuxLibmpvOpenGLBackend::sendVariantCommand(const QVariantList &command)
{
    Q_UNUSED(command);
    qCWarning(lcLinuxLibmpvBackend) << "sendVariantCommand is not implemented yet";
}

bool LinuxLibmpvOpenGLBackend::supportsEmbeddedVideo() const
{
    return m_runtimeSupported;
}

bool LinuxLibmpvOpenGLBackend::attachVideoTarget(QObject *target)
{
    if (!m_runtimeSupported || target == nullptr) {
        return false;
    }

    m_videoTarget = target;
    return true;
}

void LinuxLibmpvOpenGLBackend::detachVideoTarget(QObject *target)
{
    if (!m_videoTarget) {
        return;
    }

    if (target == nullptr || m_videoTarget == target) {
        m_videoTarget.clear();
    }
}

void LinuxLibmpvOpenGLBackend::setVideoViewport(const QRectF &viewport)
{
    m_videoViewport = viewport;
}
