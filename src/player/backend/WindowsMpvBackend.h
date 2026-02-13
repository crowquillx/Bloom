#pragma once

#include "IPlayerBackend.h"

#include <QMetaObject>
#include <QRectF>
#include <QTimer>
#include <QVariant>
#include <atomic>

#include <memory>

class ExternalMpvBackend;
class QEvent;

class WindowsMpvBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit WindowsMpvBackend(QObject *parent = nullptr);
    ~WindowsMpvBackend() override;

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

    bool tryStartDirectMpv(const QStringList &args, const QString &mediaUrl);
    bool initializeMpv(const QStringList &args);
    void teardownMpv();
    bool queueLoadFile(const QString &mediaUrl);
    void processMpvEvents();
    void observeMpvProperties(void *handle);
    void applyMpvArgs(void *handle, const QStringList &args);
    void handlePropertyChange(const QString &name, const QVariant &value);
    bool sendVariantCommandDirect(const QVariantList &command);
    void setDirectRunning(bool running);

    static void wakeupCallback(void *ctx);

    void syncContainerGeometry();
    void scheduleGeometrySync(int delayMs = 16);
    void beginTransitionMitigation(const char *reason, int settleMs = 90);
    void logHdrDiagnostics(const QStringList &args, const QString &mediaUrl) const;
    static bool isHdrRelatedArg(const QString &arg);
    QStringList sanitizeStartupArgs(const QStringList &args) const;
    bool ensureVideoHostWindow();
    void destroyVideoHostWindow();
    void clearVideoTarget();
    bool resolveContainerHandle(QObject *target);

    class WindowsNativeGeometryFilter;

    std::unique_ptr<ExternalMpvBackend> m_fallbackBackend;
    QObject *m_videoTarget = nullptr;
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
    void *m_mpvHandle = nullptr;
    std::atomic_bool m_eventDispatchQueued{false};
    QList<QByteArray> m_commandScratch;
};