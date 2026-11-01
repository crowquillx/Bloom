#pragma once

#include <QAbstractListModel>
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
 * @brief One Home shelf's cards for a ListView.
 *
 * `setItems` applies a new list as row inserts, removes, moves and data
 * changes keyed by connection and item, so a ListView keeps its current
 * card (and its index shifts with it) when other cards come and go. Each
 * row's `modelData` is the card's canonical map.
 */
class HomeCardListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        ModelDataRole = Qt::UserRole + 1
    };

    explicit HomeCardListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_items.size()); }
    /// The card at index, or an empty map.
    Q_INVOKABLE QVariantMap get(int index) const;
    const QVariantList &items() const { return m_items; }
    void setItems(const QVariantList &items);

    static QString keyOf(const QVariant &item);

signals:
    void countChanged();

private:
    QVariantList m_items;
};

/**
 * @brief Home's Continue Watching and Up Next shelves, and the actions a
 * card on them offers.
 *
 * Home hands every Next Up answer (single or merged) to `setItems`; the
 * list is already in display order. The model splits it by `positionMs`
 * into `continueWatching` and `nextUp` (`nextUp` holds the whole list when
 * `mergeRows` is on) and leaves out cards an action has hidden. The same
 * rows are offered as list models for the shelves' ListViews.
 *
 * `actionsFor(item)` lists what a card offers on its own connection's
 * provider; `trigger(id, item)` runs one on the live connection (QML routes
 * merged cards to their owner first):
 *
 * - `remove`: Remove from Continue Watching. Silo hides the card with a Home
 *   dismissal (needs the card's `progressUpdatedAt`); Jellyfin zeroes the
 *   resume position, so the item may come back as an Up Next card.
 * - `hide`: Hide from Up Next (Silo, needs `seriesId`). Jellyfin has none.
 * - On a Silo episode both are "Stop watching ‹series›": the server drops the
 *   whole show from both rows and on the profile's watch providers (Trakt),
 *   so the action carries `confirm` text and QML asks first.
 * - `markWatched` / `markUnwatched`, `favorite` / `unfavorite`,
 *   `watchlistAdd` / `watchlistRemove` and `notInterested` /
 *   `interested` write the card's own state. Favorite, watchlist and not
 *   interested are offered on movie cards, whose state the card carries.
 * - `openDetails` / `openSeries` are navigation; QML handles them.
 *
 * Removals are optimistic: the card leaves the shelf at once and comes back
 * with `actionFailed` when the server refuses, or when the session changes
 * before it answers (LibraryService answers every write once). A confirmed
 * removal stays hidden until the next list arrives, and `refreshRequested`
 * asks Home for that list. Field changes in flight are kept over incoming
 * lists. Silo dismissals offer `undo()` (`undoOffered`) until the next
 * undoable action replaces it; while an undo is in flight its cards stay
 * shown over lists read before it.
 */
class ContinueWatchingViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList continueWatching READ continueWatching NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList nextUp READ nextUp NOTIFY itemsChanged)
    Q_PROPERTY(HomeCardListModel *continueWatchingModel READ continueWatchingModel CONSTANT)
    Q_PROPERTY(HomeCardListModel *nextUpModel READ nextUpModel CONSTANT)
    Q_PROPERTY(bool mergeRows READ mergeRows WRITE setMergeRows NOTIFY mergeRowsChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY undoChanged)
    Q_PROPERTY(QString undoConnectionId READ undoConnectionId NOTIFY undoChanged)

public:
    explicit ContinueWatchingViewModel(QObject *parent = nullptr);
    explicit ContinueWatchingViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~ContinueWatchingViewModel() override = default;

    /// Every shown card, in display order.
    QVariantList items() const { return m_visible; }
    QVariantList continueWatching() const { return m_continueWatchingModel->items(); }
    QVariantList nextUp() const { return m_nextUpModel->items(); }
    HomeCardListModel *continueWatchingModel() const { return m_continueWatchingModel; }
    HomeCardListModel *nextUpModel() const { return m_nextUpModel; }
    bool mergeRows() const { return m_mergeRows; }
    void setMergeRows(bool mergeRows);
    bool undoAvailable() const { return m_undo.has_value(); }
    QString undoConnectionId() const;

    /// A new Next Up list. Hidden cards whose action is confirmed are shown
    /// again if the server still lists them.
    Q_INVOKABLE void setItems(const QVariantList &items);
    /// Drops the cards, hidden cards, writes in flight and the undo.
    Q_INVOKABLE void clear();

    /// The card's actions in menu order: {id, text, enabled, navigation},
    /// plus {confirmTitle, confirmMessage, confirmText} when QML must ask
    /// before running it.
    Q_INVOKABLE QVariantList actionsFor(const QVariantMap &item) const;
    /// Runs a non-navigation action on the live connection. Returns false
    /// when it cannot run (unknown or disabled action, a write for this
    /// card in flight, or another connection's card).
    Q_INVOKABLE bool trigger(const QString &actionId, const QVariantMap &item);
    /// Shows the last dismissed card again.
    Q_INVOKABLE bool undo();
    /// Forgets the undo (its toast closed).
    Q_INVOKABLE void dismissUndo();

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
        // Field toggles: the item field, the value sent, and the value to
        // restore.
        QString field;
        QVariant value;
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
    // Puts snapshot cards back into m_items where a list left them out.
    void restoreSnapshots(const QList<Snapshot> &snapshots);
    // Writes field on the matching cards of m_items (no rebuild).
    void applyField(const QString &connectionId, const QString &itemId,
                    const QString &field, const QVariant &value);
    void setField(const QString &connectionId, const QString &itemId,
                  const QString &field, const QVariant &value);
    QString pendingKeyFor(const QString &connectionId, const QString &itemId) const;
    QString startPending(Pending pending);
    void finish(const QString &key, bool succeeded, const QString &error);
    QString liveConnectionId() const;
    static QString titleOf(const QVariantMap &item);
    static QString newRequestKey();

    void onHomeCardWriteFinished(const QString &connectionId, const QString &requestKey,
                                 bool succeeded, const QString &error);

    QPointer<LibraryService> m_libraryService;
    HomeCardListModel *m_continueWatchingModel = nullptr;
    HomeCardListModel *m_nextUpModel = nullptr;
    QVariantList m_items;
    QVariantList m_visible;
    bool m_mergeRows = false;
    // Keyed by request key.
    QHash<QString, Hide> m_hides;
    QHash<QString, Pending> m_pending;
    std::optional<Undo> m_undo;
};
