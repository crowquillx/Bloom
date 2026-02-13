#include "WindowsLibmpvHwndBackend.h"

#include "ExternalMpvBackend.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QEvent>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <functional>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWindowsLibmpvBackend, "bloom.playback.backend.windows")

class WindowsLibmpvHwndBackend::WindowsNativeGeometryFilter : public QAbstractNativeEventFilter
{
public:
    explicit WindowsNativeGeometryFilter(std::function<void()> onGeometryChanged)
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
                m_onGeometryChanged();
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
    std::function<void()> m_onGeometryChanged;
    quintptr m_watchedWinId = 0;
};

WindowsLibmpvHwndBackend::WindowsLibmpvHwndBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_fallbackBackend(std::make_unique<ExternalMpvBackend>(this))
{
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::stateChanged,
            this, &WindowsLibmpvHwndBackend::stateChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::errorOccurred,
            this, &WindowsLibmpvHwndBackend::errorOccurred);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::positionChanged,
            this, &WindowsLibmpvHwndBackend::positionChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::durationChanged,
            this, &WindowsLibmpvHwndBackend::durationChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::pauseChanged,
            this, &WindowsLibmpvHwndBackend::pauseChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::pausedForCacheChanged,
            this, &WindowsLibmpvHwndBackend::pausedForCacheChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::playbackEnded,
            this, &WindowsLibmpvHwndBackend::playbackEnded);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::audioTrackChanged,
            this, &WindowsLibmpvHwndBackend::audioTrackChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::subtitleTrackChanged,
            this, &WindowsLibmpvHwndBackend::subtitleTrackChanged);
    connect(m_fallbackBackend.get(), &ExternalMpvBackend::scriptMessage,
            this, &WindowsLibmpvHwndBackend::scriptMessage);

#if defined(Q_OS_WIN)
    m_geometrySyncTimer.setSingleShot(true);
    m_geometrySyncTimer.setInterval(16);
    connect(&m_geometrySyncTimer, &QTimer::timeout,
            this, &WindowsLibmpvHwndBackend::syncContainerGeometry);

    m_nativeGeometryFilter = std::make_unique<WindowsNativeGeometryFilter>([this]() {
        scheduleGeometrySync();
    });

    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installNativeEventFilter(m_nativeGeometryFilter.get());
        m_nativeFilterInstalled = true;
    }
#endif
}

WindowsLibmpvHwndBackend::~WindowsLibmpvHwndBackend()
{
#if defined(Q_OS_WIN)
    if (m_nativeFilterInstalled && QCoreApplication::instance() != nullptr && m_nativeGeometryFilter != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(m_nativeGeometryFilter.get());
    }
#endif
}

QString WindowsLibmpvHwndBackend::backendName() const
{
    return QStringLiteral("win-libmpv");
}

void WindowsLibmpvHwndBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    syncContainerGeometry();
    m_fallbackBackend->startMpv(mpvBin, args, mediaUrl);
}

void WindowsLibmpvHwndBackend::stopMpv()
{
    m_fallbackBackend->stopMpv();
}

bool WindowsLibmpvHwndBackend::isRunning() const
{
    return m_fallbackBackend->isRunning();
}

void WindowsLibmpvHwndBackend::sendCommand(const QStringList &command)
{
    m_fallbackBackend->sendCommand(command);
}

void WindowsLibmpvHwndBackend::sendVariantCommand(const QVariantList &command)
{
    m_fallbackBackend->sendVariantCommand(command);
}

bool WindowsLibmpvHwndBackend::supportsEmbeddedVideo() const
{
    return false;
}

bool WindowsLibmpvHwndBackend::attachVideoTarget(QObject *target)
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

void WindowsLibmpvHwndBackend::detachVideoTarget(QObject *target)
{
    if (target == nullptr || target == m_videoTarget) {
        clearVideoTarget();
    }
}

void WindowsLibmpvHwndBackend::setVideoViewport(const QRectF &viewport)
{
    m_lastViewport = viewport;
    scheduleGeometrySync();
}

bool WindowsLibmpvHwndBackend::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoTarget && event != nullptr) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ParentChange:
        case QEvent::WindowStateChange:
            scheduleGeometrySync();
            break;
        default:
            break;
        }
    }

    return IPlayerBackend::eventFilter(watched, event);
}

void WindowsLibmpvHwndBackend::syncContainerGeometry()
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

    qCDebug(lcWindowsLibmpvBackend)
        << "Geometry sync checkpoint"
        << "winId=" << static_cast<qulonglong>(m_containerWinId)
        << "viewport=" << m_lastViewport;
#endif
}

void WindowsLibmpvHwndBackend::scheduleGeometrySync()
{
#if defined(Q_OS_WIN)
    if (m_videoTarget == nullptr) {
        return;
    }

    if (m_geometrySyncTimer.isActive()) {
        m_geometrySyncTimer.stop();
    }

    m_geometrySyncTimer.start();
#endif
}

void WindowsLibmpvHwndBackend::clearVideoTarget()
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

bool WindowsLibmpvHwndBackend::resolveContainerHandle(QObject *target)
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