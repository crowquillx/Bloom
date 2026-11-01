#include "PersonViewModel.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

namespace {

QString mediaTypeOf(const QVariant &item)
{
    return item.toMap().value(QStringLiteral("mediaType")).toString();
}

QVariantList itemsOfType(const QVariantList &items, const QString &mediaType)
{
    QVariantList result;
    for (const QVariant &item : items) {
        if (mediaTypeOf(item) == mediaType) {
            result.append(item);
        }
    }
    return result;
}

} // namespace

PersonViewModel::PersonViewModel(QObject *parent)
    : PersonViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

PersonViewModel::PersonViewModel(LibraryService *libraryService, QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    connectService();
}

void PersonViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "PersonViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalPersonLoaded,
            this, &PersonViewModel::onPersonLoaded);
    connect(m_libraryService, &LibraryService::canonicalPersonFailed,
            this, &PersonViewModel::onPersonFailed);
    connect(m_libraryService, &LibraryService::canonicalPersonItemsLoaded,
            this, &PersonViewModel::onPersonItemsLoaded);
    connect(m_libraryService, &LibraryService::canonicalPersonItemsFailed,
            this, &PersonViewModel::onPersonItemsFailed);
}

int PersonViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant PersonViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const QVariantMap item = m_items.at(index.row()).toMap();
    switch (role) {
    case ItemDataRole:
        return item;
    case ItemIdRole:
        return item.value(QStringLiteral("itemId"));
    case MediaTypeRole:
        return item.value(QStringLiteral("mediaType"));
    case NameRole:
        return item.value(QStringLiteral("name"));
    default:
        return {};
    }
}

QHash<int, QByteArray> PersonViewModel::roleNames() const
{
    return {
        {ItemDataRole, "itemData"},
        {ItemIdRole, "itemId"},
        {MediaTypeRole, "mediaType"},
        {NameRole, "name"}
    };
}

QVariantList PersonViewModel::movies() const
{
    return itemsOfType(m_items, QStringLiteral("Movie"));
}

QVariantList PersonViewModel::series() const
{
    return itemsOfType(m_items, QStringLiteral("Series"));
}

void PersonViewModel::load(const QVariantMap &person)
{
    m_seed = person;
    // A cast credit's role and kind describe one title, not the person.
    m_seed.remove(QStringLiteral("role"));
    m_seed.remove(QStringLiteral("kind"));
    m_seed.remove(QStringLiteral("subtitle"));
    m_seed.remove(QStringLiteral("sourceLabel"));
    reload();
}

void PersonViewModel::reload()
{
    // Every load is new: answers to an older one no longer match.
    ++m_requestSerial;
    m_pageRequestKey.clear();
    m_personId = m_seed.value(QStringLiteral("personId")).toString();
    const QString seedConnectionId = m_seed.value(QStringLiteral("connectionId")).toString();
    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    m_person = m_seed;
    m_person.insert(QStringLiteral("connectionId"), m_connectionId);
    emit personChanged();

    if (!m_items.isEmpty()) {
        emitModelReset([this]() { m_items.clear(); });
    }
    m_itemIds.clear();
    m_hasMore = false;
    m_nextStartIndex = 0;
    m_nextCursor.clear();
    emit filmographyChanged();
    setLoadingMore(false);
    clearError();

    m_detailsPending = false;
    m_firstPagePending = false;
    if (!m_libraryService || m_personId.isEmpty()) {
        updateLoading();
        setError(tr("This person could not be found."));
        return;
    }
    // The page reads the live connection, which must own the person
    // (merged spaces route to it first).
    if (!seedConnectionId.isEmpty() && seedConnectionId != m_connectionId) {
        updateLoading();
        setError(tr("This person belongs to a source that is not signed in."));
        return;
    }

    m_detailsPending = true;
    m_firstPagePending = true;
    updateLoading();
    m_libraryService->getPerson(m_personId);
    requestPage();
}

void PersonViewModel::loadMore()
{
    if (!m_libraryService || !m_hasMore || m_firstPagePending || m_loadingMore
        || !m_pageRequestKey.isEmpty()) {
        return;
    }
    setLoadingMore(true);
    requestPage();
}

void PersonViewModel::requestPage()
{
    m_pageRequestKey = QStringLiteral("person:%1").arg(++m_requestSerial);
    // May answer synchronously.
    m_libraryService->getPersonItems(m_personId, m_nextStartIndex, m_nextCursor,
                                     PageSize, m_pageRequestKey);
}

bool PersonViewModel::isCurrent(const QString &connectionId, const QString &personId) const
{
    return !m_personId.isEmpty() && personId == m_personId
        && connectionId == m_connectionId;
}

void PersonViewModel::onPersonLoaded(const QString &connectionId, const QString &personId,
                                     const QVariantMap &person)
{
    if (!m_detailsPending || !isCurrent(connectionId, personId)) {
        return;
    }
    m_detailsPending = false;
    // The read wins over the seed, but a seed photo stays when the read
    // has none.
    QVariantMap merged = m_seed;
    for (auto it = person.cbegin(); it != person.cend(); ++it) {
        merged.insert(it.key(), it.value());
    }
    merged.insert(QStringLiteral("connectionId"), m_connectionId);
    if (merged != m_person) {
        m_person = merged;
        emit personChanged();
    }
    updateLoading();
}

void PersonViewModel::onPersonFailed(const QString &connectionId, const QString &personId,
                                     const QString &error)
{
    if (!m_detailsPending || !isCurrent(connectionId, personId)) {
        return;
    }
    m_detailsPending = false;
    // The seed still names the person; the filmography carries the page.
    qCWarning(lcViewModels) << "PersonViewModel: person details failed:" << error;
    updateLoading();
}

void PersonViewModel::onPersonItemsLoaded(const QString &connectionId,
                                          const QString &personId,
                                          const QString &requestKey,
                                          const QVariantList &items,
                                          int totalRecordCount, bool hasMore,
                                          const QString &nextCursor)
{
    Q_UNUSED(totalRecordCount)
    if (requestKey.isEmpty() || requestKey != m_pageRequestKey
        || !isCurrent(connectionId, personId)) {
        return;
    }
    m_pageRequestKey.clear();
    m_firstPagePending = false;
    setLoadingMore(false);

    // Paging counts what the server sent, before filtering.
    m_nextStartIndex += static_cast<int>(items.size());
    m_nextCursor = nextCursor;
    // An empty page cannot advance; stop instead of asking again.
    m_hasMore = hasMore && !items.isEmpty();

    QVariantList added;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
        if (mediaType != QStringLiteral("Movie") && mediaType != QStringLiteral("Series")) {
            continue;
        }
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        if (itemId.isEmpty() || m_itemIds.contains(itemId)) {
            continue;
        }
        m_itemIds.insert(itemId);
        added.append(item);
    }
    if (!added.isEmpty()) {
        const int first = static_cast<int>(m_items.size());
        beginInsertRows(QModelIndex(), first, first + static_cast<int>(added.size()) - 1);
        m_items.append(added);
        endInsertRows();
    }
    emit filmographyChanged();
    updateLoading();
}

void PersonViewModel::onPersonItemsFailed(const QString &connectionId,
                                          const QString &personId,
                                          const QString &requestKey,
                                          const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_pageRequestKey
        || !isCurrent(connectionId, personId)) {
        return;
    }
    m_pageRequestKey.clear();
    if (m_firstPagePending) {
        m_firstPagePending = false;
        setError(mapNetworkError(QStringLiteral("getPersonItems"), error));
    } else {
        // Titles already shown stay; the next loadMore() tries again.
        qCWarning(lcViewModels) << "PersonViewModel: filmography page failed:" << error;
    }
    setLoadingMore(false);
    updateLoading();
}

void PersonViewModel::updateLoading()
{
    setLoading(m_detailsPending || m_firstPagePending);
}

void PersonViewModel::setLoadingMore(bool loadingMore)
{
    if (m_loadingMore == loadingMore) {
        return;
    }
    m_loadingMore = loadingMore;
    emit loadingMoreChanged();
}
