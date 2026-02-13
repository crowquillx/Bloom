#pragma once

#include "IPlayerBackend.h"

#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <QRectF>
#include <QVariant>
#include <atomic>

class QQuickItem;
class QQuickWindow;

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
    bool initializeMpv(const QStringList &args);
    void teardownMpv();
    void processMpvEvents();
    void observeMpvProperties(void *handle);
    void applyMpvArgs(void *handle, const QStringList &args);
    bool queueLoadFile(const QString &mediaUrl);
    void handlePropertyChange(const QString &name, const QVariant &value);
    void handleWindowChanged(QQuickWindow *window);
    void initializeRenderContextIfNeeded();
    void teardownRenderContext();
    void renderFrame();

    static void wakeupCallback(void *ctx);
    static void renderUpdateCallback(void *ctx);
    static void *getProcAddress(void *ctx, const char *name);

    bool m_running = false;
    bool m_runtimeSupported = false;
    QPointer<QQuickItem> m_videoTarget;
    QMetaObject::Connection m_videoTargetWindowConnection;
    QPointer<QQuickWindow> m_renderWindow;
    QMetaObject::Connection m_beforeRenderingConnection;
    QMetaObject::Connection m_sceneGraphInitializedConnection;
    QMetaObject::Connection m_sceneGraphInvalidatedConnection;
    QRectF m_videoViewport;

    void *m_mpvHandle = nullptr;
    void *m_mpvRenderContext = nullptr;
    std::atomic_bool m_eventDispatchQueued{false};
    std::atomic_bool m_acceptRenderUpdates{false};
    std::atomic_bool m_renderUpdateQueued{false};
    QList<QByteArray> m_commandScratch;
};
