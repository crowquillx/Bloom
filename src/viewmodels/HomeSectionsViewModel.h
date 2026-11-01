#pragma once

#include "BaseViewModel.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class LibraryService;

/**
 * @brief The Home rows a Silo profile curates, below Bloom's own shelves,
 * and the Favorites row Home shows for providers without a watchlist.
 *
 * One list row per server row, in the server's layout order, with the roles
 * `sectionId`, `title` and `items` (canonical camelCase item maps).
 * `LibraryService::getHomeSections` decides which rows Bloom shows; this
 * model only owns their lifecycle for one connection:
 *
 * - `refresh()` loads in the background (`isLoading`). A failure sets
 *   `hasError`/`errorMessage` and keeps the rows already shown.
 * - Answers for another connection than the newest request are ignored.
 * - A refresh that keeps the same rows in the same order updates their
 *   titles and items in place, so the shelves keep their delegates.
 * - A successful watchlist change refreshes rows already loaded, and so
 *   does a favorite change when the provider has the Favorites row.
 *
 * Merged spaces show no curated rows: Home calls `clear()` instead.
 */
class HomeSectionsViewModel : public BaseViewModel
{
    Q_OBJECT

public:
    enum Roles {
        SectionIdRole = Qt::UserRole + 1,
        TitleRole,
        ItemsRole
    };
    Q_ENUM(Roles)

    explicit HomeSectionsViewModel(QObject *parent = nullptr);
    explicit HomeSectionsViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~HomeSectionsViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Loads the active connection's rows. Rows of another connection are
    /// dropped first.
    Q_INVOKABLE void refresh();
    /// Drops every row and forgets the pending request.
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload() override;

private slots:
    void onSectionsLoaded(const QString &connectionId, const QVariantList &sections);
    void onSectionsFailed(const QString &connectionId, const QString &error);
    void onWatchlistStatusChanged(const QString &itemId, bool inWatchlist);
    void onFavoriteStatusChanged(const QString &itemId, bool isFavorite);

private:
    void connectService();
    void applyRows(const QVariantList &rows);
    /// Refreshes rows loaded or loading for the live connection.
    void refreshLoadedRows();
    static QString sectionIdOf(const QVariant &row);

    LibraryService *m_libraryService = nullptr;
    QVariantList m_rows;
    /// Connection the shown rows belong to.
    QString m_connectionId;
    /// Connection of the request in flight; empty when none is.
    QString m_pendingConnectionId;
    bool m_requestPending = false;
};
