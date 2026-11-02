#pragma once

#include "models/SubtitleSync.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class PlaybackService;
class PlayerController;

/**
 * @brief "Sync to Audio" for the selected external subtitle
 * (docs/viewmodels.md, docs/playback.md "Syncing subtitles to the audio").
 *
 * The playback settings pop-out's Subtitles page binds to it. It reads once
 * per connection whether the server can re-time subtitles, follows the
 * selected subtitle's sync key, starts a job on request, polls it every few
 * seconds while it is pending or running, and when the server's timing for
 * the subtitle changes, reloads it in mpv through PlayerController. A job the
 * user started that changed the timing also resets Bloom's own subtitle delay,
 * so the correction is not applied twice. "Reset Timing" restores the
 * server's original timing.
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

    /// The server can sync and the selected subtitle has a sync key.
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
    enum class Request { None, Status, Read, Start, Poll, ResetRead, Reset, ResetVerify };

    struct Context {
        QString connectionId;
        QString playbackKey;
        QString mediaFileId;
        QString syncKey;
    };

    void onPlaybackChanged();
    void onStatusLoaded(const QString &requestKey, bool available);
    void onStateLoaded(const QString &requestKey, const Bloom::SubtitleSyncState &state,
                       const QByteArray &etag);
    void onFailed(const QString &requestKey, int httpStatus, const QString &error);
    void poll();

    Context currentContext() const;
    QString send(Request request);
    void cancelAction();
    void observe(const Bloom::SubtitleSyncState &state, const QByteArray &etag);
    void finishSync();
    void finishReset(bool restored);
    void endAction(const QString &message, bool announce);
    QString failureMessage(int httpStatus) const;
    void emitStateChanged();

    QPointer<PlaybackService> m_playbackService;
    QPointer<PlayerController> m_playerController;

    Context m_context;
    QHash<QString, bool> m_statusByConnection;
    QString m_statusKey;
    QString m_statusConnectionId;

    quint64 m_requestSerial = 0;
    QString m_actionKey;
    Request m_actionRequest = Request::None;

    Phase m_phase = Phase::Idle;
    // The user asked for this job: its end is announced and may reset the delay.
    bool m_watching = false;
    bool m_timingChangedDuringJob = false;
    bool m_reloadFailed = false;
    QTimer m_pollTimer;
    QElapsedTimer m_pollClock;

    bool m_hasServerState = false;
    Bloom::SubtitleSyncState m_serverState;
    QByteArray m_etag;
    // The timing mpv has loaded for each subtitle of the playing file.
    QHash<QString, Bloom::SubtitleTiming> m_loadedTiming;
    QString m_message;
    bool m_lastAvailable = false;
    bool m_lastCanReset = false;
};
