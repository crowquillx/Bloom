#include "SubtitleSyncViewModel.h"

#include "network/PlaybackService.h"
#include "player/PlayerController.h"

#include <QChar>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <cstdlib>

SubtitleSyncViewModel::SubtitleSyncViewModel(PlaybackService *playbackService,
                                             PlayerController *playerController,
                                             QObject *parent)
    : QObject(parent)
    , m_playbackService(playbackService)
    , m_playerController(playerController)
{
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &SubtitleSyncViewModel::poll);

    if (m_playbackService) {
        connect(m_playbackService, &PlaybackService::subtitleSyncStatusLoaded,
                this, &SubtitleSyncViewModel::onStatusLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSyncStateLoaded,
                this, &SubtitleSyncViewModel::onStateLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSyncFailed,
                this, &SubtitleSyncViewModel::onFailed);
    }
    if (m_playerController) {
        for (const auto signal : {&PlayerController::isPlaybackActiveChanged,
                                  &PlayerController::playbackStateChanged,
                                  &PlayerController::currentItemIdChanged,
                                  &PlayerController::mediaSourceIdChanged,
                                  &PlayerController::playSessionIdChanged,
                                  &PlayerController::selectedSubtitleTrackChanged,
                                  &PlayerController::availableTracksChanged}) {
            connect(m_playerController, signal, this, &SubtitleSyncViewModel::onPlaybackChanged);
        }
    }
    onPlaybackChanged();
}

SubtitleSyncViewModel::~SubtitleSyncViewModel()
{
    if (m_playbackService) {
        if (!m_actionKey.isEmpty()) {
            m_playbackService->cancelSubtitleSync(m_actionKey);
        }
        if (!m_statusKey.isEmpty()) {
            m_playbackService->cancelSubtitleSync(m_statusKey);
        }
    }
}

bool SubtitleSyncViewModel::available() const
{
    return !m_context.syncKey.isEmpty() && !m_context.mediaFileId.isEmpty()
        && m_statusByConnection.value(m_context.connectionId, false);
}

bool SubtitleSyncViewModel::canReset() const
{
    return available() && !busy() && m_hasServerState && !m_serverState.timing.isIdentity();
}

double SubtitleSyncViewModel::progress() const
{
    return m_phase == Phase::Syncing && m_hasServerState ? m_serverState.job.progress : 0.0;
}

QString SubtitleSyncViewModel::describeTiming(const Bloom::SubtitleTiming &timing)
{
    QStringList parts;
    const bool scaled = std::abs(timing.scale - 1.0) >= 1e-9;
    if (timing.offsetMs != 0 || !scaled) {
        parts.append(QStringLiteral("%1%2 s")
                         .arg(timing.offsetMs < 0 ? QStringLiteral("-") : QStringLiteral("+"),
                              QString::number(std::abs(timing.offsetMs) / 1000.0, 'f', 1)));
    }
    if (scaled) {
        parts.append(tr("×%1 speed").arg(QString::number(timing.scale, 'f', 4)));
    }
    return parts.join(QStringLiteral(" · "));
}

QString SubtitleSyncViewModel::statusText() const
{
    switch (m_phase) {
    case Phase::Starting:
        return tr("Starting…");
    case Phase::Resetting:
        return tr("Restoring the original timing…");
    case Phase::Syncing: {
        const Bloom::SubtitleSyncJob &job = m_serverState.job;
        QString label = tr("Waiting to start…");
        if (job.phase == QStringLiteral("analyzing")) {
            label = tr("Listening to the audio…");
        } else if (job.phase == QStringLiteral("matching")) {
            label = tr("Matching lines to speech…");
        }
        const int percent = static_cast<int>(std::lround(progress() * 100.0));
        return percent > 0 ? tr("%1 %2%").arg(label).arg(percent) : label;
    }
    case Phase::Idle:
        break;
    }
    if (!m_message.isEmpty()) {
        return m_message;
    }
    if (m_hasServerState) {
        const Bloom::SubtitleSyncState &state = m_serverState;
        if (!state.timing.isIdentity()) {
            return state.job.status == Bloom::SubtitleSyncStatus::Synced
                ? tr("Synced %1").arg(describeTiming(state.timing))
                : tr("Timing adjusted %1").arg(describeTiming(state.timing));
        }
        if (state.job.status == Bloom::SubtitleSyncStatus::NoMatch) {
            return tr("Doesn't match this video");
        }
        if (state.job.status == Bloom::SubtitleSyncStatus::Failed) {
            return tr("The last sync failed");
        }
    }
    return tr("Match the subtitle to the speech");
}

SubtitleSyncViewModel::Context SubtitleSyncViewModel::currentContext() const
{
    Context context;
    if (!m_playbackService || !m_playerController || !m_playerController->isPlaybackActive()) {
        return context;
    }
    context.connectionId = m_playbackService->subtitleSyncConnectionId();
    context.mediaFileId = m_playerController->mediaSourceId();
    context.playbackKey = QStringList{context.connectionId,
                                      m_playerController->currentItemId(),
                                      context.mediaFileId,
                                      m_playerController->playSessionId()}
                              .join(QChar(0x1f));
    const int selected = m_playerController->selectedSubtitleTrack();
    const QVariantList tracks = m_playerController->availableSubtitleTracks();
    for (const QVariant &trackVariant : tracks) {
        const QVariantMap track = trackVariant.toMap();
        if (track.value(QStringLiteral("index"), -1).toInt() == selected
            && track.value(QStringLiteral("isExternal")).toBool()) {
            context.syncKey = track.value(QStringLiteral("syncKey")).toString();
            break;
        }
    }
    return context;
}

void SubtitleSyncViewModel::onPlaybackChanged()
{
    const Context next = currentContext();
    if (next.playbackKey.isEmpty()) {
        if (m_context.playbackKey.isEmpty()) {
            return;
        }
        cancel();
        m_context = {};
        m_loadedTiming.clear();
        m_hasServerState = false;
        m_serverState = {};
        m_etag.clear();
        m_message.clear();
        emitStateChanged();
        return;
    }

    const bool newPlayback = next.playbackKey != m_context.playbackKey;
    const bool newTrack = newPlayback || next.syncKey != m_context.syncKey;
    const bool newConnection = next.connectionId != m_context.connectionId;
    if (!newTrack && !newConnection) {
        return;
    }
    if (newTrack) {
        // The job keeps running on the server; selecting the subtitle again
        // reads it, and Sync to Audio joins it.
        cancel();
        m_hasServerState = false;
        m_serverState = {};
        m_etag.clear();
        m_message.clear();
    }
    if (newPlayback) {
        m_loadedTiming.clear();
    }
    m_context = next;

    if (!m_statusByConnection.contains(next.connectionId)) {
        if (m_statusConnectionId != next.connectionId || m_statusKey.isEmpty()) {
            if (!m_statusKey.isEmpty()) {
                m_playbackService->cancelSubtitleSync(m_statusKey);
            }
            m_statusConnectionId = next.connectionId;
            m_statusKey = QStringLiteral("subtitle-sync-status/%1").arg(++m_requestSerial);
            m_playbackService->loadSubtitleSyncStatus(m_statusKey);
        }
    } else if (available()) {
        send(Request::Read);
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::onStatusLoaded(const QString &requestKey, bool available)
{
    if (requestKey.isEmpty() || requestKey != m_statusKey) {
        return;
    }
    m_statusKey.clear();
    m_statusByConnection.insert(m_statusConnectionId, available);
    if (this->available() && m_actionKey.isEmpty() && !m_hasServerState) {
        send(Request::Read);
    }
    emitStateChanged();
}

QString SubtitleSyncViewModel::send(Request request)
{
    cancelAction();
    m_actionRequest = request;
    m_actionKey = QStringLiteral("subtitle-sync/%1").arg(++m_requestSerial);
    const QString key = m_actionKey;
    switch (request) {
    case Request::Start:
        m_playbackService->startSubtitleSync(key, m_context.mediaFileId, m_context.syncKey);
        break;
    case Request::Reset:
        m_playbackService->resetSubtitleTiming(key, m_context.mediaFileId, m_context.syncKey, m_etag);
        break;
    case Request::Read:
    case Request::Poll:
    case Request::ResetRead:
    case Request::ResetVerify:
        m_playbackService->readSubtitleSync(key, m_context.mediaFileId, m_context.syncKey);
        break;
    case Request::None:
    case Request::Status:
        break;
    }
    return key;
}

void SubtitleSyncViewModel::cancelAction()
{
    if (!m_actionKey.isEmpty() && m_playbackService) {
        m_playbackService->cancelSubtitleSync(m_actionKey);
    }
    m_actionKey.clear();
    m_actionRequest = Request::None;
}

void SubtitleSyncViewModel::cancel()
{
    cancelAction();
    m_pollTimer.stop();
    m_watching = false;
    if (m_phase != Phase::Idle) {
        m_phase = Phase::Idle;
        emitStateChanged();
    }
}

void SubtitleSyncViewModel::startSync()
{
    if (!available()) {
        return;
    }
    if (m_phase == Phase::Syncing) {
        // A job found running (an automatic sync): report its end too.
        m_watching = true;
        return;
    }
    if (busy()) {
        return;
    }
    m_message.clear();
    m_watching = true;
    m_timingChangedDuringJob = false;
    m_reloadFailed = false;
    m_phase = Phase::Starting;
    emitStateChanged();
    send(Request::Start);
}

void SubtitleSyncViewModel::resetTiming()
{
    if (!canReset()) {
        return;
    }
    m_message.clear();
    m_phase = Phase::Resetting;
    emitStateChanged();
    // setSubtitleTiming needs the current validator.
    send(Request::ResetRead);
}

void SubtitleSyncViewModel::poll()
{
    if (m_phase != Phase::Syncing || !m_actionKey.isEmpty()) {
        return;
    }
    send(Request::Poll);
}

void SubtitleSyncViewModel::observe(const Bloom::SubtitleSyncState &state, const QByteArray &etag)
{
    m_hasServerState = true;
    m_serverState = state;
    if (!etag.isEmpty()) {
        m_etag = etag;
    }
    const auto loaded = m_loadedTiming.find(state.key);
    if (loaded == m_loadedTiming.end()) {
        // mpv holds the subtitle as the server delivers it now.
        m_loadedTiming.insert(state.key, state.timing);
        return;
    }
    if (*loaded == state.timing) {
        return;
    }
    // Every delivery path applies the server's correction, so the same URL
    // now serves re-timed cues; mpv has the old ones.
    *loaded = state.timing;
    m_timingChangedDuringJob = true;
    if (!m_playerController || !m_playerController->reloadSelectedExternalSubtitle()) {
        m_reloadFailed = true;
    }
}

void SubtitleSyncViewModel::onStateLoaded(const QString &requestKey,
                                          const Bloom::SubtitleSyncState &state,
                                          const QByteArray &etag)
{
    if (requestKey.isEmpty() || requestKey != m_actionKey) {
        return;
    }
    const Request request = m_actionRequest;
    m_actionKey.clear();
    m_actionRequest = Request::None;
    if (state.key != m_context.syncKey) {
        return;
    }
    observe(state, etag);

    switch (request) {
    case Request::Read:
        if (state.isActive()) {
            m_timingChangedDuringJob = false;
            m_reloadFailed = false;
            m_phase = Phase::Syncing;
            m_pollClock.start();
            m_pollTimer.start();
        }
        break;
    case Request::Start:
        m_phase = Phase::Syncing;
        m_pollClock.start();
        if (state.isActive()) {
            m_pollTimer.start();
        } else {
            finishSync();
        }
        break;
    case Request::Poll:
        if (!state.isActive()) {
            finishSync();
        } else if (m_pollClock.isValid() && m_pollClock.elapsed() >= kPollLimitMs) {
            endAction(tr("Still syncing on the server. Select Sync to Audio later to check on it."),
                      m_watching);
        } else {
            m_pollTimer.start();
        }
        break;
    case Request::ResetRead:
        if (state.timing.isIdentity()) {
            finishReset(true);
        } else if (m_etag.isEmpty()) {
            endAction(tr("Couldn't read the subtitle's current version. Try again."), true);
        } else {
            send(Request::Reset);
        }
        break;
    case Request::Reset:
        finishReset(true);
        break;
    case Request::ResetVerify:
        finishReset(state.timing.isIdentity());
        break;
    case Request::None:
    case Request::Status:
        break;
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::onFailed(const QString &requestKey, int httpStatus, const QString &error)
{
    Q_UNUSED(error)
    if (requestKey.isEmpty()) {
        return;
    }
    if (requestKey == m_statusKey) {
        // Not remembered: the next playback or subtitle asks again.
        m_statusKey.clear();
        m_statusConnectionId.clear();
        emitStateChanged();
        return;
    }
    if (requestKey != m_actionKey) {
        return;
    }
    const Request request = m_actionRequest;
    m_actionKey.clear();
    m_actionRequest = Request::None;

    switch (request) {
    case Request::Read:
        // Status is decoration; the subtitle plays without it.
        if (httpStatus == 404) {
            m_hasServerState = false;
            m_serverState = {};
        }
        break;
    case Request::Poll:
        endAction(httpStatus == 404
                      ? tr("The server no longer has this subtitle.")
                      : tr("Lost track of the sync. Select Sync to Audio to check on it."),
                  m_watching);
        break;
    case Request::Reset:
        if (httpStatus == 412) {
            // Either another change won, or this reset already went through
            // and its replay was refused: the current timing tells which.
            send(Request::ResetVerify);
            return;
        }
        endAction(failureMessage(httpStatus), true);
        break;
    case Request::ResetVerify:
        finishReset(false);
        break;
    case Request::Start:
    case Request::ResetRead:
        endAction(failureMessage(httpStatus), true);
        break;
    case Request::None:
    case Request::Status:
        break;
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::finishSync()
{
    const Bloom::SubtitleSyncJob &job = m_serverState.job;
    QString message;
    bool resetDelay = false;
    switch (job.status) {
    case Bloom::SubtitleSyncStatus::Synced:
        if (!m_timingChangedDuringJob) {
            message = tr("Already in sync with the audio.");
        } else if (m_reloadFailed) {
            message = tr("Synced to the audio (%1). The new timing applies the next time this "
                         "subtitle loads.")
                          .arg(describeTiming(m_serverState.timing));
        } else {
            message = tr("Synced to the audio (%1).").arg(describeTiming(m_serverState.timing));
            resetDelay = m_watching;
        }
        break;
    case Bloom::SubtitleSyncStatus::AlreadySynced:
        message = tr("Already in sync with the audio.");
        break;
    case Bloom::SubtitleSyncStatus::NoMatch:
        message = tr("This subtitle doesn't match the audio. It may be for another release.");
        break;
    case Bloom::SubtitleSyncStatus::Failed:
        if (job.failure == QStringLiteral("subtitleChanged")) {
            message = tr("The subtitle changed while it was syncing. Try again.");
        } else if (job.failure == QStringLiteral("noAudio")) {
            message = tr("The server can't read this video's audio.");
        } else if (job.failure == QStringLiteral("unavailable")) {
            message = tr("The server is busy. Try again in a few minutes.");
        } else {
            message = tr("Sync failed. Try again.");
        }
        break;
    case Bloom::SubtitleSyncStatus::None:
    case Bloom::SubtitleSyncStatus::Pending:
    case Bloom::SubtitleSyncStatus::Running:
        message = tr("Sync failed. Try again.");
        break;
    }
    // The correction now plays from the server; a local delay the user set to
    // make up for the same error would apply it twice.
    if (resetDelay && m_playerController && m_playerController->subtitleDelayMs() != 0) {
        m_playerController->setSubtitleDelayMs(0);
        message += QLatin1Char(' ') + tr("Subtitle delay reset.");
    }
    endAction(message, m_watching);
}

void SubtitleSyncViewModel::finishReset(bool restored)
{
    endAction(restored ? tr("Original timing restored.")
                       : tr("The subtitle's timing changed on the server. Try again."),
              true);
}

void SubtitleSyncViewModel::endAction(const QString &message, bool announce)
{
    m_pollTimer.stop();
    m_phase = Phase::Idle;
    m_watching = false;
    m_message = message;
    if (announce && !message.isEmpty()) {
        emit notice(message);
    }
    emitStateChanged();
}

QString SubtitleSyncViewModel::failureMessage(int httpStatus) const
{
    switch (httpStatus) {
    case 401:
    case 403:
        return tr("This account can't change subtitle timing.");
    case 404:
        return tr("The server no longer has this subtitle.");
    case 412:
        return tr("The subtitle changed on the server. Try again.");
    case 422:
        return tr("This subtitle can't be synced.");
    case 429:
    case 503:
        return tr("The server is busy. Try again in a few minutes.");
    default:
        break;
    }
    return tr("Sync failed. Try again.");
}

void SubtitleSyncViewModel::emitStateChanged()
{
    if (available() != m_lastAvailable) {
        m_lastAvailable = !m_lastAvailable;
        emit availableChanged();
    }
    if (canReset() != m_lastCanReset) {
        m_lastCanReset = !m_lastCanReset;
        emit canResetChanged();
    }
    emit stateChanged();
}
