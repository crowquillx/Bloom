#pragma once

#include "IPlayerBackend.h"

#include <QMetaObject>
#include <QTimer>

#include <memory>

class ExternalMpvBackend;
class QEvent;

class WindowsLibmpvHwndBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit WindowsLibmpvHwndBackend(QObject *parent = nullptr);
    ~WindowsLibmpvHwndBackend() override;

    QString backendName() const override;

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) override;
    void stopMpv() override;
    bool isRunning() const override;

    void sendCommand(const QStringList &command) override;
    void sendVariantCommand(const QVariantList &command) override;

    bool supportsEmbeddedVideo() const override;
    bool attachVideoTarget(QObject *target) override;
    void detachVideoTarget(QObject *target) override;
    void setVideoViewport(const QRectF &viewport) override;

private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    void syncContainerGeometry();
    void scheduleGeometrySync();
    void clearVideoTarget();
    bool resolveContainerHandle(QObject *target);

    class WindowsNativeGeometryFilter;

    std::unique_ptr<ExternalMpvBackend> m_fallbackBackend;
    QObject *m_videoTarget = nullptr;
    QRectF m_lastViewport;
    quintptr m_containerWinId = 0;
    QTimer m_geometrySyncTimer;
    std::unique_ptr<WindowsNativeGeometryFilter> m_nativeGeometryFilter;
    QMetaObject::Connection m_videoTargetDestroyedConnection;
    bool m_nativeFilterInstalled = false;
};