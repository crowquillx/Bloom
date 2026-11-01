#pragma once

#include "BaseViewModel.h"

#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class LibraryService;

/**
 * @brief The shelves a Silo profile curates for one library, which
 * `LibraryScreen.qml` offers beside the library's grid.
 *
 * One list row per server row, in the server's order, with the roles
 * `sectionId`, `title`, `sectionType`, `featured`, `items` (canonical
 * camelCase movie and series maps), `hasMore` and `loadingMore`.
 * `LibraryService::getLibrarySections` picks the rows and maps the items;
 * this model owns their lifecycle for one library and connection:
 *
 * - `load(libraryId)` reads the library's rows in the background
 *   (`isLoading`). Rows of another library or connection are dropped first;
 *   a reload of the same library updates rows in place when their IDs and
 *   order hold, so shelves keep their delegates.
 * - `loadMore(row)` reads every card of a row that has more
 *   (`getLibrarySectionItems`). The answer replaces the row's items; the
 *   row then has no more, since the server pages a row no further.
 * - Only the answer to the newest request for the active library and
 *   connection is applied. A failure sets `hasError` and keeps the rows.
 * - `available` is false for providers without curated library rows
 *   (Jellyfin); `load` then only clears the model.
 */
class LibrarySectionsViewModel : public BaseViewModel
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY libraryIdChanged)

public:
    enum Roles {
        SectionIdRole = Qt::UserRole + 1,
        TitleRole,
        SectionTypeRole,
        FeaturedRole,
        ItemsRole,
        HasMoreRole,
        LoadingMoreRole
    };
    Q_ENUM(Roles)

    explicit LibrarySectionsViewModel(QObject *parent = nullptr);
    explicit LibrarySectionsViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~LibrarySectionsViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    int count() const { return static_cast<int>(m_rows.size()); }
    QString libraryId() const { return m_libraryId; }

    /// Loads @p libraryId's rows for the active connection.
    Q_INVOKABLE void load(const QString &libraryId);
    /// Reads every card of @p row when it has more.
    Q_INVOKABLE void loadMore(int row);
    /// Drops every row and forgets the pending requests.
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload() override;

signals:
    void availableChanged();
    void countChanged();
    void libraryIdChanged();

private slots:
    void onSectionsLoaded(const QString &connectionId, const QString &libraryId,
                          const QVariantList &sections);
    void onSectionsFailed(const QString &connectionId, const QString &libraryId,
                          const QString &error);
    void onSectionItemsLoaded(const QString &connectionId, const QString &libraryId,
                              const QString &sectionId, const QVariantList &items);
    void onSectionItemsFailed(const QString &connectionId, const QString &libraryId,
                              const QString &sectionId, const QString &error);

private:
    void connectService();
    void refreshAvailable();
    void applyRows(const QVariantList &rows);
    void setRows(const QVariantList &rows);
    void setLibraryId(const QString &libraryId);
    int rowOfSection(const QString &sectionId) const;
    static QString sectionIdOf(const QVariant &row);

    LibraryService *m_libraryService = nullptr;
    bool m_available = false;
    QVariantList m_rows;
    QString m_libraryId;
    /// Connection the shown rows belong to.
    QString m_connectionId;
    /// Library and connection of the rows read in flight.
    QString m_pendingLibraryId;
    QString m_pendingConnectionId;
    bool m_requestPending = false;
    /// Sections whose remaining cards are being read.
    QSet<QString> m_loadingMore;
};
