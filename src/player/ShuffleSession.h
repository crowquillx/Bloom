#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "models/ShuffleScope.h"

class LibraryService;

/**
 * @brief One running shuffle: what plays now, what plays next, moving on.
 *
 * Silo keeps the shuffle on the server (createShuffle, advanceShuffle,
 * skipShuffleItem, getShuffle, deleteShuffle). For Jellyfin Bloom keeps the
 * queue itself from random reads under the scope (getShuffleItems), with no
 * repeats until the scope has played (PlaybackPolicy::planLocalShuffleRefill).
 *
 * The shuffle belongs to the connection it started on; replies for another
 * connection or for an earlier shuffle are dropped. An episode the provider
 * names without its season (Silo cards) is handed out once its series'
 * season listing has named it, so it resolves its own season's track picks.
 *
 * PlayerController owns the session and plays what currentReady hands out;
 * see docs/playback.md "Shuffle play".
 */
class ShuffleSession : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        // Creating the shuffle or reading its first page.
        Starting,
        // current and next are known.
        Ready,
        // An advance or a skip is waiting for the server or a refill.
        Moving
    };

    explicit ShuffleSession(LibraryService *libraryService, QObject *parent = nullptr);
    ~ShuffleSession() override;

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }
    bool isBusy() const { return m_state == State::Starting || m_state == State::Moving; }
    bool usesServer() const { return m_usesServer; }
    QString connectionId() const { return m_connectionId; }
    QString shuffleId() const { return m_shuffleId; }
    Bloom::ShuffleScope::Scope scope() const { return m_scope; }
    QString scopeTitle() const { return m_scopeTitle; }
    QVariantMap current() const { return m_current; }
    QString currentItemId() const;
    /// The item that plays after current; empty while a local refill is
    /// still being read.
    QVariantMap next() const;

    /// Starts a shuffle over `{kind, id, collectionKind, title}` on the
    /// active connection, ending any running one. False when the scope is
    /// invalid or the provider cannot shuffle it.
    bool start(const QVariantMap &scope);
    /// Moves on after the current item: the next item becomes current and
    /// is handed out with currentReady.
    void advance();
    /// Replaces the announced next item with another pick (Up Next's
    /// "Shuffle again"); answered with nextChanged.
    void skipNext();
    /// Re-reads a server shuffle, which replaces a next item that can no
    /// longer play; answered with nextChanged when it changed. A local
    /// shuffle with nothing queued reads another page, and fails when that
    /// read fails (Up Next is waiting on it).
    void refresh();
    /// Ends the shuffle. A server shuffle is deleted while its connection
    /// is still the active one; the server drops abandoned ones anyway.
    void stop();

signals:
    /// The item to play now: the first one after start, the next one after
    /// advance.
    void currentReady(const QVariantMap &item);
    void nextChanged(const QVariantMap &item);
    /// The shuffle ended on its own: it could not start or move on, or the
    /// server no longer has it.
    void failed(const QString &message);
    void stateChanged();

private:
    void setState(State state);
    QString requestKey(const QString &operation) const;
    bool acceptsReply(const QString &connectionId, const QString &requestKey) const;
    void onShuffleLoaded(const QString &connectionId, const QString &requestKey,
                         const QVariantMap &result);
    void onShuffleFailed(const QString &connectionId, const QString &requestKey,
                         int httpStatus, const QString &message);
    void onSeasonsLoaded(const QString &connectionId, const QString &parentId,
                         const QString &queryKey, const QVariantList &items);
    void onSeasonsFailed(const QString &connectionId, const QString &parentId,
                         const QString &requestKey);
    void applyServerShuffle(const QVariantMap &result);
    void acceptLocalPage(const QVariantList &items, int total);
    void requestLocalPage();
    void maybeRefillLocal();
    void handOutCurrent();
    void finishWithFailure(const QString &message, bool deleteOnServer);
    // False (after ending the shuffle) once the active connection is no
    // longer the one the shuffle started on.
    bool stillOnItsConnection();
    void reset();

    QPointer<LibraryService> m_libraryService;
    State m_state = State::Idle;
    bool m_usesServer = false;
    // Bumped by every start and stop; replies of an older shuffle are dropped.
    quint64 m_generation = 0;
    // Bumped by every advance and skip, so an older refresh is dropped.
    quint64 m_moveGeneration = 0;
    QString m_connectionId;
    Bloom::ShuffleScope::Scope m_scope;
    QString m_scopeTitle;
    QString m_shuffleId;
    QVariantMap m_current;
    QVariantMap m_serverNext;
    // Whether currentReady still has to announce m_current.
    bool m_currentPending = false;
    // Local shuffles: the queue after current, the IDs handed out this
    // cycle (current and the queue included), the refill in flight, and
    // random pages read in a row that brought nothing new.
    QVariantList m_queue;
    QStringList m_handedOut;
    bool m_refillInFlight = false;
    int m_staleReads = 0;
    // A page added nothing to the queue: no more reads until the next
    // advance (a small scope would otherwise read forever).
    bool m_refillExhausted = false;
    // Up Next waits on an empty queue (refresh); a failed read then ends
    // the shuffle.
    bool m_nextWanted = false;
    // A server skip is out; an advance asked for meanwhile runs once it
    // answers.
    bool m_skipping = false;
    bool m_advanceAfterMove = false;
    // Season IDs by season number per series, for episodes named without
    // their season, the series whose listing is being read, and the series
    // whose listing just failed (its episode plays without a season once;
    // the next one asks again). Cleared with the shuffle.
    QHash<QString, QHash<int, QString>> m_seasonIds;
    QString m_awaitingSeasonsFor;
    QString m_seasonLookupFailedFor;
};
