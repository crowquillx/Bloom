#include "LinuxMpvBackend.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QVector>
#include <QtGlobal>

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}
#endif

Q_LOGGING_CATEGORY(lcLinuxLibmpvBackend, "bloom.playback.backend.linux.libmpv")

LinuxMpvBackend::LinuxMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_runtimeSupported(isRuntimeSupported())
{
}

LinuxMpvBackend::~LinuxMpvBackend()
{
    teardownMpv();
}

bool LinuxMpvBackend::isRuntimeSupported()
{
#if defined(Q_OS_LINUX)
    const QString rhiBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    return rhiBackend.isEmpty() || rhiBackend == QStringLiteral("opengl");
#else
    return false;
#endif
}

QString LinuxMpvBackend::backendName() const
{
    return QStringLiteral("linux-libmpv-opengl");
}

void LinuxMpvBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    Q_UNUSED(mpvBin);

    if (!m_runtimeSupported) {
        emit errorOccurred(QStringLiteral("Linux libmpv backend requires OpenGL scenegraph backend"));
        return;
    }

    teardownMpv();

    if (!initializeMpv()) {
        emit errorOccurred(QStringLiteral("Failed to initialize libmpv backend"));
        return;
    }

    applyMpvArgs(args);

    if (!queueLoadFile(mediaUrl)) {
        emit errorOccurred(QStringLiteral("Failed to load media with libmpv backend"));
        teardownMpv();
        return;
    }

    m_running = true;
    emit stateChanged(true);
}

void LinuxMpvBackend::stopMpv()
{
    if (m_mpvHandle != nullptr) {
#if defined(BLOOM_HAS_LIBMPV)
        mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
        const char *command[] = {"stop", nullptr};
        mpv_command_async(handle, 0, command);
#endif
    }

    teardownMpv();
}

bool LinuxMpvBackend::isRunning() const
{
    return m_running;
}

void LinuxMpvBackend::sendCommand(const QStringList &command)
{
    if (!m_mpvHandle) {
        qCWarning(lcLinuxLibmpvBackend) << "sendCommand called without active mpv handle";
        return;
    }

#if defined(BLOOM_HAS_LIBMPV)
    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);

    m_commandScratch.clear();
    m_commandScratch.reserve(command.size());

    QVector<const char *> commandPtr;
    commandPtr.reserve(command.size() + 1);

    for (const QString &part : command) {
        m_commandScratch.append(part.toUtf8());
        commandPtr.append(m_commandScratch.constLast().constData());
    }
    commandPtr.append(nullptr);

    if (mpv_command_async(handle, 0, commandPtr.constData()) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_command_async failed for command" << command;
    }
#else
    Q_UNUSED(command);
#endif
}

void LinuxMpvBackend::sendVariantCommand(const QVariantList &command)
{
    QStringList converted;
    converted.reserve(command.size());

    for (const QVariant &part : command) {
        if (part.typeId() == QMetaType::Bool) {
            converted.append(part.toBool() ? QStringLiteral("yes") : QStringLiteral("no"));
        } else {
            converted.append(part.toString());
        }
    }

    sendCommand(converted);
}

bool LinuxMpvBackend::supportsEmbeddedVideo() const
{
    return m_runtimeSupported;
}

bool LinuxMpvBackend::attachVideoTarget(QObject *target)
{
    if (!m_runtimeSupported || target == nullptr) {
        return false;
    }

    m_videoTarget = target;
    return true;
}

void LinuxMpvBackend::detachVideoTarget(QObject *target)
{
    if (!m_videoTarget) {
        return;
    }

    if (target == nullptr || m_videoTarget == target) {
        m_videoTarget.clear();
    }
}

void LinuxMpvBackend::setVideoViewport(const QRectF &viewport)
{
    m_videoViewport = viewport;
}

bool LinuxMpvBackend::initializeMpv()
{
#if !defined(BLOOM_HAS_LIBMPV)
    qCWarning(lcLinuxLibmpvBackend) << "BLOOM_HAS_LIBMPV not enabled; backend is scaffold-only";
    return false;
#else
    mpv_handle *handle = mpv_create();
    if (!handle) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_create failed";
        return false;
    }

    mpv_set_wakeup_callback(handle, &LinuxMpvBackend::wakeupCallback, this);

    if (mpv_initialize(handle) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_initialize failed";
        mpv_terminate_destroy(handle);
        return false;
    }

    m_mpvHandle = handle;
    observeMpvProperties();
    return true;
#endif
}

void LinuxMpvBackend::teardownMpv()
{
    if (!m_mpvHandle) {
        if (m_running) {
            m_running = false;
            emit stateChanged(false);
        }
        return;
    }

#if defined(BLOOM_HAS_LIBMPV)
    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    mpv_set_wakeup_callback(handle, nullptr, nullptr);
    mpv_terminate_destroy(handle);
#endif

    m_mpvHandle = nullptr;
    m_eventDispatchQueued = false;

    if (m_running) {
        m_running = false;
        emit stateChanged(false);
    }
}

void LinuxMpvBackend::processMpvEvents()
{
    m_eventDispatchQueued = false;

#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);

    while (true) {
        mpv_event *event = mpv_wait_event(handle, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (event->event_id) {
        case MPV_EVENT_SHUTDOWN:
            teardownMpv();
            break;
        case MPV_EVENT_END_FILE:
            emit playbackEnded();
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            mpv_event_property *property = static_cast<mpv_event_property *>(event->data);
            if (!property || !property->name || property->format == MPV_FORMAT_NONE || !property->data) {
                break;
            }

            const QString propertyName = QString::fromUtf8(property->name);
            QVariant value;
            switch (property->format) {
            case MPV_FORMAT_DOUBLE:
                value = *static_cast<double *>(property->data);
                break;
            case MPV_FORMAT_INT64:
                value = static_cast<qlonglong>(*static_cast<qint64 *>(property->data));
                break;
            case MPV_FORMAT_FLAG:
                value = (*static_cast<int *>(property->data) != 0);
                break;
            case MPV_FORMAT_STRING:
                value = QString::fromUtf8(static_cast<const char *>(property->data));
                break;
            default:
                break;
            }

            if (value.isValid()) {
                handlePropertyChange(propertyName, value);
            }
            break;
        }
        default:
            break;
        }
    }
#endif
}

void LinuxMpvBackend::observeMpvProperties()
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    mpv_observe_property(handle, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 0, "aid", MPV_FORMAT_INT64);
    mpv_observe_property(handle, 0, "sid", MPV_FORMAT_INT64);
#endif
}

void LinuxMpvBackend::applyMpvArgs(const QStringList &args)
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);

    for (const QString &arg : args) {
        if (!arg.startsWith("--")) {
            continue;
        }

        const QString option = arg.mid(2);
        const int equalsIndex = option.indexOf('=');

        QString name = option;
        QString value = QStringLiteral("yes");
        if (equalsIndex >= 0) {
            name = option.left(equalsIndex);
            value = option.mid(equalsIndex + 1);
        }

        if (name == QStringLiteral("input-ipc-server") || name == QStringLiteral("idle")) {
            continue;
        }

        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray valueUtf8 = value.toUtf8();
        mpv_set_option_string(handle, nameUtf8.constData(), valueUtf8.constData());
    }
#else
    Q_UNUSED(args);
#endif
}

bool LinuxMpvBackend::queueLoadFile(const QString &mediaUrl)
{
#if !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(mediaUrl);
    return false;
#else
    if (!m_mpvHandle || mediaUrl.isEmpty()) {
        return false;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    const QByteArray urlUtf8 = mediaUrl.toUtf8();
    const char *command[] = {"loadfile", urlUtf8.constData(), "replace", nullptr};
    return mpv_command_async(handle, 0, command) >= 0;
#endif
}

void LinuxMpvBackend::handlePropertyChange(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("time-pos")) {
        emit positionChanged(value.toDouble());
        return;
    }

    if (name == QStringLiteral("duration")) {
        emit durationChanged(value.toDouble());
        return;
    }

    if (name == QStringLiteral("pause")) {
        emit pauseChanged(value.toBool());
        return;
    }

    if (name == QStringLiteral("paused-for-cache")) {
        emit pausedForCacheChanged(value.toBool());
        return;
    }

    if (name == QStringLiteral("aid")) {
        emit audioTrackChanged(value.toInt());
        return;
    }

    if (name == QStringLiteral("sid")) {
        emit subtitleTrackChanged(value.toInt());
    }
}

void LinuxMpvBackend::wakeupCallback(void *ctx)
{
    LinuxMpvBackend *self = static_cast<LinuxMpvBackend *>(ctx);
    if (!self || self->m_eventDispatchQueued) {
        return;
    }

    self->m_eventDispatchQueued = true;
    QMetaObject::invokeMethod(self, [self]() {
        self->processMpvEvents();
    }, Qt::QueuedConnection);
}
