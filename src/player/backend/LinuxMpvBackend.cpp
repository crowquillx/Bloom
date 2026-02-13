#include "LinuxMpvBackend.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector>
#include <QtGlobal>

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
{
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

    if (!initializeMpv(args)) {
        emit errorOccurred(QStringLiteral("Failed to initialize libmpv backend"));
        return;
    }

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
        case MPV_EVENT_LOG_MESSAGE:
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
    mpv_observe_property(handle, 0, "aid", MPV_FORMAT_INT64);
    mpv_observe_property(handle, 0, "sid", MPV_FORMAT_INT64);
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

        if (name == QStringLiteral("input-ipc-server") || name == QStringLiteral("idle")) {
            continue;
        }

        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray valueUtf8 = value.toUtf8();
        mpv_set_option_string(handle, nameUtf8.constData(), valueUtf8.constData());
    }
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
        emit audioTrackChanged(value.toInt());
        return;
    }

    if (name == QStringLiteral("sid")) {
        emit subtitleTrackChanged(value.toInt());
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

    m_sceneGraphInitializedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInitialized,
                                                this, &LinuxMpvBackend::initializeRenderContextIfNeeded,
                                                Qt::DirectConnection);
    m_sceneGraphInvalidatedConnection = connect(m_renderWindow, &QQuickWindow::sceneGraphInvalidated,
                                                this, &LinuxMpvBackend::teardownRenderContext,
                                                Qt::DirectConnection);
    m_beforeRenderingConnection = connect(m_renderWindow, &QQuickWindow::beforeRendering,
                                          this, &LinuxMpvBackend::renderFrame,
                                          Qt::DirectConnection);
}

void LinuxMpvBackend::initializeRenderContextIfNeeded()
{
#if defined(BLOOM_HAS_LIBMPV)
    if (!m_mpvHandle || m_mpvRenderContext) {
        return;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    mpv_opengl_init_params glInitParams = {&LinuxMpvBackend::getProcAddress, this, nullptr};
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
        return;
    }

    m_mpvRenderContext = renderContext;
    mpv_render_context_set_update_callback(renderContext, &LinuxMpvBackend::renderUpdateCallback, this);
#endif
}

void LinuxMpvBackend::teardownRenderContext()
{
#if defined(BLOOM_HAS_LIBMPV)
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
    if (!m_mpvRenderContext || !m_renderWindow || !m_videoTarget) {
        return;
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

    const int viewportWidth = qMax(1, static_cast<int>(viewport.width() * dpr));
    const int viewportHeight = qMax(1, static_cast<int>(viewport.height() * dpr));

    GLint previousFbo = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    gl->glGetIntegerv(GL_VIEWPORT, previousViewport);

    const int windowPixelHeight = qMax(1, static_cast<int>(m_renderWindow->height() * dpr));
    const int viewportX = static_cast<int>(viewport.x() * dpr);
    const int viewportY = windowPixelHeight - static_cast<int>((viewport.y() + viewport.height()) * dpr);
    gl->glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    mpv_opengl_fbo fbo = {previousFbo, viewportWidth, viewportHeight, 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context_render(static_cast<mpv_render_context *>(m_mpvRenderContext), params);
    gl->glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
#endif
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

void LinuxMpvBackend::renderUpdateCallback(void *ctx)
{
    LinuxMpvBackend *self = static_cast<LinuxMpvBackend *>(ctx);
    if (!self || !self->m_renderWindow) {
        return;
    }

    QMetaObject::invokeMethod(self->m_renderWindow, &QQuickWindow::update, Qt::QueuedConnection);
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
