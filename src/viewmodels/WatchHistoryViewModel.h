#pragma once

#include "BaseViewModel.h"
#include "../models/CombinedLibraryCursor.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class LibraryService;

/**
 * @brief The History page: what the viewer watched on the live connection,
 * most recent watch first.
 *
 * Each History page owns one instance (QML type `WatchHistoryViewModel`).
 * The list rows are canonical movie, series and episode maps (LibraryKinds:
 * every other type is dropped), each item once. Roles: `itemData`,
 * `itemId`, `mediaType`, `name`.
 *
 * - `load(hiddenLibraryIds)` starts over on the live connection. Providers
 *   that read history one library at a time (Jellyfin) list the
 *   connection's movie and series libraries first, leave out the hidden
 *   ones, and merge the libraries' pages by last play
 *   (CombinedLibraryCursor, `datePlayed`). Episodes of one series show as
 *   one card, the newest watch (Silo rolls them into series cards itself).
 *   Silo pages the profile's whole history by cursor; its pages may be
 *   short or empty while more remain, so up to MaxEmptyPages pages that
 *   add no card are read in a row (MaxLibraryRounds rounds of library
 *   pages on Jellyfin).
 * - `loadMore()` reads the next page while `hasMore`. Pages only insert
 *   rows, so the grid keeps its focused card.
 * - `isLoading` covers the first page. A failed first page sets
 *   `hasError`; a failed later page keeps the cards and emits `pageFailed`
 *   (a failed library ends that library only). A cursor the server
 *   rejects (4xx) starts the list over from the top.
 * - `actionsFor(item)` lists a card's menu actions, each `{id, text,
 *   enabled, navigation}`; `remove` carries `confirmTitle`,
 *   `confirmMessage` and `confirmText`. `trigger("remove", item)` takes the
 *   card out at once and puts it back where it was, with `actionFailed`,
 *   when the server refuses (ProviderCapability::WatchHistoryRemoval,
 *   Silo). When the session changes before the server answers, the card
 *   stays out, `actionFailed` says the removal is unconfirmed, and
 *   `needsReload` asks the page to load again when it is shown next.
 * - Answers for another connection or an older load are ignored.
 */
class WatchHistoryViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY connectionChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY connectionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)
    Q_PROPERTY(bool needsReload READ needsReload NOTIFY needsReloadChanged)

public:
    enum Roles {
        ItemDataRole = Qt::UserRole + 1,
        ItemIdRole,
        MediaTypeRole,
        NameRole
    };
    Q_ENUM(Roles)

    /// Cards read per page (per library on Jellyfin).
    static constexpr int PageSize = 60;
    /// Silo pages read in a row that add no card before waiting for the
    /// next loadMore().
    static constexpr int MaxEmptyPages = 5;
    /// Rounds of library page reads for one page of cards before waiting
    /// for the next loadMore() (one series' episodes make one card).
    static constexpr int MaxLibraryRounds = 5;

    explicit WatchHistoryViewModel(QObject *parent = nullptr);
    explicit WatchHistoryViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~WatchHistoryViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString connectionId() const { return m_connectionId; }
    bool canRemove() const { return m_canRemove; }
    int count() const { return static_cast<int>(m_items.size()); }
    bool hasMore() const { return m_hasMore; }
    bool loadingMore() const { return m_loadingMore; }
    bool needsReload() const { return m_needsReload; }

    /// Starts over on the live connection. hiddenLibraryIds are the
    /// libraries the space hides; providers that read history per library
    /// leave them out.
    Q_INVOKABLE void load(const QStringList &hiddenLibraryIds = {});
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload() override;
    /// The canonical map at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// The row of itemId, or -1.
    Q_INVOKABLE int indexOfItem(const QString &itemId) const;
    Q_INVOKABLE QVariantList actionsFor(const QVariantMap &item) const;
    /// Runs a non-navigation action; false when it cannot run.
    Q_INVOKABLE bool trigger(const QString &actionId, const QVariantMap &item);

signals:
    void connectionChanged();
    void countChanged();
    void hasMoreChanged();
    void loadingMoreChanged();
    void needsReloadChanged();
    void actionFailed(const QString &message);
    /// A later page could not be read; the cards shown stay.
    void pageFailed(const QString &message);

private slots:
    void onViewsLoaded(const QString &connectionId, const QString &requestKey,
                       const QVariantList &views);
    void onViewsFailed(const QString &connectionId, const QString &requestKey,
                       const QString &error);
    void onHistoryLoaded(const QString &connectionId, const QString &requestKey,
                         const QVariantList &items, int totalRecordCount, bool hasMore,
                         const QString &nextCursor);
    void onHistoryFailed(const QString &connectionId, const QString &requestKey,
                         const QString &error, int httpStatus);
    void onWriteFinished(const QString &connectionId, const QString &requestKey,
                         bool success, const QString &error);
    void onWriteUnconfirmed(const QString &connectionId, const QString &requestKey);

private:
    struct PendingRemoval {
        QVariantMap item;
        // The card's place in the list it left (m_rowOrder).
        quint64 order = 0;
        quint64 serial = 0;
    };

    /// One card per title: an episode stands for its series.
    static QString cardKey(const QVariantMap &item);

    void connectService();
    QString nextRequestKey(const QString &kind);
    /// Appends the cards not shown yet; returns how many were added.
    int appendNew(const QVariantList &items);
    void requestStreamPage();
    /// Takes merged cards until a page is full, asking the libraries for
    /// the pages the merge waits on.
    void pumpLibraries();
    void finishPage();
    void failFirstPage(const QString &error);
    void setHasMore(bool hasMore);
    void setLoadingMore(bool loadingMore);
    void insertRow(int row, const QVariantMap &item, quint64 order);
    void setNeedsReload(bool needsReload);

    LibraryService *m_libraryService = nullptr;
    QVariantList m_items;
    // Ascending with m_items: the order cards arrived in, so a card put
    // back after a refused removal finds its place.
    QList<quint64> m_rowOrder;
    quint64 m_nextOrder = 0;
    // cardKey() of every card shown or being removed.
    QSet<QString> m_itemIds;
    QString m_connectionId;
    QStringList m_hiddenLibraryIds;
    bool m_canRemove = false;
    bool m_byLibrary = false;
    bool m_firstPagePending = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    bool m_needsReload = false;
    quint64 m_serial = 0;

    // Silo: one cursor-paged list.
    QString m_pageRequestKey;
    QString m_nextCursor;
    int m_emptyPages = 0;

    // Jellyfin: one offset-paged list per library, merged.
    QString m_viewsRequestKey;
    CombinedLibraryCursor m_cursor;
    QHash<QString, QString> m_libraryRequests;
    int m_wanted = 0;
    int m_libraryCount = 0;
    int m_failedLibraries = 0;
    int m_libraryRounds = 0;
    bool m_pumping = false;
    bool m_pumpAgain = false;

    QHash<QString, PendingRemoval> m_pendingRemovals;
};
