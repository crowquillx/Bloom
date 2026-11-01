#include "HomeSectionsViewModel.h"

#include "../core/ServiceLocator.h"
#include "../models/LibraryKinds.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

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

void HomeSectionsViewModel::refresh()
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
        m_librariesConnectionId == connectionId ? m_movieLibraryIds : QStringList());
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
    bool sameRows = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameRows && i < rows.size(); ++i) {
        sameRows = sectionIdOf(rows.at(i)) == sectionIdOf(m_rows.at(i));
    }
    if (!sameRows) {
        emitModelReset([this, &rows]() { m_rows = rows; });
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows.at(i) == m_rows.at(i)) {
            continue;
        }
        m_rows[i] = rows.at(i);
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed, {TitleRole, ItemsRole});
    }
}
