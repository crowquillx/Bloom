#include "MergedCatalogService.h"

#include "network/AuthenticationService.h"
#include "network/ConnectionSessionPool.h"
#include "network/HttpTransport.h"
#include "models/ContinueWatching.h"
#include "models/LibraryKinds.h"
#include "network/LibraryService.h"
#include "network/LibrarySearchScope.h"
#include "profiles/BloomProfileRepository.h"
#include "providers/IProviderAdapter.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

#include <QFutureWatcher>
#include <QNetworkReply>
#include <QtConcurrent>
#include <optional>
#include <utility>

namespace {

constexpr auto kStateLoading = "loading";
constexpr auto kStateReady = "ready";
constexpr auto kStateFailed = "failed";
constexpr auto kStateSignInRequired = "signInRequired";

// In-progress movies and episodes read beside Next Up, per member.
constexpr int kResumeItemLimit = 20;

QHash<QByteArray, QByteArray> replyHeaders(const QNetworkReply *reply)
{
    QHash<QByteArray, QByteArray> headers;
    if (!reply) {
        return headers;
    }
    for (const auto &pair : reply->rawHeaderPairs()) {
        headers.insert(pair.first, pair.second);
    }
    return headers;
}

} // namespace

MergedCatalogService::MergedCatalogService(AuthenticationService *authService,
                                           BloomProfileRepository *repository,
                                           ConfigManager *configManager,
                                           QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_repository(repository)
    , m_configManager(configManager)
    , m_policy(std::make_unique<DefaultMergedCatalogPolicy>())
{
    if (m_authService && m_authService->networkManager()) {
        // Private transport over the shared access manager: switching the
        // active session cancels AuthenticationService's transport, which
        // must not cancel reads for the other members.
        m_transport = std::make_unique<HttpTransport>(m_authService->networkManager());
        m_transport->setUrlRedactor([](const QUrl &url) {
            return url.toString(QUrl::RemoveQuery | QUrl::RemoveUserInfo);
        });
    }
    if (m_repository) {
        connect(m_repository, &BloomProfileRepository::profilesChanged,
                this, &MergedCatalogService::rebuildMembers);
        connect(m_repository, &BloomProfileRepository::activeProfileChanged,
                this, &MergedCatalogService::rebuildMembers);
    }
    if (m_configManager) {
        // Connection identity and display names feed member labels.
        connect(m_configManager, &ConfigManager::connectionsChanged,
                this, &MergedCatalogService::rebuildMembers);
    }
    if (m_authService) {
        connect(m_authService->sessionPool(), &ConnectionSessionPool::connectionReady,
                this, [this](const QString &connectionId, bool) {
            const auto waiting = m_waitingForSession.take(connectionId);
            for (const auto &continuation : waiting) {
                continuation();
            }
        });
    }
    rebuildMembers();
}

MergedCatalogService::~MergedCatalogService()
{
    if (m_transport) {
        m_transport->cancelAll();
    }
}

void MergedCatalogService::setPolicy(std::unique_ptr<IMergedCatalogPolicy> policy)
{
    if (policy) {
        m_policy = std::move(policy);
    }
}

QVariantList MergedCatalogService::sources() const
{
    QVariantList result;
    for (const Member &member : m_members) {
        const SourceState state = m_sourceStates.value(member.connectionId);
        result.append(QVariantMap{
            {QStringLiteral("memberId"), member.memberId},
            {QStringLiteral("connectionId"), member.connectionId},
            {QStringLiteral("label"), member.label},
            {QStringLiteral("state"), state.state.isEmpty()
                 ? QString::fromLatin1(kStateLoading) : state.state},
            {QStringLiteral("error"), state.error},
        });
    }
    return result;
}

QString MergedCatalogService::sourceLabel(const QString &connectionId) const
{
    for (const Member &member : m_members) {
        if (member.connectionId == connectionId) {
            return member.label;
        }
    }
    return {};
}

bool MergedCatalogService::isMemberConnection(const QString &connectionId) const
{
    for (const Member &member : m_members) {
        if (member.connectionId == connectionId) {
            return true;
        }
    }
    return false;
}

void MergedCatalogService::rebuildMembers()
{
    QList<Member> next;
    ConfigManager *config = m_configManager;
    const auto profile = m_repository ? m_repository->activeProfile() : std::nullopt;
    if (config && profile.has_value() && profile->mode == BloomProfileMode::Merged) {
        for (const BloomProfileMember &member : profile->members) {
            if (!member.enabled) {
                continue;
            }
            const auto connection = config->getConnection(member.connectionId);
            if (!connection.has_value()) {
                continue;
            }
            next.append({member.memberId,
                         member.connectionId,
                         BloomProfile::memberLabel(member, *connection),
                         member.priority,
                         member.hiddenLibraryIds,
                         member.searchHiddenLibraryIds});
        }
    }

    const auto sameMembers = [](const QList<Member> &a, const QList<Member> &b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (qsizetype index = 0; index < a.size(); ++index) {
            if (a[index].memberId != b[index].memberId
                || a[index].connectionId != b[index].connectionId
                || a[index].label != b[index].label
                || a[index].priority != b[index].priority
                || a[index].hiddenLibraryIds != b[index].hiddenLibraryIds
                || a[index].searchHiddenLibraryIds != b[index].searchHiddenLibraryIds) {
                return false;
            }
        }
        return true;
    };
    if (sameMembers(next, m_members)) {
        return;
    }

    // New epoch: in-flight results belong to the previous membership set.
    ++m_epoch;
    if (m_transport) {
        m_transport->cancelAll();
    }
    m_requests.clear();
    m_waitingForSession.clear();
    m_sourceStates.clear();
    m_views.clear();
    m_viewsLoading = false;
    // A member may now be another server version or account.
    m_peopleSearchScoped.clear();
    m_peopleSearchCapabilityRetry.clear();

    const bool wasActive = isActive();
    m_members = next;
    QStringList connectionIds;
    for (const Member &member : std::as_const(m_members)) {
        connectionIds.append(member.connectionId);
    }
    if (m_authService) {
        m_authService->setMemberConnections(connectionIds);
    }

    emit viewsChanged();
    emit sourcesChanged();
    if (wasActive != isActive() || isActive()) {
        emit activeChanged();
    }
}

void MergedCatalogService::refreshViews()
{
    if (!isActive()) {
        return;
    }
    m_viewsLoading = true;
    emit viewsChanged();
    fanOut(ProviderCatalogOperation::Views, ProviderCatalogQuery{},
           [this](QList<MergedSourceResult> results) {
        // Only movie and series libraries exist in the app, and each source
        // drops the libraries it hides from browsing.
        for (MergedSourceResult &result : results) {
            if (result.succeeded) {
                m_unsupportedLibraryIds.insert(
                    result.connectionId, LibraryKinds::unsupportedLibraryIds(result.items));
            }
            result.items = LibraryKinds::supportedLibraries(result.items);
            for (const Member &member : std::as_const(m_members)) {
                if (member.connectionId != result.connectionId
                    || member.hiddenLibraryIds.isEmpty()) {
                    continue;
                }
                result.items.removeIf([&member](const QVariant &value) {
                    return member.hiddenLibraryIds.contains(
                        value.toMap().value(QStringLiteral("itemId")).toString());
                });
            }
        }
        m_views = m_policy->merge(MergedCatalogOperation::Views, results);
        m_viewsLoading = false;
        emit viewsChanged();
        if (allFailed(results)) {
            emit viewsFailed(combinedError(results));
        }
    });
}

void MergedCatalogService::getNextUp()
{
    if (!isActive()) {
        return;
    }
    const quint64 generation = ++m_nextUpGeneration;
    ProviderCatalogQuery nextUpQuery;
    LibraryService::applyNextUpOptions(nextUpQuery, m_configManager, QDate::currentDate());
    fanOut([this, nextUpQuery](const Member &member, SourceCompletion completion) {
        const IProviderAdapter *adapter = m_authService
            ? m_authService->providerForConnection(member.connectionId) : nullptr;
        if (!adapter || !adapter->supportsCapability(ProviderCapability::ResumeItems)) {
            requestMember(member, ProviderCatalogOperation::NextUp, nextUpQuery, false,
                          std::move(completion));
            return;
        }
        // Jellyfin members add their resume list, so their in-progress
        // movies show too. Without Next Up the member counts as failed; a
        // failed resume list leaves its Next Up alone.
        struct Pending {
            std::optional<MergedSourceResult> nextUp;
            QVariantList resume;
            int outstanding = 2;
        };
        auto pending = std::make_shared<Pending>();
        const auto finish = [pending, completion]() {
            if (--pending->outstanding > 0) {
                return;
            }
            MergedSourceResult result = *pending->nextUp;
            if (result.succeeded) {
                result.items = ContinueWatching::combine(pending->resume, result.items);
            }
            completion(result);
        };
        requestMember(member, ProviderCatalogOperation::NextUp, nextUpQuery, false,
                      [pending, finish](const MergedSourceResult &result) {
            pending->nextUp = result;
            finish();
        });
        // Next Up alone reports the source's state.
        ProviderCatalogQuery resumeQuery;
        resumeQuery.limit = kResumeItemLimit;
        sendMemberRequest(member, ProviderCatalogOperation::ResumeItems, resumeQuery, false, false,
                          [pending, finish, connectionId = member.connectionId](
                              const ProviderCatalogResponse *response,
                              IProviderAdapter *responseAdapter, const QString &) {
            if (response && responseAdapter) {
                pending->resume = responseAdapter->mapMediaItems(response->rawItems, connectionId);
            }
            finish();
        });
    }, [this, generation](const QList<MergedSourceResult> &results) {
        if (generation != m_nextUpGeneration) {
            return;
        }
        if (allFailed(results)) {
            emit nextUpFailed(combinedError(results));
        }
        emit nextUpLoaded(m_policy->merge(MergedCatalogOperation::NextUp, results));
    });
}

void MergedCatalogService::getLatestMedia(const QString &connectionId, const QString &parentId)
{
    for (const Member &member : std::as_const(m_members)) {
        if (member.connectionId != connectionId) {
            continue;
        }
        ProviderCatalogQuery query;
        query.parentId = parentId;
        requestMember(member, ProviderCatalogOperation::LatestMedia, query, false,
                      [this, connectionId, parentId](const MergedSourceResult &result) {
            QVariantList items;
            for (const QVariant &value : result.items) {
                items.append(DefaultMergedCatalogPolicy::withSourceLabel(
                    value.toMap(), result.label));
            }
            emit latestMediaLoaded(connectionId, parentId, items);
        });
        return;
    }
}

void MergedCatalogService::search(const QString &searchTerm, int limit)
{
    const QString term = searchTerm.trimmed();
    if (!isActive()) {
        emit searchResultsFailed(term, tr("No combined space is active."));
        return;
    }
    if (term.isEmpty()) {
        emit searchResultsLoaded(term, {}, {});
        return;
    }
    ProviderCatalogQuery query;
    query.searchTerm = term;
    query.limit = limit;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    fanOut([this, query](const Member &member, SourceCompletion completion) {
        // Until a source's libraries are known, list them first (scoped
        // search) rather than assume none are of an unsupported type.
        if (member.searchHiddenLibraryIds.isEmpty()
            && m_unsupportedLibraryIds.contains(member.connectionId)
            && m_unsupportedLibraryIds.value(member.connectionId).isEmpty()) {
            requestMember(member, ProviderCatalogOperation::Search, query, false,
                          std::move(completion));
        } else {
            requestScopedSearch(member, query, std::move(completion));
        }
    }, [this, term](const QList<MergedSourceResult> &results) {
        if (allFailed(results)) {
            emit searchResultsFailed(term, combinedError(results));
            return;
        }
        QVariantList movies;
        QVariantList series;
        const QVariantList merged = m_policy->merge(MergedCatalogOperation::Search, results);
        for (const QVariant &value : merged) {
            const QString mediaType = value.toMap().value(QStringLiteral("mediaType")).toString();
            if (mediaType == QStringLiteral("Movie")) {
                movies.append(value);
            } else if (mediaType == QStringLiteral("Series")) {
                series.append(value);
            }
        }
        emit searchResultsLoaded(term, movies, series);
    });
}

void MergedCatalogService::searchPeople(const QString &searchTerm, int limit)
{
    const QString term = searchTerm.trimmed();
    if (!isActive()) {
        emit peopleSearchFailed(term, tr("No combined space is active."));
        return;
    }
    if (term.isEmpty()) {
        emit peopleSearchLoaded(term, {});
        return;
    }
    ProviderCatalogQuery query;
    query.searchTerm = term;
    query.limit = limit;
    fanOut([this, query](const Member &member, SourceCompletion completion) {
        requestMemberPeople(member, query, std::move(completion));
    }, [this, term](const QList<MergedSourceResult> &results) {
        if (allFailed(results)) {
            emit peopleSearchFailed(term, combinedError(results));
            return;
        }
        // People are not deduplicated across sources: one name on two
        // servers may be two people, and each page reads its own server.
        emit peopleSearchLoaded(term, m_policy->merge(MergedCatalogOperation::Search, results));
    });
}

void MergedCatalogService::requestMemberPeople(const Member &member,
                                               const ProviderCatalogQuery &query,
                                               SourceCompletion completion)
{
    // People search is auxiliary to title search: it never changes a
    // source's state.
    const auto search = [this, member, query, completion]() {
        sendMemberRequest(member, ProviderCatalogOperation::PersonSearch, query, false, false,
                          [this, member, completion](const ProviderCatalogResponse *response,
                                                     IProviderAdapter *adapter,
                                                     const QString &error) {
            if (!response) {
                completion(failedResult(member, error));
                return;
            }
            MergedSourceResult result;
            result.memberId = member.memberId;
            result.connectionId = member.connectionId;
            result.label = member.label;
            result.priority = member.priority;
            result.succeeded = true;
            result.items = adapter->mapPeople(response->rawItems, member.connectionId);
            completion(result);
        });
    };
    const auto unscoped = [this, member, completion]() {
        MergedSourceResult result = failedResult(member, QString());
        result.succeeded = true;
        completion(result);
    };

    const IProviderAdapter *adapter = m_authService
        ? m_authService->providerForConnection(member.connectionId) : nullptr;
    const ICatalogProvider *catalog = adapter ? adapter->catalogProvider() : nullptr;
    if (!catalog
        || !catalog->createRequest(ProviderCatalogOperation::PersonSearchCapability,
                                   ProviderCatalogQuery{}).supported) {
        search();
        return;
    }
    const auto known = m_peopleSearchScoped.constFind(member.connectionId);
    if (known != m_peopleSearchScoped.constEnd()) {
        if (known.value()) {
            search();
        } else {
            unscoped();
        }
        return;
    }
    const auto retry = m_peopleSearchCapabilityRetry.constFind(member.connectionId);
    if (retry != m_peopleSearchCapabilityRetry.constEnd() && !retry.value().hasExpired()) {
        unscoped();
        return;
    }
    m_peopleSearchCapabilityRetry.remove(member.connectionId);
    // sendMemberRequest drops answers from an older epoch.
    sendMemberRequest(member, ProviderCatalogOperation::PersonSearchCapability,
                      ProviderCatalogQuery{}, false, false,
                      [this, member, completion, search, unscoped](
                          const ProviderCatalogResponse *response, IProviderAdapter *,
                          const QString &error) {
        if (!response) {
            m_peopleSearchCapabilityRetry.insert(
                member.connectionId, QDeadlineTimer(PeopleSearchCapabilityRetryMs));
            completion(failedResult(member, error));
            return;
        }
        const bool scoped =
            response->capabilityMetadata.value(QStringLiteral("peopleMediaScope")).toBool();
        m_peopleSearchScoped.insert(member.connectionId, scoped);
        if (scoped) {
            search();
        } else {
            unscoped();
        }
    });
}

void MergedCatalogService::loadSourceLibraries(const QString &connectionId)
{
    const std::optional<Member> target = readableMember(connectionId);
    if (!target.has_value()) {
        const auto connection = m_configManager
            ? m_configManager->getConnection(connectionId) : std::nullopt;
        emit sourceLibrariesFailed(
            connectionId,
            tr("Sign in to %1 to choose its libraries.")
                .arg(connection.has_value() ? BloomProfile::connectionLabel(*connection)
                                            : connectionId));
        return;
    }
    requestMember(*target, ProviderCatalogOperation::Views, ProviderCatalogQuery{}, false,
                  [this, connectionId](const MergedSourceResult &result) {
        if (result.succeeded) {
            emit sourceLibrariesLoaded(connectionId,
                                       LibraryKinds::supportedLibraries(result.items));
        } else {
            emit sourceLibrariesFailed(connectionId, result.error);
        }
    });
}

void MergedCatalogService::requestScopedSearch(const Member &member,
                                               const ProviderCatalogQuery &query,
                                               SourceCompletion completion)
{
    requestMember(member, ProviderCatalogOperation::Views, ProviderCatalogQuery{}, false,
                  [this, member, query, completion](const MergedSourceResult &views) {
        if (!views.succeeded) {
            completion(views);
            return;
        }
        const QStringList unsupportedIds = LibraryKinds::unsupportedLibraryIds(views.items);
        m_unsupportedLibraryIds.insert(member.connectionId, unsupportedIds);
        if (member.searchHiddenLibraryIds.isEmpty() && unsupportedIds.isEmpty()) {
            // Nothing to search around: one search across the source.
            requestMember(member, ProviderCatalogOperation::Search, query, false, completion);
            return;
        }
        const QStringList libraryIds = LibrarySearchScope::searchableLibraryIds(
            LibraryKinds::supportedLibraries(views.items), member.searchHiddenLibraryIds);
        struct Aggregate {
            MergedSourceResult result;
            QList<QVariantList> perLibrary;
            qsizetype pending = 0;
            bool anySucceeded = false;
        };
        auto aggregate = std::make_shared<Aggregate>();
        aggregate->result = views;
        aggregate->result.items.clear();
        if (libraryIds.isEmpty()) {
            completion(aggregate->result);
            return;
        }
        aggregate->perLibrary.resize(libraryIds.size());
        aggregate->pending = libraryIds.size();
        for (qsizetype index = 0; index < libraryIds.size(); ++index) {
            ProviderCatalogQuery scoped = query;
            scoped.parentId = libraryIds.at(index);
            requestMember(member, ProviderCatalogOperation::Search, scoped, false,
                          [aggregate, index, limit = query.limit, completion](
                              const MergedSourceResult &library) {
                if (library.succeeded) {
                    aggregate->perLibrary[index] = library.items;
                    aggregate->anySucceeded = true;
                } else {
                    aggregate->result.error = library.error;
                }
                if (--aggregate->pending > 0) {
                    return;
                }
                aggregate->result.succeeded = aggregate->anySucceeded;
                aggregate->result.items =
                    LibrarySearchScope::interleave(aggregate->perLibrary, limit);
                completion(aggregate->result);
            });
        }
    });
}

void MergedCatalogService::fanOut(ProviderCatalogOperation operation,
                                  const ProviderCatalogQuery &query,
                                  MergeCompletion completion)
{
    fanOut([this, operation, query](const Member &member, SourceCompletion sourceCompletion) {
        requestMember(member, operation, query, false, std::move(sourceCompletion));
    }, std::move(completion));
}

void MergedCatalogService::fanOut(MemberRequest request, MergeCompletion completion)
{
    if (m_members.isEmpty()) {
        completion({});
        return;
    }

    struct Aggregate {
        QList<MergedSourceResult> results;
        qsizetype pending = 0;
    };
    auto aggregate = std::make_shared<Aggregate>();
    aggregate->pending = m_members.size();
    const quint64 epoch = m_epoch;
    for (const Member &member : std::as_const(m_members)) {
        request(member, [this, aggregate, epoch, completion](const MergedSourceResult &result) {
            if (epoch != m_epoch) {
                return;
            }
            aggregate->results.append(result);
            if (--aggregate->pending == 0) {
                completion(aggregate->results);
            }
        });
    }
}

void MergedCatalogService::requestMember(const Member &member,
                                         ProviderCatalogOperation operation,
                                         ProviderCatalogQuery query,
                                         bool renewalAttempted,
                                         SourceCompletion completion)
{
    sendMemberRequest(member, operation, std::move(query), renewalAttempted, true,
                      [this, member, completion](const ProviderCatalogResponse *response,
                                                 IProviderAdapter *adapter,
                                                 const QString &error) {
        if (!response) {
            completion(failedResult(member, error));
            return;
        }
        MergedSourceResult result;
        result.memberId = member.memberId;
        result.connectionId = member.connectionId;
        result.label = member.label;
        result.priority = member.priority;
        result.succeeded = true;
        result.items = adapter->mapMediaItems(response->rawItems, member.connectionId);
        completion(result);
    });
}

void MergedCatalogService::readConnection(const QString &connectionId,
                                          ProviderCatalogOperation operation,
                                          const ProviderCatalogQuery &query,
                                          ConnectionReadCompletion completion)
{
    const std::optional<Member> target = readableMember(connectionId);
    if (!target.has_value()) {
        completion(std::nullopt);
        return;
    }
    sendMemberRequest(*target, operation, query, false, false,
                      [completion](const ProviderCatalogResponse *response,
                                   IProviderAdapter *, const QString &) {
        completion(response ? std::optional<ProviderCatalogResponse>(*response) : std::nullopt);
    });
}

std::optional<MergedCatalogService::Member>
MergedCatalogService::readableMember(const QString &connectionId) const
{
    for (const Member &member : std::as_const(m_members)) {
        if (member.connectionId == connectionId) {
            return member;
        }
    }
    if (m_authService && m_configManager
        && m_authService->hasSessionForConnection(connectionId)) {
        // A single space reads through LibraryService and has no members
        // here; its live connection still has a session to read with.
        if (const auto connection = m_configManager->getConnection(connectionId)) {
            return Member{{}, connectionId, BloomProfile::connectionLabel(*connection), 0, {}, {}};
        }
    }
    return std::nullopt;
}

void MergedCatalogService::sendMemberRequest(const Member &member,
                                             ProviderCatalogOperation operation,
                                             ProviderCatalogQuery query,
                                             bool renewalAttempted,
                                             bool trackSource,
                                             RawCompletion completion)
{
    const quint64 epoch = m_epoch;
    // Only catalog reads report per-source state; auxiliary reads such as
    // settings never mark a source loading, failed or ready.
    const auto setState = [this, trackSource](const QString &connectionId, const char *state,
                                              const QString &error = QString()) {
        if (trackSource) {
            setSourceState(connectionId, QString::fromLatin1(state), error);
        }
    };
    if (m_sourceStates.value(member.connectionId).state.isEmpty()) {
        setState(member.connectionId, kStateLoading);
    }
    whenSessionReady(member.connectionId,
                     [this, member, operation, query, renewalAttempted, trackSource,
                      setState, completion, epoch]() mutable {
        if (epoch != m_epoch) {
            return;
        }
        const auto connection = m_configManager
            ? m_configManager->getConnection(member.connectionId) : std::nullopt;
        IProviderAdapter *adapter = m_authService
            ? m_authService->providerForConnection(member.connectionId) : nullptr;
        const ICatalogProvider *catalog = adapter ? adapter->catalogProvider() : nullptr;
        if (!connection.has_value() || !catalog || !m_transport
            || !m_authService->hasSessionForConnection(member.connectionId)) {
            const QString error = tr("Sign in to %1 again to include it.").arg(member.label);
            setState(member.connectionId, kStateSignInRequired, error);
            completion(nullptr, nullptr, error);
            return;
        }

        query.userId = connection->accountId;
        const ProviderCatalogRequest request = catalog->createRequest(operation, query);
        if (!request.supported || request.relativeEndpoint.isEmpty()) {
            const QString error = request.unsupportedReason.isEmpty()
                ? tr("%1 does not support this view.").arg(member.label)
                : request.unsupportedReason;
            completion(nullptr, nullptr, error);
            return;
        }

        HttpRequestOptions options;
        options.retryPolicy = RetryPolicy{2, 500, true};
        options.retrySafety = request.retrySafety == ProviderCatalogRetrySafety::Idempotent
            ? RetrySafety::Idempotent : RetrySafety::Never;
        // A rejected member token is reported per source; it never expires
        // the active session.
        options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
        options.attemptTimeoutMs = 15000;

        const QString connectionId = member.connectionId;
        HttpRequestHandle *handle = m_transport->sendWithRetry(
            this,
            request.relativeEndpoint,
            [this, connectionId, request]() -> QNetworkReply * {
                const auto networkRequest =
                    m_authService->createRequestForConnection(connectionId,
                                                              request.relativeEndpoint);
                QNetworkAccessManager *manager = m_authService->networkManager();
                if (!networkRequest.has_value() || !manager) {
                    return nullptr;
                }
                QNetworkRequest prepared = *networkRequest;
                for (auto it = request.extraHeaders.cbegin();
                     it != request.extraHeaders.cend(); ++it) {
                    prepared.setRawHeader(it.key(), it.value());
                }
                switch (request.method) {
                case ProviderHttpMethod::Get:
                    return manager->get(prepared);
                case ProviderHttpMethod::Post:
                    return manager->post(prepared, request.body);
                case ProviderHttpMethod::Put:
                case ProviderHttpMethod::Delete:
                    // Merged reads never mutate; writes route to the owner.
                    return nullptr;
                }
                return nullptr;
            },
            [this, member, operation, adapter, catalog, setState, completion, epoch](
                QNetworkReply *reply) {
                if (epoch != m_epoch) {
                    return;
                }
                const QByteArray body = reply->readAll();
                const auto headers = replyHeaders(reply);
                auto *watcher = new QFutureWatcher<ProviderCatalogResponse>(this);
                connect(watcher, &QFutureWatcher<ProviderCatalogResponse>::finished, this,
                        [this, watcher, member, adapter, setState, completion, epoch]() {
                    const ProviderCatalogResponse response = watcher->result();
                    watcher->deleteLater();
                    if (epoch != m_epoch) {
                        return;
                    }
                    if (!response.valid) {
                        const QString error = response.error.isEmpty()
                            ? tr("%1 returned an invalid response.").arg(member.label)
                            : response.error;
                        setState(member.connectionId, kStateFailed, error);
                        completion(nullptr, nullptr, error);
                        return;
                    }
                    setState(member.connectionId, kStateReady);
                    completion(&response, adapter, QString());
                });
                watcher->setFuture(QtConcurrent::run([catalog, operation, body, headers]() {
                    return catalog->parseResponse(operation, body, headers);
                }));
            },
            [this, member, operation, query, renewalAttempted, trackSource, setState,
             completion, epoch](const NetworkError &error) {
                if (epoch != m_epoch) {
                    return;
                }
                const bool unauthorized = error.httpStatus == 401 || error.httpStatus == 403;
                if (unauthorized && !renewalAttempted) {
                    // Renew this member's token, sharing the attempt with any
                    // other request that hit the same expiry, and retry once.
                    m_authService->renewConnection(
                        member.connectionId,
                        [this, member, operation, query, trackSource, setState, completion,
                         epoch](TokenRenewal outcome) {
                        if (epoch != m_epoch) {
                            return;
                        }
                        if (outcome == TokenRenewal::Renewed) {
                            sendMemberRequest(member, operation, query, true, trackSource,
                                              completion);
                            return;
                        }
                        // Only a refused refresh token needs a sign-in; the
                        // saved tokens stay for a later attempt otherwise.
                        const bool rejected = outcome == TokenRenewal::Rejected;
                        const QString message = rejected
                            ? tr("Sign in to %1 again to include it.").arg(member.label)
                            : tr("%1 is unavailable.").arg(member.label);
                        setState(member.connectionId,
                                 rejected ? kStateSignInRequired : kStateFailed, message);
                        completion(nullptr, nullptr, message);
                    });
                    return;
                }
                // Still refused with freshly renewed tokens.
                const bool rejected = unauthorized;
                const QString message = rejected
                    ? tr("Sign in to %1 again to include it.").arg(member.label)
                    : (error.userMessage.isEmpty()
                           ? tr("%1 is unavailable.").arg(member.label)
                           : error.userMessage);
                setState(member.connectionId,
                         rejected ? kStateSignInRequired : kStateFailed, message);
                qCWarning(lcLibrary) << "Merged read failed for one source:"
                                     << error.endpoint << error.httpStatus;
                completion(nullptr, nullptr, message);
            },
            options);
        m_requests.removeIf([](const QPointer<HttpRequestHandle> &candidate) {
            return candidate.isNull();
        });
        m_requests.append(handle);
    });
}

void MergedCatalogService::whenSessionReady(const QString &connectionId,
                                            std::function<void()> continuation)
{
    if (!m_authService || m_authService->hasSessionForConnection(connectionId)
        || !m_authService->sessionPool()->isLoading(connectionId)) {
        continuation();
        return;
    }
    m_waitingForSession[connectionId].append(std::move(continuation));
}

void MergedCatalogService::setSourceState(const QString &connectionId,
                                          const QString &state,
                                          const QString &error)
{
    // Library listings for a single space's live connection have no source.
    if (!isMemberConnection(connectionId)) {
        return;
    }
    SourceState &current = m_sourceStates[connectionId];
    if (current.state == state && current.error == error) {
        return;
    }
    current.state = state;
    current.error = error;
    emit sourcesChanged();
}

MergedSourceResult MergedCatalogService::failedResult(const Member &member,
                                                      const QString &error) const
{
    MergedSourceResult result;
    result.memberId = member.memberId;
    result.connectionId = member.connectionId;
    result.label = member.label;
    result.priority = member.priority;
    result.succeeded = false;
    result.error = error;
    return result;
}

QString MergedCatalogService::combinedError(const QList<MergedSourceResult> &results)
{
    QStringList errors;
    for (const MergedSourceResult &result : results) {
        if (!result.succeeded && !result.error.isEmpty()) {
            errors.append(result.error);
        }
    }
    errors.removeDuplicates();
    return errors.join(QLatin1Char('\n'));
}

bool MergedCatalogService::allFailed(const QList<MergedSourceResult> &results)
{
    if (results.isEmpty()) {
        return true;
    }
    for (const MergedSourceResult &result : results) {
        if (result.succeeded) {
            return false;
        }
    }
    return true;
}
