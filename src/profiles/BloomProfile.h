#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

/**
 * @brief Workspace composition mode for a Bloom profile.
 *
 * Single resolves through exactly one membership. Merged may include multiple
 * independent connection memberships (no cross-server deduplication in MVP).
 */
enum class BloomProfileMode {
    Single,
    Merged
};

/**
 * @brief One ServerConnection membership inside a Bloom profile.
 *
 * Identity is memberId + connectionId. Never keyed by serverId, baseUrl, or
 * provider profile alone — the same physical server may appear via distinct
 * connectionIds (different accounts/users).
 */
struct BloomProfileMember {
    QString memberId;
    QString connectionId;
    bool enabled = true;
    int priority = 0;
    QString labelOverride;

    bool isValid() const;
    QJsonObject toJson() const;
    static BloomProfileMember fromJson(const QJsonObject &json);
};

/**
 * @brief Bloom-owned workspace grouping independent connection memberships.
 *
 * Distinct from provider household profiles. Credentials, base URLs, accounts,
 * and provider profile IDs are never stored here — only connectionId references.
 */
struct BloomProfile {
    QString id;
    QString name;
    BloomProfileMode mode = BloomProfileMode::Single;
    QList<BloomProfileMember> members;
    QString defaultMemberId;
    QDateTime createdAt;
    QDateTime updatedAt;

    bool isValid() const;
    QJsonObject toJson() const;
    static BloomProfile fromJson(const QJsonObject &json);

    static QString modeName(BloomProfileMode mode);
    static BloomProfileMode modeFromName(const QString &name);

    static QString createProfileId();
    static QString createMemberId();
    /// Deterministic UUIDv5 from connectionId (migration / repair idempotency).
    static QString createDeterministicProfileId(const QString &connectionId);
    static QString createDeterministicMemberId(const QString &profileId,
                                               const QString &connectionId);
};

/**
 * @brief Immutable request/source context for a membership-scoped operation.
 *
 * memberId stays out of MediaRef and caches. generation/epoch lets callers
 * detect stale contexts after active profile, default member, or membership
 * context switches. This does not change ConfigManager's active connection.
 */
struct BloomProfileRequestContext {
    QString bloomProfileId;
    QString memberId;
    QString connectionId;
    quint64 generation = 0;

    bool isValid() const;
    bool isCurrent(quint64 currentGeneration) const;
};

Q_DECLARE_METATYPE(BloomProfileMode)
Q_DECLARE_METATYPE(BloomProfileMember)
Q_DECLARE_METATYPE(BloomProfile)
Q_DECLARE_METATYPE(BloomProfileRequestContext)
Q_DECLARE_METATYPE(QList<BloomProfileMember>)
Q_DECLARE_METATYPE(QList<BloomProfile>)
