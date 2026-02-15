#include "LinuxMpvBackend.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QImage>
#include <QMutexLocker>
#include <QVector>
#include <QtGlobal>
#include <QtMath>
#include <clocale>
#include <algorithm>

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
}
#endif

Q_LOGGING_CATEGORY(lcLinuxLibmpvBackend, "bloom.playback.backend.linux.libmpv")

namespace {
bool isTruthyEnv(const char *name)
{
    return qEnvironmentVariableIntValue(name) == 1;
}

const char *mpvLogLevelForEnv(bool debugLogging)
{
    if (debugLogging && isTruthyEnv("BLOOM_LINUX_LIBMPV_MPV_DEBUG")) {
        return "debug";
    }
    if (debugLogging) {
        return "info";
    }
    return "warn";
}

const char *graphicsApiToString(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Unknown: return "Unknown";
    case QSGRendererInterface::Software: return "Software";
    case QSGRendererInterface::OpenVG: return "OpenVG";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Direct3D11: return "Direct3D11";
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Null: return "Null";
    }
    return "Unrecognized";
}

#if defined(BLOOM_HAS_LIBMPV)
const char *endFileReasonToString(int reason)
{
    switch (reason) {
    #if defined(MPV_END_FILE_REASON_EOF)
    case MPV_END_FILE_REASON_EOF: return "eof";
    #endif
    #if defined(MPV_END_FILE_REASON_STOP)
    case MPV_END_FILE_REASON_STOP: return "stop";
    #endif
    #if defined(MPV_END_FILE_REASON_QUIT)
    case MPV_END_FILE_REASON_QUIT: return "quit";
    #endif
    #if defined(MPV_END_FILE_REASON_ERROR)
    case MPV_END_FILE_REASON_ERROR: return "error";
    #endif
    #if defined(MPV_END_FILE_REASON_REDIRECT)
    case MPV_END_FILE_REASON_REDIRECT: return "redirect";
    #endif
    default: return "unknown";
    }
}
#endif

bool isEmbeddedUnsafeOptionName(const QString &name)
{
    if (name == QStringLiteral("input-ipc-server")
        || name == QStringLiteral("idle")
        || name == QStringLiteral("vo")
        || name == QStringLiteral("hwdec")
        || name == QStringLiteral("wid")
        || name == QStringLiteral("fullscreen")
        || name == QStringLiteral("gpu-context")
        || name == QStringLiteral("gpu-api")) {
        return true;
    }

    return name.startsWith(QStringLiteral("vulkan-"))
        || name.startsWith(QStringLiteral("opengl-"))
        || name.startsWith(QStringLiteral("wayland-"))
        || name.startsWith(QStringLiteral("x11-"));
}
}

LinuxMpvBackend::LinuxMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_runtimeSupported(isRuntimeSupported())
    , m_allowFbo0Fallback(isTruthyEnv("BLOOM_LINUX_LIBMPV_ALLOW_FBO0"))
    , m_debugLogging(isTruthyEnv("BLOOM_LINUX_LIBMPV_DEBUG"))
    , m_enableSoftwareFallback(!qEnvironmentVariableIsSet("BLOOM_LINUX_LIBMPV_SW_FALLBACK")
                                   || isTruthyEnv("BLOOM_LINUX_LIBMPV_SW_FALLBACK"))
    , m_forceSoftwareRender(isTruthyEnv("BLOOM_LINUX_LIBMPV_FORCE_SW"))
{
    if (m_debugLogging) {
        qCInfo(lcLinuxLibmpvBackend)
            << "LinuxMpvBackend init:"
            << "runtimeSupported=" << m_runtimeSupported
            << "allowFbo0Fallback=" << m_allowFbo0Fallback
            << "softwareFallbackEnabled=" << m_enableSoftwareFallback
            << "forceSoftwareRender=" << m_forceSoftwareRender;
    }
}

LinuxMpvBackend::~LinuxMpvBackend()
{
    teardownMpv();
}

bool LinuxMpvBackend::isRuntimeSupported()
{
#if defined(Q_OS_LINUX) && defined(BLOOM_HAS_LIBMPV)
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
    m_consecutiveZeroFboFrames = 0;
    m_renderFailureQueued = false;
    m_switchedToSoftwareFallback = false;
    m_renderMode = m_forceSoftwareRender ? RenderMode::Software : RenderMode::OpenGL;
    m_swRenderImage = QImage();
    m_swFrameDispatchQueued.store(false, std::memory_order_release);
    {
        QMutexLocker locker(&m_swLatestFrameMutex);
        m_swLatestFrame = QImage();
    }

    if (m_debugLogging) {
        qCInfo(lcLinuxLibmpvBackend) << "startMpv args" << args;
    }

    if (!initializeMpv(args)) {
        emit errorOccurred(QStringLiteral("Failed to initialize libmpv backend"));
        return;
    }

    if (!queueLoadFile(mediaUrl)) {
        emit errorOccurred(QStringLiteral("Failed to load media with libmpv backend"));
        teardownMpv();
        return;
    }

    if (m_renderWindow) {
        m_renderWindow->update();
    } else {
        qWarning() << "LinuxMpvBackend: startMpv without render window; waiting for target/window attach";
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

    if (mpv_command_async(handle, 0, commandPtr.data()) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_command_async failed for command" << command;
    }
#else
    Q_UNUSED(command);
#endif
}

void LinuxMpvBackend::sendVariantCommand(const QVariantList &command)
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle) {
        qCWarning(lcLinuxLibmpvBackend) << "sendVariantCommand called without active mpv handle";
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);

    if (command.size() >= 3
        && command.at(0).typeId() == QMetaType::QString
        && command.at(0).toString() == QStringLiteral("set_property")) {
        const QString propertyName = command.at(1).toString();
        const QVariant propertyValue = command.at(2);
        const QByteArray propertyNameUtf8 = propertyName.toUtf8();

        int status = MPV_ERROR_GENERIC;
        switch (propertyValue.typeId()) {
        case QMetaType::Bool: {
            int flagValue = propertyValue.toBool() ? 1 : 0;
            status = mpv_set_property(handle, propertyNameUtf8.constData(), MPV_FORMAT_FLAG, &flagValue);
            break;
        }
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::UChar: {
            qint64 intValue = propertyValue.toLongLong();
            status = mpv_set_property(handle, propertyNameUtf8.constData(), MPV_FORMAT_INT64, &intValue);
            break;
        }
        case QMetaType::Float:
        case QMetaType::Double: {
            double doubleValue = propertyValue.toDouble();
            status = mpv_set_property(handle, propertyNameUtf8.constData(), MPV_FORMAT_DOUBLE, &doubleValue);
            break;
        }
        default: {
            const QByteArray valueUtf8 = propertyValue.toString().toUtf8();
            status = mpv_set_property_string(handle, propertyNameUtf8.constData(), valueUtf8.constData());
            break;
        }
        }

        if (status < 0) {
            qCWarning(lcLinuxLibmpvBackend)
                << "Direct libmpv set_property failed:"
                << QString::fromUtf8(mpv_error_string(status))
                << "property=" << propertyName
                << "value=" << propertyValue;
            return;
        }
        return;
    }

    QVector<mpv_node> commandNodes(command.size());
    QVector<QByteArray> commandStrings;
    commandStrings.reserve(command.size());

    for (int index = 0; index < command.size(); ++index) {
        const QVariant &part = command.at(index);
        mpv_node &node = commandNodes[index];

        switch (part.typeId()) {
        case QMetaType::Bool:
            node.format = MPV_FORMAT_FLAG;
            node.u.flag = part.toBool() ? 1 : 0;
            break;
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::UChar:
            node.format = MPV_FORMAT_INT64;
            node.u.int64 = part.toLongLong();
            break;
        case QMetaType::Float:
        case QMetaType::Double:
            node.format = MPV_FORMAT_DOUBLE;
            node.u.double_ = part.toDouble();
            break;
        default: {
            node.format = MPV_FORMAT_STRING;
            commandStrings.append(part.toString().toUtf8());
            node.u.string = commandStrings.last().data();
            break;
        }
        }
    }

    mpv_node_list commandList;
    commandList.num = commandNodes.size();
    commandList.values = commandNodes.data();
    commandList.keys = nullptr;

    mpv_node commandArray;
    commandArray.format = MPV_FORMAT_NODE_ARRAY;
    commandArray.u.list = &commandList;

    if (mpv_command_node_async(handle, 0, &commandArray) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_command_node_async failed for command" << command;
    }
#else
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
#endif
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

    QQuickItem *item = qobject_cast<QQuickItem *>(target);
    if (!item) {
        qCWarning(lcLinuxLibmpvBackend) << "attachVideoTarget expects QQuickItem-compatible target";
        return false;
    }

    if (m_videoTarget == item) {
        return true;
    }

    if (m_videoTarget) {
        detachVideoTarget(m_videoTarget);
    }

    m_videoTarget = item;
    qInfo() << "LinuxMpvBackend: attached video target" << item;

    if (item->window()) {
        handleWindowChanged(item->window());
    }

    m_videoTargetWindowConnection = connect(item, &QQuickItem::windowChanged,
                                            this, &LinuxMpvBackend::handleWindowChanged);

    return true;
}

void LinuxMpvBackend::detachVideoTarget(QObject *target)
{
    if (!m_videoTarget) {
        return;
    }

    if (target == nullptr || m_videoTarget == target) {
        if (m_videoTarget) {
            QMetaObject::invokeMethod(m_videoTarget, "clearSoftwareFrame", Qt::QueuedConnection);
        }

        if (m_videoTargetWindowConnection) {
            disconnect(m_videoTargetWindowConnection);
            m_videoTargetWindowConnection = {};
        }

        handleWindowChanged(nullptr);
        m_videoTarget.clear();
    }
}

void LinuxMpvBackend::setVideoViewport(const QRectF &viewport)
{
    // Ignore clearly invalid/tiny viewport updates that can occur during early layout churn.
    // A 1px-wide viewport leads to effectively invisible output and stalled startup UX.
    if (viewport.width() < 16.0 || viewport.height() < 16.0) {
        if (m_debugLogging) {
            qCInfo(lcLinuxLibmpvBackend) << "Ignoring tiny viewport update" << viewport;
        }
        return;
    }

    if (m_debugLogging) {
        qCInfo(lcLinuxLibmpvBackend) << "Accepted viewport update" << viewport;
    }
    m_videoViewport = viewport;
}

bool LinuxMpvBackend::initializeMpv(const QStringList &args)
{
#if !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(args);
    qCWarning(lcLinuxLibmpvBackend) << "BLOOM_HAS_LIBMPV not enabled; backend is scaffold-only";
    return false;
#else
    // libmpv requires C numeric locale; enforce it at the callsite as well.
    if (setlocale(LC_NUMERIC, "C") == nullptr) {
        qCWarning(lcLinuxLibmpvBackend) << "Failed to enforce LC_NUMERIC=C before mpv_create";
    }

    mpv_handle *handle = mpv_create();
    if (!handle) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_create failed";
        return false;
    }

    mpv_set_wakeup_callback(handle, &LinuxMpvBackend::wakeupCallback, this);
    mpv_request_log_messages(handle, mpvLogLevelForEnv(m_debugLogging));

    m_mpvHandle = handle;
    applyMpvArgs(handle, args);

    if (mpv_initialize(handle) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_initialize failed";
        mpv_terminate_destroy(handle);
        m_mpvHandle = nullptr;
        return false;
    }

    observeMpvProperties(handle);
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

    teardownRenderContext();

#if defined(BLOOM_HAS_LIBMPV)
    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    mpv_set_wakeup_callback(handle, nullptr, nullptr);
    mpv_terminate_destroy(handle);
#endif

    m_mpvHandle = nullptr;
    m_eventDispatchQueued.store(false, std::memory_order_release);

    if (m_running) {
        m_running = false;
        emit stateChanged(false);
    }
}

void LinuxMpvBackend::processMpvEvents()
{
    m_eventDispatchQueued.store(false, std::memory_order_release);

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
            return;
        case MPV_EVENT_END_FILE:
            if (event->data) {
                const mpv_event_end_file *endFile = static_cast<const mpv_event_end_file *>(event->data);
                qCInfo(lcLinuxLibmpvBackend)
                    << "MPV_EVENT_END_FILE reason=" << endFileReasonToString(endFile->reason)
                    << "error=" << QString::fromUtf8(mpv_error_string(endFile->error));
            } else {
                qCInfo(lcLinuxLibmpvBackend) << "MPV_EVENT_END_FILE (no data)";
            }
            emit playbackEnded();
            break;
        case MPV_EVENT_CLIENT_MESSAGE: {
            mpv_event_client_message *message = static_cast<mpv_event_client_message *>(event->data);
            if (!message || message->num_args <= 0 || !message->args) {
                break;
            }

            const QString messageName = QString::fromUtf8(message->args[0] ? message->args[0] : "");
            if (messageName.isEmpty()) {
                break;
            }

            QStringList messageArgs;
            for (int index = 1; index < message->num_args; ++index) {
                messageArgs.append(QString::fromUtf8(message->args[index] ? message->args[index] : ""));
            }

            emit scriptMessage(messageName, messageArgs);
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            const mpv_event_log_message *logMessage = static_cast<const mpv_event_log_message *>(event->data);
            if (!logMessage || !logMessage->text) {
                break;
            }
            const QString prefix = QString::fromUtf8(logMessage->prefix ? logMessage->prefix : "");
            const QString text = QString::fromUtf8(logMessage->text).trimmed();
            if (!text.isEmpty()) {
                qWarning().noquote() << QStringLiteral("[libmpv][%1] %2").arg(prefix, text);
            }
            break;
        }
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
            case MPV_FORMAT_STRING: {
                const char *str = *static_cast<char **>(property->data);
                if (!str) {
                    break;
                }
                value = QString::fromUtf8(str);
                break;
            }
            case MPV_FORMAT_NODE: {
                const mpv_node *node = static_cast<const mpv_node *>(property->data);
                if (!node) {
                    break;
                }

                switch (node->format) {
                case MPV_FORMAT_INT64:
                    value = static_cast<qlonglong>(node->u.int64);
                    break;
                case MPV_FORMAT_DOUBLE:
                    value = node->u.double_;
                    break;
                case MPV_FORMAT_FLAG:
                    value = (node->u.flag != 0);
                    break;
                case MPV_FORMAT_STRING:
                    value = QString::fromUtf8(node->u.string ? node->u.string : "");
                    break;
                default:
                    break;
                }
                break;
            }
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

void LinuxMpvBackend::observeMpvProperties(void *handlePtr)
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!handlePtr) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(handlePtr);
    mpv_observe_property(handle, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 0, "aid", MPV_FORMAT_NODE);
    mpv_observe_property(handle, 0, "sid", MPV_FORMAT_NODE);
    mpv_observe_property(handle, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "mute", MPV_FORMAT_FLAG);
#endif
}

void LinuxMpvBackend::applyMpvArgs(void *handlePtr, const QStringList &args)
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!handlePtr) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(handlePtr);

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

        if (isEmbeddedUnsafeOptionName(name)) {
            if (m_debugLogging) {
                qCInfo(lcLinuxLibmpvBackend) << "Skipping unsafe embedded mpv option" << name;
            }
            continue;
        }

        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray valueUtf8 = value.toUtf8();
        mpv_set_option_string(handle, nameUtf8.constData(), valueUtf8.constData());
    }

    // Prefer software decode first on Linux embedded path to avoid HW interop failures.
    mpv_set_option_string(handle, "hwdec", "no");

    // Render API backends must force libmpv VO after profile/arg application.
    mpv_set_option_string(handle, "vo", "libmpv");
#else
    Q_UNUSED(handlePtr);
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
        const int mpvTrackId = value.toInt();
        emit audioTrackChanged(mpvTrackId > 0 ? mpvTrackId - 1 : -1);
        return;
    }

    if (name == QStringLiteral("sid")) {
        const int mpvTrackId = value.toInt();
        emit subtitleTrackChanged(mpvTrackId > 0 ? mpvTrackId - 1 : -1);
        return;
    }

    if (name == QStringLiteral("volume")) {
        emit volumeChanged(qRound(value.toDouble()));
        return;
    }

    if (name == QStringLiteral("mute")) {
        emit muteChanged(value.toBool());
    }
}

void LinuxMpvBackend::handleWindowChanged(QQuickWindow *window)
{
    if (m_renderWindow == window) {
        return;
    }

    if (m_beforeRenderingConnection) {
        disconnect(m_beforeRenderingConnection);
        m_beforeRenderingConnection = {};
    }
    if (m_sceneGraphInitializedConnection) {
        disconnect(m_sceneGraphInitializedConnection);
        m_sceneGraphInitializedConnection = {};
    }
    if (m_sceneGraphInvalidatedConnection) {
        disconnect(m_sceneGraphInvalidatedConnection);
        m_sceneGraphInvalidatedConnection = {};
    }

    teardownRenderContext();
    m_renderWindow = window;

    if (!m_renderWindow) {
        return;
    }

    m_renderWindow->setColor(Qt::transparent);

    if (m_debugLogging && m_renderWindow->rendererInterface()) {
        qCInfo(lcLinuxLibmpvBackend)
            << "handleWindowChanged graphics api:"
            << graphicsApiToString(m_renderWindow->rendererInterface()->graphicsApi());
    }

    m_sceneGraphInitializedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInitialized,
                                                this, &LinuxMpvBackend::initializeRenderContextIfNeeded,
                                                Qt::DirectConnection);
    m_sceneGraphInvalidatedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInvalidated,
                                                this, &LinuxMpvBackend::teardownRenderContext,
                                                Qt::DirectConnection);
    // On some Wayland/OpenGL stacks the render target is not yet materialized at
    // beforeRenderPassRecording for our external GL usage, yielding persistent FBO=0.
    // beforeRendering provides a more reliable point for libmpv's OpenGL render call.
    m_beforeRenderingConnection = connect(m_renderWindow, &QQuickWindow::beforeRendering,
                                          this, &LinuxMpvBackend::renderFrame,
                                          Qt::DirectConnection);
    qInfo() << "LinuxMpvBackend: connected render hook to window" << m_renderWindow.data();

    if (m_renderWindow->isSceneGraphInitialized()) {
        initializeRenderContextIfNeeded();
    }
}

void LinuxMpvBackend::initializeRenderContextIfNeeded()
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle || m_mpvRenderContext) {
        return;
    }

    if (!m_renderWindow || !m_renderWindow->rendererInterface()) {
        return;
    }

    if (createRenderContext(m_renderMode)) {
        return;
    }

    if (m_renderMode == RenderMode::OpenGL
        && m_enableSoftwareFallback
        && switchToSoftwareRenderMode("OpenGL render context init failed")) {
        return;
    }

    if (!m_renderFailureQueued) {
        m_renderFailureQueued = true;
        QMetaObject::invokeMethod(this, [this]() {
            emit errorOccurred(QStringLiteral("linux-libmpv-render-unavailable: mpv_render_context_create failed"));
            stopMpv();
        }, Qt::QueuedConnection);
    }
#endif
}

bool LinuxMpvBackend::createRenderContext(RenderMode mode)
{
#if !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(mode);
    return false;
#else
    if (!m_mpvHandle) {
        return false;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    int advancedControl = 1;

    mpv_render_context *renderContext = nullptr;
    if (mode == RenderMode::OpenGL) {
        if (!m_renderWindow || !m_renderWindow->rendererInterface()) {
            return false;
        }

        const auto graphicsApi = m_renderWindow->rendererInterface()->graphicsApi();
        if (graphicsApi != QSGRendererInterface::OpenGL) {
            qCWarning(lcLinuxLibmpvBackend)
                << "Embedded render unavailable: Qt graphics API is"
                << graphicsApiToString(graphicsApi)
                << "(requires OpenGL)";
            return false;
        }

        mpv_opengl_init_params glInitParams = {&LinuxMpvBackend::getProcAddress, this};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        if (mpv_render_context_create(&renderContext, handle, params) < 0) {
            return false;
        }
    } else {
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        if (mpv_render_context_create(&renderContext, handle, params) < 0) {
            return false;
        }
    }

    m_mpvRenderContext = renderContext;
    m_acceptRenderUpdates.store(true, std::memory_order_release);
    mpv_render_context_set_update_callback(renderContext, &LinuxMpvBackend::renderUpdateCallback, this);
    qCInfo(lcLinuxLibmpvBackend)
        << "mpv_render_context created with mode"
        << (mode == RenderMode::OpenGL ? "OpenGL" : "Software");
    return true;
#endif
}

bool LinuxMpvBackend::switchToSoftwareRenderMode(const char *reason)
{
#if !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(reason);
    return false;
#else
    if (!m_enableSoftwareFallback || m_renderMode == RenderMode::Software) {
        return false;
    }

    qCWarning(lcLinuxLibmpvBackend) << "Switching embedded render mode to software fallback. Reason:" << reason;
    teardownRenderContext();
    m_renderMode = RenderMode::Software;
    m_switchedToSoftwareFallback = true;
    m_consecutiveZeroFboFrames = 0;
    return createRenderContext(RenderMode::Software);
#endif
}

void LinuxMpvBackend::teardownRenderContext()
{
#if defined(BLOOM_HAS_LIBMPV)
    m_acceptRenderUpdates.store(false, std::memory_order_release);
    m_renderUpdateQueued.store(false, std::memory_order_release);
    m_swFrameDispatchQueued.store(false, std::memory_order_release);

    if (!m_mpvRenderContext) {
        return;
    }

    mpv_render_context *renderContext = static_cast<mpv_render_context *>(m_mpvRenderContext);
    mpv_render_context_set_update_callback(renderContext, nullptr, nullptr);
    mpv_render_context_free(renderContext);
    m_mpvRenderContext = nullptr;
    m_swRenderImage = QImage();
    {
        QMutexLocker locker(&m_swLatestFrameMutex);
        m_swLatestFrame = QImage();
    }
#endif
}

void LinuxMpvBackend::renderFrame()
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_renderWindow || !m_videoTarget) {
        return;
    }

    if (!m_mpvRenderContext) {
        initializeRenderContextIfNeeded();
        if (!m_mpvRenderContext) {
            return;
        }
    }

    const qreal dpr = m_renderWindow->effectiveDevicePixelRatio();

    QRectF viewport = m_videoViewport;
    if (viewport.width() < 16.0 || viewport.height() < 16.0) {
        viewport = QRectF(0.0, 0.0,
                          m_renderWindow->width(),
                          m_renderWindow->height());
        if (m_debugLogging) {
            qCInfo(lcLinuxLibmpvBackend) << "Using full-window fallback viewport" << viewport;
        }
    }

    const QRectF windowBounds(0.0, 0.0, m_renderWindow->width(), m_renderWindow->height());
    viewport = viewport.intersected(windowBounds);
    if (viewport.width() <= 0.0 || viewport.height() <= 0.0) {
        return;
    }

    const int viewportWidth = qMax(1, static_cast<int>(viewport.width() * dpr));
    const int viewportHeight = qMax(1, static_cast<int>(viewport.height() * dpr));
    if (m_debugLogging) {
        qCInfo(lcLinuxLibmpvBackend)
            << "renderFrame geometry:"
            << "window=" << m_renderWindow->width() << "x" << m_renderWindow->height()
            << "dpr=" << dpr
            << "viewportLogical=" << viewport
            << "viewportPx=" << viewportWidth << "x" << viewportHeight;
    }

    if (m_renderMode == RenderMode::Software) {
        if (m_swRenderImage.size() != QSize(viewportWidth, viewportHeight)
            || m_swRenderImage.format() != QImage::Format_RGBX8888) {
            m_swRenderImage = QImage(viewportWidth, viewportHeight, QImage::Format_RGBX8888);
        }
        if (m_swRenderImage.isNull()) {
            return;
        }
        m_swRenderImage.fill(Qt::black);

        int swSize[2] = {viewportWidth, viewportHeight};
        char format[] = "rgb0";
        void *pixels = m_swRenderImage.bits();
        size_t swStride = static_cast<size_t>(m_swRenderImage.bytesPerLine());

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, swSize},
            {MPV_RENDER_PARAM_SW_FORMAT, format},
            {MPV_RENDER_PARAM_SW_STRIDE, &swStride},
            {MPV_RENDER_PARAM_SW_POINTER, pixels},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        mpv_render_context_render(static_cast<mpv_render_context *>(m_mpvRenderContext), params);

        if (!m_videoTarget) {
            if (!m_renderFailureQueued) {
                m_renderFailureQueued = true;
                QMetaObject::invokeMethod(this, [this]() {
                    emit errorOccurred(QStringLiteral("linux-libmpv-render-unavailable: software render target unavailable"));
                    stopMpv();
                }, Qt::QueuedConnection);
            }
            return;
        }

        const QImage frameCopy = m_swRenderImage.copy();
        {
            QMutexLocker locker(&m_swLatestFrameMutex);
            m_swLatestFrame = frameCopy;
        }

        bool expected = false;
        if (m_swFrameDispatchQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_swFrameDispatchQueued.store(false, std::memory_order_release);
                const QPointer<QObject> target = m_videoTarget;
                if (!target) {
                    return;
                }

                QImage latestFrame;
                {
                    QMutexLocker locker(&m_swLatestFrameMutex);
                    latestFrame = m_swLatestFrame;
                }

                if (!latestFrame.isNull()) {
                    QMetaObject::invokeMethod(target,
                                              "setSoftwareFrame",
                                              Qt::QueuedConnection,
                                              Q_ARG(QImage, latestFrame));
                }
            }, Qt::QueuedConnection);
        }
        return;
    }

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        if (m_debugLogging) {
            qCWarning(lcLinuxLibmpvBackend) << "renderFrame skipped: no current OpenGL context";
        }
        return;
    }

    QOpenGLFunctions *gl = context->functions();
    if (!gl) {
        return;
    }

    GLint previousFbo = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLint previousScissorBox[4] = {0, 0, 0, 0};
    GLboolean previousColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    const GLboolean previousScissorEnabled = gl->glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean previousBlendEnabled = gl->glIsEnabled(GL_BLEND);
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    gl->glGetIntegerv(GL_VIEWPORT, previousViewport);
    gl->glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    gl->glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    const GLint targetFbo = previousFbo != 0
        ? previousFbo
        : static_cast<GLint>(context->defaultFramebufferObject());
    if (m_debugLogging) {
        qCInfo(lcLinuxLibmpvBackend)
            << "renderFrame framebuffers previous=" << previousFbo
            << "default=" << context->defaultFramebufferObject()
            << "target=" << targetFbo;
    }
    if (targetFbo == 0) {
        ++m_consecutiveZeroFboFrames;
        static bool sLoggedZeroFbo = false;
        if (!sLoggedZeroFbo) {
            qWarning() << "LinuxMpvBackend: rendering via FBO 0 fallback";
            sLoggedZeroFbo = true;
        }

        if (m_enableSoftwareFallback && m_consecutiveZeroFboFrames >= 3) {
            if (switchToSoftwareRenderMode("repeated invalid OpenGL framebuffer")) {
                return;
            }
        }

        if (!m_allowFbo0Fallback && m_consecutiveZeroFboFrames >= 3 && !m_renderFailureQueued) {
            m_renderFailureQueued = true;
            qCWarning(lcLinuxLibmpvBackend)
                << "Embedded render path unhealthy: repeated FBO=0 frames;"
                << "requesting fallback backend";
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("linux-libmpv-render-unavailable: invalid render framebuffer"));
                stopMpv();
            }, Qt::QueuedConnection);
            return;
        }
    } else {
        m_consecutiveZeroFboFrames = 0;
    }

    const int windowPixelHeight = qMax(1, static_cast<int>(m_renderWindow->height() * dpr));
    const int viewportX = qMax(0, static_cast<int>(viewport.x() * dpr));
    const int viewportY = std::clamp(windowPixelHeight - static_cast<int>((viewport.y() + viewport.height()) * dpr),
                                     0,
                                     windowPixelHeight - 1);
    gl->glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_BLEND);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    mpv_opengl_fbo fbo = {targetFbo, viewportWidth, viewportHeight, 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    m_renderWindow->beginExternalCommands();
    mpv_render_context_render(static_cast<mpv_render_context *>(m_mpvRenderContext), params);
    m_renderWindow->endExternalCommands();

    gl->glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    gl->glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    gl->glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
    gl->glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    if (previousScissorEnabled) {
        gl->glEnable(GL_SCISSOR_TEST);
    } else {
        gl->glDisable(GL_SCISSOR_TEST);
    }
    if (previousBlendEnabled) {
        gl->glEnable(GL_BLEND);
    } else {
        gl->glDisable(GL_BLEND);
    }
#endif
}

void LinuxMpvBackend::wakeupCallback(void *ctx)
{
    LinuxMpvBackend *self = static_cast<LinuxMpvBackend *>(ctx);
    if (!self) {
        return;
    }

    bool expected = false;
    if (!self->m_eventDispatchQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    QMetaObject::invokeMethod(self, [self]() {
        self->processMpvEvents();
    }, Qt::QueuedConnection);
}

void LinuxMpvBackend::renderUpdateCallback(void *ctx)
{
    LinuxMpvBackend *self = static_cast<LinuxMpvBackend *>(ctx);
    if (!self || !self->m_acceptRenderUpdates.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!self->m_renderUpdateQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    static std::atomic_int sUpdateCallbacks{0};
    const int count = sUpdateCallbacks.fetch_add(1, std::memory_order_relaxed);
    if (count < 5) {
        qInfo() << "LinuxMpvBackend: renderUpdateCallback queued update" << (count + 1);
    }

    QMetaObject::invokeMethod(self, [self]() {
        self->m_renderUpdateQueued.store(false, std::memory_order_release);

        if (!self->m_acceptRenderUpdates.load(std::memory_order_acquire)) {
            return;
        }

        const QPointer<QQuickWindow> window = self->m_renderWindow;
        if (!window) {
            return;
        }

        window->update();
    }, Qt::QueuedConnection);
}

void *LinuxMpvBackend::getProcAddress(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context || !name) {
        return nullptr;
    }

    return reinterpret_cast<void *>(context->getProcAddress(QByteArray(name).constData()));
}
