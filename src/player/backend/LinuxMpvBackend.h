#pragma once

#include "IPlayerBackend.h"

#include <QPointer>
#include <QRectF>

class LinuxLibmpvOpenGLBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit LinuxLibmpvOpenGLBackend(QObject *parent = nullptr);
    ~LinuxLibmpvOpenGLBackend() override = default;

    static bool isRuntimeSupported();

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
    bool m_running = false;
    bool m_runtimeSupported = false;
    QPointer<QObject> m_videoTarget;
    QRectF m_videoViewport;
};
