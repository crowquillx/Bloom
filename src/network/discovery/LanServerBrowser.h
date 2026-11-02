#pragma once

#include "providers/LanServerAdvert.h"

#include <QObject>

/**
 * @brief One way of finding servers on the local network (multicast DNS,
 * Jellyfin's UDP broadcast). ServerDiscoveryService decides when it runs and
 * when it asks again; a browser only owns its sockets and decoding.
 */
class LanServerBrowser : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~LanServerBrowser() override = default;

    /// Opens the browser's sockets; false when it cannot run on this host.
    virtual bool start() = 0;
    /// Sends one round of queries. Only valid between start() and stop().
    virtual void query() = 0;
    /// Closes the sockets and forgets what was found.
    virtual void stop() = 0;

signals:
    /// A complete advert, emitted once per distinct server id and URL set.
    void advertFound(const LanServerAdvert &advert);
};
