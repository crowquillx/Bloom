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
    m_autoSyncTimer.setSingleShot(true);
    m_autoSyncTimer.setInterval(kAutoSyncRecheckMs);
    connect(&m_autoSyncTimer, &QTimer::timeout, this, &SubtitleSyncViewModel::recheckAutoSync);

    if (m_playbackService) {
        connect(m_playbackService, &PlaybackService::subtitleSyncStatusLoaded,
                this, &SubtitleSyncViewModel::onStatusLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSyncStateLoaded,
                this, &SubtitleSyncViewModel::onStateLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSyncFailed,
                this, &SubtitleSyncViewModel::onFailed);
        connect(m_playbackService, &PlaybackService::subtitleSyncIdentityChanged,
                this, &SubtitleSyncViewModel::onIdentityChanged);
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
        connect(m_playerController, &PlayerController::externalSubtitleReloadFinished,
                this, &SubtitleSyncViewModel::onReloadFinished);
        connect(m_playerController, &PlayerController::externalSubtitleLoaded,
                this, &SubtitleSyncViewModel::onExternalSubtitleLoaded);
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
    if (m_context.syncKey.isEmpty() || m_context.mediaFileId.isEmpty() || !m_hasCapability
        || m_capabilityIdentity != m_context.identity || !m_capability.available) {
        return false;
    }
    // A file beside the media is synced only where the server says so.
    if (m_context.syncKey.startsWith(QStringLiteral("external-")) && !m_capability.external) {
        return false;
    }
    return !m_unsyncableKeys.contains(m_context.syncKey);
}

bool SubtitleSyncViewModel::canReset() const
{
    return available() && !busy() && m_hasServerState && !m_serverState.timing.isIdentity();
}

double SubtitleSyncViewModel::progress() const
{
    return m_phase == Phase::Syncing && m_hasServerState && m_pendingFinish == PendingFinish::None
        ? m_serverState.job.progress
        : 0.0;
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
    if (m_pendingFinish != PendingFinish::None) {
        return tr("Loading the new timing…");
    }
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
    context.identity = m_playbackService->subtitleSyncIdentity();
    context.mediaFileId = m_playerController->mediaSourceId();
    context.playbackKey = QStringList{m_playerController->currentItemId(),
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
        forgetTrackState();
        m_context = {};
        m_loadedTiming.clear();
        m_unsyncableKeys.clear();
        m_autoSyncChecked.clear();
        emitStateChanged();
        return;
    }

    const bool newPlayback = next.playbackKey != m_context.playbackKey;
    const bool newIdentity = next.identity != m_context.identity;
    if (!newPlayback && !newIdentity && next.syncKey == m_context.syncKey) {
        // A reload playback could not take a moment ago (buffering).
        if (m_reload == Reload::Deferred) {
            requestReload();
            if (m_reload == Reload::None) {
                settlePendingFinish();
            }
            emitStateChanged();
        }
        return;
    }
    // The job keeps running on the server; selecting the subtitle again
    // reads it, and Sync to Audio joins it.
    stopTrackWork();
    forgetTrackState();
    if (newPlayback) {
        m_loadedTiming.clear();
        m_unsyncableKeys.clear();
        m_autoSyncChecked.clear();
    }
    m_context = next;
    // Each playback reads the capability again, so a server that turned the
    // sync on since is picked up.
    if (newPlayback || newIdentity) {
        loadCapability();
    } else {
        readCapabilityOrState();
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::onIdentityChanged()
{
    // The transport may have dropped every request in flight without an
    // answer: nothing may wait on them.
    cancelStatus();
    const bool hadCapability = m_hasCapability;
    m_hasCapability = false;
    if (m_context.playbackKey.isEmpty()) {
        return;
    }
    const Context next = currentContext();
    if (!next.playbackKey.isEmpty() && next.identity == m_context.identity
        && next.syncKey == m_context.syncKey) {
        // Same account and profile: send the request again. Each of them
        // may be replayed; a reset that may have gone through is settled by
        // a read.
        if (!m_actionKey.isEmpty()) {
            send(m_actionRequest == Request::Reset ? Request::ResetVerify : m_actionRequest);
        }
        // The rows stay while the capability is read again.
        m_hasCapability = hadCapability;
        loadCapability();
        emitStateChanged();
        return;
    }
    const bool wasBusy = busy();
    const bool announce = m_watching || m_phase == Phase::Resetting;
    stopTrackWork();
    forgetTrackState();
    m_context = next;
    if (wasBusy) {
        m_message = tr("Lost track of the sync. Select Sync to Audio to check on it.");
        if (announce) {
            emit notice(m_message);
        }
    }
    if (!m_context.playbackKey.isEmpty()) {
        loadCapability();
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::readCapabilityOrState()
{
    if (m_hasCapability && m_capabilityIdentity == m_context.identity) {
        if (available()) {
            send(Request::Read);
        }
        return;
    }
    if (!m_statusKey.isEmpty() && m_statusIdentity == m_context.identity) {
        return; // the read in flight answers for this subtitle too
    }
    loadCapability();
}

void SubtitleSyncViewModel::loadCapability()
{
    cancelStatus();
    if (!m_playbackService) {
        return;
    }
    m_statusIdentity = m_context.identity;
    m_statusKey = QStringLiteral("subtitle-sync-status/%1").arg(++m_requestSerial);
    m_playbackService->loadSubtitleSyncStatus(m_statusKey);
}

void SubtitleSyncViewModel::cancelStatus()
{
    if (!m_statusKey.isEmpty() && m_playbackService) {
        m_playbackService->cancelSubtitleSync(m_statusKey);
    }
    m_statusKey.clear();
    m_statusIdentity.clear();
}

void SubtitleSyncViewModel::onStatusLoaded(const QString &requestKey,
                                           const Bloom::SubtitleSyncCapability &capability)
{
    if (requestKey.isEmpty() || requestKey != m_statusKey) {
        return;
    }
    const QString identity = m_statusIdentity;
    m_statusKey.clear();
    m_statusIdentity.clear();
    if (identity != m_context.identity) {
        return;
    }
    m_hasCapability = true;
    m_capabilityIdentity = identity;
    m_capability = capability;
    if (available() && m_actionKey.isEmpty() && !busy() && !m_hasServerState) {
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

void SubtitleSyncViewModel::stopTrackWork()
{
    cancelAction();
    m_pollTimer.stop();
    m_autoSyncTimer.stop();
    m_autoSyncKey.clear();
    m_reload = Reload::None;
    m_pendingFinish = PendingFinish::None;
    m_watching = false;
    if (m_phase != Phase::Idle) {
        m_phase = Phase::Idle;
        emitStateChanged();
    }
}

void SubtitleSyncViewModel::forgetTrackState()
{
    m_hasServerState = false;
    m_serverState = {};
    m_etag.clear();
    m_message.clear();
}

void SubtitleSyncViewModel::cancel()
{
    stopTrackWork();
    cancelStatus();
}

Bloom::SubtitleTiming SubtitleSyncViewModel::loadedTiming() const
{
    return m_loadedTiming.value(m_context.syncKey,
                                m_hasServerState ? m_serverState.timing : Bloom::SubtitleTiming{});
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
    m_autoSyncTimer.stop();
    m_autoSyncKey.clear();
    m_message.clear();
    m_watching = true;
    m_delayReplaced = false;
    m_jobStartTiming = loadedTiming();
    m_phase = Phase::Starting;
    emitStateChanged();
    send(Request::Start);
}

void SubtitleSyncViewModel::resetTiming()
{
    if (!canReset()) {
        return;
    }
    m_autoSyncTimer.stop();
    m_autoSyncKey.clear();
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
    const auto loaded = m_loadedTiming.constFind(state.key);
    if (loaded == m_loadedTiming.constEnd()) {
        // mpv holds the subtitle as the server delivers it now. A timing
        // reset elsewhere also ends the delay a sync gave the subtitle.
        m_loadedTiming.insert(state.key, state.timing);
        if (state.timing.isIdentity() && m_playerController) {
            m_playerController->clearSubtitleDelayOverride(state.key);
        }
        return;
    }
    if (*loaded == state.timing) {
        if (m_reload == Reload::Deferred) {
            m_reload = Reload::None;
        }
        return;
    }
    if (m_reload == Reload::Running) {
        return; // compared again once the copy in flight is loaded
    }
    // Every delivery path applies the server's correction, so the same URL
    // now serves re-timed cues; mpv has the old ones.
    requestReload();
}

void SubtitleSyncViewModel::requestReload()
{
    if (!m_playerController || !m_hasServerState || m_serverState.key != m_context.syncKey) {
        m_reload = Reload::None;
        return;
    }
    m_reloadKey = m_serverState.key;
    m_reloadTiming = m_serverState.timing;
    switch (m_playerController->reloadSelectedExternalSubtitle()) {
    case PlayerController::SubtitleReload::Started:
        m_reload = Reload::Running;
        break;
    case PlayerController::SubtitleReload::FreshLoad:
        // mpv fetches the URL when it loads the subtitle, so it gets the
        // new cues then.
        m_reload = Reload::None;
        timingLoaded(m_reloadKey, m_reloadTiming);
        break;
    case PlayerController::SubtitleReload::Deferred:
        m_reload = Reload::Deferred;
        break;
    case PlayerController::SubtitleReload::Unavailable:
        // The loaded timing stays, so the next read tries again.
        m_reload = Reload::None;
        break;
    }
}

void SubtitleSyncViewModel::onReloadFinished(bool loaded)
{
    if (m_reload == Reload::Deferred) {
        // Another reload (a download saved over the subtitle) held this one.
        requestReload();
        if (m_reload == Reload::None) {
            settlePendingFinish();
        }
        emitStateChanged();
        return;
    }
    if (m_reload != Reload::Running) {
        return;
    }
    m_reload = Reload::None;
    if (loaded) {
        timingLoaded(m_reloadKey, m_reloadTiming);
        // The server may have moved on while the copy loaded.
        if (m_hasServerState && m_serverState.key == m_context.syncKey
            && m_serverState.timing != m_reloadTiming) {
            requestReload();
        }
    }
    if (m_reload == Reload::None) {
        settlePendingFinish();
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::timingLoaded(const QString &syncKey, const Bloom::SubtitleTiming &timing)
{
    m_loadedTiming.insert(syncKey, timing);
    if (!m_playerController) {
        return;
    }
    if (timing.isIdentity()) {
        // The subtitle has its own timing again: so has the season delay.
        m_playerController->clearSubtitleDelayOverride(syncKey);
    } else if (m_serverState.key == syncKey
               && m_serverState.job.status == Bloom::SubtitleSyncStatus::Synced) {
        // The correction now plays from the server; the season's delay,
        // set to make up for the same error, would apply it twice. Other
        // subtitles keep it.
        const int before = m_playerController->subtitleDelayMs();
        m_playerController->setSubtitleDelayOverride(syncKey, 0);
        m_delayReplaced = m_delayReplaced || before != 0;
    }
}

void SubtitleSyncViewModel::watchForAutoSync(const Bloom::SubtitleSyncState &state)
{
    // The server starts its automatic sync when a player is first served
    // the subtitle, which may be just after this read.
    if (!m_capability.autoSync || m_autoSyncChecked.contains(state.key) || state.isActive()
        || !state.timing.isIdentity() || state.job.status != Bloom::SubtitleSyncStatus::None) {
        return;
    }
    m_autoSyncChecked.insert(state.key);
    m_autoSyncKey = state.key;
    if (m_playerController && m_playerController->isSelectedExternalSubtitleLoaded()) {
        m_autoSyncTimer.start();
    }
}

void SubtitleSyncViewModel::onExternalSubtitleLoaded(int index)
{
    if (!m_autoSyncKey.isEmpty() && m_autoSyncKey == m_context.syncKey && m_playerController
        && index == m_playerController->selectedSubtitleTrack()) {
        m_autoSyncTimer.start();
    }
}

void SubtitleSyncViewModel::recheckAutoSync()
{
    if (m_autoSyncKey.isEmpty() || m_autoSyncKey != m_context.syncKey) {
        return;
    }
    m_autoSyncKey.clear();
    if (available() && !busy() && m_actionKey.isEmpty()) {
        send(Request::Read);
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
        // Not this subtitle's answer: nothing will follow it.
        if (busy()) {
            endAction(failureMessage(0), m_watching || m_phase == Phase::Resetting);
        }
        return;
    }
    observe(state, etag);

    switch (request) {
    case Request::Read:
        if (state.isActive()) {
            m_delayReplaced = false;
            m_jobStartTiming = loadedTiming();
            m_phase = Phase::Syncing;
            m_pollClock.start();
            m_pollTimer.start();
        } else {
            watchForAutoSync(state);
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
        } else if (m_pollClock.isValid() && m_pollClock.elapsed() >= m_pollLimitMs) {
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
        // Not remembered: the next subtitle or playback asks again.
        m_statusKey.clear();
        m_statusIdentity.clear();
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
        // Status is decoration; the subtitle plays without it. One the
        // server does not know is not offered.
        if (httpStatus == 404) {
            m_hasServerState = false;
            m_serverState = {};
            m_unsyncableKeys.insert(m_context.syncKey);
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
        break;
    }
    emitStateChanged();
}

void SubtitleSyncViewModel::finishSync()
{
    m_pollTimer.stop();
    if (m_reload != Reload::None) {
        // Reported once the new timing is loaded.
        m_pendingFinish = PendingFinish::Sync;
        return;
    }
    const Bloom::SubtitleSyncJob &job = m_serverState.job;
    const bool loaded = m_loadedTiming.value(m_serverState.key) == m_serverState.timing;
    QString message;
    switch (job.status) {
    case Bloom::SubtitleSyncStatus::Synced:
        if (m_serverState.timing == m_jobStartTiming) {
            message = tr("Already in sync with the audio.");
        } else if (!loaded) {
            message = tr("Synced to the audio (%1). The new timing applies the next time this "
                         "subtitle loads.")
                          .arg(describeTiming(m_serverState.timing));
        } else {
            message = tr("Synced to the audio (%1).").arg(describeTiming(m_serverState.timing));
            if (m_delayReplaced) {
                message += QLatin1Char(' ') + tr("This subtitle now plays with no delay.");
            }
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
    endAction(message, m_watching);
}

void SubtitleSyncViewModel::finishReset(bool restored)
{
    if (restored && m_reload != Reload::None) {
        m_pendingFinish = PendingFinish::Reset;
        m_pendingResetRestored = restored;
        return;
    }
    QString message = tr("The subtitle's timing changed on the server. Try again.");
    if (restored) {
        message = m_loadedTiming.value(m_serverState.key) == m_serverState.timing
            ? tr("Original timing restored.")
            : tr("Original timing restored. It applies the next time this subtitle loads.");
    }
    endAction(message, true);
}

void SubtitleSyncViewModel::settlePendingFinish()
{
    const PendingFinish pending = m_pendingFinish;
    m_pendingFinish = PendingFinish::None;
    switch (pending) {
    case PendingFinish::Sync:
        finishSync();
        break;
    case PendingFinish::Reset:
        finishReset(m_pendingResetRestored);
        break;
    case PendingFinish::None:
        break;
    }
}

void SubtitleSyncViewModel::endAction(const QString &message, bool announce)
{
    m_pollTimer.stop();
    m_pendingFinish = PendingFinish::None;
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
