#include "LinuxMpvBackend.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector>
#include <QtGlobal>
#include <clocale>
#include <algorithm>

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
}
#endif

Q_LOGGING_CATEGORY(lcLinuxLibmpvBackend, "bloom.playback.backend.linux.libmpv")

LinuxMpvBackend::LinuxMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_runtimeSupported(isRuntimeSupported())
    , m_allowFbo0Fallback(qEnvironmentVariableIntValue("BLOOM_LINUX_LIBMPV_ALLOW_FBO0") == 1)
{
}

LinuxMpvBackend::~LinuxMpvBackend()
{
    teardownMpv();
}

bool LinuxMpvBackend::isRuntimeSupported()
{
#if defined(Q_OS_LINUX) && defined(BLOOM_HAS_LIBMPV)
    // Temporary guard: embedded GL render path is unreliable on some Wayland compositors
    // (e.g. FBO 0-only paths observed on niri). Allow explicit opt-in for debugging.
    const bool allowWaylandEmbedded = qEnvironmentVariableIntValue("BLOOM_ENABLE_WAYLAND_LIBMPV") == 1;
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") && !allowWaylandEmbedded) {
        return false;
    }

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

        if (name == QStringLiteral("input-ipc-server")
            || name == QStringLiteral("idle")
            || name == QStringLiteral("vo")
            || name == QStringLiteral("hwdec")
            || name == QStringLiteral("wid")
            || name == QStringLiteral("fullscreen")) {
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

    m_sceneGraphInitializedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInitialized,
                                                this, &LinuxMpvBackend::initializeRenderContextIfNeeded,
                                                Qt::DirectConnection);
    m_sceneGraphInvalidatedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInvalidated,
                                                this, &LinuxMpvBackend::teardownRenderContext,
                                                Qt::DirectConnection);
    m_beforeRenderingConnection = connect(m_renderWindow, &QQuickWindow::afterRendering,
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

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    mpv_opengl_init_params glInitParams = {&LinuxMpvBackend::getProcAddress, this};
    int advancedControl = 1;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context *renderContext = nullptr;
    if (mpv_render_context_create(&renderContext, handle, params) < 0) {
        qCWarning(lcLinuxLibmpvBackend) << "mpv_render_context_create failed";
        if (!m_renderFailureQueued) {
            m_renderFailureQueued = true;
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("linux-libmpv-render-unavailable: mpv_render_context_create failed"));
                stopMpv();
            }, Qt::QueuedConnection);
        }
        return;
    }

    m_mpvRenderContext = renderContext;
    m_acceptRenderUpdates.store(true, std::memory_order_release);
    mpv_render_context_set_update_callback(renderContext, &LinuxMpvBackend::renderUpdateCallback, this);
    qInfo() << "LinuxMpvBackend: mpv_render_context created for window" << m_renderWindow.data();
#endif
}

void LinuxMpvBackend::teardownRenderContext()
{
#if defined(BLOOM_HAS_LIBMPV)
    m_acceptRenderUpdates.store(false, std::memory_order_release);
    m_renderUpdateQueued.store(false, std::memory_order_release);

    if (!m_mpvRenderContext) {
        return;
    }

    mpv_render_context *renderContext = static_cast<mpv_render_context *>(m_mpvRenderContext);
    mpv_render_context_set_update_callback(renderContext, nullptr, nullptr);
    mpv_render_context_free(renderContext);
    m_mpvRenderContext = nullptr;
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

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        return;
    }

    QOpenGLFunctions *gl = context->functions();
    if (!gl) {
        return;
    }

    const qreal dpr = m_renderWindow->effectiveDevicePixelRatio();

    QRectF viewport = m_videoViewport;
    if (viewport.width() <= 0.0 || viewport.height() <= 0.0) {
        viewport = QRectF(0.0, 0.0,
                          m_renderWindow->width(),
                          m_renderWindow->height());
    }

    const QRectF windowBounds(0.0, 0.0, m_renderWindow->width(), m_renderWindow->height());
    viewport = viewport.intersected(windowBounds);
    if (viewport.width() <= 0.0 || viewport.height() <= 0.0) {
        return;
    }

    const int viewportWidth = qMax(1, static_cast<int>(viewport.width() * dpr));
    const int viewportHeight = qMax(1, static_cast<int>(viewport.height() * dpr));

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
    if (targetFbo == 0) {
        ++m_consecutiveZeroFboFrames;

        static bool sLoggedZeroFbo = false;
        if (!sLoggedZeroFbo) {
            qWarning() << "LinuxMpvBackend: rendering via FBO 0 fallback";
            sLoggedZeroFbo = true;
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

    static int sLoggedFrames = 0;
    if (sLoggedFrames < 5) {
        qInfo() << "LinuxMpvBackend: renderFrame"
                << "fbo=" << targetFbo
                << "viewport=" << viewportX << viewportY << viewportWidth << viewportHeight
                << "dpr=" << dpr;
        ++sLoggedFrames;
    }

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
