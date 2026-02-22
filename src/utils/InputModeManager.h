#pragma once

#include <QObject>

class QGuiApplication;
class QEvent;
class QString;

class InputModeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pointerActive READ pointerActive NOTIFY pointerActiveChanged)

public:
    explicit InputModeManager(QGuiApplication *app);

    bool pointerActive() const;
    Q_INVOKABLE void setNavigationMode(const QString &mode);
    Q_INVOKABLE void hideCursor(bool hide);

signals:
    void pointerActiveChanged();
    void navigationKeyPressed();
    void selectKeyPressed();
    void backKeyPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setPointerActive(bool active);

    QGuiApplication *m_app = nullptr;
    bool m_pointerActive = true;
};
