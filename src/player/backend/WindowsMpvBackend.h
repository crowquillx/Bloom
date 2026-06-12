#pragma once

#include "IPlayerBackend.h"

#include <QMetaObject>
#include <QRectF>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <atomic>

#include <memory>

class QEvent;

class WindowsMpvBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit WindowsMpvBackend(QObject *parent = nullptr);
    ~WindowsMpvBackend() override;

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

#ifdef BLOOM_TESTING
    QStringList sanitizeStartupArgsForTest(const QStringList &args) const { return sanitizeStartupArgs(args); }
    QStringList renderApiStartupArgsForTest(const QStringList &args)
    {
        captureStartupMetadata(args);
        QStringList result{QStringLiteral("--vo=") + m_effectiveVo};
        if (!m_effectiveGpuApi.isEmpty()) {
            result << QStringLiteral("--gpu-api=") + m_effectiveGpuApi;
        }
        if (!m_effectiveGpuContext.isEmpty()) {
            result << QStringLiteral("--gpu-context=") + m_effectiveGpuContext;
        }
        if (m_windows10BitOutput && m_currentWindowsRenderApi == QStringLiteral("d3d11")) {
            result << QStringLiteral("--d3d11-output-format=rgb10_a2");
        }
        return result;
    }
#endif

private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    bool tryStartDirectMpv(const QStringList &args, const QString &mediaUrl);
    bool initializeMpv(const QStringList &args);
    void teardownMpv();
    bool queueLoadFile(const QString &mediaUrl);
    void processMpvEvents();
    void observeMpvProperties(void *handle);
    void applyMpvArgs(void *handle, const QStringList &args);
    void applyRenderApiOptions(void *handle);
    void handlePropertyChange(const QString &name, const QVariant &value);
    bool sendVariantCommandDirect(const QVariantList &command);
    void setDirectRunning(bool running);
    void logLifecycleCheckpoint(const char *checkpoint) const;
    void recordMpvLogMessage(const QString &level, const QString &prefix, const QString &text);
    void clearRecentStreamFailure();
    [[nodiscard]] bool hasRecentStreamFailure() const;

    static void wakeupCallback(void *ctx);

    void syncContainerGeometry();
    void scheduleGeometrySync(int delayMs = 16);
    void beginTransitionMitigation(const char *reason, int settleMs = 90);
    void logHdrDiagnostics(const QStringList &args, const QString &mediaUrl) const;
    void captureStartupMetadata(const QStringList &args);
    static bool isHdrRelatedArg(const QString &arg);
    QStringList sanitizeStartupArgs(const QStringList &args) const;
    bool ensureVideoHostWindow();
    void destroyVideoHostWindow();
    void clearVideoTarget();
    bool resolveContainerHandle(QObject *target);
    void scheduleStopTeardownWatchdog(quint64 replyUserdata);

    class WindowsNativeGeometryFilter;

    QPointer<QObject> m_videoTarget;
    QRectF m_lastViewport;
    quintptr m_containerWinId = 0;
    quintptr m_videoHostWinId = 0;
    QTimer m_geometrySyncTimer;
    QTimer m_transitionSettleTimer;
    std::unique_ptr<WindowsNativeGeometryFilter> m_nativeGeometryFilter;
    QMetaObject::Connection m_videoTargetDestroyedConnection;
    bool m_nativeFilterInstalled = false;
    bool m_transitionMitigationActive = false;
    bool m_running = false;
    bool m_directControlActive = false;
    bool m_stopRequested = false;
    bool m_videoHostDestroyDeferred = false;
    quint64 m_pendingStopReplyUserdata = 0;
    quint64 m_stopTeardownGeneration = 0;
    qint64 m_recentStreamFailureTimeMs = 0;
    QString m_recentStreamFailureText;
    QString m_currentProfileName = QStringLiteral("unknown");
    QString m_currentWindowsRenderApi = QStringLiteral("auto");
    QString m_effectiveVo = QStringLiteral("gpu-next");
    QString m_effectiveGpuApi;
    QString m_effectiveGpuContext;
    bool m_windows10BitOutput = false;
    void *m_mpvHandle = nullptr;
    std::atomic_bool m_eventDispatchQueued{false};
    QList<QByteArray> m_commandScratch;
    int m_playlistPosition = -1;
    int m_playlistCount = 0;
};
