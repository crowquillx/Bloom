#pragma once

#include "providers/ServerConnection.h"

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
    // Candidate base URLs (scheme, host, port; no path), most preferred first.
    QList<QUrl> urls;
};

/// The public identity a provider reports at a candidate address.
struct ProviderServerIdentity {
    QString serverId;
    QString serverName;
};
