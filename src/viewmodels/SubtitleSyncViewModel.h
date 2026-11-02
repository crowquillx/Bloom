#pragma once

#include "models/SubtitleSync.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

class PlaybackService;
class PlayerController;

/**
 * @brief "Sync to Audio" for the selected external subtitle
 * (docs/viewmodels.md, docs/playback.md "Syncing subtitles to the audio").
 *
 * The playback settings pop-out's Subtitles page binds to it. It reads at
 * each playback start, and after an account or profile change, what the
 * server's sync offers, follows the selected subtitle's sync key, starts a
 * job on request, polls it every few seconds while it is pending or running,
 * and when the server's timing for the subtitle changes, reloads it in mpv
 * through PlayerController. Once a sync's timing is loaded, the subtitle
 * plays with its own delay of 0 instead of the season/movie delay, so the
 * correction is not applied twice; Reset Timing restores the server's
 * original timing and the season/movie delay. With the server's automatic
 * sync on, a subtitle first read with its own timing is read again shortly
 * after mpv loads it, since that load is what starts the server's job.
 *
 * State belongs to one playing file and session; a stop, another item or
 * another subtitle cancels the requests and the polling.
 */
class SubtitleSyncViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canReset READ canReset NOTIFY canResetChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)

public:
    enum class Phase { Idle, Starting, Syncing, Resetting };

    explicit SubtitleSyncViewModel(PlaybackService *playbackService,
                                   PlayerController *playerController,
                                   QObject *parent = nullptr);
    ~SubtitleSyncViewModel() override;

    /// The server can sync the selected subtitle, which has a sync key.
    bool available() const;
    /// A start, a job or a reset is under way.
    bool busy() const { return m_phase != Phase::Idle; }
    /// The server holds a correction that Reset Timing can undo.
    bool canReset() const;
    /// The active job's progress, 0..1.
    double progress() const;
    QString statusText() const;
    Phase phase() const { return m_phase; }

    /// Starts a job for the selected subtitle, or follows its active one.
    Q_INVOKABLE void startSync();
    /// Restores the selected subtitle's original timing.
    Q_INVOKABLE void resetTiming();
    /// Drops every request in flight and stops polling.
    Q_INVOKABLE void cancel();

    static constexpr int kPollIntervalMs = 3000;
    // A job still running after this long is left to the server.
    static constexpr int kPollLimitMs = 5 * 60 * 1000;
    // How long after mpv loads a subtitle the server's automatic sync is
    // looked for.
    static constexpr int kAutoSyncRecheckMs = 4000;

    // Tests shorten the limits.
    void setPollLimitMs(int limitMs) { m_pollLimitMs = limitMs; }
    void setAutoSyncRecheckMs(int delayMs) { m_autoSyncTimer.setInterval(delayMs); }

    /// "+2.3 s", "−0.4 s · ×1.0427 speed".
    static QString describeTiming(const Bloom::SubtitleTiming &timing);

signals:
    // available and canReset change less often than the progress, so the
    // pop-out rebuilds its rows only when they do.
    void availableChanged();
    void canResetChanged();
    void stateChanged();
    /// A job the user started, or a reset, finished or failed.
    void notice(const QString &message);

private:
    enum class Request { None, Read, Start, Poll, ResetRead, Reset, ResetVerify };
    enum class Reload { None, Running, Deferred };
    enum class PendingFinish { None, Sync, Reset };

    struct Context {
        QString identity;
        QString playbackKey;
        QString mediaFileId;
        QString syncKey;
    };

    void onPlaybackChanged();
    void onIdentityChanged();
    void onStatusLoaded(const QString &requestKey, const Bloom::SubtitleSyncCapability &capability);
    void onStateLoaded(const QString &requestKey, const Bloom::SubtitleSyncState &state,
                       const QByteArray &etag);
    void onFailed(const QString &requestKey, int httpStatus, const QString &error);
    void onReloadFinished(bool loaded);
    void onExternalSubtitleLoaded(int index);
    void poll();
    void recheckAutoSync();

    Context currentContext() const;
    void readCapabilityOrState();
    void loadCapability();
    void cancelStatus();
    QString send(Request request);
    void cancelAction();
    void stopTrackWork();
    void forgetTrackState();
    void observe(const Bloom::SubtitleSyncState &state, const QByteArray &etag);
    void requestReload();
    void timingLoaded(const QString &syncKey, const Bloom::SubtitleTiming &timing);
    void watchForAutoSync(const Bloom::SubtitleSyncState &state);
    Bloom::SubtitleTiming loadedTiming() const;
    void finishSync();
    void finishReset(bool restored);
    void settlePendingFinish();
    void endAction(const QString &message, bool announce);
    QString failureMessage(int httpStatus) const;
    void emitStateChanged();

    QPointer<PlaybackService> m_playbackService;
    QPointer<PlayerController> m_playerController;

    Context m_context;
    // What the server offers, read for m_capabilityIdentity.
    bool m_hasCapability = false;
    QString m_capabilityIdentity;
    Bloom::SubtitleSyncCapability m_capability;
    QString m_statusKey;
    QString m_statusIdentity;
    // Subtitles of the playing file the server answered 404 for.
    QSet<QString> m_unsyncableKeys;

    quint64 m_requestSerial = 0;
    QString m_actionKey;
    Request m_actionRequest = Request::None;

    Phase m_phase = Phase::Idle;
    // The user asked for this job: its end is announced.
    bool m_watching = false;
    // The timing mpv had loaded when the job was joined; the outcome says
    // whether the job changed it.
    Bloom::SubtitleTiming m_jobStartTiming;
    // Loading the sync's timing replaced a non-zero delay.
    bool m_delayReplaced = false;
    PendingFinish m_pendingFinish = PendingFinish::None;
    bool m_pendingResetRestored = false;
    QTimer m_pollTimer;
    QElapsedTimer m_pollClock;
    int m_pollLimitMs = kPollLimitMs;

    bool m_hasServerState = false;
    Bloom::SubtitleSyncState m_serverState;
    QByteArray m_etag;
    // The timing mpv has loaded for each subtitle of the playing file.
    QHash<QString, Bloom::SubtitleTiming> m_loadedTiming;
    Reload m_reload = Reload::None;
    QString m_reloadKey;
    Bloom::SubtitleTiming m_reloadTiming;

    // Automatic sync: each subtitle is looked at again once per playback.
    QSet<QString> m_autoSyncChecked;
    QString m_autoSyncKey;
    QTimer m_autoSyncTimer;

    QString m_message;
    bool m_lastAvailable = false;
    bool m_lastCanReset = false;
};
