#pragma once

#include "BaseViewModel.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ConfigManager;
class LibraryService;

/**
 * @brief Whether Home offers taste seeding (QML type
 * `TasteSeedPromptViewModel`, one per Home).
 *
 * `shown` holds when the live connection has taste seeding
 * (LibraryService::supportsTasteSeed, Silo), Home is not a merged space
 * (`merged`), the viewer has not finished or skipped it for this connection
 * and profile (ConfigManager::isTasteSeedDone), and the profile's taste
 * summary is still cold. `check()` reads the summary once per scope;
 * `check(true)` reads it again (after playback). A failed read shows
 * nothing. `dismiss()` remembers the scope as done, so the prompt never
 * comes back there; Settings still opens the picker.
 */
class TasteSeedPromptViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool merged READ merged WRITE setMerged NOTIFY mergedChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    explicit TasteSeedPromptViewModel(QObject *parent = nullptr);
    TasteSeedPromptViewModel(LibraryService *libraryService, ConfigManager *config,
                             QObject *parent = nullptr);

    bool merged() const { return m_merged; }
    void setMerged(bool merged);
    bool shown() const { return m_shown; }

    Q_INVOKABLE void check(bool force = false);
    Q_INVOKABLE void dismiss();

signals:
    void mergedChanged();
    void shownChanged();

private:
    void onStateLoaded(const QString &connectionId, const QString &requestKey, bool cold);
    void onStateFailed(const QString &connectionId, const QString &requestKey,
                       const QString &error);
    QString liveScope() const;
    void update();

    LibraryService *m_libraryService = nullptr;
    ConfigManager *m_config = nullptr;
    bool m_merged = false;
    bool m_shown = false;
    // The scope the last answer belongs to, and whether it was cold.
    QString m_scope;
    bool m_cold = false;
    // The read in flight and the scope it asks for.
    QString m_requestKey;
    QString m_requestScope;
};

/**
 * @brief The taste seeding picker: candidate titles the viewer picks a few
 * favorites from (QML type `TasteSeedViewModel`, one per picker page).
 *
 * Rows are canonical movie and series maps (LibraryKinds), each title once,
 * most recognizable first. Roles: `itemData`, `itemId`, `name`, `selected`.
 *
 * - `load()` starts over on the live connection; without taste seeding
 *   (`available` false) nothing is read. `loadMore()` reads the next page
 *   while `hasMore`. A page may hold no title Bloom shows while more
 *   remain, so up to MaxEmptyPages such pages are read in a row.
 * - `isLoading` covers the first page; a failed first page sets `hasError`,
 *   a failed later page keeps the cards and emits `pageFailed`.
 * - Titles the profile already favorited start selected. `toggle(row)`
 *   flips one; new picks stop at MaximumPicks (`toggle` answers false).
 *   `canSubmit` needs MinimumPicks selected (Silo's own picker asks for
 *   three; the server takes one or more).
 * - `submit()` sends the new picks only (never replayed). A success, or a
 *   selection of favorites alone (no request), marks the scope done and
 *   emits `submitted`; a refusal or a session change before the answer
 *   emits `submitFailed` and keeps the selection. `skip()` marks the scope
 *   done without sending anything.
 * - Answers for another connection or an older load are ignored.
 */
class TasteSeedViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY connectionChanged)
    Q_PROPERTY(bool available READ available NOTIFY connectionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int minimumPicks READ minimumPicks CONSTANT)
    Q_PROPERTY(int maximumPicks READ maximumPicks CONSTANT)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY selectionChanged)
    Q_PROPERTY(bool submitting READ submitting NOTIFY submittingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)

public:
    enum Roles {
        ItemDataRole = Qt::UserRole + 1,
        ItemIdRole,
        NameRole,
        SelectedRole
    };
    Q_ENUM(Roles)

    /// Cards read per page (listTasteSeedItems answers at most 60).
    static constexpr int PageSize = 60;
    /// Pages read in a row that add no card before waiting for loadMore().
    static constexpr int MaxEmptyPages = 5;
    static constexpr int MinimumPicks = 3;
    /// New picks one submission takes (createTasteSeed's maxItems).
    static constexpr int MaximumPicks = 200;

    explicit TasteSeedViewModel(QObject *parent = nullptr);
    TasteSeedViewModel(LibraryService *libraryService, ConfigManager *config,
                       QObject *parent = nullptr);
    ~TasteSeedViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString connectionId() const { return m_connectionId; }
    bool available() const { return m_available; }
    int count() const { return static_cast<int>(m_items.size()); }
    int selectedCount() const { return static_cast<int>(m_selected.size()); }
    int minimumPicks() const { return MinimumPicks; }
    int maximumPicks() const { return MaximumPicks; }
    bool canSubmit() const;
    bool submitting() const { return m_submitting; }
    bool hasMore() const { return m_hasMore; }
    bool loadingMore() const { return m_loadingMore; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload() override;
    /// The canonical map at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Selects or unselects the title at `row`; false when nothing changed.
    Q_INVOKABLE bool toggle(int row);
    /// The picks submit() would send (selected, not favorites already).
    Q_INVOKABLE QStringList newPicks() const;
    /// Sends the new picks; false when it cannot (too few, or busy).
    Q_INVOKABLE bool submit();
    Q_INVOKABLE void skip();

signals:
    void connectionChanged();
    void countChanged();
    void selectionChanged();
    void submittingChanged();
    void hasMoreChanged();
    void loadingMoreChanged();
    /// The picks were recorded; `added` of them were sent.
    void submitted(int added);
    void submitFailed(const QString &message);
    /// A later page could not be read; the cards shown stay.
    void pageFailed(const QString &message);

private:
    void connectService();
    void requestPage();
    void onItemsLoaded(const QString &connectionId, const QString &requestKey,
                       const QVariantList &items, bool hasMore, const QString &nextCursor);
    void onItemsFailed(const QString &connectionId, const QString &requestKey,
                       const QString &error, int httpStatus);
    void onWriteFinished(const QString &connectionId, const QString &requestKey,
                         bool success, const QString &error);
    void setHasMore(bool hasMore);
    void setLoadingMore(bool loadingMore);
    void setSubmitting(bool submitting);
    void finish(int added);
    static bool isFavorite(const QVariantMap &item);

    LibraryService *m_libraryService = nullptr;
    ConfigManager *m_config = nullptr;
    QVariantList m_items;
    QSet<QString> m_itemIds;
    // Selected titles, and those the profile had favorited already.
    QSet<QString> m_selected;
    QSet<QString> m_favorites;
    QString m_connectionId;
    QString m_scope;
    bool m_available = false;
    bool m_firstPagePending = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    bool m_submitting = false;
    QString m_pageRequestKey;
    QString m_nextCursor;
    int m_emptyPages = 0;
    QString m_submitRequestKey;
    int m_submitCount = 0;
};
