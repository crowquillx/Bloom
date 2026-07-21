#include "BloomProfileRepository.h"

#include "utils/ConfigManager.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <utility>

#include "providers/ServerConnection.h"

BloomProfileRepository::BloomProfileRepository(ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
{
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged,
                this, &BloomProfileRepository::reload);
    }
    reload();
}

QList<BloomProfile> BloomProfileRepository::profiles() const
{
    return m_store.items;
}

std::optional<BloomProfile> BloomProfileRepository::activeProfile() const
{
    return profile(m_store.activeProfileId);
}

std::optional<BloomProfile> BloomProfileRepository::profile(const QString &id) const
{
    const QString normalized = id.trimmed();
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    for (const BloomProfile &item : m_store.items) {
        if (item.id == normalized) {
            return item;
        }
    }
    return std::nullopt;
}

bool BloomProfileRepository::upsertProfile(BloomProfile &profile)
{
    if (!m_configManager) {
        return false;
    }

    const QString previousActiveProfileId = m_store.activeProfileId;
    profile.id = profile.id.trimmed();
    profile.name = profile.name.trimmed();
    if (profile.id.isEmpty()) {
        profile.id = BloomProfile::createProfileId();
    }
    if (profile.name.isEmpty()) {
        return false;
    }

    bool changed = false;
    profile = sanitizeProfile(profile, knownConnectionIds(), &changed);
    if (!profile.isValid()) {
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool found = false;
    for (BloomProfile &existing : m_store.items) {
        if (existing.id != profile.id) {
            continue;
        }
        profile.createdAt = existing.createdAt.isValid() ? existing.createdAt : now;
        profile.updatedAt = now;
        existing = profile;
        found = true;
        break;
    }
    if (!found) {
        if (!profile.createdAt.isValid()) {
            profile.createdAt = now;
        }
        profile.updatedAt = now;
        m_store.items.append(profile);
    }

    if (m_store.activeProfileId.trimmed().isEmpty()) {
        m_store.activeProfileId = profile.id;
    }

    persistStore(m_store);
    const bool activeProfileIdChanged = previousActiveProfileId != m_store.activeProfileId;
    const bool activeProfileUpdated = !previousActiveProfileId.isEmpty()
        && profile.id == previousActiveProfileId;
    if (activeProfileIdChanged || activeProfileUpdated) {
        bumpGeneration();
    }
    emit profilesChanged();
    if (activeProfileIdChanged) {
        emit activeProfileChanged();
    }
    return true;
}

bool BloomProfileRepository::removeProfile(const QString &id)
{
    const QString normalized = id.trimmed();
    if (normalized.isEmpty() || !m_configManager) {
        return false;
    }

    const QString previousActiveProfileId = m_store.activeProfileId;
    const auto before = m_store.items.size();
    m_store.items.removeIf([&](const BloomProfile &item) { return item.id == normalized; });
    if (m_store.items.size() == before) {
        return false;
    }

    if (m_store.activeProfileId == normalized) {
        m_store.activeProfileId = m_store.items.isEmpty() ? QString() : m_store.items.first().id;
    }

    persistStore(m_store);
    const bool activeProfileIdChanged = previousActiveProfileId != m_store.activeProfileId;
    if (activeProfileIdChanged) {
        bumpGeneration();
    }
    emit profilesChanged();
    if (activeProfileIdChanged) {
        emit activeProfileChanged();
    }
    return true;
}

bool BloomProfileRepository::setActiveProfile(const QString &id)
{
    const QString normalized = id.trimmed();
    if (!m_configManager || !profile(normalized).has_value()) {
        return false;
    }
    if (m_store.activeProfileId == normalized) {
        return true;
    }

    m_store.activeProfileId = normalized;
    persistStore(m_store);
    bumpGeneration();
    emit activeProfileChanged();
    emit profilesChanged();
    return true;
}

bool BloomProfileRepository::addMember(const QString &profileId, BloomProfileMember member)
{
    auto existing = profile(profileId);
    if (!existing.has_value() || !m_configManager) {
        return false;
    }

    BloomProfile updated = *existing;
    if (updated.mode == BloomProfileMode::Single && !updated.members.isEmpty()) {
        return false;
    }

    member.memberId = member.memberId.trimmed();
    member.connectionId = member.connectionId.trimmed();
    if (member.connectionId.isEmpty()) {
        return false;
    }
    if (!knownConnectionIds().contains(member.connectionId)) {
        return false;
    }
    for (const BloomProfileMember &current : updated.members) {
        if (current.connectionId == member.connectionId) {
            return false;
        }
    }
    if (member.memberId.isEmpty()) {
        member.memberId = BloomProfile::createDeterministicMemberId(updated.id,
                                                                    member.connectionId);
    }

    updated.members.append(member);
    if (updated.defaultMemberId.trimmed().isEmpty()) {
        updated.defaultMemberId = member.memberId;
    }
    return upsertProfile(updated);
}

bool BloomProfileRepository::removeMember(const QString &profileId, const QString &memberId)
{
    auto existing = profile(profileId);
    if (!existing.has_value() || !m_configManager) {
        return false;
    }

    BloomProfile updated = *existing;
    const QString normalizedMember = memberId.trimmed();
    const auto before = updated.members.size();
    updated.members.removeIf([&](const BloomProfileMember &member) {
        return member.memberId == normalizedMember;
    });
    if (updated.members.size() == before) {
        return false;
    }
    if (updated.members.isEmpty()) {
        return false;
    }
    if (updated.defaultMemberId == normalizedMember) {
        updated.defaultMemberId.clear();
    }
    return upsertProfile(updated);
}

bool BloomProfileRepository::reorderMembers(const QString &profileId,
                                            const QStringList &orderedMemberIds)
{
    auto existing = profile(profileId);
    if (!existing.has_value() || !m_configManager) {
        return false;
    }

    BloomProfile updated = *existing;
    QHash<QString, BloomProfileMember> byId;
    for (const BloomProfileMember &member : updated.members) {
        byId.insert(member.memberId, member);
    }
    if (byId.size() != orderedMemberIds.size()) {
        return false;
    }

    QList<BloomProfileMember> reordered;
    QSet<QString> seen;
    for (const QString &memberId : orderedMemberIds) {
        const QString normalized = memberId.trimmed();
        if (!byId.contains(normalized) || seen.contains(normalized)) {
            return false;
        }
        seen.insert(normalized);
        reordered.append(byId.value(normalized));
    }
    if (reordered.size() != updated.members.size()) {
        return false;
    }

    updated.members = reordered;
    normalizeMemberPriorities(updated.members);
    return upsertProfile(updated);
}

bool BloomProfileRepository::setDefaultMember(const QString &profileId, const QString &memberId)
{
    auto existing = profile(profileId);
    if (!existing.has_value() || !m_configManager) {
        return false;
    }

    BloomProfile updated = *existing;
    const QString normalized = memberId.trimmed();
    const auto member = findMember(updated, normalized);
    if (!member.has_value() || !member->enabled) {
        return false;
    }
    if (updated.defaultMemberId == normalized) {
        return true;
    }
    updated.defaultMemberId = normalized;
    return upsertProfile(updated);
}

quint64 BloomProfileRepository::generation() const
{
    return m_generation;
}

BloomProfileRequestContext BloomProfileRepository::activeRequestContext() const
{
    const auto active = activeProfile();
    if (!active.has_value() || active->members.isEmpty()) {
        return {};
    }

    QString memberId = active->defaultMemberId.trimmed();
    auto member = findMember(*active, memberId);
    if (!member.has_value() || !member->enabled) {
        member.reset();
        for (const BloomProfileMember &candidate : active->members) {
            if (candidate.enabled) {
                member = candidate;
                break;
            }
        }
    }
    if (!member.has_value()) {
        return {};
    }

    BloomProfileRequestContext context;
    context.bloomProfileId = active->id;
    context.memberId = member->memberId;
    context.connectionId = member->connectionId;
    context.generation = m_generation;
    return context;
}

BloomProfileRequestContext BloomProfileRepository::requestContext(const QString &profileId,
                                                                  const QString &memberId) const
{
    const auto existing = profile(profileId);
    if (!existing.has_value()) {
        return {};
    }
    const auto member = findMember(*existing, memberId);
    if (!member.has_value() || !member->enabled) {
        return {};
    }

    BloomProfileRequestContext context;
    context.bloomProfileId = existing->id;
    context.memberId = member->memberId;
    context.connectionId = member->connectionId;
    context.generation = m_generation;
    return context;
}

bool BloomProfileRepository::isCurrent(const BloomProfileRequestContext &context) const
{
    if (!context.isCurrent(m_generation)) {
        return false;
    }
    const auto existing = profile(context.bloomProfileId);
    if (!existing.has_value()) {
        return false;
    }
    const auto member = findMember(*existing, context.memberId);
    return member.has_value()
        && member->enabled
        && member->connectionId == context.connectionId;
}

void BloomProfileRepository::reload()
{
    if (!m_configManager) {
        m_store = {};
        return;
    }

    const QString previousActiveProfileId = m_store.activeProfileId;
    const auto previousActiveProfile = activeProfile();
    const QJsonObject previousStore = storeToJson(m_store);
    bool normalized = false;
    Store loaded = loadStore(&normalized);
    bool repaired = false;
    Store sanitized = sanitizeStore(std::move(loaded), &repaired);
    const bool needsPersistence = normalized || repaired;
    const bool storeChanged = previousStore != storeToJson(sanitized);
    m_store = std::move(sanitized);
    if (needsPersistence) {
        persistStore(m_store);
    }
    if (storeChanged) {
        const auto currentActiveProfile = activeProfile();
        const bool activeProfileIdChanged = previousActiveProfileId != m_store.activeProfileId;
        const bool activeProfileContentChanged = previousActiveProfile.has_value()
            != currentActiveProfile.has_value()
            || (previousActiveProfile.has_value() && currentActiveProfile.has_value()
                && previousActiveProfile->toJson() != currentActiveProfile->toJson());
        if (activeProfileIdChanged || activeProfileContentChanged) {
            bumpGeneration();
        }
        emit profilesChanged();
        if (activeProfileIdChanged || activeProfileContentChanged) {
            emit activeProfileChanged();
        }
    }
}

BloomProfileRepository::Store BloomProfileRepository::loadStore(bool *normalized) const
{
    Store store;
    if (normalized) {
        *normalized = false;
    }
    if (!m_configManager) {
        return store;
    }

    const QJsonObject root = m_configManager->getBloomProfilesConfig();
    const QString storedActiveProfileId =
        root.value(QStringLiteral("active_profile_id")).toString();
    store.activeProfileId = storedActiveProfileId.trimmed();
    if (normalized && store.activeProfileId != storedActiveProfileId) {
        *normalized = true;
    }

    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            if (normalized) {
                *normalized = true;
            }
            continue;
        }
        const QJsonObject storedProfile = value.toObject();
        const BloomProfile profile = BloomProfile::fromJson(storedProfile);
        if (normalized
            && (storedProfile.value(QStringLiteral("id")).toString() != profile.id
                || storedProfile.value(QStringLiteral("name")).toString() != profile.name
                || storedProfile.value(QStringLiteral("default_member_id")).toString()
                    != profile.defaultMemberId)) {
            *normalized = true;
        }

        const QJsonArray storedMembers = storedProfile.value(QStringLiteral("members")).toArray();
        for (qsizetype index = 0;
             normalized && index < storedMembers.size() && index < profile.members.size();
             ++index) {
            const QJsonObject storedMember = storedMembers.at(index).toObject();
            const BloomProfileMember &member = profile.members.at(index);
            if (storedMember.value(QStringLiteral("member_id")).toString() != member.memberId
                || storedMember.value(QStringLiteral("connection_id")).toString()
                    != member.connectionId) {
                *normalized = true;
            }
        }
        store.items.append(profile);
    }
    return store;
}

QJsonObject BloomProfileRepository::storeToJson(const Store &store) const
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("active_profile_id")] = store.activeProfileId;
    QJsonArray items;
    for (const BloomProfile &profile : store.items) {
        items.append(profile.toJson());
    }
    root[QStringLiteral("items")] = items;
    return root;
}

void BloomProfileRepository::persistStore(const Store &store)
{
    if (!m_configManager) {
        return;
    }
    m_configManager->setBloomProfilesConfig(storeToJson(store));
}

BloomProfileRepository::Store BloomProfileRepository::sanitizeStore(Store store, bool *changed) const
{
    if (changed) {
        *changed = false;
    }

    const QSet<QString> validConnections = knownConnectionIds();
    QList<BloomProfile> sanitizedItems;
    QSet<QString> seenProfileIds;

    for (BloomProfile profile : store.items) {
        bool profileChanged = false;
        profile = sanitizeProfile(profile, validConnections, &profileChanged);
        if (!profile.isValid()) {
            if (changed) {
                *changed = true;
            }
            continue;
        }
        if (seenProfileIds.contains(profile.id)) {
            if (changed) {
                *changed = true;
            }
            continue;
        }
        seenProfileIds.insert(profile.id);
        if (profileChanged && changed) {
            *changed = true;
        }
        sanitizedItems.append(profile);
    }

    if (sanitizedItems.size() != store.items.size() && changed) {
        *changed = true;
    }
    store.items = sanitizedItems;

    bool activeValid = false;
    for (const BloomProfile &profile : store.items) {
        if (profile.id == store.activeProfileId) {
            activeValid = true;
            break;
        }
    }
    if (!activeValid) {
        const QString repaired = store.items.isEmpty() ? QString() : store.items.first().id;
        if (repaired != store.activeProfileId && changed) {
            *changed = true;
        }
        store.activeProfileId = repaired;
    }

    return store;
}

BloomProfile BloomProfileRepository::sanitizeProfile(BloomProfile profile,
                                                     const QSet<QString> &validConnectionIds,
                                                     bool *changed) const
{
    if (changed) {
        *changed = false;
    }

    profile.id = profile.id.trimmed();
    profile.name = profile.name.trimmed();
    profile.defaultMemberId = profile.defaultMemberId.trimmed();
    if (profile.id.isEmpty()) {
        profile.id = BloomProfile::createProfileId();
        if (changed) {
            *changed = true;
        }
    }
    if (profile.name.isEmpty()) {
        profile.name = QStringLiteral("Profile");
        if (changed) {
            *changed = true;
        }
    }

    QList<BloomProfileMember> sanitizedMembers;
    QSet<QString> seenConnectionIds;
    QSet<QString> seenMemberIds;

    for (BloomProfileMember member : profile.members) {
        member.connectionId = member.connectionId.trimmed();
        member.memberId = member.memberId.trimmed();
        if (member.connectionId.isEmpty() || !validConnectionIds.contains(member.connectionId)) {
            if (changed) {
                *changed = true;
            }
            continue;
        }
        if (seenConnectionIds.contains(member.connectionId)) {
            if (changed) {
                *changed = true;
            }
            continue;
        }
        seenConnectionIds.insert(member.connectionId);

        if (member.memberId.isEmpty()) {
            member.memberId = BloomProfile::createDeterministicMemberId(profile.id,
                                                                        member.connectionId);
            if (changed) {
                *changed = true;
            }
        }
        if (seenMemberIds.contains(member.memberId)) {
            member.memberId = BloomProfile::createMemberId();
            if (changed) {
                *changed = true;
            }
        }
        seenMemberIds.insert(member.memberId);
        sanitizedMembers.append(member);
    }

    if (profile.mode == BloomProfileMode::Single && sanitizedMembers.size() > 1) {
        BloomProfileMember retained = sanitizedMembers.first();
        for (const BloomProfileMember &member : sanitizedMembers) {
            if (member.memberId == profile.defaultMemberId) {
                retained = member;
                break;
            }
        }
        sanitizedMembers = {retained};
        if (changed) {
            *changed = true;
        }
    }

    if (sanitizedMembers.isEmpty()) {
        profile.members.clear();
        profile.defaultMemberId.clear();
        if (changed) {
            *changed = true;
        }
        return profile;
    }

    bool priorityChanged = false;
    for (qsizetype index = 0; index < sanitizedMembers.size(); ++index) {
        if (sanitizedMembers[index].priority != static_cast<int>(index)) {
            priorityChanged = true;
        }
        sanitizedMembers[index].priority = static_cast<int>(index);
    }
    if (priorityChanged && changed) {
        *changed = true;
    }

    const bool hasEnabledMember = std::any_of(
        sanitizedMembers.cbegin(), sanitizedMembers.cend(),
        [](const BloomProfileMember &member) { return member.enabled; });
    bool defaultValid = false;
    for (const BloomProfileMember &member : sanitizedMembers) {
        if (member.memberId == profile.defaultMemberId
            && (member.enabled || !hasEnabledMember)) {
            defaultValid = true;
            break;
        }
    }
    if (!defaultValid) {
        QString repaired;
        for (const BloomProfileMember &member : sanitizedMembers) {
            if (member.enabled) {
                repaired = member.memberId;
                break;
            }
        }
        if (repaired.isEmpty()) {
            repaired = sanitizedMembers.first().memberId;
        }
        if (repaired != profile.defaultMemberId && changed) {
            *changed = true;
        }
        profile.defaultMemberId = repaired;
    }

    profile.members = sanitizedMembers;
    if (!profile.createdAt.isValid()) {
        profile.createdAt = QDateTime::currentDateTimeUtc();
        if (changed) {
            *changed = true;
        }
    }
    if (!profile.updatedAt.isValid()) {
        profile.updatedAt = profile.createdAt;
        if (changed) {
            *changed = true;
        }
    }
    return profile;
}

void BloomProfileRepository::normalizeMemberPriorities(QList<BloomProfileMember> &members) const
{
    for (qsizetype index = 0; index < members.size(); ++index) {
        members[index].priority = static_cast<int>(index);
    }
}

void BloomProfileRepository::bumpGeneration()
{
    ++m_generation;
    emit generationChanged(m_generation);
}

QSet<QString> BloomProfileRepository::knownConnectionIds() const
{
    QSet<QString> ids;
    if (!m_configManager) {
        return ids;
    }
    for (const ServerConnection &connection : m_configManager->getConnections()) {
        if (!connection.connectionId.trimmed().isEmpty()) {
            ids.insert(connection.connectionId);
        }
    }
    return ids;
}

std::optional<BloomProfileMember> BloomProfileRepository::findMember(const BloomProfile &profile,
                                                                     const QString &memberId) const
{
    const QString normalized = memberId.trimmed();
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    for (const BloomProfileMember &member : profile.members) {
        if (member.memberId == normalized) {
            return member;
        }
    }
    return std::nullopt;
}
