#pragma once

#include "network/discovery/DnsSdResolver.h"
#include "network/discovery/LanServerBrowser.h"

#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QPointer>
#include <QSet>

#include <functional>
#include <optional>

class QUdpSocket;

/**
 * @brief Browses one DNS-SD service type over multicast DNS with its own
 * QUdpSocket querier (docs/server-discovery.md).
 *
 * Queries go out from UDP 5353 and answers are read from the mDNS groups
 * (224.0.0.251, ff02::fb) on every up, multicast-capable, non-loopback,
 * non-point-to-point interface. Silo's responder answers only multicast
 * queries sent from port 5353, and only by multicast, so an ephemeral-port
 * (legacy unicast) querier would hear nothing. The socket shares the port
 * with the operating system's mDNS daemon (avahi, systemd-resolved, the
 * Windows DNS Client) the same way Silo's responder does: SO_REUSEADDR, which
 * Qt sets for ShareAddress | ReuseAddressHint. If 5353 cannot be shared the
 * browser reports itself unavailable instead of falling back to unicast.
 * Packets are read only from senders on this link (acceptsSender()).
 */
class MdnsServerBrowser final : public LanServerBrowser
{
    Q_OBJECT

public:
    using AdvertMapper =
        std::function<std::optional<LanServerAdvert>(const Bloom::Dns::ServiceInstance &)>;

    static constexpr quint16 MdnsPort = 5353;
    static constexpr int MaxReported = 64;
    static constexpr int MaxQuestionsPerQuery = 16;

    MdnsServerBrowser(const QByteArray &serviceType, AdvertMapper mapper,
                      QObject *parent = nullptr);
    ~MdnsServerBrowser() override;

    bool start() override;
    void query() override;
    void stop() override;

    /// The query query() sends: a PTR question for the service type plus
    /// follow-ups for instances still missing SRV, TXT or an address.
    QByteArray buildQuery() const;
    /// Absorbs one received mDNS packet; emits adverts that became complete.
    void handlePacket(const QByteArray &packet, const QHostAddress &sender = {});

    /// Whether a packet from @p sender is from this link: its IP hop limit
    /// is 255 (RFC 6762 §11; -1 when the platform does not report it), it
    /// is IPv6 link-local, or it lies in one of @p localNetworks.
    static bool acceptsSender(const QHostAddress &sender, int hopLimit,
                              const QList<QNetworkAddressEntry> &localNetworks);

private:
    struct Membership {
        QNetworkInterface interface;
        bool ipv4 = false;
        bool ipv6 = false;
    };

    Bloom::Dns::DnsSdResolver m_resolver;
    AdvertMapper m_mapper;
    QPointer<QUdpSocket> m_socket4;
    QPointer<QUdpSocket> m_socket6;
    QList<Membership> m_memberships;
    QList<QNetworkAddressEntry> m_localNetworks;
    QSet<QByteArray> m_reported;

    QUdpSocket *openSocket(const QHostAddress &any, const QHostAddress &group, bool ipv6);
    void readPending(QUdpSocket *socket);
    void closeSocket(QPointer<QUdpSocket> &socket);
};
