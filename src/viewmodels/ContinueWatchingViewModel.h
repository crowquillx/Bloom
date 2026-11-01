#pragma once

#include <QHash>
#include <QVariant>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class LibraryService;

/**
 * @brief Home's Continue Watching and Up Next shelves, and the actions a
 * card on them offers.
 *
 * Home hands every Next Up answer (single or merged) to `setItems`; the
 * list is already in display order. The model splits it by `positionMs`
 * into `continueWatching` and `nextUp` (`nextUp` holds the whole list when
 * `mergeRows` is on) and leaves out cards an action has hidden.
 *
 * `actionsFor(item)` lists what a card offers on its own connection's
 * provider; `trigger(id, item)` runs one on the live connection (QML routes
 * merged cards to their owner first):
 *
 * - `remove`: Remove from Continue Watching. Silo hides the card with a Home
 *   dismissal (needs the card's `progressUpdatedAt`); Jellyfin zeroes the
 *   resume position, so the item may come back as an Up Next card.
 * - `hide`: Hide from Up Next (Silo, needs `seriesId`). Jellyfin has none.
 * - `markWatched` / `markUnwatched`, `favorite` / `unfavorite`, and
 *   `watchlistAdd` / `watchlistRemove` reuse LibraryService's writes.
 *   Favorite and watchlist are offered on movie cards, whose state the
 *   card carries.
 * - `openDetails` / `openSeries` are navigation; QML handles them.
 *
 * Removals are optimistic: the card leaves the shelf at once and comes back
 * with `actionFailed` when the server refuses. A Silo dismissal of an
 * episode drops the whole show, so every card of that series leaves both
 * rows. A confirmed removal stays hidden until the next list arrives, and
 * `refreshRequested` asks Home for that list. Silo dismissals offer `undo()`
 * (`undoOffered`) until the next undoable action replaces it. A write that
 * never answers (the session moved on) releases its card after a timeout.
 */
class ContinueWatchingViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList continueWatching READ continueWatching NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList nextUp READ nextUp NOTIFY itemsChanged)
    Q_PROPERTY(bool mergeRows READ mergeRows WRITE setMergeRows NOTIFY mergeRowsChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY undoChanged)
    Q_PROPERTY(QString undoConnectionId READ undoConnectionId NOTIFY undoChanged)

public:
    explicit ContinueWatchingViewModel(QObject *parent = nullptr);
    explicit ContinueWatchingViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~ContinueWatchingViewModel() override = default;

    /// Every shown card, in display order.
    QVariantList items() const { return m_visible; }
    QVariantList continueWatching() const { return m_continueWatching; }
    QVariantList nextUp() const { return m_nextUp; }
    bool mergeRows() const { return m_mergeRows; }
    void setMergeRows(bool mergeRows);
    bool undoAvailable() const { return m_undo.has_value(); }
    QString undoConnectionId() const;

    /// A new Next Up list. Hidden cards whose action is confirmed are shown
    /// again if the server still lists them.
    Q_INVOKABLE void setItems(const QVariantList &items);
    /// Drops the cards, hidden cards, writes in flight and the undo.
    Q_INVOKABLE void clear();

    /// The card's actions in menu order: {id, text, enabled, navigation}.
    Q_INVOKABLE QVariantList actionsFor(const QVariantMap &item) const;
    /// Runs a non-navigation action on the live connection. Returns false
    /// when it cannot run (unknown or disabled action, a write for this
    /// card in flight, or another connection's card).
    Q_INVOKABLE bool trigger(const QString &actionId, const QVariantMap &item);
    /// Shows the last dismissed card again.
    Q_INVOKABLE bool undo();
    /// Forgets the undo (its toast closed).
    Q_INVOKABLE void dismissUndo();

    /// How long a write may stay unanswered before its card is released.
    void setPendingTimeoutMs(int timeoutMs) { m_pendingTimeoutMs = timeoutMs; }

    static QString surfaceOf(const QVariantMap &item);

signals:
    void itemsChanged();
    void mergeRowsChanged();
    void undoChanged();
    /// A dismissal was confirmed; the toast offers undo().
    void undoOffered(const QString &message);
    void actionFailed(const QString &message);
    /// Home should read Next Up again.
    void refreshRequested();

private:
    // Cards an action keeps off the shelves.
    struct Hide {
        QString connectionId;
        QString itemId;
        // Non-empty: every card of this series.
        QString seriesId;
        // Empty: both rows.
        QString surface;
        bool confirmed = false;
    };

    // A card removed from the list, and where it was.
    struct Snapshot {
        int index = 0;
        QVariantMap item;
    };

    // What an undo sends and shows again.
    struct Undo {
        QString hideKey;
        QString connectionId;
        QString itemId;
        QString surface;
        QString title;
        Hide hide;
        QList<Snapshot> snapshots;
    };

    struct Pending {
        QString actionId;
        QString connectionId;
        QString itemId;
        QString title;
        // Field toggles: the item field and the value to restore.
        QString field;
        QVariant previousValue;
        bool hides = false;
        bool undoable = false;
        bool isUndo = false;
        QString wireSurface;
        bool dropsSeries = false;
        QList<Snapshot> snapshots;
        // An undo in flight: the dismissal it reverts.
        std::optional<Undo> undo;
    };

    void connectService();
    void rebuild();
    bool isHidden(const QVariantMap &item) const;
    static bool matches(const Hide &hide, const QVariantMap &item);
    QList<Snapshot> snapshotsFor(const Hide &hide) const;
    void setField(const QString &connectionId, const QString &itemId,
                  const QString &field, const QVariant &value);
    QString pendingKeyFor(const QString &connectionId, const QString &itemId) const;
    QString startPending(Pending pending);
    void finish(const QString &key, bool succeeded, const QString &error);
    void expire(const QString &key);
    QString findPending(const QString &actionId, const QString &itemId) const;
    QString liveConnectionId() const;
    static QString titleOf(const QVariantMap &item);
    static QString newRequestKey();

    void onHomeCardWriteFinished(const QString &connectionId, const QString &requestKey,
                                 bool succeeded, const QString &error);
    void onPlayedChanged(const QString &itemId, bool played, bool succeeded,
                         const QString &error);
    void onFavoriteChanged(const QString &itemId, bool favorite, bool succeeded,
                           const QString &error);
    void onWatchlistChanged(const QString &itemId, bool inWatchlist, bool succeeded,
                            const QString &error);

    QPointer<LibraryService> m_libraryService;
    QVariantList m_items;
    QVariantList m_visible;
    QVariantList m_continueWatching;
    QVariantList m_nextUp;
    bool m_mergeRows = false;
    // Keyed by request key.
    QHash<QString, Hide> m_hides;
    QHash<QString, Pending> m_pending;
    std::optional<Undo> m_undo;
    int m_pendingTimeoutMs = 30000;
};
