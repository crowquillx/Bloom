#include "ScreensaverController.h"

#include "network/AuthenticationService.h"
#include "player/PlayerController.h"
#include "utils/ConfigManager.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QProcessEnvironment>
#include <QWheelEvent>

ScreensaverController::ScreensaverController(QGuiApplication *app,
                                             ConfigManager *config,
                                             PlayerController *player,
                                             AuthenticationService *auth,
                                             QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_config(config)
    , m_player(player)
    , m_auth(auth)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ScreensaverController::activate);

    if (m_app) {
        m_app->installEventFilter(this);
    }

    if (m_config) {
        connect(m_config, &ConfigManager::screensaverEnabledChanged, this, [this]() {
            emit effectiveSettingsChanged();
            if (!effectiveEnabled()) {
                dismiss();
                return;
            }
            schedule();
        });
        connect(m_config, &ConfigManager::screensaverModeChanged, this, [this]() {
            emit effectiveSettingsChanged();
        });
        connect(m_config, &ConfigManager::screensaverTimeoutSecondsChanged, this, [this]() {
            emit effectiveSettingsChanged();
            schedule();
        });
    }

    if (m_player) {
        connect(m_player, &PlayerController::playbackStateChanged, this, [this]() {
            if (playbackBlocksScreensaver()) {
                dismiss();
            }
            schedule();
        });
        connect(m_player, &PlayerController::awaitingNextEpisodeResolutionChanged, this, [this]() {
            if (playbackBlocksScreensaver()) {
                dismiss();
            }
            schedule();
        });
    }

    if (m_auth) {
        connect(m_auth, &AuthenticationService::loginSuccess, this, [this]() {
            schedule();
        });
        connect(m_auth, &AuthenticationService::loggedOut, this, [this]() {
            dismiss();
            schedule();
        });
        connect(m_auth, &AuthenticationService::sessionExpired, this, [this]() {
            dismiss();
            schedule();
        });
    }

    schedule();
}

bool ScreensaverController::effectiveEnabled() const
{
    return debugForceEnabled() || (m_config && m_config->getScreensaverEnabled());
}

QString ScreensaverController::mode() const
{
    return m_config ? m_config->getScreensaverMode() : QStringLiteral("libraryBackdrops");
}

int ScreensaverController::timeoutMs() const
{
    if (debugForceEnabled()) {
        return debugTimeoutMs();
    }
    const int seconds = m_config ? m_config->getScreensaverTimeoutSeconds() : 300;
    return qBound(15000, seconds * 1000, 86400000);
}

void ScreensaverController::setAppWindowVisible(bool visible)
{
    if (m_appWindowVisible == visible) {
        return;
    }
    m_appWindowVisible = visible;
    emit appWindowVisibleChanged();
    if (!m_appWindowVisible) {
        dismiss();
    }
    schedule();
}

void ScreensaverController::noteActivity()
{
    if (m_active) {
        dismiss();
        return;
    }
    schedule();
}

void ScreensaverController::dismiss()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    emit activeChanged();
    schedule();
}

bool ScreensaverController::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!eventCountsAsActivity(event)) {
        return QObject::eventFilter(watched, event);
    }

    if (m_active) {
        dismiss();
        return true;
    }

    schedule();
    return QObject::eventFilter(watched, event);
}

bool ScreensaverController::canArm() const
{
    return effectiveEnabled()
           && m_appWindowVisible
           && m_auth
           && m_auth->isAuthenticated()
           && !playbackBlocksScreensaver();
}

bool ScreensaverController::playbackBlocksScreensaver() const
{
    if (!m_player) {
        return false;
    }

    const PlayerController::PlaybackState state = m_player->playbackState();
    return state == PlayerController::Playing
           || state == PlayerController::Loading
           || state == PlayerController::Buffering
           || m_player->awaitingNextEpisodeResolution();
}

bool ScreensaverController::eventCountsAsActivity(QEvent *event) const
{
    switch (event->type()) {
    case QEvent::KeyPress:
        return true;
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
        return true;
    default:
        return false;
    }
}

void ScreensaverController::schedule()
{
    m_timer.stop();
    if (m_active || !canArm()) {
        return;
    }
    m_timer.start(timeoutMs());
}

void ScreensaverController::activate()
{
    if (!canArm() || m_active) {
        schedule();
        return;
    }
    m_active = true;
    emit activeChanged();
}

bool ScreensaverController::debugForceEnabled() const
{
    const QByteArray value = qgetenv("BLOOM_SCREENSAVER_DEBUG").trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

int ScreensaverController::debugTimeoutMs() const
{
    bool ok = false;
    const int value = QString::fromUtf8(qgetenv("BLOOM_SCREENSAVER_DEBUG_TIMEOUT_MS")).toInt(&ok);
    return ok && value > 0 ? qBound(250, value, 60000) : 3000;
}
