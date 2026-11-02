#pragma once

#include "BaseViewModel.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class LibraryService;

/**
 * @brief The collections of one movie or series library (the library
 * page's Collections view).
 *
 * Collections are a browse view over the library's titles, not a library
 * kind (models/MediaCollections.h). Each LibraryScreen owns one instance
 * (QML type `LibraryCollectionsViewModel`).
 *
 * - Rows are canonical collections in the server's order, each ID once.
 *   Roles: `collectionData` (the canonical map), `collectionId`, `name`.
 * - `available` is false while the active provider has no collections
 *   (`LibraryService::supportsCollections`); `load` then sends nothing.
 * - `load(libraryId)` reads the first page unless the same library on the
 *   same connection is already loaded or loading; `reload()` starts over.
 *   `loadMore()` reads the next page while `hasMore`. Later pages only
 *   insert rows, so the focused card stays put.
 * - `isLoading` covers the first page; its failure sets `hasError`. A
 *   failed later page keeps the rows and `hasMore`, so the next
 *   `loadMore()` tries again.
 * - Answers for another library, connection or an older load are ignored.
 */
class LibraryCollectionsViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryIdChanged)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY libraryIdChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY collectionsChanged)
    Q_PROPERTY(int count READ count NOTIFY collectionsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY collectionsChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)

public:
    enum Roles {
        CollectionDataRole = Qt::UserRole + 1,
        CollectionIdRole,
        NameRole
    };
    Q_ENUM(Roles)

    /// Collections read per page.
    static constexpr int PageSize = 100;

    explicit LibraryCollectionsViewModel(QObject *parent = nullptr);
    explicit LibraryCollectionsViewModel(LibraryService *libraryService,
                                         QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString libraryId() const { return m_libraryId; }
    QString connectionId() const { return m_connectionId; }
    bool available() const;
    bool loaded() const { return m_loaded; }
    int count() const { return static_cast<int>(m_collections.size()); }
    bool hasMore() const { return m_hasMore; }
    bool loadingMore() const { return m_loadingMore; }

    Q_INVOKABLE void load(const QString &libraryId);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload() override;
    Q_INVOKABLE void clear();
    /// The canonical collection at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// The row of the collection with this ID, or -1.
    Q_INVOKABLE int indexOfCollection(const QString &collectionId) const;

signals:
    void libraryIdChanged();
    void availableChanged();
    void collectionsChanged();
    void loadingMoreChanged();

private slots:
    void onLoaded(const QString &connectionId, const QString &libraryId,
                  const QString &requestKey, const QVariantList &collections,
                  int totalRecordCount, bool hasMore);
    void onFailed(const QString &connectionId, const QString &libraryId,
                  const QString &requestKey, const QString &error);

private:
    void requestPage();
    void setLoadingMore(bool loadingMore);

    LibraryService *m_libraryService = nullptr;
    QString m_libraryId;
    QString m_connectionId;
    QVariantList m_collections;
    QSet<QString> m_collectionIds;
    bool m_loaded = false;
    bool m_firstPagePending = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    /// Collections the server has sent, where the next page starts.
    int m_nextStartIndex = 0;
    QString m_requestKey;
};

/**
 * @brief One collection's titles (the Collection page).
 *
 * Each CollectionScreen owns one instance (QML type `CollectionViewModel`).
 *
 * - `load(collection)` takes a canonical collection map. Its name, overview
 *   and artwork show at once while the first page loads. A collection of
 *   another connection than the live one is refused with an error (merged
 *   spaces route to the owning connection before the page opens).
 * - Rows are canonical movie and series maps only (LibraryKinds), each ID
 *   once, in `sortBy` order: `""` (the collection's own order),
 *   `releaseDate` (oldest first) or `title`. `sorts` lists them.
 *   Changing `sortBy` reads the collection again from the first page.
 *   Roles: `itemData`, `itemId`, `mediaType`, `name`.
 * - `loadMore()` reads the next page while `hasMore`, continuing from the
 *   server's cursor (Silo) or the number of titles it has sent before
 *   filtering (Jellyfin). Pages only insert rows.
 * - `isLoading` covers the first page; its failure sets `hasError`. A
 *   failed later page keeps the titles and `hasMore`.
 * - Answers for another collection, connection, sort or an older load are
 *   ignored.
 */
class CollectionViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap collection READ collection NOTIFY collectionChanged)
    Q_PROPERTY(QString collectionId READ collectionId NOTIFY collectionChanged)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY collectionChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(QStringList sorts READ sorts CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY itemsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY itemsChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)

public:
    enum Roles {
        ItemDataRole = Qt::UserRole + 1,
        ItemIdRole,
        MediaTypeRole,
        NameRole
    };
    Q_ENUM(Roles)

    /// Titles read per page (Silo's structured limit).
    static constexpr int PageSize = 100;

    explicit CollectionViewModel(QObject *parent = nullptr);
    explicit CollectionViewModel(LibraryService *libraryService, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantMap collection() const { return m_collection; }
    QString collectionId() const { return m_collectionId; }
    QString connectionId() const { return m_connectionId; }
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString &sortBy);
    QStringList sorts() const;
    int count() const { return static_cast<int>(m_items.size()); }
    /// The titles the server counts, or the loaded count when it says none.
    int totalCount() const;
    bool hasMore() const { return m_hasMore; }
    bool loadingMore() const { return m_loadingMore; }

    Q_INVOKABLE void load(const QVariantMap &collection);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload() override;
    /// The canonical title at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// The row of the title with this ID, or -1.
    Q_INVOKABLE int indexOfItem(const QString &itemId) const;

signals:
    void collectionChanged();
    void sortByChanged();
    void itemsChanged();
    void loadingMoreChanged();

private slots:
    void onLoaded(const QString &connectionId, const QString &collectionId,
                  const QString &requestKey, const QVariantList &items,
                  int totalRecordCount, bool hasMore, const QString &nextCursor);
    void onFailed(const QString &connectionId, const QString &collectionId,
                  const QString &requestKey, const QString &error);

private:
    void requestPage();
    void setLoadingMore(bool loadingMore);

    LibraryService *m_libraryService = nullptr;
    QVariantMap m_collection;
    QString m_collectionId;
    QString m_collectionKind;
    QString m_connectionId;
    QString m_sortBy;
    QVariantList m_items;
    QSet<QString> m_itemIds;
    bool m_firstPagePending = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    int m_serverTotal = -1;
    int m_nextStartIndex = 0;
    QString m_nextCursor;
    QString m_requestKey;
};

/**
 * @brief The collections that hold one title (the detail pages' "Part of"
 * shelf).
 *
 * Each detail view owns one instance (QML type `ItemCollectionsViewModel`).
 * `load(itemId)` reads `LibraryService::getItemCollections` on the live
 * connection; reloading the same title keeps the collections shown until
 * the answer replaces them, and another title clears them first. A failed
 * or unsupported read leaves no collections and no error, since the shelf
 * is optional. `clear()` drops everything.
 */
class ItemCollectionsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString itemId READ itemId NOTIFY collectionsChanged)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY collectionsChanged)
    Q_PROPERTY(int count READ count NOTIFY collectionsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit ItemCollectionsViewModel(QObject *parent = nullptr);
    explicit ItemCollectionsViewModel(LibraryService *libraryService,
                                      QObject *parent = nullptr);

    QString itemId() const { return m_itemId; }
    QString connectionId() const { return m_connectionId; }
    QVariantList collections() const { return m_collections; }
    int count() const { return static_cast<int>(m_collections.size()); }
    bool loading() const { return !m_requestKey.isEmpty(); }

    Q_INVOKABLE void load(const QString &itemId);
    Q_INVOKABLE void clear();

signals:
    void collectionsChanged();
    void loadingChanged();

private slots:
    void onLoaded(const QString &connectionId, const QString &itemId,
                  const QString &requestKey, const QVariantList &collections);
    void onFailed(const QString &connectionId, const QString &itemId,
                  const QString &requestKey, const QString &error);

private:
    void finish();

    LibraryService *m_libraryService = nullptr;
    QString m_itemId;
    QString m_connectionId;
    QVariantList m_collections;
    QString m_requestKey;
};
