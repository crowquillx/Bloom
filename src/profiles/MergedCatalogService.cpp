#include "MergedCatalogService.h"

#include "network/AuthenticationService.h"
#include "network/ConnectionSessionPool.h"
#include "network/HttpTransport.h"
#include "profiles/BloomProfileRepository.h"
#include "providers/IProviderAdapter.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

#include <QFutureWatcher>
#include <QNetworkReply>
#include <QtConcurrent>
#include <utility>

namespace {

constexpr auto kStateLoading = "loading";
constexpr auto kStateReady = "ready";
constexpr auto kStateFailed = "failed";
constexpr auto kStateSignInRequired = "signInRequired";

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
                         member.priority});
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
                || a[index].priority != b[index].priority) {
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
           [this](const QList<MergedSourceResult> &results) {
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
    fanOut(ProviderCatalogOperation::NextUp, ProviderCatalogQuery{},
           [this](const QList<MergedSourceResult> &results) {
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
        emit searchResultsFailed(term, tr("No merged workspace is active."));
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
    fanOut(ProviderCatalogOperation::Search, query,
           [this, term](const QList<MergedSourceResult> &results) {
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

void MergedCatalogService::fanOut(ProviderCatalogOperation operation,
                                  const ProviderCatalogQuery &query,
                                  MergeCompletion completion)
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
        requestMember(member, operation, query, false,
                      [this, aggregate, epoch, completion](const MergedSourceResult &result) {
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
                                         bool reloadedCredentials,
                                         SourceCompletion completion)
{
    const quint64 epoch = m_epoch;
    if (m_sourceStates.value(member.connectionId).state.isEmpty()) {
        setSourceState(member.connectionId, QString::fromLatin1(kStateLoading));
    }
    whenSessionReady(member.connectionId,
                     [this, member, operation, query, reloadedCredentials,
                      completion, epoch]() mutable {
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
            setSourceState(member.connectionId, QString::fromLatin1(kStateSignInRequired), error);
            completion(failedResult(member, error));
            return;
        }

        query.userId = connection->accountId;
        const ProviderCatalogRequest request = catalog->createRequest(operation, query);
        if (!request.supported || request.relativeEndpoint.isEmpty()) {
            const QString error = request.unsupportedReason.isEmpty()
                ? tr("%1 does not support this view.").arg(member.label)
                : request.unsupportedReason;
            completion(failedResult(member, error));
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
            [this, member, operation, adapter, catalog, completion, epoch](
                QNetworkReply *reply) {
                if (epoch != m_epoch) {
                    return;
                }
                const QByteArray body = reply->readAll();
                const auto headers = replyHeaders(reply);
                auto *watcher = new QFutureWatcher<ProviderCatalogResponse>(this);
                connect(watcher, &QFutureWatcher<ProviderCatalogResponse>::finished, this,
                        [this, watcher, member, adapter, completion, epoch]() {
                    const ProviderCatalogResponse response = watcher->result();
                    watcher->deleteLater();
                    if (epoch != m_epoch) {
                        return;
                    }
                    if (!response.valid) {
                        const QString error = response.error.isEmpty()
                            ? tr("%1 returned an invalid response.").arg(member.label)
                            : response.error;
                        setSourceState(member.connectionId,
                                       QString::fromLatin1(kStateFailed), error);
                        completion(failedResult(member, error));
                        return;
                    }
                    MergedSourceResult result;
                    result.memberId = member.memberId;
                    result.connectionId = member.connectionId;
                    result.label = member.label;
                    result.priority = member.priority;
                    result.succeeded = true;
                    result.items = adapter->mapMediaItems(response.rawItems,
                                                          member.connectionId);
                    setSourceState(member.connectionId, QString::fromLatin1(kStateReady));
                    completion(result);
                });
                watcher->setFuture(QtConcurrent::run([catalog, operation, body, headers]() {
                    return catalog->parseResponse(operation, body, headers);
                }));
            },
            [this, member, operation, query, reloadedCredentials, completion, epoch](
                const NetworkError &error) {
                if (epoch != m_epoch) {
                    return;
                }
                const bool rejected = error.httpStatus == 401 || error.httpStatus == 403;
                const bool pooled = member.connectionId != m_authService->activeConnectionId();
                if (rejected && pooled && !reloadedCredentials) {
                    // The active session may have rotated this member's
                    // tokens in storage since they were pooled.
                    m_authService->sessionPool()->reload(member.connectionId);
                    requestMember(member, operation, query, true, completion);
                    return;
                }
                const QString message = rejected
                    ? tr("Sign in to %1 again to include it.").arg(member.label)
                    : (error.userMessage.isEmpty()
                           ? tr("%1 is unavailable.").arg(member.label)
                           : error.userMessage);
                setSourceState(member.connectionId,
                               QString::fromLatin1(rejected ? kStateSignInRequired
                                                            : kStateFailed),
                               message);
                qCWarning(lcLibrary) << "Merged read failed for one source:"
                                     << error.endpoint << error.httpStatus;
                completion(failedResult(member, message));
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
