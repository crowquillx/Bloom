#pragma once

#include "providers/LanServerAdvert.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>

#include <optional>

/**
 * @brief Jellyfin's UDP auto-discovery and the public read that confirms it
 * (Jellyfin.Networking AutoDiscoveryHost).
 *
 * A client sends "Who is JellyfinServer?" to UDP 7359; each server replies
 * to the sender with ServerDiscoveryInfo JSON: `Address` (its API URL as the
 * server sees it for that client, or a published URL), `Id`, `Name` and
 * `EndpointAddress` (usually null). The published URL may be unreachable
 * from the LAN, so the address the reply came from is a candidate too. A
 * candidate is kept only when `GET /System/Info/Public` there answers the
 * same `Id`.
 */
namespace JellyfinLanDiscovery {

inline constexpr quint16 DiscoveryPort = 7359;
/// The port a Jellyfin server serves plain HTTP on unless configured.
inline constexpr quint16 DefaultHttpPort = 8096;

QByteArray probe();

/// The advert of one reply from @p sender, or nullopt when it is not a
/// ServerDiscoveryInfo object with a non-empty Id and a usable address.
std::optional<LanServerAdvert> advert(const QByteArray &reply, const QHostAddress &sender);

/// GetPublicSystemInfo: `Id` and `ServerName`; nullopt without an Id.
std::optional<ProviderServerIdentity> publicSystemInfoIdentity(const QJsonObject &wireInfo);

} // namespace JellyfinLanDiscovery
