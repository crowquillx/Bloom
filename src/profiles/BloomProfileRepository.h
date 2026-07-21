#pragma once

#include "BloomProfile.h"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <optional>

class ConfigManager;

/**
 * @brief Provider-neutral repository for Bloom profile persistence and request context.
 *
 * Uses ConfigManager only for raw settings.bloom_profiles accessors. Owns validation,
 * repair, and CRUD. Does not implement merged catalog reads or change the active
 * ServerConnection — request context is the foundation for later service routing.
 */
class BloomProfileRepository : public QObject
{
    Q_OBJECT

public:
    explicit BloomProfileRepository(ConfigManager *configManager, QObject *parent = nullptr);

    QList<BloomProfile> profiles() const;
    std::optional<BloomProfile> activeProfile() const;
    std::optional<BloomProfile> profile(const QString &id) const;

    bool upsertProfile(BloomProfile &profile);
    bool removeProfile(const QString &id);
    bool setActiveProfile(const QString &id);

    bool addMember(const QString &profileId, BloomProfileMember member);
    bool removeMember(const QString &profileId, const QString &memberId);
    bool reorderMembers(const QString &profileId, const QStringList &orderedMemberIds);
    bool setDefaultMember(const QString &profileId, const QString &memberId);

    quint64 generation() const;
    BloomProfileRequestContext activeRequestContext() const;
    BloomProfileRequestContext requestContext(const QString &profileId,
                                              const QString &memberId) const;
    bool isCurrent(const BloomProfileRequestContext &context) const;

    /// Reloads from ConfigManager, repairs, and persists when changed.
    void reload();

signals:
    void profilesChanged();
    void activeProfileChanged();
    void generationChanged(quint64 generation);

private:
    struct Store {
        QString activeProfileId;
        QList<BloomProfile> items;
    };

    Store loadStore(bool *normalized) const;
    QJsonObject storeToJson(const Store &store) const;
    void persistStore(const Store &store);
    Store sanitizeStore(Store store, bool *changed) const;
    BloomProfile sanitizeProfile(BloomProfile profile,
                                 const QSet<QString> &validConnectionIds,
                                 bool *changed) const;
    void normalizeMemberPriorities(QList<BloomProfileMember> &members) const;
    void bumpGeneration();
    QSet<QString> knownConnectionIds() const;
    std::optional<BloomProfileMember> findMember(const BloomProfile &profile,
                                                 const QString &memberId) const;

    ConfigManager *m_configManager = nullptr;
    Store m_store;
    quint64 m_generation = 1;
};
