#pragma once

#include "IPlayerBackend.h"

#include <QByteArray>
#include <QPointer>
#include <QRectF>
#include <QVariant>

class LinuxMpvBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit LinuxMpvBackend(QObject *parent = nullptr);
    ~LinuxMpvBackend() override;

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
    bool initializeMpv();
    void teardownMpv();
    void processMpvEvents();
    void observeMpvProperties();
    void applyMpvArgs(const QStringList &args);
    bool queueLoadFile(const QString &mediaUrl);
    void handlePropertyChange(const QString &name, const QVariant &value);
    static void wakeupCallback(void *ctx);

    bool m_running = false;
    bool m_runtimeSupported = false;
    QPointer<QObject> m_videoTarget;
    QRectF m_videoViewport;

    void *m_mpvHandle = nullptr;
    bool m_eventDispatchQueued = false;
    QList<QByteArray> m_commandScratch;
};
