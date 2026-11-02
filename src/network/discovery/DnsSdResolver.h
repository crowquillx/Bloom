#pragma once

#include "network/discovery/DnsMessage.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QString>

namespace Bloom::Dns {

/// One DNS-SD service instance with everything needed to connect.
struct ServiceInstance {
    Name instance;          // "<label>._silo._tcp.local"
    QString instanceLabel;  // the first label, UTF-8 decoded
    Name host;
    quint16 port = 0;
    QMap<QByteArray, QByteArray> txt;
    QList<QHostAddress> addresses;
};

/**
 * @brief Assembles mDNS responses into resolved instances of one service
 * type: PTR to instance, SRV and TXT of the instance, A/AAAA of its host.
 *
 * Records may arrive in any section and any message; a goodbye (TTL 0)
 * withdraws one. A cache-flush A (or AAAA) set replaces the host's earlier
 * addresses of that family, and addresses of a host no instance points at
 * any more are dropped. The browse window is short (ServerDiscoveryService), so records
 * do not expire on their own. Link-local IPv6, loopback and unspecified
 * addresses are dropped: a URL cannot carry the zone link-local needs.
 * Sizes are bounded so a noisy link cannot grow it without limit.
 */
class DnsSdResolver
{
public:
    static constexpr int MaxInstances = 32;
    static constexpr int MaxAddressesPerHost = 8;

    explicit DnsSdResolver(Name serviceType);

    const Name &serviceType() const { return m_serviceType; }

    void absorb(const Message &message);
    void clear();

    /// Instances with SRV, TXT and at least one usable address.
    QList<ServiceInstance> resolved() const;
    /// Hosts with cached addresses; never more than MaxInstances.
    int cachedHostCount() const { return static_cast<int>(m_hostAddresses.size()); }
    /// Follow-up questions for instances that still lack SRV or TXT, or
    /// whose host has no address yet.
    QList<Question> missingQuestions() const;

private:
    struct Instance {
        Name name;
        bool hasSrv = false;
        Name host;
        quint16 port = 0;
        bool hasTxt = false;
        QMap<QByteArray, QByteArray> txt;
    };

    Name m_serviceType;
    QList<QByteArray> m_order; // instance keys in discovery order
    QHash<QByteArray, Instance> m_instances;
    QHash<QByteArray, QList<QHostAddress>> m_hostAddresses;

    bool knowsHostKey(const QByteArray &hostKey) const;
};

} // namespace Bloom::Dns
