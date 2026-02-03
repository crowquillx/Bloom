#include "InputModeManager.h"

#include <QEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>

InputModeManager::InputModeManager(QGuiApplication *app)
    : QObject(app)
    , m_app(app)
{
    if (m_app) {
        m_app->installEventFilter(this);
    }
}

bool InputModeManager::pointerActive() const
{
    return m_pointerActive;
}

bool InputModeManager::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride:
    {
        auto *keyEvent = dynamic_cast<QKeyEvent*>(event);
        if (keyEvent && !keyEvent->isAutoRepeat()) {
            const int key = keyEvent->key();
            if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
                emit navigationKeyPressed();
            } else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
                emit selectKeyPressed();
            } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
                emit backKeyPressed();
            }
        }
        setPointerActive(false);
        break;
    }
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        setPointerActive(true);
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

void InputModeManager::setPointerActive(bool active)
{
    if (m_pointerActive == active) {
        return;
    }

    m_pointerActive = active;
    emit pointerActiveChanged();

    // Control the system cursor visibility based on pointer activity.
    if (m_app) {
        if (m_pointerActive) {
            QGuiApplication::restoreOverrideCursor();
        } else {
            QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        }
    }
}
