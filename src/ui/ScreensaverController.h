#pragma once

#include <QObject>
#include <QTimer>

class QGuiApplication;
class QEvent;
class ConfigManager;
class PlayerController;
class AuthenticationService;

class ScreensaverController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool effectiveEnabled READ effectiveEnabled NOTIFY effectiveSettingsChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY effectiveSettingsChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs NOTIFY effectiveSettingsChanged)
    Q_PROPERTY(bool appWindowVisible READ appWindowVisible WRITE setAppWindowVisible NOTIFY appWindowVisibleChanged)

public:
    explicit ScreensaverController(QGuiApplication *app,
                                   ConfigManager *config,
                                   PlayerController *player,
                                   AuthenticationService *auth,
                                   QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool effectiveEnabled() const;
    QString mode() const;
    int timeoutMs() const;
    bool appWindowVisible() const { return m_appWindowVisible; }

    Q_INVOKABLE void setAppWindowVisible(bool visible);
    Q_INVOKABLE void noteActivity();
    Q_INVOKABLE void dismiss();

signals:
    void activeChanged();
    void effectiveSettingsChanged();
    void appWindowVisibleChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool canArm() const;
    bool playbackBlocksScreensaver() const;
    bool eventCountsAsActivity(QEvent *event) const;
    void schedule();
    void activate();
    bool debugForceEnabled() const;
    int debugTimeoutMs() const;

    QGuiApplication *m_app = nullptr;
    ConfigManager *m_config = nullptr;
    PlayerController *m_player = nullptr;
    AuthenticationService *m_auth = nullptr;
    QTimer m_timer;
    bool m_active = false;
    bool m_appWindowVisible = false;
};
