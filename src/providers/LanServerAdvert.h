#pragma once

#include "providers/ServerConnection.h"

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QUrl>

/**
 * @brief A server a local-network advertisement offered
 * (docs/server-discovery.md).
 *
 * Every field is the advertiser's claim. Bloom lists the server only after the
 * provider's public identity read at one of the URLs answers the same
 * serverId, so an advert is a hint, never a credential.
 */
struct LanServerAdvert {
    ProviderKind providerKind = ProviderKind::Jellyfin;
    QString serverId;
    // The advertised name, shown when the identity read carries none.
    QString name;
    // Candidate base URLs (scheme, host, port, optional base path), most
    // preferred first.
    QList<QUrl> urls;
    // Who made the claim, set by the browser: the DNS-SD host name, or the
    // address a Jellyfin reply came from. URLs of one origin are one server
    // entry; the same id confirmed from another origin is listed separately.
    QString origin;
    // The address of the datagram that carried the advert; discovery caps
    // the candidates one sender can queue.
    QHostAddress sender;
};

/// The public identity a provider reports at a candidate address.
struct ProviderServerIdentity {
    QString serverId;
    QString serverName;
};
