#include "LibrarySectionsViewModel.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

LibrarySectionsViewModel::LibrarySectionsViewModel(QObject *parent)
    : LibrarySectionsViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

LibrarySectionsViewModel::LibrarySectionsViewModel(LibraryService *libraryService,
                                                   QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    connectService();
}

void LibrarySectionsViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "LibrarySectionsViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalLibrarySectionsLoaded,
            this, &LibrarySectionsViewModel::onSectionsLoaded);
    connect(m_libraryService, &LibraryService::canonicalLibrarySectionsFailed,
            this, &LibrarySectionsViewModel::onSectionsFailed);
    connect(m_libraryService, &LibraryService::canonicalLibrarySectionItemsLoaded,
            this, &LibrarySectionsViewModel::onSectionItemsLoaded);
    connect(m_libraryService, &LibraryService::canonicalLibrarySectionItemsFailed,
            this, &LibrarySectionsViewModel::onSectionItemsFailed);
}

int LibrarySectionsViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant LibrarySectionsViewModel::data(const QModelIndex &index, int role) const
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
    case SectionTypeRole:
        return row.value(QStringLiteral("sectionType"));
    case FeaturedRole:
        return row.value(QStringLiteral("featured")).toBool();
    case ItemsRole:
        return row.value(QStringLiteral("items"));
    case HasMoreRole:
        return row.value(QStringLiteral("hasMore")).toBool();
    case LoadingMoreRole:
        return m_loadingMore.contains(row.value(QStringLiteral("sectionId")).toString());
    default:
        return {};
    }
}

QHash<int, QByteArray> LibrarySectionsViewModel::roleNames() const
{
    return {
        {SectionIdRole, "sectionId"},
        {TitleRole, "title"},
        {SectionTypeRole, "sectionType"},
        {FeaturedRole, "featured"},
        {ItemsRole, "items"},
        {HasMoreRole, "hasMore"},
        {LoadingMoreRole, "loadingMore"}
    };
}

void LibrarySectionsViewModel::refreshAvailable()
{
    const bool available = m_libraryService && m_libraryService->supportsLibrarySections();
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
}

void LibrarySectionsViewModel::load(const QString &libraryId)
{
    refreshAvailable();
    if (!m_libraryService || !m_available || libraryId.isEmpty()) {
        clear();
        return;
    }
    const QString connectionId = m_libraryService->getActiveConnectionId();
    if (connectionId != m_connectionId || libraryId != m_libraryId) {
        // Rows belong to one library, connection and profile; never show
        // them under another while the new ones load.
        m_connectionId.clear();
        m_loadingMore.clear();
        setRows({});
    }
    setLibraryId(libraryId);
    m_pendingLibraryId = libraryId;
    m_pendingConnectionId = connectionId;
    m_requestPending = true;
    clearError();
    setLoading(true);
    m_libraryService->getLibrarySections(libraryId);
}

void LibrarySectionsViewModel::loadMore(int row)
{
    if (!m_libraryService || row < 0 || row >= m_rows.size() || m_libraryId.isEmpty()) {
        return;
    }
    const QVariantMap entry = m_rows.at(row).toMap();
    const QString sectionId = entry.value(QStringLiteral("sectionId")).toString();
    if (!entry.value(QStringLiteral("hasMore")).toBool() || m_loadingMore.contains(sectionId)) {
        return;
    }
    m_loadingMore.insert(sectionId);
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {LoadingMoreRole});
    m_libraryService->getLibrarySectionItems(m_libraryId, sectionId);
}

void LibrarySectionsViewModel::clear()
{
    m_requestPending = false;
    m_pendingLibraryId.clear();
    m_pendingConnectionId.clear();
    m_connectionId.clear();
    m_loadingMore.clear();
    setRows({});
    setLibraryId({});
    setLoading(false);
    clearError();
}

void LibrarySectionsViewModel::reload()
{
    load(m_libraryId);
}

void LibrarySectionsViewModel::onSectionsLoaded(const QString &connectionId,
                                                const QString &libraryId,
                                                const QVariantList &sections)
{
    if (!m_requestPending || connectionId != m_pendingConnectionId
        || libraryId != m_pendingLibraryId) {
        return;
    }
    m_requestPending = false;
    m_pendingConnectionId.clear();
    m_pendingLibraryId.clear();
    m_connectionId = connectionId;
    applyRows(sections);
    clearError();
    setLoading(false);
}

void LibrarySectionsViewModel::onSectionsFailed(const QString &connectionId,
                                                const QString &libraryId,
                                                const QString &error)
{
    if (!m_requestPending || connectionId != m_pendingConnectionId
        || libraryId != m_pendingLibraryId) {
        return;
    }
    m_requestPending = false;
    m_pendingConnectionId.clear();
    m_pendingLibraryId.clear();
    qCWarning(lcViewModels) << "LibrarySectionsViewModel: library rows failed:" << error;
    setError(mapNetworkError(QStringLiteral("getLibrarySections"), error));
    setLoading(false);
}

void LibrarySectionsViewModel::onSectionItemsLoaded(const QString &connectionId,
                                                    const QString &libraryId,
                                                    const QString &sectionId,
                                                    const QVariantList &items)
{
    if (connectionId != m_connectionId || libraryId != m_libraryId
        || !m_loadingMore.remove(sectionId)) {
        return;
    }
    const int row = rowOfSection(sectionId);
    if (row < 0) {
        return;
    }
    QVariantMap entry = m_rows.at(row).toMap();
    // The whole row as the server holds it now, including cards a refresh
    // kept from an older read; the shelf finds its focused card by ID.
    entry.insert(QStringLiteral("items"), items);
    entry.insert(QStringLiteral("hasMore"), false);
    m_rows[row] = entry;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {ItemsRole, HasMoreRole, LoadingMoreRole});
}

void LibrarySectionsViewModel::onSectionItemsFailed(const QString &connectionId,
                                                    const QString &libraryId,
                                                    const QString &sectionId,
                                                    const QString &error)
{
    if (connectionId != m_connectionId || libraryId != m_libraryId
        || !m_loadingMore.remove(sectionId)) {
        return;
    }
    qCWarning(lcViewModels) << "LibrarySectionsViewModel: row" << sectionId
                            << "failed to load more:" << error;
    const int row = rowOfSection(sectionId);
    if (row >= 0) {
        // No read on every focus move: the row asks again only once the
        // shelves are read again.
        QVariantMap entry = m_rows.at(row).toMap();
        entry.insert(QStringLiteral("hasMore"), false);
        m_rows[row] = entry;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {HasMoreRole, LoadingMoreRole});
    }
}

QString LibrarySectionsViewModel::sectionIdOf(const QVariant &row)
{
    return row.toMap().value(QStringLiteral("sectionId")).toString();
}

int LibrarySectionsViewModel::rowOfSection(const QString &sectionId) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (sectionIdOf(m_rows.at(i)) == sectionId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LibrarySectionsViewModel::indexOfItem(int row, const QString &itemId) const
{
    if (row < 0 || row >= m_rows.size() || itemId.isEmpty()) {
        return -1;
    }
    const QVariantList items = m_rows.at(row).toMap().value(QStringLiteral("items")).toList();
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items.at(i).toMap().value(QStringLiteral("itemId")).toString() == itemId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void LibrarySectionsViewModel::setRows(const QVariantList &rows)
{
    if (rows.isEmpty() && m_rows.isEmpty()) {
        return;
    }
    const bool countChanges = rows.size() != m_rows.size();
    emitModelReset([this, &rows]() { m_rows = rows; });
    if (countChanges) {
        emit countChanged();
    }
}

void LibrarySectionsViewModel::setLibraryId(const QString &libraryId)
{
    if (m_libraryId != libraryId) {
        m_libraryId = libraryId;
        emit libraryIdChanged();
    }
}

void LibrarySectionsViewModel::applyRows(const QVariantList &rows)
{
    bool sameRows = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameRows && i < rows.size(); ++i) {
        sameRows = sectionIdOf(rows.at(i)) == sectionIdOf(m_rows.at(i));
    }
    if (!sameRows) {
        m_loadingMore.clear();
        setRows(rows);
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QVariantMap fresh = rows.at(i).toMap();
        const QVariantMap current = m_rows.at(i).toMap();
        // A row already read in full keeps, after the fresh first page,
        // every card it showed that the page lacks (by ID, wherever it
        // sat), so a focused card there (a return from its details) stays
        // and a card the page pushed out is not lost. The row keeps its
        // more flag, so the next read near its end replaces the merge with
        // the whole row.
        const QVariantList currentItems = current.value(QStringLiteral("items")).toList();
        QVariantList freshItems = fresh.value(QStringLiteral("items")).toList();
        if (fresh.value(QStringLiteral("hasMore")).toBool()
            && currentItems.size() > freshItems.size()) {
            QSet<QString> freshIds;
            for (const QVariant &item : std::as_const(freshItems)) {
                freshIds.insert(item.toMap().value(QStringLiteral("itemId")).toString());
            }
            for (const QVariant &item : currentItems) {
                const QString itemId = item.toMap().value(QStringLiteral("itemId")).toString();
                if (!freshIds.contains(itemId)) {
                    freshIds.insert(itemId);
                    freshItems.append(item);
                }
            }
            fresh.insert(QStringLiteral("items"), freshItems);
        }
        if (QVariant(fresh) == m_rows.at(i)) {
            continue;
        }
        m_rows[i] = fresh;
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed,
                         {TitleRole, SectionTypeRole, FeaturedRole, ItemsRole, HasMoreRole});
    }
}
