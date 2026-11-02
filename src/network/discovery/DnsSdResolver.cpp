#include "network/discovery/DnsSdResolver.h"

#include <QSet>

#include <iterator>

namespace Bloom::Dns {

namespace {

bool usableAddress(const QHostAddress &address)
{
    if (address.isNull() || address.isLoopback()) {
        return false;
    }
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        return address != QHostAddress(QHostAddress::AnyIPv4);
    }
    return address != QHostAddress(QHostAddress::AnyIPv6) && !address.isLinkLocal();
}

} // namespace

DnsSdResolver::DnsSdResolver(Name serviceType)
    : m_serviceType(std::move(serviceType))
{
}

void DnsSdResolver::clear()
{
    m_order.clear();
    m_instances.clear();
    m_hostAddresses.clear();
}

bool DnsSdResolver::knowsHostKey(const QByteArray &hostKey) const
{
    for (const Instance &instance : m_instances) {
        if (instance.hasSrv && nameKey(instance.host) == hostKey) {
            return true;
        }
    }
    return false;
}

void DnsSdResolver::absorb(const Message &message)
{
    if (!message.isResponse()) {
        return;
    }
    QList<const Record *> records;
    for (const QList<Record> *section :
         {&message.answers, &message.authorities, &message.additionals}) {
        for (const Record &record : *section) {
            if (record.rrclass == ClassIN) {
                records.append(&record);
            }
        }
    }

    // Instances first, then their SRV/TXT, then host addresses, so one
    // response that carries everything resolves in one pass.
    for (const Record *record : records) {
        if (record->type != TypePTR || !namesEqual(record->name, m_serviceType)
            || !isDirectChild(record->target, m_serviceType)) {
            continue;
        }
        const QByteArray key = nameKey(record->target);
        if (record->ttl == 0) {
            m_instances.remove(key);
            m_order.removeAll(key);
        } else if (!m_instances.contains(key) && m_instances.size() < MaxInstances) {
            m_instances.insert(key, Instance{record->target});
            m_order.append(key);
        }
    }
    for (const Record *record : records) {
        if (record->type != TypeSRV && record->type != TypeTXT) {
            continue;
        }
        const auto found = m_instances.find(nameKey(record->name));
        if (found == m_instances.end()) {
            continue;
        }
        Instance &instance = found.value();
        if (record->type == TypeSRV) {
            instance.hasSrv = record->ttl != 0 && !record->target.isEmpty();
            instance.host = instance.hasSrv ? record->target : Name();
            instance.port = instance.hasSrv ? record->port : 0;
        } else {
            instance.hasTxt = record->ttl != 0;
            instance.txt = instance.hasTxt ? txtAttributes(record->txt)
                                           : QMap<QByteArray, QByteArray>();
        }
    }
    // Addresses of hosts no instance points at any more go, so an SRV
    // target that keeps changing cannot grow the cache.
    for (auto host = m_hostAddresses.begin(); host != m_hostAddresses.end();) {
        host = knowsHostKey(host.key()) ? std::next(host) : m_hostAddresses.erase(host);
    }
    // A cache-flush set replaces the earlier records of its own name and
    // type only (RFC 6762 §10.2): A and AAAA are flushed separately.
    QSet<QByteArray> flushedSets;
    for (const Record *record : records) {
        if (record->type != TypeA && record->type != TypeAAAA) {
            continue;
        }
        const QByteArray key = nameKey(record->name);
        if (!knowsHostKey(key)) {
            continue;
        }
        QList<QHostAddress> &addresses = m_hostAddresses[key];
        const QByteArray setKey = key + '/' + QByteArray::number(record->type);
        if (record->cacheFlush && !flushedSets.contains(setKey)) {
            flushedSets.insert(setKey);
            const auto family = record->type == TypeA ? QAbstractSocket::IPv4Protocol
                                                      : QAbstractSocket::IPv6Protocol;
            addresses.removeIf([family](const QHostAddress &address) {
                return address.protocol() == family;
            });
        }
        if (record->ttl == 0) {
            addresses.removeAll(record->address);
        } else if (usableAddress(record->address) && !addresses.contains(record->address)
                   && addresses.size() < MaxAddressesPerHost) {
            addresses.append(record->address);
        }
    }
}

QList<ServiceInstance> DnsSdResolver::resolved() const
{
    QList<ServiceInstance> result;
    for (const QByteArray &key : m_order) {
        const Instance &instance = m_instances.value(key);
        const QList<QHostAddress> addresses = m_hostAddresses.value(nameKey(instance.host));
        if (!instance.hasSrv || !instance.hasTxt || instance.port == 0 || addresses.isEmpty()) {
            continue;
        }
        ServiceInstance service;
        service.instance = instance.name;
        service.instanceLabel = QString::fromUtf8(instance.name.constFirst());
        service.host = instance.host;
        service.port = instance.port;
        service.txt = instance.txt;
        // IPv4 first: every LAN routes it, while a global IPv6 address may
        // not be reachable from this host.
        for (const QHostAddress &address : addresses) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                service.addresses.append(address);
            }
        }
        for (const QHostAddress &address : addresses) {
            if (address.protocol() != QAbstractSocket::IPv4Protocol) {
                service.addresses.append(address);
            }
        }
        result.append(service);
    }
    return result;
}

QList<Question> DnsSdResolver::missingQuestions() const
{
    QList<Question> questions;
    QSet<QByteArray> askedHosts;
    for (const QByteArray &key : m_order) {
        const Instance &instance = m_instances.value(key);
        if (!instance.hasSrv) {
            questions.append(Question{instance.name, TypeSRV});
        }
        if (!instance.hasTxt) {
            questions.append(Question{instance.name, TypeTXT});
        }
        if (instance.hasSrv) {
            const QByteArray hostKey = nameKey(instance.host);
            if (m_hostAddresses.value(hostKey).isEmpty() && !askedHosts.contains(hostKey)) {
                askedHosts.insert(hostKey);
                questions.append(Question{instance.host, TypeA});
                questions.append(Question{instance.host, TypeAAAA});
            }
        }
    }
    return questions;
}

} // namespace Bloom::Dns
