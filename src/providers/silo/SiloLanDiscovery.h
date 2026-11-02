#pragma once

#include "providers/LanServerAdvert.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

/**
 * @brief Silo's local-network advertisement and the public reads that
 * confirm it (pinned docs/architecture/server-discovery.md and
 * server-identity.md).
 *
 * Every API process advertises DNS-SD `_silo._tcp` over multicast DNS: the
 * SRV port speaks plain HTTP, TXT `v=1` and `id=<server_id>`. A found address
 * is a candidate until `GET /api/v2/system/identity` there answers the same
 * `server_id`; `GET /api/v2/theme/branding` supplies the live server name.
 */
namespace SiloLanDiscovery {

/// The DNS-SD service type browsed, as a dotted name.
QByteArray serviceType();

/// The advert of one resolved `_silo._tcp` instance, or nullopt when its TXT
/// is not version 1, carries no server id, or no address is usable.
/// Candidate URLs are `http://<address>:<port>`, IPv4 first, IPv6 in
/// brackets; link-local IPv6 never becomes a URL.
std::optional<LanServerAdvert> advert(const QString &instanceLabel,
                                      const QMap<QByteArray, QByteArray> &txt,
                                      quint16 port,
                                      const QList<QHostAddress> &addresses);

/// getServerIdentity: `{"server_id": ...}`; nullopt without a non-empty id.
std::optional<ProviderServerIdentity> serverIdentity(const QJsonObject &wireIdentity);

/// getBranding: the trimmed `server_name`, empty when absent.
QString brandingServerName(const QJsonObject &wireBranding);

} // namespace SiloLanDiscovery
