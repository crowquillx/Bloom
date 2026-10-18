#pragma once

#include <QObject>
#include <QRectF>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "player/DolbyVisionPolicy.h"

class IPlayerBackend : public QObject
{
    Q_OBJECT

public:
    explicit IPlayerBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IPlayerBackend() override = default;

    virtual QString backendName() const = 0;

    virtual void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) = 0;
    virtual void appendUrlsToPlaylist(const QStringList &mediaUrls) = 0;
    virtual void stopMpv() = 0;
    virtual bool isRunning() const = 0;

    virtual void sendCommand(const QStringList &command) = 0;
    virtual void sendVariantCommand(const QVariantList &command) = 0;

    virtual bool supportsEmbeddedVideo() const = 0;
    virtual bool attachVideoTarget(QObject *target) = 0;
    virtual void detachVideoTarget(QObject *target) = 0;
    virtual void setVideoViewport(const QRectF &viewport) = 0;

    // Starts (or refreshes) non-blocking runtime capability discovery for the
    // mpv this backend will play with. Safe to call repeatedly.
    virtual void requestAudioDiagnostics(const QString &tag) { Q_UNUSED(tag); }

    // Enumerates output devices while no playback is running and reports them
    // through audioDeviceListChanged. A running player already observes mpv's
    // live audio-device-list, so this is a no-op then. Never blocks the caller.
    virtual void refreshAudioDevices() {}

    virtual void prepareRuntime(const QString &mpvBin) { Q_UNUSED(mpvBin); }
    // Best-known provenance and Dolby Vision filter support of that runtime.
    // `formatFilter` stays Unknown until discovery has completed.
    virtual Bloom::PlaybackPolicy::MpvRuntimeInfo runtimeInfo() const { return {}; }
    // Renderer this backend drives (independent of profile `vo` for the
    // embedded render-API backend, which always uses mpv's legacy renderer).
    virtual Bloom::PlaybackPolicy::RendererKind rendererKind(const QStringList &args) const;

signals:
    void stateChanged(bool running);
    void errorOccurred(const QString &error);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void pausedForCacheChanged(bool paused);
    void playbackEnded();
    void cacheEndChanged(double seconds);
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void audioTrackChanged(int trackIndex);
    void subtitleTrackChanged(int trackIndex);

    /// Emitted whenever mpv's set of available audio output devices changes
    /// (e.g. a Bluetooth headset connects/disconnects, an HDMI sink wakes up).
    /// Each entry is a QVariantMap with "name" (the mpv audio-device id, e.g.
    /// "wasapi/{...}" or "auto") and "description" (human readable label).
    void audioDeviceListChanged(const QVariantList &devices);
    void playlistPositionChanged(int index);
    void scriptMessage(const QString &messageName, const QStringList &args);

    void runtimeInfoChanged();
    // Structured mpv property snapshot (names from
    // PlaybackPolicy::playbackDiagnosticProperties() plus provenance
    // properties). `stage` is "startup" (first frames) or "steady".
    void playbackDiagnosticsAvailable(const QString &stage, const QVariantMap &properties);
};

inline Bloom::PlaybackPolicy::RendererKind IPlayerBackend::rendererKind(const QStringList &args) const
{
    QString vo;
    for (const QString &arg : args) {
        if (arg.startsWith(QStringLiteral("--vo="))) {
            vo = arg.mid(5).section(QLatin1Char(','), 0, 0);
        }
    }
    // No explicit --vo: mpv picks its own default, reported at runtime via current-vo.
    return Bloom::PlaybackPolicy::rendererKindForVo(vo);
}
