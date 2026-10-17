#pragma once

#include "IPlayerBackend.h"
#include "../MpvRuntimeProbe.h"
#include "../PlayerProcessManager.h"

class ExternalMpvBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit ExternalMpvBackend(QObject *parent = nullptr);
    ~ExternalMpvBackend() override = default;

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

private:
    void scheduleDiagnosticSnapshots();

    PlayerProcessManager *m_processManager;
    MpvRuntimeProbe *m_runtimeProbe;
    quint64 m_fileGeneration = 0;
};
