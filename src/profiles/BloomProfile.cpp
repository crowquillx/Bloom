#include "BloomProfile.h"

#include <QJsonArray>
#include <QUrl>
#include <QUuid>

namespace {

const QUuid &bloomProfileNamespace()
{
    static const QUuid ns(QStringLiteral("{a3f7c2e1-9b4d-5e8a-b1c6-2d4f8a0e7b93}"));
    return ns;
}

QString formatTimestamp(const QDateTime &value)
{
    if (!value.isValid()) {
        return {};
    }
    return value.toUTC().toString(Qt::ISODate);
}

QDateTime parseTimestamp(const QString &value)
{
    if (value.trimmed().isEmpty()) {
        return {};
    }
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODate);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    }
    if (!parsed.isValid()) {
        return {};
    }
    return parsed.toUTC();
}

QJsonArray libraryIdsToJson(const QStringList &ids)
{
    QJsonArray json;
    for (const QString &id : ids) {
        json.append(id);
    }
    return json;
}

QStringList libraryIdsFromJson(const QJsonValue &value)
{
    QStringList ids;
    for (const QJsonValue &entry : value.toArray()) {
        ids.append(entry.toString());
    }
    return BloomProfileMember::normalizedLibraryIds(ids);
}

} // namespace

bool BloomProfileMember::isLibraryShown(const QString &libraryId) const
{
    return !hiddenLibraryIds.contains(libraryId);
}

bool BloomProfileMember::isLibrarySearchable(const QString &libraryId) const
{
    return !searchHiddenLibraryIds.contains(libraryId);
}

QStringList BloomProfileMember::normalizedLibraryIds(const QStringList &ids)
{
    QStringList result;
    for (const QString &id : ids) {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed)) {
            result.append(trimmed);
        }
    }
    return result;
}

bool BloomProfileMember::isValid() const
{
    return !memberId.trimmed().isEmpty() && !connectionId.trimmed().isEmpty();
}

QJsonObject BloomProfileMember::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("member_id")] = memberId;
    json[QStringLiteral("connection_id")] = connectionId;
    json[QStringLiteral("enabled")] = enabled;
    json[QStringLiteral("priority")] = priority;
    if (!labelOverride.trimmed().isEmpty()) {
        json[QStringLiteral("label_override")] = labelOverride;
    }
    if (!hiddenLibraryIds.isEmpty()) {
        json[QStringLiteral("hidden_library_ids")] = libraryIdsToJson(hiddenLibraryIds);
    }
    if (!searchHiddenLibraryIds.isEmpty()) {
        json[QStringLiteral("search_hidden_library_ids")] =
            libraryIdsToJson(searchHiddenLibraryIds);
    }
    return json;
}

BloomProfileMember BloomProfileMember::fromJson(const QJsonObject &json)
{
    BloomProfileMember member;
    member.memberId = json.value(QStringLiteral("member_id")).toString().trimmed();
    member.connectionId = json.value(QStringLiteral("connection_id")).toString().trimmed();
    member.enabled = json.value(QStringLiteral("enabled")).toBool(true);
    member.priority = json.value(QStringLiteral("priority")).toInt(0);
    member.labelOverride = json.value(QStringLiteral("label_override")).toString();
    member.hiddenLibraryIds = libraryIdsFromJson(json.value(QStringLiteral("hidden_library_ids")));
    member.searchHiddenLibraryIds =
        libraryIdsFromJson(json.value(QStringLiteral("search_hidden_library_ids")));
    return member;
}

bool BloomProfile::isValid() const
{
    return !id.trimmed().isEmpty()
        && !name.trimmed().isEmpty()
        && !members.isEmpty();
}

QJsonObject BloomProfile::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = name;
    json[QStringLiteral("mode")] = modeName(mode);
    QJsonArray membersJson;
    for (const BloomProfileMember &member : members) {
        membersJson.append(member.toJson());
    }
    json[QStringLiteral("members")] = membersJson;
    json[QStringLiteral("default_member_id")] = defaultMemberId;
    json[QStringLiteral("created_at")] = formatTimestamp(createdAt);
    json[QStringLiteral("updated_at")] = formatTimestamp(updatedAt);
    return json;
}

BloomProfile BloomProfile::fromJson(const QJsonObject &json)
{
    BloomProfile profile;
    profile.id = json.value(QStringLiteral("id")).toString().trimmed();
    profile.name = json.value(QStringLiteral("name")).toString().trimmed();
    profile.mode = modeFromName(json.value(QStringLiteral("mode")).toString());
    profile.defaultMemberId = json.value(QStringLiteral("default_member_id")).toString().trimmed();
    profile.createdAt = parseTimestamp(json.value(QStringLiteral("created_at")).toString());
    profile.updatedAt = parseTimestamp(json.value(QStringLiteral("updated_at")).toString());

    const QJsonArray membersJson = json.value(QStringLiteral("members")).toArray();
    for (const QJsonValue &value : membersJson) {
        if (!value.isObject()) {
            continue;
        }
        profile.members.append(BloomProfileMember::fromJson(value.toObject()));
    }
    return profile;
}

QString BloomProfile::modeName(BloomProfileMode mode)
{
    switch (mode) {
    case BloomProfileMode::Merged:
        return QStringLiteral("merged");
    case BloomProfileMode::Single:
        return QStringLiteral("single");
    }
    return QStringLiteral("single");
}

BloomProfileMode BloomProfile::modeFromName(const QString &name)
{
    return name.trimmed().compare(QStringLiteral("merged"), Qt::CaseInsensitive) == 0
        ? BloomProfileMode::Merged
        : BloomProfileMode::Single;
}

QString BloomProfile::createProfileId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString BloomProfile::createMemberId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString BloomProfile::createDeterministicProfileId(const QString &connectionId)
{
    const QByteArray identity =
        QStringLiteral("bloom-profile\n%1").arg(connectionId.trimmed()).toUtf8();
    return QUuid::createUuidV5(bloomProfileNamespace(), identity).toString(QUuid::WithoutBraces);
}

QString BloomProfile::createDeterministicMemberId(const QString &profileId,
                                                  const QString &connectionId)
{
    const QByteArray identity = QStringLiteral("bloom-member\n%1\n%2")
                                    .arg(profileId.trimmed(), connectionId.trimmed())
                                    .toUtf8();
    return QUuid::createUuidV5(bloomProfileNamespace(), identity).toString(QUuid::WithoutBraces);
}

bool BloomProfileRequestContext::isValid() const
{
    return !bloomProfileId.trimmed().isEmpty()
        && !memberId.trimmed().isEmpty()
        && !connectionId.trimmed().isEmpty();
}

bool BloomProfileRequestContext::isCurrent(quint64 currentGeneration) const
{
    return isValid() && generation == currentGeneration;
}

QString BloomProfile::connectionServerLabel(const ServerConnection &connection)
{
    if (!connection.serverName.trimmed().isEmpty()) {
        return connection.serverName.trimmed();
    }
    const QUrl url(connection.baseUrl);
    return url.host().isEmpty() ? connection.baseUrl : url.host();
}

QString BloomProfile::connectionAccountLabel(const ServerConnection &connection)
{
    if (!connection.displayName.trimmed().isEmpty()) {
        return connection.displayName.trimmed();
    }
    return connection.username.trimmed();
}

QString BloomProfile::connectionLabel(const ServerConnection &connection)
{
    const QString account = connectionAccountLabel(connection);
    const QString server = connectionServerLabel(connection);
    if (account.isEmpty()) {
        return server;
    }
    return server.isEmpty() ? account : account + QStringLiteral(" · ") + server;
}

QString BloomProfile::memberLabel(const BloomProfileMember &member,
                                  const ServerConnection &connection)
{
    const QString overridden = member.labelOverride.trimmed();
    return overridden.isEmpty() ? connectionLabel(connection) : overridden;
}
