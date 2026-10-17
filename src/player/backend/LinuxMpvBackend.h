#pragma once

#include "IPlayerBackend.h"
#include "MpvLibmpvRuntime.h"

#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <QRectF>
#include <QVariant>
#include <QImage>
#include <QMutex>
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
    void appendUrlsToPlaylist(const QStringList &mediaUrls) override;
    void stopMpv() override;
    bool isRunning() const override;

    void sendCommand(const QStringList &command) override;
    void sendVariantCommand(const QVariantList &command) override;

    bool supportsEmbeddedVideo() const override;
    bool attachVideoTarget(QObject *target) override;
    void detachVideoTarget(QObject *target) override;
    void setVideoViewport(const QRectF &viewport) override;
    void prepareRuntime(const QString &mpvBin) override;
    Bloom::PlaybackPolicy::MpvRuntimeInfo runtimeInfo() const override;
    // The OpenGL render API always drives mpv's legacy vo_gpu renderer.
    Bloom::PlaybackPolicy::RendererKind rendererKind(const QStringList &args) const override;

private:
    enum class RenderMode {
        OpenGL,
        Software
    };

    bool initializeMpv(const QStringList &args);
    void teardownMpv(bool emitStateChange = true);
    void scheduleDiagnosticSnapshots();
    void processMpvEvents();
    void observeMpvProperties(void *handle);
    void applyMpvArgs(void *handle, const QStringList &args);
    void applyEmbeddedShaderList(void *handle);
    bool queueLoadFile(const QString &mediaUrl);
    void handlePropertyChange(const QString &name, const QVariant &value);
    void recordMpvLogMessage(const QString &level, const QString &prefix, const QString &text);
    void clearRecentStreamFailure();
    [[nodiscard]] bool hasRecentStreamFailure() const;
    void handleWindowChanged(QQuickWindow *window);
    void initializeRenderContextIfNeeded();
    void teardownRenderContext();
    bool createRenderContext(RenderMode mode);
    bool switchToSoftwareRenderMode(const char *reason);
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
    int m_consecutiveZeroFboFrames = 0;
    bool m_renderFailureQueued = false;
    bool m_allowFbo0Fallback = false;
    bool m_debugLogging = false;
    bool m_enableSoftwareFallback = true;
    bool m_forceSoftwareRender = false;
    bool m_switchedToSoftwareFallback = false;
    RenderMode m_renderMode = RenderMode::OpenGL;
    QImage m_swRenderImage;
    QMutex m_swLatestFrameMutex;
    QImage m_swLatestFrame;
    std::atomic_bool m_swFrameDispatchQueued{false};
    int m_playlistPosition = -1;
    int m_playlistCount = 0;
    qint64 m_recentStreamFailureTimeMs = 0;
    QString m_recentStreamFailureText;
    QStringList m_pendingEmbeddedShaderPaths;
    QList<BloomBackend::VideoFilterListOperation> m_pendingVideoFilterOperations;
    Bloom::PlaybackPolicy::MpvRuntimeInfo m_runtimeInfo;
    bool m_runtimeInfoFromHandle = false;
    quint64 m_diagnosticsGeneration = 0;
#if defined(BLOOM_HAS_LIBMPV)
    BloomBackend::LibmpvPropertySnapshots m_propertySnapshots;
#endif
};
