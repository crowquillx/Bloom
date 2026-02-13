#include "WindowsMpvBackend.h"

#include "ExternalMpvBackend.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QMetaType>
#include <QVector>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <functional>

#include <QLoggingCategory>

#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}
#endif

Q_LOGGING_CATEGORY(lcWindowsLibmpvBackend, "bloom.playback.backend.windows.libmpv")

class WindowsMpvBackend::WindowsNativeGeometryFilter : public QAbstractNativeEventFilter
{
public:
    explicit WindowsNativeGeometryFilter(std::function<void(quint32, quintptr)> onGeometryChanged)
        : m_onGeometryChanged(std::move(onGeometryChanged))
    {
    }

    void setWatchedWinId(quintptr winId)
    {
        m_watchedWinId = winId;
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(result);
#if defined(Q_OS_WIN)
        if (m_watchedWinId == 0 || message == nullptr) {
            return false;
        }

        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }

        MSG *nativeMessage = static_cast<MSG *>(message);
        if (nativeMessage == nullptr) {
            return false;
        }

        if (reinterpret_cast<quintptr>(nativeMessage->hwnd) != m_watchedWinId) {
            return false;
        }

        switch (nativeMessage->message) {
        case WM_SIZE:
        case WM_MOVE:
        case WM_WINDOWPOSCHANGED:
            if (m_onGeometryChanged) {
                m_onGeometryChanged(static_cast<quint32>(nativeMessage->message),
                                    static_cast<quintptr>(nativeMessage->wParam));
            }
            break;
        default:
            break;
        }
#else
        Q_UNUSED(eventType);
        Q_UNUSED(message);
#endif
        return false;
    }

private:
    std::function<void(quint32, quintptr)> m_onGeometryChanged;
    quintptr m_watchedWinId = 0;
};

WindowsMpvBackend::WindowsMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_fallbackBackend(std::make_unique<ExternalMpvBackend>(this))
{
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::stateChanged,
            this, &WindowsMpvBackend::stateChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::errorOccurred,
            this, &WindowsMpvBackend::errorOccurred);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::positionChanged,
            this, &WindowsMpvBackend::positionChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::durationChanged,
            this, &WindowsMpvBackend::durationChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::pauseChanged,
            this, &WindowsMpvBackend::pauseChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::pausedForCacheChanged,
            this, &WindowsMpvBackend::pausedForCacheChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::playbackEnded,
            this, &WindowsMpvBackend::playbackEnded);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::audioTrackChanged,
            this, &WindowsMpvBackend::audioTrackChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::subtitleTrackChanged,
            this, &WindowsMpvBackend::subtitleTrackChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::scriptMessage,
            this, &WindowsMpvBackend::scriptMessage);

#if defined(Q_OS_WIN)
    m_geometrySyncTimer.setSingleShot(true);
    m_geometrySyncTimer.setInterval(16);
    connect(&m_geometrySyncTimer, &QTimer::timeout,
            this, &WindowsMpvBackend::syncContainerGeometry);

    m_transitionSettleTimer.setSingleShot(true);
    connect(&m_transitionSettleTimer, &QTimer::timeout, this, [this]() {
        m_transitionMitigationActive = false;
        qCDebug(lcWindowsLibmpvBackend) << "Transition mitigation settled; scheduling sync";
        scheduleGeometrySync(0);
    });

    m_nativeGeometryFilter = std::make_unique<WindowsNativeGeometryFilter>([this](quint32 message, quintptr wParam) {
        switch (message) {
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) {
                beginTransitionMitigation("wm-size-state-transition", 90);
            }
            break;
        }
        case WM_WINDOWPOSCHANGED:
            beginTransitionMitigation("wm-windowposchanged", 75);
            break;
        case WM_MOVE:
            scheduleGeometrySync();
            break;
        default:
            break;
        }
    });

    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installNativeEventFilter(m_nativeGeometryFilter.get());
        m_nativeFilterInstalled = true;
    }
#endif
}

WindowsMpvBackend::~WindowsMpvBackend()
{
    teardownMpv();
    destroyVideoHostWindow();

#if defined(Q_OS_WIN)
    if (m_nativeFilterInstalled && QCoreApplication::instance() != nullptr && m_nativeGeometryFilter != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(m_nativeGeometryFilter.get());
    }
#endif
}

QString WindowsMpvBackend::backendName() const
{
    return QStringLiteral("win-libmpv");
}

void WindowsMpvBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    const QStringList finalArgs = sanitizeStartupArgs(args);

#if defined(Q_OS_WIN)
    if (m_videoTarget != nullptr) {
        resolveContainerHandle(m_videoTarget);
        ensureVideoHostWindow();
    }
#endif

    syncContainerGeometry();
    logHdrDiagnostics(finalArgs, mediaUrl);

    m_fallbackBackend->stopMpv();

    if (tryStartDirectMpv(finalArgs, mediaUrl)) {
        syncContainerGeometry();
        qCInfo(lcWindowsLibmpvBackend) << "Using direct libmpv control path";
        return;
    }

    QStringList fallbackArgs = finalArgs;
#if defined(Q_OS_WIN)
    if (m_videoHostWinId != 0) {
        fallbackArgs.append(QStringLiteral("--wid=%1").arg(static_cast<qulonglong>(m_videoHostWinId)));
        qCInfo(lcWindowsLibmpvBackend)
            << "Direct libmpv unavailable, falling back to external IPC backend with host wid"
            << static_cast<qulonglong>(m_videoHostWinId);
    } else if (m_containerWinId != 0) {
        fallbackArgs.append(QStringLiteral("--wid=%1").arg(static_cast<qulonglong>(m_containerWinId)));
        qCInfo(lcWindowsLibmpvBackend)
            << "Direct libmpv unavailable, falling back to external IPC backend with container wid"
            << static_cast<qulonglong>(m_containerWinId);
    } else {
        qCWarning(lcWindowsLibmpvBackend)
            << "No embedded target winId available for fallback launch; using top-level fallback";
    }
#endif
    m_fallbackBackend->startMpv(mpvBin, fallbackArgs, mediaUrl);
    syncContainerGeometry();
}

void WindowsMpvBackend::stopMpv()
{
    if (m_directControlActive) {
#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
        if (m_mpvHandle != nullptr) {
            mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
            const char *command[] = {"stop", nullptr};
            mpv_command_async(handle, 0, command);
        }
#endif
        teardownMpv();
        syncContainerGeometry();
        return;
    }

    m_fallbackBackend->stopMpv();
    syncContainerGeometry();
}

bool WindowsMpvBackend::isRunning() const
{
    if (m_directControlActive) {
        return m_running;
    }

    return m_fallbackBackend->isRunning();
}

void WindowsMpvBackend::sendCommand(const QStringList &command)
{
    if (m_directControlActive) {
        QVariantList variantCommand;
        variantCommand.reserve(command.size());
        for (const QString &part : command) {
            variantCommand.append(part);
        }

        if (!sendVariantCommandDirect(variantCommand)) {
            qCWarning(lcWindowsLibmpvBackend) << "Failed direct command dispatch" << command;
        }
        return;
    }

    m_fallbackBackend->sendCommand(command);
}

void WindowsMpvBackend::sendVariantCommand(const QVariantList &command)
{
    if (m_directControlActive) {
        if (!sendVariantCommandDirect(command)) {
            qCWarning(lcWindowsLibmpvBackend) << "Failed direct variant command dispatch" << command;
        }
        return;
    }

    m_fallbackBackend->sendVariantCommand(command);
}

bool WindowsMpvBackend::supportsEmbeddedVideo() const
{
    return true;
}

bool WindowsMpvBackend::attachVideoTarget(QObject *target)
{
    clearVideoTarget();

    m_videoTarget = target;
    if (m_videoTarget == nullptr) {
        return false;
    }

    m_videoTarget->installEventFilter(this);
    m_videoTargetDestroyedConnection = connect(m_videoTarget, &QObject::destroyed, this, [this]() {
        m_videoTarget = nullptr;
        m_containerWinId = 0;
        if (m_nativeGeometryFilter != nullptr) {
            m_nativeGeometryFilter->setWatchedWinId(0);
        }
    });

    const bool resolved = resolveContainerHandle(target);
    scheduleGeometrySync();
    return resolved;
}

void WindowsMpvBackend::detachVideoTarget(QObject *target)
{
    if (target == nullptr || target == m_videoTarget) {
        clearVideoTarget();
    }
}

void WindowsMpvBackend::setVideoViewport(const QRectF &viewport)
{
    m_lastViewport = viewport;
    scheduleGeometrySync(0);
}

bool WindowsMpvBackend::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoTarget && event != nullptr) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ParentChange:
        case QEvent::WindowStateChange:
            beginTransitionMitigation("qt-window-transition", 90);
            break;
        default:
            break;
        }
    }

    return IPlayerBackend::eventFilter(watched, event);
}

void WindowsMpvBackend::syncContainerGeometry()
{
#if defined(Q_OS_WIN)
    if (m_videoTarget == nullptr) {
        return;
    }

    if (!resolveContainerHandle(m_videoTarget)) {
        qCDebug(lcWindowsLibmpvBackend) << "Container handle unavailable; postponing geometry sync";
        return;
    }

    if (!m_lastViewport.isValid() || m_lastViewport.isEmpty()) {
        return;
    }

    if (!ensureVideoHostWindow()) {
        qCDebug(lcWindowsLibmpvBackend) << "Video host window unavailable; skipping geometry sync";
        return;
    }

    HWND hostWindow = reinterpret_cast<HWND>(m_videoHostWinId);
    if (hostWindow == nullptr) {
        return;
    }

    if (!isRunning()) {
        ShowWindow(hostWindow, SW_HIDE);
        return;
    }

    const QRect viewportRect = m_lastViewport.toAlignedRect();
    if (viewportRect.width() < 1 || viewportRect.height() < 1) {
        ShowWindow(hostWindow, SW_HIDE);
        return;
    }

    SetWindowPos(hostWindow,
                 HWND_BOTTOM,
                 viewportRect.x(),
                 viewportRect.y(),
                 viewportRect.width(),
                 viewportRect.height(),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    qCDebug(lcWindowsLibmpvBackend)
        << "Geometry sync checkpoint"
        << "containerWinId=" << static_cast<qulonglong>(m_containerWinId)
        << "hostWinId=" << static_cast<qulonglong>(m_videoHostWinId)
        << "viewport=" << m_lastViewport;
#endif
}

void WindowsMpvBackend::scheduleGeometrySync(int delayMs)
{
#if defined(Q_OS_WIN)
    if (m_videoTarget == nullptr) {
        return;
    }

    int effectiveDelayMs = delayMs;
    if (m_transitionMitigationActive && effectiveDelayMs < 75) {
        effectiveDelayMs = 75;
    }

    if (effectiveDelayMs < 0) {
        effectiveDelayMs = 0;
    }

    if (m_geometrySyncTimer.isActive()) {
        m_geometrySyncTimer.stop();
    }

    m_geometrySyncTimer.start(effectiveDelayMs);
#endif
}

void WindowsMpvBackend::beginTransitionMitigation(const char *reason, int settleMs)
{
#if defined(Q_OS_WIN)
    m_transitionMitigationActive = true;

    int effectiveSettleMs = settleMs;
    if (effectiveSettleMs < 1) {
        effectiveSettleMs = 1;
    }

    if (m_transitionSettleTimer.isActive()) {
        m_transitionSettleTimer.stop();
    }

    m_transitionSettleTimer.start(effectiveSettleMs);
    qCDebug(lcWindowsLibmpvBackend)
        << "Transition mitigation active"
        << "reason=" << reason
        << "settleMs=" << effectiveSettleMs;

    scheduleGeometrySync(effectiveSettleMs);
#else
    Q_UNUSED(reason);
    Q_UNUSED(settleMs);
#endif
}

void WindowsMpvBackend::logHdrDiagnostics(const QStringList &args, const QString &mediaUrl) const
{
    QStringList hdrArgs;
    hdrArgs.reserve(args.size());

    for (const QString &arg : args) {
        if (isHdrRelatedArg(arg)) {
            hdrArgs.append(arg);
        }
    }

    const bool hasHdrHint = args.contains(QStringLiteral("--target-colorspace-hint=yes"));
    const bool hasGpuNext = args.contains(QStringLiteral("--vo=gpu-next"));

    qCInfo(lcWindowsLibmpvBackend)
        << "HDR diagnostics"
        << "media=" << mediaUrl
        << "hasGpuNext=" << hasGpuNext
        << "hasHdrHint=" << hasHdrHint
        << "hdrArgCount=" << hdrArgs.size();

    if (!hdrArgs.isEmpty()) {
        qCDebug(lcWindowsLibmpvBackend) << "HDR diagnostics args:" << hdrArgs;
    }
}

bool WindowsMpvBackend::isHdrRelatedArg(const QString &arg)
{
    static const QStringList kHdrPrefixes = {
        QStringLiteral("--hdr"),
        QStringLiteral("--target-trc"),
        QStringLiteral("--target-prim"),
        QStringLiteral("--target-colorspace"),
        QStringLiteral("--target-colorspace-hint"),
        QStringLiteral("--tone-mapping"),
        QStringLiteral("--gamut-mapping"),
        QStringLiteral("--peak"),
        QStringLiteral("--max-luminance"),
        QStringLiteral("--min-luminance"),
        QStringLiteral("--color-primaries"),
        QStringLiteral("--colorspace"),
        QStringLiteral("--video-output-levels"),
        QStringLiteral("--vf=format=")
    };

    const QString lowered = arg.toLower();
    for (const QString &prefix : kHdrPrefixes) {
        if (lowered.startsWith(prefix)) {
            return true;
        }
    }

    return false;
}

void WindowsMpvBackend::clearVideoTarget()
{
    if (m_videoTarget != nullptr) {
        m_videoTarget->removeEventFilter(this);
    }

    if (m_videoTargetDestroyedConnection) {
        disconnect(m_videoTargetDestroyedConnection);
        m_videoTargetDestroyedConnection = QMetaObject::Connection();
    }

    m_videoTarget = nullptr;
    m_containerWinId = 0;
    destroyVideoHostWindow();
#if defined(Q_OS_WIN)
    if (m_nativeGeometryFilter != nullptr) {
        m_nativeGeometryFilter->setWatchedWinId(0);
    }
#endif
}

bool WindowsMpvBackend::resolveContainerHandle(QObject *target)
{
#if defined(Q_OS_WIN)
    if (target == nullptr) {
        return false;
    }

    const quintptr handleValue = static_cast<quintptr>(target->property("winId").toULongLong());
    if (handleValue == 0) {
        m_containerWinId = 0;
#if defined(Q_OS_WIN)
        if (m_nativeGeometryFilter != nullptr) {
            m_nativeGeometryFilter->setWatchedWinId(0);
        }
#endif
        return false;
    }

    m_containerWinId = handleValue;
#if defined(Q_OS_WIN)
    if (m_nativeGeometryFilter != nullptr) {
        m_nativeGeometryFilter->setWatchedWinId(m_containerWinId);
    }
#endif

    HWND hwnd = reinterpret_cast<HWND>(handleValue);
    return hwnd != nullptr;
#else
    Q_UNUSED(target);
    return false;
#endif
}

bool WindowsMpvBackend::tryStartDirectMpv(const QStringList &args, const QString &mediaUrl)
{
#if !defined(Q_OS_WIN) || !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(args);
    Q_UNUSED(mediaUrl);
    return false;
#else
    teardownMpv();

    if (!initializeMpv(args)) {
        qCWarning(lcWindowsLibmpvBackend) << "Direct libmpv initialize failed; fallback backend will be used";
        return false;
    }

    if (!queueLoadFile(mediaUrl)) {
        qCWarning(lcWindowsLibmpvBackend) << "Direct libmpv loadfile failed; fallback backend will be used";
        teardownMpv();
        return false;
    }

    m_running = true;
    m_directControlActive = true;
    emit stateChanged(true);
    return true;
#endif
}

bool WindowsMpvBackend::initializeMpv(const QStringList &args)
{
#if !defined(Q_OS_WIN) || !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(args);
    return false;
#else
    mpv_handle *handle = mpv_create();
    if (!handle) {
        qCWarning(lcWindowsLibmpvBackend) << "mpv_create failed";
        return false;
    }

    mpv_set_wakeup_callback(handle, &WindowsMpvBackend::wakeupCallback, this);

    m_mpvHandle = handle;
    applyMpvArgs(handle, args);

    if (m_videoHostWinId != 0) {
        const QByteArray widValue = QByteArray::number(static_cast<qulonglong>(m_videoHostWinId));
        if (mpv_set_option_string(handle, "wid", widValue.constData()) < 0) {
            qCWarning(lcWindowsLibmpvBackend) << "Failed to set wid option for direct libmpv path";
        }
    }

    if (mpv_initialize(handle) < 0) {
        qCWarning(lcWindowsLibmpvBackend) << "mpv_initialize failed";
        mpv_set_wakeup_callback(handle, nullptr, nullptr);
        mpv_terminate_destroy(handle);
        m_mpvHandle = nullptr;
        return false;
    }

    observeMpvProperties(handle);
    return true;
#endif
}

void WindowsMpvBackend::teardownMpv()
{
#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
    if (m_mpvHandle != nullptr) {
        mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
        mpv_set_wakeup_callback(handle, nullptr, nullptr);
        mpv_terminate_destroy(handle);
    }
#endif

    m_mpvHandle = nullptr;
    m_eventDispatchQueued.store(false, std::memory_order_release);

    const bool wasRunning = m_running;
    m_running = false;
    m_directControlActive = false;

    if (wasRunning) {
        emit stateChanged(false);
    }
}

bool WindowsMpvBackend::queueLoadFile(const QString &mediaUrl)
{
#if !defined(Q_OS_WIN) || !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(mediaUrl);
    return false;
#else
    if (m_mpvHandle == nullptr || mediaUrl.isEmpty()) {
        return false;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);
    const QByteArray mediaUrlUtf8 = mediaUrl.toUtf8();
    const char *command[] = {"loadfile", mediaUrlUtf8.constData(), "replace", nullptr};
    return mpv_command_async(handle, 0, command) >= 0;
#endif
}

void WindowsMpvBackend::processMpvEvents()
{
    m_eventDispatchQueued.store(false, std::memory_order_release);

#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
    if (!m_directControlActive || m_mpvHandle == nullptr) {
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

void WindowsMpvBackend::observeMpvProperties(void *handlePtr)
{
#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
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
#else
    Q_UNUSED(handlePtr);
#endif
}

void WindowsMpvBackend::applyMpvArgs(void *handlePtr, const QStringList &args)
{
#if defined(Q_OS_WIN) && defined(BLOOM_HAS_LIBMPV)
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
            || name == QStringLiteral("input-ipc-client")
            || name == QStringLiteral("wid")) {
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

void WindowsMpvBackend::handlePropertyChange(const QString &name, const QVariant &value)
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
        if (value.typeId() == QMetaType::QString) {
            const QString sidValue = value.toString().trimmed().toLower();
            if (sidValue == QStringLiteral("no") || sidValue == QStringLiteral("none")) {
                emit subtitleTrackChanged(-1);
                return;
            }
        }

        const int mpvTrackId = value.toInt();
        emit subtitleTrackChanged(mpvTrackId > 0 ? mpvTrackId - 1 : -1);
    }
}

bool WindowsMpvBackend::sendVariantCommandDirect(const QVariantList &command)
{
#if !defined(Q_OS_WIN) || !defined(BLOOM_HAS_LIBMPV)
    Q_UNUSED(command);
    return false;
#else
    if (m_mpvHandle == nullptr) {
        return false;
    }

    mpv_handle *handle = static_cast<mpv_handle *>(m_mpvHandle);

    QVector<mpv_node> commandNodes(command.size());
    QList<QByteArray> commandStrings;
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
        default:
            node.format = MPV_FORMAT_STRING;
            commandStrings.append(part.toString().toUtf8());
            node.u.string = commandStrings.constLast().data();
            break;
        }
    }

    mpv_node_list commandList;
    commandList.num = commandNodes.size();
    commandList.values = commandNodes.data();
    commandList.keys = nullptr;

    mpv_node commandArray;
    commandArray.format = MPV_FORMAT_NODE_ARRAY;
    commandArray.u.list = &commandList;

    return mpv_command_node_async(handle, 0, &commandArray) >= 0;
#endif
}

void WindowsMpvBackend::wakeupCallback(void *ctx)
{
    auto *self = static_cast<WindowsMpvBackend *>(ctx);
    if (self == nullptr) {
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

QStringList WindowsMpvBackend::sanitizeStartupArgs(const QStringList &args) const
{
    QStringList finalArgs;
    finalArgs.reserve(args.size());

    bool skipNextValue = false;
    for (const QString &arg : args) {
        if (skipNextValue) {
            skipNextValue = false;
            continue;
        }

        if (arg.compare(QStringLiteral("--wid"), Qt::CaseInsensitive) == 0) {
            skipNextValue = true;
            continue;
        }

        if (arg.startsWith(QStringLiteral("--wid="), Qt::CaseInsensitive)) {
            continue;
        }

        finalArgs.append(arg);
    }

    return finalArgs;
}

bool WindowsMpvBackend::ensureVideoHostWindow()
{
#if defined(Q_OS_WIN)
    if (m_containerWinId == 0) {
        return false;
    }

    if (m_videoHostWinId != 0) {
        return true;
    }

    HWND parentWindow = reinterpret_cast<HWND>(m_containerWinId);
    if (parentWindow == nullptr) {
        return false;
    }

    HWND hostWindow = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,
        L"STATIC",
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        1,
        1,
        parentWindow,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (hostWindow == nullptr) {
        qCWarning(lcWindowsLibmpvBackend) << "Failed to create embedded mpv host window";
        return false;
    }

    SetWindowPos(hostWindow, HWND_BOTTOM, 0, 0, 1, 1, SWP_NOACTIVATE);
    ShowWindow(hostWindow, SW_HIDE);
    m_videoHostWinId = reinterpret_cast<quintptr>(hostWindow);
    qCInfo(lcWindowsLibmpvBackend) << "Created embedded mpv host window" << static_cast<qulonglong>(m_videoHostWinId);
    return true;
#else
    return false;
#endif
}

void WindowsMpvBackend::destroyVideoHostWindow()
{
#if defined(Q_OS_WIN)
    if (m_videoHostWinId == 0) {
        return;
    }

    HWND hostWindow = reinterpret_cast<HWND>(m_videoHostWinId);
    if (hostWindow != nullptr) {
        DestroyWindow(hostWindow);
    }
#endif
    m_videoHostWinId = 0;
}