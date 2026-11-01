#pragma once

#include "BaseViewModel.h"

#include <QAbstractListModel>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class LibraryService;

/**
 * @brief One shelf of a person's filmography (their movies, or their
 * series). Pages only add rows (`rowsInserted`), so a shelf keeps its
 * current card and scroll position while more titles load; a new load
 * resets it.
 *
 * Roles: `itemData` (the canonical map), `itemId`, `mediaType`, `name`.
 */
class PersonTitlesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        ItemDataRole = Qt::UserRole + 1,
        ItemIdRole,
        MediaTypeRole,
        NameRole
    };
    Q_ENUM(Roles)

    explicit PersonTitlesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_items.size()); }
    QVariantList items() const { return m_items; }
    /// The canonical map at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;

    void append(const QVariantList &items);
    void clear();

signals:
    void countChanged();

private:
    QVariantList m_items;
};

/**
 * @brief One person's page: who they are and the movies and series they
 * appear in on the live connection.
 *
 * Each Person page owns its own instance (QML type `PersonViewModel`), so
 * stacked person pages keep their state. `load(person)` takes the canonical
 * person map a cast card or a people search result carries; its name and
 * photo show at once while the person's details and the first filmography
 * page load.
 *
 * - `person` is the canonical person map (`personId`, `connectionId`,
 *   `name`, `artwork`, and when known `overview`, `birthDate`, `deathDate`,
 *   `birthplace`, `providerIds`).
 * - The list rows are the filmography, newest first: canonical movie and
 *   series maps only (LibraryKinds: every other type is dropped), each item
 *   once. `movies` and `series` split them into one PersonTitlesModel per
 *   shelf; later pages only insert rows.
 * - `isLoading` covers the details and the first page. A failed first page
 *   sets `hasError`/`errorMessage`; failed details keep the seed and show
 *   no error. `reload()` starts over.
 * - `loadMore()` reads the next page while `hasMore`; a failure keeps
 *   `hasMore` so a later call tries again.
 * - Answers for another person, connection or an older load are ignored.
 */
class PersonViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QString personId READ personId NOTIFY personChanged)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY personChanged)
    Q_PROPERTY(QVariantMap person READ person NOTIFY personChanged)
    Q_PROPERTY(PersonTitlesModel *movies READ movies CONSTANT)
    Q_PROPERTY(PersonTitlesModel *series READ series CONSTANT)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY filmographyChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)

public:
    enum Roles {
        ItemDataRole = Qt::UserRole + 1,
        ItemIdRole,
        MediaTypeRole,
        NameRole
    };
    Q_ENUM(Roles)

    /// Titles read per filmography page.
    static constexpr int PageSize = 60;

    explicit PersonViewModel(QObject *parent = nullptr);
    explicit PersonViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~PersonViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString personId() const { return m_personId; }
    QString connectionId() const { return m_connectionId; }
    QVariantMap person() const { return m_person; }
    PersonTitlesModel *movies() const { return m_movies; }
    PersonTitlesModel *series() const { return m_series; }
    bool hasMore() const { return m_hasMore; }
    bool loadingMore() const { return m_loadingMore; }

    /// Shows a person: the canonical person map from a cast card or a
    /// people search result.
    Q_INVOKABLE void load(const QVariantMap &person);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload() override;

signals:
    void personChanged();
    void filmographyChanged();
    void loadingMoreChanged();

private slots:
    void onPersonLoaded(const QString &connectionId, const QString &personId,
                        const QVariantMap &person);
    void onPersonFailed(const QString &connectionId, const QString &personId,
                        const QString &error);
    void onPersonItemsLoaded(const QString &connectionId, const QString &personId,
                             const QString &requestKey, const QVariantList &items,
                             int totalRecordCount, bool hasMore,
                             const QString &nextCursor);
    void onPersonItemsFailed(const QString &connectionId, const QString &personId,
                             const QString &requestKey, const QString &error);

private:
    void connectService();
    void requestPage();
    void updateLoading();
    void setLoadingMore(bool loadingMore);
    bool isCurrent(const QString &connectionId, const QString &personId) const;

    LibraryService *m_libraryService = nullptr;
    QVariantMap m_seed;
    QVariantMap m_person;
    QString m_personId;
    QString m_connectionId;
    QVariantList m_items;
    QSet<QString> m_itemIds;
    PersonTitlesModel *m_movies = nullptr;
    PersonTitlesModel *m_series = nullptr;
    bool m_detailsPending = false;
    bool m_firstPagePending = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    /// Where the next page starts: the count of titles the server has sent
    /// (Jellyfin), or its continuation cursor (Silo).
    int m_nextStartIndex = 0;
    QString m_nextCursor;
    /// The page request in flight; empty when none is.
    QString m_pageRequestKey;
    quint64 m_requestSerial = 0;
};
