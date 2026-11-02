#include "ShuffleSession.h"

#include "network/LibraryService.h"
#include "player/PlaybackPolicy.h"
#include "utils/BloomLogging.h"

#include <utility>

namespace PlayerPolicy = Bloom::PlaybackPolicy;
namespace Scope = Bloom::ShuffleScope;

namespace {
const QString kRequestPrefix = QStringLiteral("shuffle:");
const QString kSeasonsKeyPrefix = QStringLiteral("shuffle-seasons");
}

ShuffleSession::ShuffleSession(LibraryService *libraryService, QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
{
    if (!m_libraryService) {
        return;
    }
    connect(m_libraryService, &LibraryService::shuffleLoaded,
            this, &ShuffleSession::onShuffleLoaded);
    connect(m_libraryService, &LibraryService::shuffleFailed,
            this, &ShuffleSession::onShuffleFailed);
    connect(m_libraryService, &LibraryService::canonicalItemsLoadedForConnection, this,
            [this](const QString &connectionId, const QString &parentId,
                   const QString &queryKey, const QVariantList &items, int) {
                onSeasonsLoaded(connectionId, parentId, queryKey, items);
            });
    connect(m_libraryService, &LibraryService::canonicalItemsFailedForConnection, this,
            [this](const QString &connectionId, const QString &parentId,
                   const QString &requestKey, const QString &) {
                onSeasonsFailed(connectionId, parentId, requestKey);
            });
}

ShuffleSession::~ShuffleSession() = default;

QString ShuffleSession::currentItemId() const
{
    return m_current.value(QStringLiteral("itemId")).toString();
}

QVariantMap ShuffleSession::next() const
{
    if (m_usesServer) {
        return m_serverNext;
    }
    return m_queue.isEmpty() ? QVariantMap{} : m_queue.constFirst().toMap();
}

bool ShuffleSession::start(const QVariantMap &scopeMap)
{
    stop();
    const Scope::Scope scope = Scope::fromVariantMap(scopeMap);
    // A scope from another connection (a merged space that has not routed
    // to its owner) is never shuffled on this one.
    if (!m_libraryService || !scope.isValid()
        || (!scope.connectionId.isEmpty()
            && scope.connectionId != m_libraryService->getActiveConnectionId())
        || !m_libraryService->supportsShuffle(scope.kind, scope.collectionKind)) {
        qCWarning(lcPlayback) << "Shuffle: the active provider cannot shuffle"
                              << scope.kind << scope.collectionKind << scope.id;
        return false;
    }
    m_scope = scope;
    m_scopeTitle = scope.title;
    m_connectionId = m_libraryService->getActiveConnectionId();
    m_usesServer = m_libraryService->hasServerShuffle();
    qCInfo(lcPlayback) << "Shuffle: starting"
                       << "kind=" << scope.kind << "collectionKind=" << scope.collectionKind
                       << "scopeId=" << scope.id << "server=" << m_usesServer;
    setState(State::Starting);
    if (m_usesServer) {
        m_libraryService->createShuffle(scopeMap, requestKey(QStringLiteral("create")));
    } else {
        requestLocalPage();
    }
    return true;
}

void ShuffleSession::advance()
{
    if (m_skipping) {
        // Move on once the skip answers.
        m_advanceAfterMove = true;
        return;
    }
    if (m_state != State::Ready || !stillOnItsConnection()) {
        return;
    }
    ++m_moveGeneration;
    if (m_usesServer) {
        setState(State::Moving);
        m_libraryService->advanceShuffle(m_shuffleId, currentItemId(),
                                         requestKey(QStringLiteral("advance")));
        return;
    }
    m_refillExhausted = false;
    if (m_queue.isEmpty()) {
        // The refill has not arrived yet; it hands out the next item.
        setState(State::Moving);
        m_current.clear();
        maybeRefillLocal();
        return;
    }
    m_current = m_queue.takeFirst().toMap();
    m_currentPending = true;
    handOutCurrent();
    emit nextChanged(next());
    maybeRefillLocal();
}

void ShuffleSession::skipNext()
{
    if (m_state != State::Ready || !stillOnItsConnection()) {
        return;
    }
    ++m_moveGeneration;
    if (m_usesServer) {
        const QString nextId = m_serverNext.value(QStringLiteral("itemId")).toString();
        if (nextId.isEmpty()) {
            return;
        }
        m_skipping = true;
        setState(State::Moving);
        m_libraryService->skipShuffleItem(m_shuffleId, nextId, requestKey(QStringLiteral("skip")));
        return;
    }
    if (m_queue.isEmpty()) {
        return;
    }
    // A skipped item never played, so it may come up again this cycle.
    const QVariantMap skipped = m_queue.takeFirst().toMap();
    m_handedOut.removeAll(skipped.value(QStringLiteral("itemId")).toString());
    if (m_queue.isEmpty() && !m_refillInFlight) {
        // A scope this small keeps the skipped item rather than none.
        m_queue.append(skipped);
        m_handedOut.append(skipped.value(QStringLiteral("itemId")).toString());
    }
    emit nextChanged(next());
    maybeRefillLocal();
}

void ShuffleSession::refresh()
{
    if (m_state != State::Ready || !stillOnItsConnection()) {
        return;
    }
    if (!m_usesServer) {
        if (m_queue.isEmpty()) {
            m_nextWanted = true;
            m_refillExhausted = false;
            requestLocalPage();
        }
        return;
    }
    if (m_shuffleId.isEmpty()) {
        return;
    }
    m_libraryService->getShuffle(
        m_shuffleId, requestKey(QStringLiteral("refresh:%1").arg(m_moveGeneration)));
}

void ShuffleSession::stop()
{
    if (m_state == State::Idle) {
        return;
    }
    qCInfo(lcPlayback) << "Shuffle: stopping" << "shuffleId=" << m_shuffleId;
    if (m_usesServer && !m_shuffleId.isEmpty() && m_libraryService
        && m_libraryService->getActiveConnectionId() == m_connectionId) {
        m_libraryService->deleteShuffle(m_shuffleId);
    }
    reset();
}

void ShuffleSession::reset()
{
    ++m_generation;
    m_moveGeneration = 0;
    m_usesServer = false;
    m_connectionId.clear();
    m_scope = {};
    m_scopeTitle.clear();
    m_shuffleId.clear();
    m_current.clear();
    m_serverNext.clear();
    m_currentPending = false;
    m_queue.clear();
    m_handedOut.clear();
    m_refillInFlight = false;
    m_staleReads = 0;
    m_refillExhausted = false;
    m_nextWanted = false;
    m_skipping = false;
    m_advanceAfterMove = false;
    m_seasonIds.clear();
    m_awaitingSeasonsFor.clear();
    m_seasonLookupFailedFor.clear();
    setState(State::Idle);
}

void ShuffleSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
    if (m_state == State::Ready && std::exchange(m_advanceAfterMove, false)) {
        advance();
    }
}

QString ShuffleSession::requestKey(const QString &operation) const
{
    return kRequestPrefix + QString::number(m_generation) + QLatin1Char(':') + operation;
}

bool ShuffleSession::acceptsReply(const QString &connectionId, const QString &key) const
{
    return isActive() && key.startsWith(kRequestPrefix + QString::number(m_generation) + QLatin1Char(':'))
        && connectionId == m_connectionId;
}

void ShuffleSession::onShuffleLoaded(const QString &connectionId, const QString &key,
                                     const QVariantMap &result)
{
    if (!acceptsReply(connectionId, key)) {
        return;
    }
    const QString operation = key.section(QLatin1Char(':'), 2);
    if (operation == QStringLiteral("page")) {
        m_refillInFlight = false;
        acceptLocalPage(result.value(QStringLiteral("items")).toList(),
                        result.value(QStringLiteral("total")).toInt());
        return;
    }
    if (operation.startsWith(QStringLiteral("refresh:"))) {
        // A move since the read answers for itself.
        if (m_state != State::Ready
            || operation != QStringLiteral("refresh:%1").arg(m_moveGeneration)
            || result.value(QStringLiteral("current")).toMap().value(QStringLiteral("itemId"))
                   != m_current.value(QStringLiteral("itemId"))) {
            return;
        }
        const QVariantMap next = result.value(QStringLiteral("next")).toMap();
        if (next.value(QStringLiteral("itemId")) != m_serverNext.value(QStringLiteral("itemId"))) {
            m_serverNext = next;
            emit nextChanged(m_serverNext);
        }
        return;
    }
    const bool movesCurrent = operation == QStringLiteral("create")
        || operation == QStringLiteral("advance");
    if (movesCurrent) {
        if (operation == QStringLiteral("create")) {
            m_shuffleId = result.value(QStringLiteral("shuffleId")).toString();
            const QString title = result.value(QStringLiteral("scopeTitle")).toString();
            if (!title.isEmpty()) {
                m_scopeTitle = title;
            }
        }
        applyServerShuffle(result);
        m_currentPending = true;
        setState(State::Ready);
        handOutCurrent();
        emit nextChanged(m_serverNext);
        return;
    }
    if (operation == QStringLiteral("skip")) {
        m_skipping = false;
        m_serverNext = result.value(QStringLiteral("next")).toMap();
        setState(State::Ready);
        emit nextChanged(m_serverNext);
    }
}

void ShuffleSession::onShuffleFailed(const QString &connectionId, const QString &key,
                                     int httpStatus, const QString &message)
{
    if (!acceptsReply(connectionId, key)) {
        return;
    }
    const QString operation = key.section(QLatin1Char(':'), 2);
    qCWarning(lcPlayback) << "Shuffle: request failed" << operation
                          << "status=" << httpStatus << message;
    // 404: the shuffle is gone (expired, or stopped elsewhere).
    const bool gone = httpStatus == 404 && !operation.startsWith(QStringLiteral("create"));
    if (operation == QStringLiteral("page")) {
        m_refillInFlight = false;
        // A failed refill is tried again on the next move; a shuffle with
        // nothing to hand out, or whose Up Next waits on an empty queue,
        // ends.
        if (m_state == State::Starting || m_state == State::Moving
            || (m_nextWanted && m_queue.isEmpty())) {
            finishWithFailure(message.isEmpty() ? tr("Shuffle could not continue.") : message,
                              false);
        }
        return;
    }
    if (operation.startsWith(QStringLiteral("refresh:"))) {
        if (gone) {
            finishWithFailure(tr("This shuffle has ended."), false);
        }
        return;
    }
    if (operation == QStringLiteral("skip") && !gone) {
        // The announced item stays next.
        m_skipping = false;
        setState(State::Ready);
        emit nextChanged(m_serverNext);
        return;
    }
    if (gone) {
        finishWithFailure(tr("This shuffle has ended."), false);
        return;
    }
    if (operation == QStringLiteral("create")) {
        // 409: nothing in the scope can be played; the server words it.
        finishWithFailure(message.isEmpty() ? tr("Shuffle could not start.") : message, false);
        return;
    }
    // Without an HTTP answer (offline, or dropped because the connection,
    // profile or provider changed) nothing is deleted: the server drops
    // abandoned shuffles itself.
    finishWithFailure(message.isEmpty() ? tr("Shuffle could not continue.") : message,
                      httpStatus > 0);
}

void ShuffleSession::applyServerShuffle(const QVariantMap &result)
{
    m_current = result.value(QStringLiteral("current")).toMap();
    m_serverNext = result.value(QStringLiteral("next")).toMap();
}

void ShuffleSession::requestLocalPage()
{
    if (m_refillInFlight || !m_libraryService) {
        return;
    }
    m_refillInFlight = true;
    QVariantMap scope{
        {QStringLiteral("kind"), m_scope.kind},
        {QStringLiteral("id"), m_scope.id},
        {QStringLiteral("collectionKind"), m_scope.collectionKind}};
    m_libraryService->getShuffleItems(scope, PlayerPolicy::kLocalShufflePageSize,
                                      requestKey(QStringLiteral("page")));
}

void ShuffleSession::maybeRefillLocal()
{
    if (!m_usesServer && isActive() && !m_refillExhausted
        && m_queue.size() <= PlayerPolicy::kLocalShuffleRefillBelow) {
        requestLocalPage();
    }
}

void ShuffleSession::acceptLocalPage(const QVariantList &items, int total)
{
    const PlayerPolicy::LocalShuffleRefill plan = PlayerPolicy::planLocalShuffleRefill(
        items, m_handedOut, currentItemId(), total, m_staleReads);
    if (plan.empty && m_current.isEmpty() && m_queue.isEmpty()) {
        finishWithFailure(tr("Nothing here can be played."), false);
        return;
    }
    if (plan.readAgain) {
        ++m_staleReads;
        requestLocalPage();
        return;
    }
    m_staleReads = 0;
    if (plan.newCycle) {
        // Everything has played: a new cycle. What is queued stays queued
        // once, and the item playing now comes again only when nothing else
        // can (a scope of one item repeats it).
        QStringList handedOut;
        if (!currentItemId().isEmpty()) {
            handedOut.append(currentItemId());
        }
        for (const QVariant &queued : std::as_const(m_queue)) {
            handedOut.append(queued.toMap().value(QStringLiteral("itemId")).toString());
        }
        m_handedOut = handedOut;
    }
    qsizetype added = 0;
    for (const QVariant &value : plan.accepted) {
        const QString itemId = value.toMap().value(QStringLiteral("itemId")).toString();
        if (m_handedOut.contains(itemId)) {
            continue;
        }
        m_handedOut.append(itemId);
        m_queue.append(value);
        ++added;
    }
    if (added == 0 && plan.newCycle && m_queue.isEmpty() && !plan.accepted.isEmpty()) {
        m_queue.append(plan.accepted.constFirst());
        ++added;
    }
    // A scope smaller than the queue wants brings nothing new on every
    // read: wait for the next advance instead of reading again.
    m_refillExhausted = added == 0;
    if (m_state == State::Starting || m_state == State::Moving) {
        if (m_queue.isEmpty()) {
            finishWithFailure(tr("Nothing here can be played."), false);
            return;
        }
        m_current = m_queue.takeFirst().toMap();
        m_currentPending = true;
        setState(State::Ready);
        handOutCurrent();
    } else if (m_nextWanted && m_queue.isEmpty()) {
        finishWithFailure(tr("Shuffle could not continue."), false);
        return;
    }
    if (!m_queue.isEmpty()) {
        m_nextWanted = false;
    }
    emit nextChanged(next());
    maybeRefillLocal();
}

void ShuffleSession::handOutCurrent()
{
    if (!m_currentPending || m_current.isEmpty()) {
        return;
    }
    QVariantMap item = m_current;
    const bool episode = item.value(QStringLiteral("mediaType")).toString() == QStringLiteral("Episode");
    const QString seriesId = item.value(QStringLiteral("seriesId")).toString();
    QString seasonId = item.value(QStringLiteral("seasonId")).toString();
    if (episode && seasonId.isEmpty()) {
        if (m_scope.kind == Scope::kSeason) {
            seasonId = m_scope.id;
        } else if (!seriesId.isEmpty()) {
            const auto seasons = m_seasonIds.constFind(seriesId);
            if (seasons != m_seasonIds.cend()) {
                seasonId = seasons->value(item.value(QStringLiteral("parentIndexNumber"), -1).toInt());
            } else if (std::exchange(m_seasonLookupFailedFor, QString()) != seriesId) {
                if (m_awaitingSeasonsFor != seriesId) {
                    m_awaitingSeasonsFor = seriesId;
                    LibraryItemQuery query;
                    query.parentId = seriesId;
                    query.includeHeavyFields = false;
                    query.includeItemTypes = {QStringLiteral("Season")};
                    query.requestKey = kSeasonsKeyPrefix + QLatin1Char('\n')
                        + QString::number(m_generation) + QLatin1Char('\n') + seriesId;
                    m_libraryService->getItems(query);
                }
                return;
            }
        }
        if (!seasonId.isEmpty()) {
            item.insert(QStringLiteral("seasonId"), seasonId);
            item.insert(QStringLiteral("parentId"), seasonId);
        }
    }
    m_current = item;
    m_currentPending = false;
    qCInfo(lcPlayback) << "Shuffle: playing" << "itemId=" << currentItemId()
                       << "mediaType=" << item.value(QStringLiteral("mediaType")).toString()
                       << "seasonId=" << seasonId;
    emit currentReady(item);
}

void ShuffleSession::onSeasonsLoaded(const QString &connectionId, const QString &parentId,
                                     const QString &queryKey, const QVariantList &items)
{
    Q_UNUSED(parentId)
    const QStringList parts = queryKey.split(QLatin1Char('\n'));
    if (parts.size() != 3 || parts.at(0) != kSeasonsKeyPrefix
        || parts.at(1) != QString::number(m_generation) || connectionId != m_connectionId
        || parts.at(2) != m_awaitingSeasonsFor) {
        return;
    }
    QHash<int, QString> seasons;
    for (const QVariant &value : items) {
        const QVariantMap season = value.toMap();
        const int number = season.value(QStringLiteral("indexNumber"), -1).toInt();
        const QString seasonId = season.value(QStringLiteral("itemId")).toString();
        if (number >= 0 && !seasonId.isEmpty()) {
            seasons.insert(number, seasonId);
        }
    }
    m_seasonIds.insert(m_awaitingSeasonsFor, seasons);
    m_awaitingSeasonsFor.clear();
    handOutCurrent();
}

void ShuffleSession::onSeasonsFailed(const QString &connectionId, const QString &parentId,
                                     const QString &key)
{
    Q_UNUSED(parentId)
    const QStringList parts = key.split(QLatin1Char('\n'));
    if (parts.size() != 3 || parts.at(0) != kSeasonsKeyPrefix
        || parts.at(1) != QString::number(m_generation) || connectionId != m_connectionId
        || parts.at(2) != m_awaitingSeasonsFor) {
        return;
    }
    // Played without a season: no season picks apply. The failure is not
    // kept, so the series' next episode asks again.
    m_seasonLookupFailedFor = std::exchange(m_awaitingSeasonsFor, QString());
    handOutCurrent();
}

bool ShuffleSession::stillOnItsConnection()
{
    if (m_libraryService && m_libraryService->getActiveConnectionId() == m_connectionId) {
        return true;
    }
    finishWithFailure(tr("This shuffle has ended."), false);
    return false;
}

void ShuffleSession::finishWithFailure(const QString &message, bool deleteOnServer)
{
    qCWarning(lcPlayback) << "Shuffle: ended" << message;
    if (deleteOnServer) {
        stop();
    } else {
        reset();
    }
    emit failed(message);
}
