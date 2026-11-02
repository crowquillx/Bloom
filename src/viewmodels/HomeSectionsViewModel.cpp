#include "HomeSectionsViewModel.h"

#include "../core/ServiceLocator.h"
#include "../models/HomeRecommendations.h"
#include "../models/LibraryKinds.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

#include <QSet>

#include <algorithm>

HomeSectionsViewModel::HomeSectionsViewModel(QObject *parent)
    : HomeSectionsViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

HomeSectionsViewModel::HomeSectionsViewModel(LibraryService *libraryService, QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    connectService();
}

void HomeSectionsViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "HomeSectionsViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalHomeSectionsLoaded,
            this, &HomeSectionsViewModel::onSectionsLoaded);
    connect(m_libraryService, &LibraryService::canonicalHomeSectionsFailed,
            this, &HomeSectionsViewModel::onSectionsFailed);
    connect(m_libraryService, &LibraryService::canonicalHomeSectionsUpdated,
            this, &HomeSectionsViewModel::onSectionsUpdated);
    connect(m_libraryService, &LibraryService::watchlistStatusChanged,
            this, &HomeSectionsViewModel::onWatchlistStatusChanged);
    connect(m_libraryService, &LibraryService::favoriteStatusChanged,
            this, &HomeSectionsViewModel::onFavoriteStatusChanged);
    // Recommendation rows leave out (or bring back) a title marked Not
    // interested once the server rebuilds them.
    connect(m_libraryService, &LibraryService::notInterestedStatusChanged,
            this, &HomeSectionsViewModel::onWatchlistStatusChanged);
}

int HomeSectionsViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant HomeSectionsViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const QVariantMap row = m_rows.at(index.row()).toMap();
    switch (role) {
    case SectionIdRole:
        return row.value(QStringLiteral("sectionId"));
    case TitleRole:
        return row.value(QStringLiteral("title"));
    case ItemsRole:
        return row.value(QStringLiteral("items"));
    default:
        return {};
    }
}

QHash<int, QByteArray> HomeSectionsViewModel::roleNames() const
{
    return {
        {SectionIdRole, "sectionId"},
        {TitleRole, "title"},
        {ItemsRole, "items"}
    };
}

void HomeSectionsViewModel::refresh(bool reloadRecommendations)
{
    if (!m_libraryService) {
        return;
    }
    const QString connectionId = m_libraryService->getActiveConnectionId();
    if (connectionId != m_connectionId) {
        // Rows belong to one connection and profile; never show them under
        // another while the new ones load.
        m_connectionId.clear();
        if (!m_rows.isEmpty()) {
            emitModelReset([this]() { m_rows.clear(); });
        }
    }
    m_pendingConnectionId = connectionId;
    m_requestPending = true;
    clearError();
    setLoading(true);
    // May answer synchronously (no curated rows for this provider).
    m_libraryService->getHomeSections(
        m_librariesConnectionId == connectionId ? m_movieLibraryIds : QStringList(),
        reloadRecommendations);
}

void HomeSectionsViewModel::removeRecommendationRows()
{
    removeRowsWithPrefix(HomeRecommendations::SectionIdPrefix);
}

void HomeSectionsViewModel::removeWatchTonightRow()
{
    removeRowsWithPrefix(LibraryService::WatchTonightRowId);
}

void HomeSectionsViewModel::removeRowsWithPrefix(const QString &prefix)
{
    // From the end, so the rows above keep their delegates.
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i) {
        if (sectionIdOf(m_rows.at(i)).startsWith(prefix)) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
}

int HomeSectionsViewModel::restoreRowIndex(const QString &sectionId, int position) const
{
    if (m_rows.isEmpty()) {
        return -1;
    }
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (!sectionId.isEmpty() && sectionIdOf(m_rows.at(i)) == sectionId) {
            return static_cast<int>(i);
        }
    }
    return std::clamp(position, 0, static_cast<int>(m_rows.size()) - 1);
}

void HomeSectionsViewModel::setLibraries(const QVariantList &views)
{
    m_movieLibraryIds.clear();
    m_librariesConnectionId = m_libraryService ? m_libraryService->getActiveConnectionId()
                                               : QString();
    for (const QVariant &value : views) {
        const QVariantMap view = value.toMap();
        QString kind = view.value(QStringLiteral("libraryKind")).toString();
        if (kind.isEmpty()) {
            kind = LibraryKinds::kindForCollectionType(
                view.value(QStringLiteral("collectionType")).toString());
        }
        const QString libraryId = view.value(QStringLiteral("itemId")).toString();
        if (kind == LibraryKinds::Movies && !libraryId.isEmpty()
            && !m_movieLibraryIds.contains(libraryId)) {
            m_movieLibraryIds.append(libraryId);
        }
    }
}

void HomeSectionsViewModel::clear()
{
    m_requestPending = false;
    m_pendingConnectionId.clear();
    m_connectionId.clear();
    m_movieLibraryIds.clear();
    m_librariesConnectionId.clear();
    if (!m_rows.isEmpty()) {
        emitModelReset([this]() { m_rows.clear(); });
    }
    setLoading(false);
    clearError();
}

void HomeSectionsViewModel::reload()
{
    refresh();
}

void HomeSectionsViewModel::onSectionsLoaded(const QString &connectionId,
                                             const QVariantList &sections)
{
    if (!m_requestPending || connectionId != m_pendingConnectionId) {
        return;
    }
    m_requestPending = false;
    m_pendingConnectionId.clear();
    m_connectionId = connectionId;
    applyRows(sections);
    clearError();
    setLoading(false);
}

void HomeSectionsViewModel::onSectionsFailed(const QString &connectionId, const QString &error)
{
    if (!m_requestPending || connectionId != m_pendingConnectionId) {
        return;
    }
    m_requestPending = false;
    m_pendingConnectionId.clear();
    qCWarning(lcViewModels) << "HomeSectionsViewModel: home rows failed:" << error;
    // Rows already shown stay; Home keeps its own shelves either way.
    setError(mapNetworkError(QStringLiteral("getHomeSections"), error));
    setLoading(false);
}

void HomeSectionsViewModel::onSectionsUpdated(const QString &connectionId,
                                              const QVariantList &sections)
{
    // Only the rows shown for this connection; a newer refresh in flight
    // answers for itself.
    if (m_requestPending || connectionId.isEmpty() || connectionId != m_connectionId) {
        return;
    }
    applyRows(sections);
}

void HomeSectionsViewModel::onWatchlistStatusChanged(const QString &itemId, bool inWatchlist)
{
    Q_UNUSED(itemId)
    Q_UNUSED(inWatchlist)
    // A Watchlist row (the server's or Bloom's) may have changed.
    refreshLoadedRows();
}

void HomeSectionsViewModel::onFavoriteStatusChanged(const QString &itemId, bool isFavorite)
{
    Q_UNUSED(itemId)
    Q_UNUSED(isFavorite)
    // The Favorites row may gain, lose or reorder a card. Providers with a
    // watchlist have no such row, so their rows stay.
    if (m_libraryService && m_libraryService->supportsHomeFavoritesRow()) {
        refreshLoadedRows();
    }
}

void HomeSectionsViewModel::refreshLoadedRows()
{
    // Only rows loaded or loading for the live connection are refreshed;
    // Home loads fresh rows itself otherwise. A read in flight may predate
    // the change, so it is replaced too.
    if (!m_libraryService) {
        return;
    }
    const QString activeConnectionId = m_libraryService->getActiveConnectionId();
    if (activeConnectionId.isEmpty()) {
        return;
    }
    if (m_connectionId == activeConnectionId
        || (m_requestPending && m_pendingConnectionId == activeConnectionId)) {
        refresh();
    }
}

QString HomeSectionsViewModel::sectionIdOf(const QVariant &row)
{
    return row.toMap().value(QStringLiteral("sectionId")).toString();
}

void HomeSectionsViewModel::applyRows(const QVariantList &rows)
{
    QStringList oldIds;
    for (const QVariant &row : std::as_const(m_rows)) {
        oldIds.append(sectionIdOf(row));
    }
    QStringList newIds;
    for (const QVariant &row : rows) {
        newIds.append(sectionIdOf(row));
    }
    // Rows kept must keep their order and IDs must be unique; otherwise
    // the shelves are rebuilt.
    const auto unique = [](const QStringList &ids) {
        return QSet<QString>(ids.cbegin(), ids.cend()).size() == ids.size();
    };
    QStringList keptOld;
    for (const QString &id : std::as_const(oldIds)) {
        if (newIds.contains(id)) {
            keptOld.append(id);
        }
    }
    QStringList keptNew;
    for (const QString &id : std::as_const(newIds)) {
        if (oldIds.contains(id)) {
            keptNew.append(id);
        }
    }
    if (m_rows.isEmpty() || !unique(oldIds) || !unique(newIds) || keptOld != keptNew) {
        if (m_rows.isEmpty() && rows.isEmpty()) {
            return;
        }
        emitModelReset([this, &rows]() { m_rows = rows; });
        return;
    }
    // Rows that are gone, from the end so the rows above keep their place.
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i) {
        if (!newIds.contains(oldIds.at(i))) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    // Now m_rows is newIds' kept rows in order: insert the new ones and
    // update the kept ones where they changed.
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (i >= m_rows.size() || sectionIdOf(m_rows.at(i)) != newIds.at(i)) {
            beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
            m_rows.insert(i, rows.at(i));
            endInsertRows();
            continue;
        }
        if (rows.at(i) == m_rows.at(i)) {
            continue;
        }
        m_rows[i] = rows.at(i);
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed, {TitleRole, ItemsRole});
    }
}
