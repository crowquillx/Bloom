#pragma once

#include "profiles/MergedCatalogPolicy.h"
#include "providers/ICatalogProvider.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>

class AuthenticationService;
class BloomProfileRepository;
class ConfigManager;
class HttpRequestHandle;
class HttpTransport;
class IProviderAdapter;
struct NetworkError;

/**
 * @brief Fans read operations out across the enabled members of a merged
 * Bloom profile and combines them through an IMergedCatalogPolicy.
 *
 * Every item keeps its owning connectionId, so details, artwork, playback and
 * writes route to the member that returned it. One failing member degrades
 * only its own source state. Requests use a private HttpTransport, so active
 * session switches (which cancel the shared transport) never cancel merged
 * reads; a profile or membership change bumps the epoch and drops stale
 * results. Single profiles leave this service inactive and keep using
 * LibraryService directly.
 */
class MergedCatalogService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantList views READ views NOTIFY viewsChanged)
    Q_PROPERTY(bool viewsLoading READ viewsLoading NOTIFY viewsChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)

public:
    MergedCatalogService(AuthenticationService *authService,
                         BloomProfileRepository *repository,
                         ConfigManager *configManager,
                         QObject *parent = nullptr);
    ~MergedCatalogService() override;

    void setPolicy(std::unique_ptr<IMergedCatalogPolicy> policy);

    bool isActive() const { return !m_members.isEmpty(); }
    QVariantList views() const { return m_views; }
    bool viewsLoading() const { return m_viewsLoading; }
    QVariantList sources() const;
    quint64 epoch() const { return m_epoch; }

    Q_INVOKABLE void refreshViews();
    Q_INVOKABLE void getNextUp();
    Q_INVOKABLE void getLatestMedia(const QString &connectionId, const QString &parentId);
    Q_INVOKABLE void search(const QString &searchTerm, int limit = 50);
    /// Every library of a connection, including hidden ones, for choosing
    /// what a source shows. Works for the live connection and the enabled
    /// members of the active space (single or merged).
    Q_INVOKABLE void loadSourceLibraries(const QString &connectionId);
    Q_INVOKABLE QString sourceLabel(const QString &connectionId) const;
    Q_INVOKABLE bool isMemberConnection(const QString &connectionId) const;

    using ConnectionReadCompletion =
        std::function<void(const std::optional<ProviderCatalogResponse> &response)>;
    /// One raw read on the live connection or an enabled member of the active
    /// space, with the same token renewal as merged reads but no source state.
    /// Completes with nullopt on failure; a space or membership change drops
    /// it without completing.
    void readConnection(const QString &connectionId,
                        ProviderCatalogOperation operation,
                        const ProviderCatalogQuery &query,
                        ConnectionReadCompletion completion);

signals:
    void activeChanged();
    void viewsChanged();
    void sourcesChanged();
    void viewsFailed(const QString &error);
    void nextUpLoaded(const QVariantList &items);
    void nextUpFailed(const QString &error);
    void latestMediaLoaded(const QString &connectionId,
                           const QString &parentId,
                           const QVariantList &items);
    void searchResultsLoaded(const QString &searchTerm,
                             const QVariantList &movies,
                             const QVariantList &series);
    void searchResultsFailed(const QString &searchTerm, const QString &error);
    void sourceLibrariesLoaded(const QString &connectionId, const QVariantList &libraries);
    void sourceLibrariesFailed(const QString &connectionId, const QString &error);

private:
    struct Member {
        QString memberId;
        QString connectionId;
        QString label;
        int priority = 0;
        QStringList hiddenLibraryIds;
        QStringList searchHiddenLibraryIds;
    };
    struct SourceState {
        QString state;
        QString error;
    };
    using SourceCompletion = std::function<void(const MergedSourceResult &)>;
    using MergeCompletion = std::function<void(const QList<MergedSourceResult> &)>;
    using MemberRequest = std::function<void(const Member &, SourceCompletion)>;
    // A parsed response (null on failure, with the error) and the adapter
    // that maps it.
    using RawCompletion = std::function<void(const ProviderCatalogResponse *response,
                                             IProviderAdapter *adapter,
                                             const QString &error)>;

    void rebuildMembers();
    void fanOut(ProviderCatalogOperation operation,
                const ProviderCatalogQuery &query,
                MergeCompletion completion);
    void fanOut(MemberRequest request, MergeCompletion completion);
    /// Searches only the member's searchable libraries, one request each.
    void requestScopedSearch(const Member &member,
                             const ProviderCatalogQuery &query,
                             SourceCompletion completion);
    void requestMember(const Member &member,
                       ProviderCatalogOperation operation,
                       ProviderCatalogQuery query,
                       bool renewalAttempted,
                       SourceCompletion completion);
    void sendMemberRequest(const Member &member,
                           ProviderCatalogOperation operation,
                           ProviderCatalogQuery query,
                           bool renewalAttempted,
                           bool trackSource,
                           RawCompletion completion);
    /// The member to read a connection with: an enabled member, or the live
    /// connection of a single space.
    std::optional<Member> readableMember(const QString &connectionId) const;
    void whenSessionReady(const QString &connectionId, std::function<void()> continuation);
    void setSourceState(const QString &connectionId, const QString &state,
                        const QString &error = QString());
    MergedSourceResult failedResult(const Member &member, const QString &error) const;
    static QString combinedError(const QList<MergedSourceResult> &results);
    static bool allFailed(const QList<MergedSourceResult> &results);

    AuthenticationService *m_authService = nullptr;
    BloomProfileRepository *m_repository = nullptr;
    ConfigManager *m_configManager = nullptr;
    std::unique_ptr<HttpTransport> m_transport;
    std::unique_ptr<IMergedCatalogPolicy> m_policy;
    QList<Member> m_members;
    QHash<QString, SourceState> m_sourceStates;
    QHash<QString, QList<std::function<void()>>> m_waitingForSession;
    QList<QPointer<HttpRequestHandle>> m_requests;
    QVariantList m_views;
    /// Libraries of unsupported types per member, from the last views fan-out.
    QHash<QString, QStringList> m_unsupportedLibraryIds;
    bool m_viewsLoading = false;
    quint64 m_epoch = 0;
    // Newest Next Up fan-out; an older one must not publish after it.
    quint64 m_nextUpGeneration = 0;
};
