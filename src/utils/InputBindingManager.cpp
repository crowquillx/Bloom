#include "InputBindingManager.h"

#include "ConfigManager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QWindow>
#include <utility>

#ifdef BLOOM_HAS_SDL
#include <SDL.h>
#endif

namespace {
Q_LOGGING_CATEGORY(lcInputBindings, "bloom.input.bindings")

constexpr auto kDeviceKeyboard = "keyboard";
constexpr auto kDeviceGamepad = "gamepad";

}

InputBindingManager::InputBindingManager(QGuiApplication *app, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_config(config)
{
    initializeActions();
    connect(m_config, &ConfigManager::inputBindingsChanged, this, &InputBindingManager::bindingsChanged);

#ifdef BLOOM_HAS_SDL
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) == 0) {
        m_gamepadTimer.setInterval(16);
        connect(&m_gamepadTimer, &QTimer::timeout, this, &InputBindingManager::pollGamepad);
        m_gamepadTimer.start();
        pollGamepad();
    } else {
        qCWarning(lcInputBindings) << "SDL game controller init failed:" << SDL_GetError();
    }
#endif
}

InputBindingManager::~InputBindingManager()
{
#ifdef BLOOM_HAS_SDL
    if (m_sdlController) {
        SDL_GameControllerClose(static_cast<SDL_GameController *>(m_sdlController));
        m_sdlController = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
#endif
}

void InputBindingManager::initializeActions()
{
    const auto add = [this](QString id, QString context, QString label, QString description,
                            QStringList keyboard, QStringList gamepad) {
        ActionDefinition action{std::move(id), std::move(context), std::move(label),
                                std::move(description), std::move(keyboard), std::move(gamepad)};
        m_actionById.insert(action.id, action);
        m_actions.append(std::move(action));
    };

    add("nav.up", "navigation", tr("Up"), tr("Move focus up"), {keyBinding(Qt::Key_Up)}, {"gamepad:dpad_up", "gamepad:left_stick_up"});
    add("nav.down", "navigation", tr("Down"), tr("Move focus down"), {keyBinding(Qt::Key_Down)}, {"gamepad:dpad_down", "gamepad:left_stick_down"});
    add("nav.left", "navigation", tr("Left"), tr("Move focus left"), {keyBinding(Qt::Key_Left)}, {"gamepad:dpad_left", "gamepad:left_stick_left"});
    add("nav.right", "navigation", tr("Right"), tr("Move focus right"), {keyBinding(Qt::Key_Right)}, {"gamepad:dpad_right", "gamepad:left_stick_right"});
    add("nav.select", "navigation", tr("Select"), tr("Activate focused item"), {keyBinding(Qt::Key_Return), keyBinding(Qt::Key_Enter), keyBinding(Qt::Key_Space)}, {"gamepad:a"});
    add("nav.back", "navigation", tr("Back"), tr("Dismiss or go back"), {keyBinding(Qt::Key_Escape)}, {"gamepad:b"});

    add("playback.playPause", "playback", tr("Play/Pause"), tr("Toggle playback pause"), {keyBinding(Qt::Key_Space)}, {"gamepad:start"});
    add("playback.seekBack", "playback", tr("Seek Back"), tr("Seek backward 10 seconds"), {}, {"gamepad:left_shoulder"});
    add("playback.seekForward", "playback", tr("Seek Forward"), tr("Seek forward 10 seconds"), {}, {"gamepad:right_shoulder"});
    add("playback.previousChapter", "playback", tr("Previous Chapter"), tr("Jump to previous chapter"), {}, {"gamepad:left_trigger"});
    add("playback.nextChapter", "playback", tr("Next Chapter"), tr("Jump to next chapter"), {}, {"gamepad:right_trigger"});
    add("playback.skipSegment", "playback", tr("Skip Intro/Credits"), tr("Skip the active media segment"), {}, {});
    add("playback.volumeUp", "playback", tr("Volume Up"), tr("Raise playback volume"), {keyBinding(Qt::Key_Plus), keyBinding(Qt::Key_Equal), keyBinding(Qt::Key_VolumeUp)}, {"gamepad:right_stick_up"});
    add("playback.volumeDown", "playback", tr("Volume Down"), tr("Lower playback volume"), {keyBinding(Qt::Key_Minus), keyBinding(Qt::Key_VolumeDown)}, {"gamepad:right_stick_down"});
    add("playback.mute", "playback", tr("Mute"), tr("Toggle playback mute"), {}, {});
    add("playback.volumePanel", "playback", tr("Volume Panel"), tr("Open playback volume controls"), {keyBinding(Qt::Key_V)}, {"gamepad:back"});
    add("playback.audioSelector", "playback", tr("Audio Selector"), tr("Open audio track selector"), {keyBinding(Qt::Key_A)}, {"gamepad:y"});
    add("playback.subtitleSelector", "playback", tr("Subtitle Selector"), tr("Open subtitle selector"), {keyBinding(Qt::Key_S), keyBinding(Qt::Key_T), keyBinding(Qt::Key_C)}, {"gamepad:x"});
    add("playback.subtitleOverride", "advanced", tr("Subtitle Style Override"), tr("Toggle subtitle ASS override"), {keyBinding(Qt::Key_K)}, {"gamepad:left_stick_button"});
    add("playback.deband", "advanced", tr("Deband"), tr("Toggle mpv debanding"), {keyBinding(Qt::Key_B)}, {"gamepad:right_stick_button"});
    add("playback.stats", "advanced", tr("Stats"), tr("Toggle mpv stats"), {keyBinding(Qt::Key_I)}, {});
    add("playback.statsOnce", "advanced", tr("Stats Once"), tr("Show mpv stats once"), {keyBinding(Qt::Key_I, Qt::ShiftModifier)}, {});
}

QVariantList InputBindingManager::actions() const
{
    QVariantList values;
    for (const auto &action : m_actions) {
        QVariantMap value;
        value["id"] = action.id;
        value["context"] = action.context;
        value["label"] = action.label;
        value["description"] = action.description;
        values.append(value);
    }
    return values;
}

QVariantMap InputBindingManager::bindings() const
{
    return mergedBindings();
}

QVariantMap InputBindingManager::mergedBindings() const
{
    QVariantMap result = m_config ? m_config->getInputBindings() : QVariantMap();
    result["schema"] = 1;
    for (const QString &device : {QString(kDeviceKeyboard), QString(kDeviceGamepad)}) {
        QVariantMap deviceMap = result.value(device).toMap();
        for (const auto &action : m_actions) {
            if (!deviceMap.contains(action.id)) {
                deviceMap[action.id] = defaultBindings(device, action.id);
            }
        }
        result[device] = deviceMap;
    }
    return result;
}

QString InputBindingManager::actionForKeyboardEvent(int key, int modifiers) const
{
    const QString binding = keyBinding(key, modifiers);
    const QString fallbackBinding = keyBinding(key, 0);
    for (const auto &action : m_actions) {
        const QStringList actionBindings = effectiveBindings(kDeviceKeyboard, action.id);
        if (actionBindings.contains(binding) || actionBindings.contains(fallbackBinding)) {
            return action.id;
        }
    }
    return {};
}

QString InputBindingManager::bindingForKeyboardEvent(int key, int modifiers) const
{
    return keyBinding(key, modifiers);
}

QString InputBindingManager::displayTextForBinding(const QString &binding) const
{
    QString value = binding;
    value.remove(QStringLiteral("key:"));
    value.remove(QStringLiteral("gamepad:"));
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    const QStringList parts = value.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QStringList titled;
    for (QString part : parts) {
        if (!part.isEmpty()) {
            part[0] = part[0].toUpper();
        }
        titled.append(part);
    }
    return titled.join(QStringLiteral(" + "));
}

QVariantList InputBindingManager::bindingsForAction(const QString &device, const QString &actionId) const
{
    QVariantList values;
    for (const QString &binding : effectiveBindings(normalizeDevice(device), actionId)) {
        values.append(binding);
    }
    return values;
}

bool InputBindingManager::setBindingsForAction(const QString &device, const QString &actionId, const QVariantList &bindings)
{
    const QString normalizedDevice = normalizeDevice(device);
    if (!isKnownAction(actionId) || normalizedDevice.isEmpty()) {
        return false;
    }
    QVariantMap saved = m_config ? m_config->getInputBindings() : QVariantMap();
    QVariantMap deviceMap = saved.value(normalizedDevice).toMap();
    QStringList normalizedBindings;
    for (const QString &binding : variantListToStringList(bindings)) {
        const QString normalized = normalizedBinding(binding);
        if (!normalized.isEmpty() && !normalizedBindings.contains(normalized)) {
            normalizedBindings.append(normalized);
        }
    }
    deviceMap[actionId] = normalizedBindings;
    saved[normalizedDevice] = deviceMap;
    persistBindings(saved);
    return true;
}

void InputBindingManager::resetActionBindings(const QString &device, const QString &actionId)
{
    const QString normalizedDevice = normalizeDevice(device);
    QVariantMap saved = m_config ? m_config->getInputBindings() : QVariantMap();
    QVariantMap deviceMap = saved.value(normalizedDevice).toMap();
    deviceMap.remove(actionId);
    saved[normalizedDevice] = deviceMap;
    persistBindings(saved);
}

void InputBindingManager::resetContextBindings(const QString &device, const QString &context)
{
    const QString normalizedDevice = normalizeDevice(device);
    QVariantMap saved = m_config ? m_config->getInputBindings() : QVariantMap();
    QVariantMap deviceMap = saved.value(normalizedDevice).toMap();
    for (const auto &action : m_actions) {
        if (action.context == context) {
            deviceMap.remove(action.id);
        }
    }
    saved[normalizedDevice] = deviceMap;
    persistBindings(saved);
}

void InputBindingManager::resetAllBindings()
{
    QVariantMap saved;
    saved["schema"] = 1;
    saved[kDeviceKeyboard] = QVariantMap();
    saved[kDeviceGamepad] = QVariantMap();
    persistBindings(saved);
}

QVariantList InputBindingManager::conflictsForBinding(const QString &device,
                                                      const QString &actionId,
                                                      const QString &binding) const
{
    QVariantList conflicts;
    const QString normalizedDevice = normalizeDevice(device);
    const QString normalized = normalizedBinding(binding);
    if (normalizedDevice.isEmpty() || normalized.isEmpty()) {
        return conflicts;
    }
    for (const auto &action : m_actions) {
        if (action.id == actionId) {
            continue;
        }
        if (effectiveBindings(normalizedDevice, action.id).contains(normalized)) {
            QVariantMap conflict;
            conflict["id"] = action.id;
            conflict["label"] = action.label;
            conflict["context"] = action.context;
            conflicts.append(conflict);
        }
    }
    return conflicts;
}

QStringList InputBindingManager::effectiveBindings(const QString &device, const QString &actionId) const
{
    const QVariantMap saved = m_config ? m_config->getInputBindings() : QVariantMap();
    const QVariantMap deviceMap = saved.value(device).toMap();
    if (deviceMap.contains(actionId)) {
        return variantListToStringList(deviceMap.value(actionId).toList());
    }
    return defaultBindings(device, actionId);
}

QStringList InputBindingManager::defaultBindings(const QString &device, const QString &actionId) const
{
    const auto it = m_actionById.constFind(actionId);
    if (it == m_actionById.constEnd()) {
        return {};
    }
    return device == kDeviceGamepad ? it->defaultGamepad : it->defaultKeyboard;
}

void InputBindingManager::persistBindings(const QVariantMap &bindings)
{
    if (!m_config) {
        return;
    }
    m_config->setInputBindings(bindings);
    emit bindingsChanged();
}

void InputBindingManager::dispatchAction(const QString &actionId)
{
    if (actionId == "nav.up") postKey(Qt::Key_Up);
    else if (actionId == "nav.down") postKey(Qt::Key_Down);
    else if (actionId == "nav.left") postKey(Qt::Key_Left);
    else if (actionId == "nav.right") postKey(Qt::Key_Right);
    else if (actionId == "nav.select") postKey(Qt::Key_Return);
    else if (actionId == "nav.back") postKey(Qt::Key_Escape);
    emit actionTriggered(actionId);
}

void InputBindingManager::postKey(int key)
{
    QWindow *window = m_app ? m_app->focusWindow() : nullptr;
    if (!window && m_app && !m_app->allWindows().isEmpty()) {
        window = m_app->allWindows().constFirst();
    }
    if (!window) {
        return;
    }
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

bool InputBindingManager::isKnownAction(const QString &actionId) const
{
    return m_actionById.contains(actionId);
}

QString InputBindingManager::normalizeDevice(const QString &device)
{
    const QString normalized = device.trimmed().toLower();
    if (normalized == kDeviceKeyboard || normalized == kDeviceGamepad) {
        return normalized;
    }
    return {};
}

QString InputBindingManager::keyBinding(int key, int modifiers)
{
    QStringList parts;
    if (modifiers & Qt::ControlModifier) parts << "ctrl";
    if (modifiers & Qt::AltModifier) parts << "alt";
    if (modifiers & Qt::ShiftModifier) parts << "shift";
    if (modifiers & Qt::MetaModifier) parts << "meta";
    parts << QKeySequence(key).toString(QKeySequence::PortableText).toLower();
    return QStringLiteral("key:%1").arg(parts.join(QLatin1Char('+')));
}

QString InputBindingManager::normalizedBinding(const QString &binding)
{
    return binding.trimmed().toLower();
}

QStringList InputBindingManager::variantListToStringList(const QVariantList &values)
{
    QStringList result;
    for (const QVariant &value : values) {
        const QString text = normalizedBinding(value.toString());
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

void InputBindingManager::pollGamepad()
{
#ifdef BLOOM_HAS_SDL
    SDL_GameControllerUpdate();
    if (!m_sdlController) {
        const int joystickCount = SDL_NumJoysticks();
        for (int i = 0; i < joystickCount; ++i) {
            if (SDL_IsGameController(i)) {
                m_sdlController = SDL_GameControllerOpen(i);
                break;
            }
        }
        const bool available = m_sdlController != nullptr;
        if (m_gamepadAvailable != available) {
            m_gamepadAvailable = available;
            emit gamepadAvailableChanged();
        }
        if (!m_sdlController) {
            return;
        }
    }

    auto *controller = static_cast<SDL_GameController *>(m_sdlController);
    QSet<QString> pressed;
    const auto addButton = [&](SDL_GameControllerButton button, const char *binding) {
        if (SDL_GameControllerGetButton(controller, button)) {
            pressed.insert(QStringLiteral("gamepad:%1").arg(QString::fromLatin1(binding)));
        }
    };
    addButton(SDL_CONTROLLER_BUTTON_A, "a");
    addButton(SDL_CONTROLLER_BUTTON_B, "b");
    addButton(SDL_CONTROLLER_BUTTON_X, "x");
    addButton(SDL_CONTROLLER_BUTTON_Y, "y");
    addButton(SDL_CONTROLLER_BUTTON_BACK, "back");
    addButton(SDL_CONTROLLER_BUTTON_START, "start");
    addButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "left_shoulder");
    addButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "right_shoulder");
    addButton(SDL_CONTROLLER_BUTTON_LEFTSTICK, "left_stick_button");
    addButton(SDL_CONTROLLER_BUTTON_RIGHTSTICK, "right_stick_button");
    addButton(SDL_CONTROLLER_BUTTON_DPAD_UP, "dpad_up");
    addButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN, "dpad_down");
    addButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT, "dpad_left");
    addButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "dpad_right");

    constexpr Sint16 axisThreshold = 18000;
    const auto leftX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const auto leftY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    const auto rightY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    const auto triggerLeft = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const auto triggerRight = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (leftX < -axisThreshold) pressed.insert(QStringLiteral("gamepad:left_stick_left"));
    if (leftX > axisThreshold) pressed.insert(QStringLiteral("gamepad:left_stick_right"));
    if (leftY < -axisThreshold) pressed.insert(QStringLiteral("gamepad:left_stick_up"));
    if (leftY > axisThreshold) pressed.insert(QStringLiteral("gamepad:left_stick_down"));
    if (rightY < -axisThreshold) pressed.insert(QStringLiteral("gamepad:right_stick_up"));
    if (rightY > axisThreshold) pressed.insert(QStringLiteral("gamepad:right_stick_down"));
    if (triggerLeft > axisThreshold) pressed.insert(QStringLiteral("gamepad:left_trigger"));
    if (triggerRight > axisThreshold) pressed.insert(QStringLiteral("gamepad:right_trigger"));

    const QSet<QString> newlyPressed = pressed - m_pressedGamepadBindings;
    m_pressedGamepadBindings = pressed;
    for (const QString &binding : newlyPressed) {
        for (const auto &action : m_actions) {
            if (effectiveBindings(kDeviceGamepad, action.id).contains(binding)) {
                dispatchAction(action.id);
                break;
            }
        }
    }
#endif
}
