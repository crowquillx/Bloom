#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVariantMap>

class ConfigManager;
class QGuiApplication;

class InputBindingManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(int bindingsRevision READ bindingsRevision NOTIFY bindingsChanged)
    Q_PROPERTY(bool gamepadAvailable READ gamepadAvailable NOTIFY gamepadAvailableChanged)
    Q_PROPERTY(QString currentRuntimeContext READ currentRuntimeContext WRITE setCurrentRuntimeContext NOTIFY currentRuntimeContextChanged)

public:
    explicit InputBindingManager(QGuiApplication *app, ConfigManager *config, QObject *parent = nullptr);
    ~InputBindingManager() override;

    QVariantList actions() const;
    QVariantMap bindings() const;
    int bindingsRevision() const { return m_bindingsRevision; }
    bool gamepadAvailable() const { return m_gamepadAvailable; }
    QString currentRuntimeContext() const { return m_currentRuntimeContext; }
    void setCurrentRuntimeContext(const QString &runtimeContext);

    Q_INVOKABLE QString actionForKeyboardEvent(int key, int modifiers) const;
    Q_INVOKABLE QString actionForKeyboardEvent(int key, int modifiers, const QString &runtimeContext) const;
    Q_INVOKABLE QString bindingForKeyboardEvent(int key, int modifiers) const;
    Q_INVOKABLE QString displayTextForBinding(const QString &binding) const;
    Q_INVOKABLE QVariantList bindingsForAction(const QString &device, const QString &actionId) const;
    Q_INVOKABLE bool setBindingsForAction(const QString &device, const QString &actionId, const QVariantList &bindings);
    Q_INVOKABLE bool setBindingForAction(const QString &device,
                                         const QString &actionId,
                                         const QString &binding,
                                         bool clearConflicts = false);
    Q_INVOKABLE void resetActionBindings(const QString &device, const QString &actionId);
    Q_INVOKABLE void resetDeviceBindings(const QString &device);
    Q_INVOKABLE void resetContextBindings(const QString &device, const QString &context);
    Q_INVOKABLE void resetAllBindings();
    Q_INVOKABLE QVariantList conflictsForBinding(const QString &device,
                                                 const QString &actionId,
                                                 const QString &binding,
                                                 const QString &runtimeContext = QString()) const;
    Q_INVOKABLE void beginGamepadCapture(const QString &actionId);
    Q_INVOKABLE void cancelGamepadCapture();

signals:
    void bindingsChanged();
    void gamepadAvailableChanged();
    void currentRuntimeContextChanged();
    void actionTriggered(const QString &actionId);
    void actionTriggeredWithContext(const QString &actionId, const QString &runtimeContext);
    void gamepadBindingCaptured(const QString &actionId, const QString &binding);

private slots:
    void pollGamepad();

private:
    struct ActionDefinition {
        QString id;
        QString context;
        QString runtimeContext;
        QString label;
        QString description;
        QStringList defaultKeyboard;
        QStringList defaultGamepad;
    };

    void initializeActions();
    QVariantMap mergedBindings() const;
    QString actionForBinding(const QString &device, const QString &binding, const QString &runtimeContext) const;
    QStringList effectiveBindings(const QString &device, const QString &actionId) const;
    QStringList defaultBindings(const QString &device, const QString &actionId) const;
    void persistBindings(const QVariantMap &bindings);
    void dispatchAction(const QString &actionId);
    void dispatchAction(const QString &actionId, const QString &runtimeContext);
    void dispatchGamepadBinding(const QString &binding, bool repeat);
    void setGamepadAvailable(bool available);
    void postKey(int key);
    bool isKnownAction(const QString &actionId) const;
    bool actionMatchesRuntimeContext(const ActionDefinition &action, const QString &runtimeContext) const;
    static QString normalizeDevice(const QString &device);
    static QString normalizeRuntimeContext(const QString &runtimeContext);
    static QString keyBinding(int key, int modifiers = 0);
    static QString normalizedBinding(const QString &binding);
    static QStringList variantListToStringList(const QVariantList &values);

    QGuiApplication *m_app = nullptr;
    ConfigManager *m_config = nullptr;
    QList<ActionDefinition> m_actions;
    QHash<QString, ActionDefinition> m_actionById;
    QTimer m_gamepadTimer;
    bool m_gamepadAvailable = false;
    int m_bindingsRevision = 0;
    QString m_currentRuntimeContext;
    QSet<QString> m_pressedGamepadBindings;
    QHash<QString, qint64> m_lastRepeatedGamepadBindings;
    QElapsedTimer m_gamepadRepeatClock;
    QString m_gamepadCaptureActionId;
    void *m_sdlController = nullptr;
    bool m_sdlInitialized = false;
};
