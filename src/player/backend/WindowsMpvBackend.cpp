#include "WindowsMpvBackend.h"

#include "ExternalMpvBackend.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QEvent>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <functional>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWindowsMpvBackend, "bloom.playback.backend.windows")

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
        qCDebug(lcWindowsMpvBackend) << "Transition mitigation settled; scheduling sync";
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
    syncContainerGeometry();
    logHdrDiagnostics(args, mediaUrl);
    m_fallbackBackend->startMpv(mpvBin, args, mediaUrl);
}

void WindowsMpvBackend::stopMpv()
{
    m_fallbackBackend->stopMpv();
}

bool WindowsMpvBackend::isRunning() const
{
    return m_fallbackBackend->isRunning();
}

void WindowsMpvBackend::sendCommand(const QStringList &command)
{
    m_fallbackBackend->sendCommand(command);
}

void WindowsMpvBackend::sendVariantCommand(const QVariantList &command)
{
    m_fallbackBackend->sendVariantCommand(command);
}

bool WindowsMpvBackend::supportsEmbeddedVideo() const
{
    return false;
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
        qCDebug(lcWindowsMpvBackend) << "Container handle unavailable; postponing geometry sync";
        return;
    }

    if (!m_lastViewport.isValid() || m_lastViewport.isEmpty()) {
        return;
    }

    qCDebug(lcWindowsMpvBackend)
        << "Geometry sync checkpoint"
        << "winId=" << static_cast<qulonglong>(m_containerWinId)
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
    qCDebug(lcWindowsMpvBackend)
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

    qCInfo(lcWindowsMpvBackend)
        << "HDR diagnostics"
        << "media=" << mediaUrl
        << "hasGpuNext=" << hasGpuNext
        << "hasHdrHint=" << hasHdrHint
        << "hdrArgCount=" << hdrArgs.size();

    if (!hdrArgs.isEmpty()) {
        qCDebug(lcWindowsMpvBackend) << "HDR diagnostics args:" << hdrArgs;
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

    const quintptr handleValue = target->property("winId").value<quintptr>();
    if (handleValue == 0) {
        m_containerWinId = 0;
#if defined(Q_OS_WIN)
        if (m_nativeGeometryFilter != nullptr) {
            m_nativeGeometryFilter->setWatchedWinId(0);
        }
#endif
        return true;
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