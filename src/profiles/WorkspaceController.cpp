#include "WorkspaceController.h"

#include "network/AuthenticationService.h"
#include "profiles/BloomProfileRepository.h"
#include "utils/BloomLogging.h"
#include "utils/ConfigManager.h"

#include <QHash>
#include <QUrl>

#include <utility>

namespace {

// "3 sources · Avery · Home, Family · Silo · 1 hidden"
QString sourcesDetail(qsizetype count, int hiddenCount, const QStringList &enabledLabels)
{
    QStringList parts{WorkspaceController::tr("%n sources", nullptr, static_cast<int>(count))};
    if (!enabledLabels.isEmpty()) {
        parts.append(enabledLabels.join(QStringLiteral(", ")));
    }
    if (hiddenCount > 0) {
        parts.append(WorkspaceController::tr("%n hidden", nullptr, hiddenCount));
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

WorkspaceController::WorkspaceController(AuthenticationService *authService,
                                         BloomProfileRepository *repository,
                                         ConfigManager *configManager,
                                         QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_repository(repository)
    , m_configManager(configManager)
{
    if (m_repository) {
        // Edits to the active profile (mode, members, enabled state) arrive as
        // profilesChanged; both paths re-check that the live session is still
        // one of the active profile's enabled members.
        connect(m_repository, &BloomProfileRepository::profilesChanged,
                this, &WorkspaceController::handleActiveProfileChanged);
        connect(m_repository, &BloomProfileRepository::activeProfileChanged,
                this, &WorkspaceController::handleActiveProfileChanged);
    }
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged, this, [this]() {
            emit connectionsChanged();
            emit profilesChanged();
            emit activeConnectionChanged();
        });
    }
    if (m_authService) {
        connect(m_authService, &AuthenticationService::authenticationStepChanged,
                this, [this]() {
            if (m_authService->isAuthenticated()) {
                handleAuthenticated();
            }
        });
        connect(m_authService, &AuthenticationService::connectionActivated,
                this, &WorkspaceController::handleConnectionActivated);
        connect(m_authService, &AuthenticationService::connectionActivationFailed,
                this, &WorkspaceController::handleActivationFailed);
        connect(m_authService, &AuthenticationService::signInRequired,
                this, &WorkspaceController::handleSignInRequired);
        connect(m_authService, &AuthenticationService::userIdChanged,
                this, &WorkspaceController::activeConnectionChanged);
    }
}

QVariantList WorkspaceController::profiles() const
{
    QVariantList result;
    if (!m_repository) {
        return result;
    }
    const QString activeId = activeProfileId();
    for (const BloomProfile &profile : m_repository->profiles()) {
        QVariantList members;
        QStringList enabledLabels;
        int hiddenCount = 0;
        QString soleDetail;
        for (const BloomProfileMember &member : profile.members) {
            const auto connection = m_configManager
                ? m_configManager->getConnection(member.connectionId) : std::nullopt;
            if (!connection.has_value()) {
                continue;
            }
            const QString label = BloomProfile::memberLabel(member, *connection);
            if (member.enabled) {
                enabledLabels.append(label);
            } else {
                ++hiddenCount;
            }
            QVariantMap map = connectionMap(*connection);
            // A one-source space named after its source would repeat the
            // label; name the server type and host instead.
            soleDetail = label == profile.name
                ? QStringLiteral("%1 · %2").arg(map.value(QStringLiteral("providerName")).toString(),
                                                QUrl(connection->baseUrl).host())
                : label;
            map.insert(QStringLiteral("memberId"), member.memberId);
            map.insert(QStringLiteral("label"), label);
            map.insert(QStringLiteral("labelOverride"), member.labelOverride);
            map.insert(QStringLiteral("enabled"), member.enabled);
            map.insert(QStringLiteral("priority"), member.priority);
            map.insert(QStringLiteral("isDefault"), member.memberId == profile.defaultMemberId);
            map.insert(QStringLiteral("hiddenLibraryIds"), member.hiddenLibraryIds);
            map.insert(QStringLiteral("searchHiddenLibraryIds"), member.searchHiddenLibraryIds);
            members.append(map);
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("mode"), BloomProfile::modeName(profile.mode)},
            {QStringLiteral("merged"), profile.mode == BloomProfileMode::Merged},
            {QStringLiteral("active"), profile.id == activeId},
            {QStringLiteral("members"), members},
            {QStringLiteral("summary"), enabledLabels.join(QStringLiteral(", "))},
            {QStringLiteral("sourceCount"), members.size()},
            {QStringLiteral("detail"), members.size() == 1
                 ? soleDetail : sourcesDetail(members.size(), hiddenCount, enabledLabels)},
        });
    }
    return result;
}

QVariantList WorkspaceController::connections() const
{
    QVariantList result;
    if (!m_configManager) {
        return result;
    }
    for (const ServerConnection &connection : m_configManager->getConnections()) {
        result.append(connectionMap(connection));
    }
    return result;
}

QString WorkspaceController::activeProfileId() const
{
    const auto active = m_repository ? m_repository->activeProfile() : std::nullopt;
    return active.has_value() ? active->id : QString();
}

QString WorkspaceController::activeProfileName() const
{
    const auto active = m_repository ? m_repository->activeProfile() : std::nullopt;
    return active.has_value() ? active->name : QString();
}

bool WorkspaceController::isMerged() const
{
    const auto active = m_repository ? m_repository->activeProfile() : std::nullopt;
    return active.has_value() && active->mode == BloomProfileMode::Merged;
}

QString WorkspaceController::activeConnectionId() const
{
    return m_authService ? m_authService->activeConnectionId() : QString();
}

bool WorkspaceController::switchProfile(const QString &profileId)
{
    const auto target = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!target.has_value() || target->members.isEmpty()) {
        return false;
    }
    const bool alreadyLive = m_authService && m_authService->isAuthenticated()
        && profileContainsEnabled(*target, m_authService->activeConnectionId());
    if (target->id == activeProfileId() && alreadyLive) {
        return true;
    }

    m_previousProfileId = activeProfileId();
    m_previousConnectionId = m_authService && m_authService->isAuthenticated()
        ? m_authService->activeConnectionId() : QString();
    m_routeTarget.clear();
    // Selecting the profile first keeps the picker state truthful while the
    // session switches; handleActiveProfileChanged is suppressed so the
    // switch starts exactly once below.
    m_suppressActivation = true;
    const bool selected = m_repository->setActiveProfile(target->id);
    m_suppressActivation = false;
    if (!selected) {
        return false;
    }
    startSwitch(*target);
    return true;
}

QString WorkspaceController::createProfile(const QString &name,
                                           const QStringList &connectionIds,
                                           bool merged)
{
    if (!m_repository) {
        return {};
    }
    BloomProfile profile;
    profile.name = name.trimmed();
    profile.mode = merged ? BloomProfileMode::Merged : BloomProfileMode::Single;
    for (const QString &connectionId : connectionIds) {
        BloomProfileMember member;
        member.connectionId = connectionId;
        profile.members.append(member);
        if (!merged) {
            break;
        }
    }
    if (profile.name.isEmpty() && !profile.members.isEmpty() && m_configManager) {
        const auto connection = m_configManager->getConnection(profile.members.first().connectionId);
        profile.name = connection.has_value()
            ? BloomProfile::connectionLabel(*connection) : tr("Space");
    }
    // Member IDs derive from the profile ID, so assign it before upserting.
    profile.id = BloomProfile::createProfileId();
    for (BloomProfileMember &member : profile.members) {
        member.memberId = BloomProfile::createDeterministicMemberId(profile.id,
                                                                    member.connectionId);
    }
    m_suppressActivation = true;
    const bool stored = m_repository->upsertProfile(profile);
    m_suppressActivation = false;
    return stored ? profile.id : QString();
}

bool WorkspaceController::renameProfile(const QString &profileId, const QString &name)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value() || name.trimmed().isEmpty()) {
        return false;
    }
    profile->name = name.trimmed();
    return m_repository->upsertProfile(*profile);
}

bool WorkspaceController::setProfileMerged(const QString &profileId, bool merged)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    const BloomProfileMode mode = merged ? BloomProfileMode::Merged : BloomProfileMode::Single;
    if (profile->mode == mode) {
        return true;
    }
    // Merged -> Single keeps only the default member (repository rule).
    profile->mode = mode;
    return m_repository->upsertProfile(*profile);
}

bool WorkspaceController::removeProfile(const QString &profileId)
{
    if (!m_repository || !m_repository->profile(profileId).has_value()) {
        return false;
    }
    const bool wasActive = profileId == activeProfileId();
    m_suppressActivation = true;
    const bool removed = m_repository->removeProfile(profileId);
    m_suppressActivation = false;
    if (removed && wasActive) {
        if (const auto next = m_repository->activeProfile()) {
            switchProfile(next->id);
        }
    }
    return removed;
}

bool WorkspaceController::moveProfile(const QString &profileId, int delta)
{
    if (!m_repository) {
        return false;
    }
    QStringList ids;
    for (const BloomProfile &profile : m_repository->profiles()) {
        ids.append(profile.id);
    }
    const qsizetype from = ids.indexOf(profileId);
    const qsizetype to = from + delta;
    if (from < 0 || to < 0 || to >= ids.size() || delta == 0) {
        return false;
    }
    ids.move(from, to);
    return m_repository->reorderProfiles(ids);
}

bool WorkspaceController::addMember(const QString &profileId, const QString &connectionId)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    for (const BloomProfileMember &member : profile->members) {
        if (member.connectionId == connectionId) {
            return true;
        }
    }
    // A second membership turns a single workspace into a merged one; the
    // existing member stays the default so the live session is unchanged.
    profile->mode = BloomProfileMode::Merged;
    BloomProfileMember member;
    member.connectionId = connectionId;
    member.memberId = BloomProfile::createDeterministicMemberId(profile->id, connectionId);
    profile->members.append(member);
    if (profile->defaultMemberId.isEmpty()) {
        profile->defaultMemberId = member.memberId;
    }
    return m_repository->upsertProfile(*profile);
}

bool WorkspaceController::removeMember(const QString &profileId, const QString &memberId)
{
    // Removing the member that holds the live session moves the session to
    // the profile's repaired default member (handleActiveProfileChanged).
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    // Mode follows membership: a merged space left with one source becomes
    // single again, and that source is shown even if it had been hidden.
    if (profile->mode == BloomProfileMode::Merged && profile->members.size() == 2) {
        const auto before = profile->members.size();
        profile->members.removeIf([&](const BloomProfileMember &member) {
            return member.memberId == memberId;
        });
        if (profile->members.size() == before) {
            return false;
        }
        profile->mode = BloomProfileMode::Single;
        profile->members.first().enabled = true;
        profile->defaultMemberId = profile->members.first().memberId;
        return m_repository->upsertProfile(*profile);
    }
    return m_repository->removeMember(profileId, memberId);
}

bool WorkspaceController::moveMember(const QString &profileId, const QString &memberId, int delta)
{
    const auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    QStringList ids;
    for (const BloomProfileMember &member : profile->members) {
        ids.append(member.memberId);
    }
    const qsizetype from = ids.indexOf(memberId);
    const qsizetype to = from + delta;
    if (from < 0 || to < 0 || to >= ids.size() || delta == 0) {
        return false;
    }
    ids.move(from, to);
    return m_repository->reorderMembers(profileId, ids);
}

bool WorkspaceController::setMemberEnabled(const QString &profileId,
                                           const QString &memberId,
                                           bool enabled)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    int enabledCount = 0;
    BloomProfileMember *target = nullptr;
    for (BloomProfileMember &member : profile->members) {
        if (member.enabled) {
            ++enabledCount;
        }
        if (member.memberId == memberId) {
            target = &member;
        }
    }
    if (!target) {
        return false;
    }
    if (target->enabled == enabled) {
        return true;
    }
    // A workspace always keeps at least one enabled member.
    if (!enabled && enabledCount <= 1) {
        return false;
    }
    target->enabled = enabled;
    return m_repository->upsertProfile(*profile);
}

bool WorkspaceController::setDefaultMember(const QString &profileId, const QString &memberId)
{
    return m_repository && m_repository->setDefaultMember(profileId, memberId);
}

bool WorkspaceController::setMemberLabel(const QString &profileId,
                                         const QString &memberId,
                                         const QString &label)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    for (BloomProfileMember &member : profile->members) {
        if (member.memberId == memberId) {
            member.labelOverride = label.trimmed();
            return m_repository->upsertProfile(*profile);
        }
    }
    return false;
}

bool WorkspaceController::setMemberLibraries(const QString &profileId,
                                             const QString &memberId,
                                             const QStringList &hiddenLibraryIds,
                                             const QStringList &searchHiddenLibraryIds)
{
    auto profile = m_repository ? m_repository->profile(profileId) : std::nullopt;
    if (!profile.has_value()) {
        return false;
    }
    for (BloomProfileMember &member : profile->members) {
        if (member.memberId != memberId) {
            continue;
        }
        const QStringList hidden = BloomProfileMember::normalizedLibraryIds(hiddenLibraryIds);
        const QStringList searchHidden =
            BloomProfileMember::normalizedLibraryIds(searchHiddenLibraryIds);
        if (member.hiddenLibraryIds == hidden && member.searchHiddenLibraryIds == searchHidden) {
            return true;
        }
        member.hiddenLibraryIds = hidden;
        member.searchHiddenLibraryIds = searchHidden;
        if (!m_repository->upsertProfile(*profile)) {
            return false;
        }
        if (profile->id == activeProfileId()) {
            emit libraryVisibilityChanged();
        }
        return true;
    }
    return false;
}

QVariantList WorkspaceController::visibleLibraries(const QVariantList &views) const
{
    const auto profile = m_repository ? m_repository->activeProfile() : std::nullopt;
    if (!profile.has_value()) {
        return views;
    }
    QHash<QString, QStringList> hiddenByConnection;
    for (const BloomProfileMember &member : profile->members) {
        hiddenByConnection.insert(member.connectionId, member.hiddenLibraryIds);
    }
    QVariantList result;
    for (const QVariant &value : views) {
        const QVariantMap view = value.toMap();
        const QStringList hidden =
            hiddenByConnection.value(view.value(QStringLiteral("connectionId")).toString());
        if (!hidden.contains(view.value(QStringLiteral("itemId")).toString())) {
            result.append(value);
        }
    }
    return result;
}

QStringList WorkspaceController::hiddenLibraryIds(const QString &connectionId) const
{
    const auto member = activeMember(connectionId);
    return member.has_value() ? member->hiddenLibraryIds : QStringList();
}

QStringList WorkspaceController::searchHiddenLibraryIds(const QString &connectionId) const
{
    const auto member = activeMember(connectionId);
    return member.has_value() ? member->searchHiddenLibraryIds : QStringList();
}

void WorkspaceController::beginAddConnection(const QString &targetProfileId)
{
    if (!m_authService) {
        return;
    }
    m_addTargetProfileId = targetProfileId;
    m_connectionBeforeAdd = m_authService->isAuthenticated()
        ? m_authService->activeConnectionId() : QString();
    setAddingConnection(true);
    if (m_authService->isAuthenticated() || m_authService->isActivatingConnection()) {
        // Keeps every stored credential; emits loggedOut for the login screen.
        m_authService->suspendSession();
    }
}

void WorkspaceController::cancelAddConnection()
{
    if (!m_addingConnection) {
        return;
    }
    const QString previous = std::exchange(m_connectionBeforeAdd, QString());
    m_addTargetProfileId.clear();
    setAddingConnection(false);
    if (!previous.isEmpty() && m_authService) {
        m_authService->activateConnection(previous);
    }
}

bool WorkspaceController::removeConnection(const QString &connectionId)
{
    const auto connection = m_configManager
        ? m_configManager->getConnection(connectionId) : std::nullopt;
    if (!connection.has_value() || !m_authService) {
        return false;
    }
    const bool wasLive = m_authService->activeConnectionId() == connectionId;
    if (wasLive) {
        // Deletes this connection's credentials and returns to sign-in.
        m_authService->logout();
    } else {
        m_authService->removeStoredCredentials(*connection);
    }
    m_suppressActivation = true;
    m_configManager->removeConnection(connectionId);
    m_suppressActivation = false;

    // Continue in the next workspace instead of leaving the user signed out
    // when other saved accounts remain.
    if (wasLive && m_repository) {
        if (const auto next = m_repository->activeProfile()) {
            switchProfile(next->id);
        }
    }
    return true;
}

bool WorkspaceController::routeTo(const QString &connectionId)
{
    if (!m_authService || connectionId.isEmpty()) {
        return false;
    }
    if (m_authService->isAuthenticated() && m_authService->activeConnectionId() == connectionId) {
        return true;
    }
    const auto active = m_repository ? m_repository->activeProfile() : std::nullopt;
    if (!active.has_value() || !profileContainsEnabled(*active, connectionId)) {
        emit routeFailed(connectionId, tr("This item belongs to a server outside the current space."));
        return false;
    }
    m_routeTarget = connectionId;
    setSwitching(true);
    m_authService->activateConnection(connectionId);
    return false;
}

void WorkspaceController::handleActiveProfileChanged()
{
    emit activeProfileChanged();
    emit profilesChanged();
    if (m_suppressActivation || m_switching || !m_authService || !m_repository) {
        return;
    }
    // Repairs (a removed member or connection) can change the active profile
    // underneath a live session; keep the session on one of its members.
    const auto active = m_repository->activeProfile();
    if (!active.has_value() || !m_authService->isAuthenticated()) {
        return;
    }
    if (!profileContainsEnabled(*active, m_authService->activeConnectionId())) {
        m_previousProfileId.clear();
        m_previousConnectionId = m_authService->activeConnectionId();
        startSwitch(*active);
    }
}

void WorkspaceController::handleAuthenticated()
{
    emit activeConnectionChanged();
    if (m_switching || !m_repository || !m_authService) {
        return;
    }
    const QString connectionId = m_authService->activeConnectionId();
    if (connectionId.isEmpty()) {
        return;
    }
    const QString awaitedSignIn = std::exchange(m_switchAwaitingSignIn, QString());
    if (!awaitedSignIn.isEmpty() && awaitedSignIn == connectionId && !m_addingConnection) {
        // The switch finished on the login screen (provider profile or PIN).
        finishSwitch();
        return;
    }
    if (m_addingConnection) {
        finishAddConnection(connectionId);
        return;
    }

    // A plain sign-in (startup restore or the login screen): keep the active
    // workspace when it contains this account, otherwise select or create one.
    const auto active = m_repository->activeProfile();
    if (active.has_value() && profileContainsEnabled(*active, connectionId)) {
        return;
    }
    QString target;
    for (const BloomProfile &profile : m_repository->profiles()) {
        if (profileContainsEnabled(profile, connectionId)) {
            target = profile.id;
            break;
        }
    }
    m_suppressActivation = true;
    if (target.isEmpty()) {
        target = createSingleProfileFor(connectionId);
    }
    if (!target.isEmpty()) {
        m_repository->setActiveProfile(target);
    }
    m_suppressActivation = false;
}

void WorkspaceController::handleConnectionActivated(const QString &connectionId)
{
    emit activeConnectionChanged();
    if (!m_routeTarget.isEmpty() && connectionId == m_routeTarget) {
        m_routeTarget.clear();
        setSwitching(false);
        emit routeReady(connectionId);
        return;
    }
    if (m_switching && connectionId == m_switchingConnectionId) {
        finishSwitch();
    }
}

void WorkspaceController::handleActivationFailed(const QString &connectionId,
                                                 const QString &error)
{
    if (!m_routeTarget.isEmpty() && connectionId == m_routeTarget) {
        // AuthenticationService falls back to the previous session on its own.
        m_routeTarget.clear();
        setSwitching(false);
        emit routeFailed(connectionId, error);
        return;
    }
    if (!m_switching || connectionId != m_switchingConnectionId) {
        return;
    }
    qCWarning(lcAuth) << "Workspace member unavailable, trying the next one";
    if (!m_switchCandidates.isEmpty()) {
        // Starting another activation inside the failure signal suppresses
        // AuthenticationService's own fallback.
        activateNextCandidate();
        return;
    }
    failSwitch(error);
}

void WorkspaceController::handleSignInRequired(const QString &connectionId)
{
    if (!m_routeTarget.isEmpty() && connectionId == m_routeTarget) {
        // A routed action cannot wait for an interactive sign-in; the login
        // screen continues as a plain sign-in for that account.
        m_routeTarget.clear();
        setSwitching(false);
        emit routeFailed(connectionId, tr("This account needs you to sign in again."));
        return;
    }
    if (!m_switching || connectionId != m_switchingConnectionId) {
        return;
    }
    // The workspace is selected; the switch completes once the login screen
    // authenticates this member. Leaving the switching state keeps profile
    // edits and repairs working meanwhile.
    m_switchCandidates.clear();
    m_switchingConnectionId.clear();
    m_previousProfileId.clear();
    m_previousConnectionId.clear();
    m_switchAwaitingSignIn = connectionId;
    setSwitching(false);
}

void WorkspaceController::startSwitch(const BloomProfile &profile)
{
    m_switchCandidates.clear();
    m_switchAwaitingSignIn.clear();
    QString defaultConnection;
    for (const BloomProfileMember &member : profile.members) {
        if (!member.enabled) {
            continue;
        }
        if (member.memberId == profile.defaultMemberId) {
            defaultConnection = member.connectionId;
        } else {
            m_switchCandidates.append(member.connectionId);
        }
    }
    if (!defaultConnection.isEmpty()) {
        m_switchCandidates.prepend(defaultConnection);
    }
    if (m_switchCandidates.isEmpty()) {
        return;
    }
    setSwitching(true);
    activateNextCandidate();
}

void WorkspaceController::activateNextCandidate()
{
    m_switchingConnectionId = m_switchCandidates.takeFirst();
    m_authService->activateConnection(m_switchingConnectionId);
}

void WorkspaceController::finishSwitch()
{
    m_switchCandidates.clear();
    m_switchingConnectionId.clear();
    m_previousProfileId.clear();
    m_previousConnectionId.clear();
    setSwitching(false);
    emit workspaceSwitched();
}

void WorkspaceController::failSwitch(const QString &error)
{
    m_switchCandidates.clear();
    m_switchingConnectionId.clear();
    const QString previousProfile = std::exchange(m_previousProfileId, QString());
    const QString previousConnection = std::exchange(m_previousConnectionId, QString());
    setSwitching(false);
    if (!previousProfile.isEmpty() && m_repository) {
        m_suppressActivation = true;
        m_repository->setActiveProfile(previousProfile);
        m_suppressActivation = false;
    }
    if (!previousConnection.isEmpty() && m_authService) {
        m_authService->activateConnection(previousConnection);
    }
    emit switchFailed(error);
}

void WorkspaceController::finishAddConnection(const QString &connectionId)
{
    const QString target = std::exchange(m_addTargetProfileId, QString());
    m_connectionBeforeAdd.clear();
    setAddingConnection(false);

    m_suppressActivation = true;
    QString profileId;
    if (!target.isEmpty() && m_repository->profile(target).has_value()) {
        if (addMember(target, connectionId)) {
            profileId = target;
        }
    }
    if (profileId.isEmpty()) {
        profileId = createSingleProfileFor(connectionId);
    }
    if (!profileId.isEmpty()) {
        m_repository->setActiveProfile(profileId);
    }
    m_suppressActivation = false;
}

void WorkspaceController::setSwitching(bool switching)
{
    if (m_switching == switching) {
        return;
    }
    m_switching = switching;
    emit switchingChanged();
}

void WorkspaceController::setAddingConnection(bool adding)
{
    if (m_addingConnection == adding) {
        return;
    }
    m_addingConnection = adding;
    emit addingConnectionChanged();
}

QString WorkspaceController::createSingleProfileFor(const QString &connectionId)
{
    // Reuse an existing single workspace for this account before adding one.
    for (const BloomProfile &profile : m_repository->profiles()) {
        if (profile.mode == BloomProfileMode::Single && profile.members.size() == 1
            && profile.members.first().connectionId == connectionId) {
            return profile.id;
        }
    }
    return createProfile(QString(), {connectionId}, false);
}

bool WorkspaceController::profileContainsEnabled(const BloomProfile &profile,
                                                 const QString &connectionId) const
{
    if (connectionId.isEmpty()) {
        return false;
    }
    for (const BloomProfileMember &member : profile.members) {
        if (member.enabled && member.connectionId == connectionId) {
            return true;
        }
    }
    return false;
}

std::optional<BloomProfileMember> WorkspaceController::activeMember(const QString &connectionId) const
{
    const auto profile = m_repository ? m_repository->activeProfile() : std::nullopt;
    if (!profile.has_value() || connectionId.isEmpty()) {
        return std::nullopt;
    }
    for (const BloomProfileMember &member : profile->members) {
        if (member.connectionId == connectionId) {
            return member;
        }
    }
    return std::nullopt;
}

QVariantMap WorkspaceController::connectionMap(const ServerConnection &connection) const
{
    return {
        {QStringLiteral("connectionId"), connection.connectionId},
        {QStringLiteral("label"), BloomProfile::connectionLabel(connection)},
        {QStringLiteral("accountLabel"), BloomProfile::connectionAccountLabel(connection)},
        {QStringLiteral("serverLabel"), BloomProfile::connectionServerLabel(connection)},
        {QStringLiteral("providerName"), connection.providerKind == ProviderKind::Silo
             ? QStringLiteral("Silo") : QStringLiteral("Jellyfin")},
        {QStringLiteral("baseUrl"), connection.baseUrl},
        {QStringLiteral("live"), m_authService && m_authService->isAuthenticated()
             && m_authService->activeConnectionId() == connection.connectionId},
    };
}
