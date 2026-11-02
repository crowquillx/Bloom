#pragma once

#include "network/discovery/LanServerBrowser.h"

#include <QHostAddress>
#include <QPointer>
#include <QSet>

class QUdpSocket;

/**
 * @brief Jellyfin's UDP auto-discovery: "Who is JellyfinServer?" broadcast to
 * port 7359 from an ephemeral port, JSON replies unicast back
 * (docs/server-discovery.md).
 *
 * The probe goes to the limited broadcast address and to each IPv4
 * interface's directed broadcast address, since the limited broadcast leaves
 * only through the default route on some systems. Jellyfin listens on IPv4
 * only.
 */
class JellyfinServerBrowser final : public LanServerBrowser
{
    Q_OBJECT

public:
    static constexpr int MaxReported = 64;

    explicit JellyfinServerBrowser(QObject *parent = nullptr);
    ~JellyfinServerBrowser() override;

    bool start() override;
    void query() override;
    void stop() override;

    /// Handles one reply datagram; emits an advert the first time it is seen.
    void handleReply(const QByteArray &reply, const QHostAddress &sender);

private:
    QPointer<QUdpSocket> m_socket;
    QSet<QByteArray> m_reported;

    void readPending();
};
