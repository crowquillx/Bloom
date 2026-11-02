#include "TasteSeedViewModels.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"
#include "../utils/ConfigManager.h"

namespace {

// Distinguishes the requests of every taste seed view model: LibraryService
// answers all of them on the same signals.
QString nextRequestKey(const QString &kind)
{
    static quint64 counter = 0;
    return QStringLiteral("taste-seed-%1-%2").arg(kind).arg(++counter);
}

bool isTitleCard(const QVariantMap &item)
{
    const QString mediaType = item.value(QStringLiteral("mediaType")).toString();
    return mediaType == QStringLiteral("Movie") || mediaType == QStringLiteral("Series");
}

} // namespace

// ============================================================================
// TasteSeedPromptViewModel
// ============================================================================

TasteSeedPromptViewModel::TasteSeedPromptViewModel(QObject *parent)
    : TasteSeedPromptViewModel(ServiceLocator::tryGet<LibraryService>(),
                               ServiceLocator::tryGet<ConfigManager>(), parent)
{
}

TasteSeedPromptViewModel::TasteSeedPromptViewModel(LibraryService *libraryService,
                                                   ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
    , m_config(config)
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "TasteSeedPromptViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::tasteSeedStateLoaded,
            this, &TasteSeedPromptViewModel::onStateLoaded);
    connect(m_libraryService, &LibraryService::tasteSeedStateFailed,
            this, &TasteSeedPromptViewModel::onStateFailed);
    connect(m_libraryService, &LibraryService::favoriteStatusChanged,
            this, &TasteSeedPromptViewModel::onFavoriteStatusChanged);
    connect(m_libraryService, &LibraryService::tasteSeedSubmitted,
            this, &TasteSeedPromptViewModel::onTasteSeedSubmitted);
    if (m_config) {
        connect(m_config, &ConfigManager::tasteSeedDoneChanged,
                this, &TasteSeedPromptViewModel::update);
    }
}

void TasteSeedPromptViewModel::setMerged(bool merged)
{
    if (m_merged == merged) {
        return;
    }
    m_merged = merged;
    emit mergedChanged();
    update();
}

QString TasteSeedPromptViewModel::liveScope() const
{
    return m_libraryService ? m_libraryService->tasteSeedScope() : QString();
}

void TasteSeedPromptViewModel::check(bool force)
{
    const QString scope = liveScope();
    if (scope.isEmpty() || m_merged || (m_config && m_config->isTasteSeedDone(scope))) {
        update();
        return;
    }
    const bool known = scope == m_scope;
    const bool reading = !m_requestKey.isEmpty() && m_requestScope == scope;
    if (!force && (known || reading)) {
        update();
        return;
    }
    m_requestKey = nextRequestKey(QStringLiteral("profile"));
    m_requestScope = scope;
    update();
    // May answer synchronously.
    m_libraryService->getTasteSeedState(m_requestKey);
}

void TasteSeedPromptViewModel::dismiss()
{
    const QString scope = liveScope();
    if (m_config && !scope.isEmpty()) {
        m_config->setTasteSeedDone(scope, true);
    }
    update();
}

void TasteSeedPromptViewModel::onStateLoaded(const QString &connectionId,
                                             const QString &requestKey, bool cold)
{
    Q_UNUSED(connectionId)
    if (requestKey.isEmpty() || requestKey != m_requestKey) {
        return;
    }
    m_requestKey.clear();
    // The session moved on while the summary was read.
    if (m_requestScope != liveScope()) {
        update();
        return;
    }
    m_scope = m_requestScope;
    m_cold = cold;
    // A warm profile stays warm: later sessions skip the read.
    if (!cold && m_config) {
        m_config->setTasteSeedDone(m_scope, true);
    }
    update();
}

void TasteSeedPromptViewModel::onStateFailed(const QString &connectionId,
                                             const QString &requestKey, const QString &error)
{
    Q_UNUSED(connectionId)
    if (requestKey.isEmpty() || requestKey != m_requestKey) {
        return;
    }
    m_requestKey.clear();
    qCDebug(lcViewModels) << "TasteSeedPromptViewModel: taste profile read failed:" << error;
    update();
}

void TasteSeedPromptViewModel::onFavoriteStatusChanged(const QString &itemId, bool isFavorite)
{
    Q_UNUSED(itemId)
    // The live profile has a favorite now; the next session's read
    // remembers it as warm.
    if (isFavorite && !m_scope.isEmpty() && m_scope == liveScope()) {
        m_cold = false;
        update();
    }
}

void TasteSeedPromptViewModel::onTasteSeedSubmitted(const QString &connectionId)
{
    // The picker may be gone before the answer (Back while saving); the
    // seed still finishes taste seeding for the live scope.
    const QString scope = liveScope();
    if (m_config && !scope.isEmpty()
        && scope.section(QLatin1Char('\n'), 0, 0) == connectionId) {
        m_config->setTasteSeedDone(scope, true);
    }
    update();
}

void TasteSeedPromptViewModel::update()
{
    const QString scope = liveScope();
    const bool shown = !m_merged && !scope.isEmpty() && scope == m_scope && m_cold
        && !(m_config && m_config->isTasteSeedDone(scope));
    if (shown != m_shown) {
        m_shown = shown;
        emit shownChanged();
    }
}

// ============================================================================
// TasteSeedViewModel
// ============================================================================

TasteSeedViewModel::TasteSeedViewModel(QObject *parent)
    : TasteSeedViewModel(ServiceLocator::tryGet<LibraryService>(),
                         ServiceLocator::tryGet<ConfigManager>(), parent)
{
}

TasteSeedViewModel::TasteSeedViewModel(LibraryService *libraryService, ConfigManager *config,
                                       QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
    , m_config(config)
{
    connectService();
}

void TasteSeedViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "TasteSeedViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalTasteSeedItemsLoaded,
            this, &TasteSeedViewModel::onItemsLoaded);
    connect(m_libraryService, &LibraryService::canonicalTasteSeedItemsFailed,
            this, &TasteSeedViewModel::onItemsFailed);
    connect(m_libraryService, &LibraryService::homeCardWriteFinished,
            this, &TasteSeedViewModel::onWriteFinished);
}

int TasteSeedViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant TasteSeedViewModel::data(const QModelIndex &index, int role) const
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
    case NameRole:
        return item.value(QStringLiteral("name"));
    case SelectedRole:
        return m_selected.contains(item.value(QStringLiteral("itemId")).toString());
    case FavoriteRole:
        return m_favorites.contains(item.value(QStringLiteral("itemId")).toString());
    default:
        return {};
    }
}

QHash<int, QByteArray> TasteSeedViewModel::roleNames() const
{
    return {
        {ItemDataRole, "itemData"},
        {ItemIdRole, "itemId"},
        {NameRole, "name"},
        {SelectedRole, "selected"},
        {FavoriteRole, "favorite"}
    };
}

bool TasteSeedViewModel::canSubmit() const
{
    return !m_submitting && selectedCount() >= MinimumPicks;
}

bool TasteSeedViewModel::isFavoriteItem(const QVariantMap &item)
{
    return item.value(QStringLiteral("userState")).toMap()
        .value(QStringLiteral("favorite")).toBool();
}

void TasteSeedViewModel::load()
{
    reload();
}

void TasteSeedViewModel::reload()
{
    // Every load is new: answers to an older one no longer match.
    m_pageRequestKey.clear();
    m_submitRequestKey.clear();
    m_nextCursor.clear();
    m_emptyPages = 0;
    if (!m_items.isEmpty()) {
        emitModelReset([this]() { m_items.clear(); });
        emit countChanged();
    }
    m_itemIds.clear();
    const bool hadSelection = !m_selected.isEmpty();
    m_selected.clear();
    m_favorites.clear();
    if (hadSelection) {
        emit selectionChanged();
    }
    setHasMore(false);
    setLoadingMore(false);
    setSubmitting(false);
    clearError();

    m_connectionId = m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    m_available = m_libraryService && !m_connectionId.isEmpty()
        && m_libraryService->supportsTasteSeed();
    m_scope = m_available ? m_libraryService->tasteSeedScope() : QString();
    emit connectionChanged();

    if (!m_available) {
        m_firstPagePending = false;
        setLoading(false);
        return;
    }
    m_firstPagePending = true;
    setLoading(true);
    requestPage();
}

void TasteSeedViewModel::loadMore()
{
    if (!m_available || !m_hasMore || m_firstPagePending || m_loadingMore
        || !m_pageRequestKey.isEmpty()) {
        return;
    }
    setLoadingMore(true);
    m_emptyPages = 0;
    requestPage();
}

void TasteSeedViewModel::requestPage()
{
    m_pageRequestKey = nextRequestKey(QStringLiteral("page"));
    // May answer synchronously.
    m_libraryService->getTasteSeedItems(m_nextCursor, PageSize, m_pageRequestKey);
}

QVariantMap TasteSeedViewModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).toMap();
}

bool TasteSeedViewModel::toggle(int row)
{
    if (m_submitting || row < 0 || row >= m_items.size()) {
        return false;
    }
    const QString itemId = m_items.at(row).toMap().value(QStringLiteral("itemId")).toString();
    // Unpicking a favorite would not remove it on the server.
    if (m_favorites.contains(itemId)) {
        return false;
    }
    if (m_selected.contains(itemId)) {
        m_selected.remove(itemId);
    } else {
        if (newPicks().size() >= MaximumPicks) {
            return false;
        }
        m_selected.insert(itemId);
    }
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {SelectedRole});
    emit selectionChanged();
    return true;
}

bool TasteSeedViewModel::favoriteAt(int row) const
{
    return row >= 0 && row < m_items.size()
        && m_favorites.contains(m_items.at(row).toMap().value(QStringLiteral("itemId")).toString());
}

QStringList TasteSeedViewModel::newPicks() const
{
    // In the order the titles are shown.
    QStringList picks;
    for (const QVariant &value : m_items) {
        const QString itemId = value.toMap().value(QStringLiteral("itemId")).toString();
        if (m_selected.contains(itemId) && !m_favorites.contains(itemId)) {
            picks.append(itemId);
        }
    }
    return picks;
}

bool TasteSeedViewModel::submit()
{
    if (!m_available || !canSubmit() || !m_libraryService) {
        return false;
    }
    // The picks belong to the session they were made in.
    if (m_libraryService->getActiveConnectionId() != m_connectionId
        || m_libraryService->tasteSeedScope() != m_scope) {
        reload();
        return false;
    }
    const QStringList picks = newPicks();
    if (picks.isEmpty()) {
        // Only titles the profile favorited already: nothing to send.
        finish(0);
        return true;
    }
    m_submitRequestKey = nextRequestKey(QStringLiteral("submit"));
    m_submitCount = static_cast<int>(picks.size());
    setSubmitting(true);
    // Answers exactly once, possibly synchronously.
    m_libraryService->submitTasteSeed(picks, m_submitRequestKey);
    return true;
}

void TasteSeedViewModel::skip()
{
    if (m_config && !m_scope.isEmpty()) {
        m_config->setTasteSeedDone(m_scope, true);
    }
}

void TasteSeedViewModel::finish(int added)
{
    if (m_config && !m_scope.isEmpty()) {
        m_config->setTasteSeedDone(m_scope, true);
    }
    // The picks are favorites now.
    for (const QString &itemId : std::as_const(m_selected)) {
        m_favorites.insert(itemId);
    }
    emit submitted(added);
}

void TasteSeedViewModel::onItemsLoaded(const QString &connectionId, const QString &requestKey,
                                       const QVariantList &items, bool hasMore,
                                       const QString &nextCursor)
{
    if (requestKey.isEmpty() || requestKey != m_pageRequestKey
        || connectionId != m_connectionId) {
        return;
    }
    m_pageRequestKey.clear();
    m_nextCursor = nextCursor;
    const bool more = hasMore && !nextCursor.isEmpty();

    QVariantList added;
    bool selectionGrew = false;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString itemId = item.value(QStringLiteral("itemId")).toString();
        if (!isTitleCard(item) || itemId.isEmpty() || m_itemIds.contains(itemId)) {
            continue;
        }
        m_itemIds.insert(itemId);
        if (isFavoriteItem(item)) {
            m_favorites.insert(itemId);
            m_selected.insert(itemId);
            selectionGrew = true;
        }
        added.append(item);
    }
    if (!added.isEmpty()) {
        const int first = count();
        beginInsertRows(QModelIndex(), first, first + static_cast<int>(added.size()) - 1);
        m_items.append(added);
        endInsertRows();
        emit countChanged();
    }
    if (selectionGrew) {
        emit selectionChanged();
    }

    // A page that added nothing reads on, up to MaxEmptyPages in a row.
    if (added.isEmpty() && more && ++m_emptyPages < MaxEmptyPages) {
        requestPage();
        return;
    }
    m_emptyPages = 0;
    setHasMore(more);
    if (m_firstPagePending) {
        m_firstPagePending = false;
        setLoading(false);
    }
    setLoadingMore(false);
}

void TasteSeedViewModel::onItemsFailed(const QString &connectionId, const QString &requestKey,
                                       const QString &error, int httpStatus)
{
    Q_UNUSED(httpStatus)
    if (requestKey.isEmpty() || requestKey != m_pageRequestKey
        || connectionId != m_connectionId) {
        return;
    }
    m_pageRequestKey.clear();
    m_emptyPages = 0;
    qCWarning(lcViewModels) << "TasteSeedViewModel: taste seed page failed:" << error;
    const QString message = mapNetworkError(QStringLiteral("listTasteSeedItems"), error);
    if (m_firstPagePending && m_items.isEmpty()) {
        m_firstPagePending = false;
        setHasMore(false);
        setError(message);
        setLoading(false);
        return;
    }
    // The cards shown stay; loadMore() tries the same page again.
    if (m_firstPagePending) {
        m_firstPagePending = false;
        setLoading(false);
    }
    setLoadingMore(false);
    emit pageFailed(message);
}

void TasteSeedViewModel::onWriteFinished(const QString &connectionId, const QString &requestKey,
                                         bool success, const QString &error)
{
    Q_UNUSED(connectionId)
    if (requestKey.isEmpty() || requestKey != m_submitRequestKey) {
        return;
    }
    m_submitRequestKey.clear();
    setSubmitting(false);
    if (success) {
        finish(m_submitCount);
        return;
    }
    // A session change answers here too: the picks may or may not have
    // landed, and the viewer can send them again from the new session.
    emit submitFailed(error.isEmpty() ? tr("Couldn't save your picks.") : error);
}

void TasteSeedViewModel::setHasMore(bool hasMore)
{
    if (m_hasMore != hasMore) {
        m_hasMore = hasMore;
        emit hasMoreChanged();
    }
}

void TasteSeedViewModel::setLoadingMore(bool loadingMore)
{
    if (m_loadingMore != loadingMore) {
        m_loadingMore = loadingMore;
        emit loadingMoreChanged();
    }
}

void TasteSeedViewModel::setSubmitting(bool submitting)
{
    if (m_submitting != submitting) {
        m_submitting = submitting;
        emit submittingChanged();
        emit selectionChanged();
    }
}
