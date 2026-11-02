#include "network/discovery/MdnsServerBrowser.h"

#include "utils/BloomLogging.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

#include <algorithm>

namespace {

const QHostAddress &groupIPv4()
{
    static const QHostAddress group(QStringLiteral("224.0.0.251"));
    return group;
}

const QHostAddress &groupIPv6()
{
    static const QHostAddress group(QStringLiteral("ff02::fb"));
    return group;
}

// Interfaces that carry multicast to other hosts. Point-to-point links (VPN
// and overlay tunnels) carry none.
bool eligible(const QNetworkInterface &interface)
{
    const auto flags = interface.flags();
    return interface.isValid() && flags.testFlag(QNetworkInterface::IsUp)
        && flags.testFlag(QNetworkInterface::IsRunning)
        && flags.testFlag(QNetworkInterface::CanMulticast)
        && !flags.testFlag(QNetworkInterface::IsLoopBack)
        && !flags.testFlag(QNetworkInterface::IsPointToPoint);
}

bool hasAddress(const QNetworkInterface &interface, QAbstractSocket::NetworkLayerProtocol family)
{
    const auto entries = interface.addressEntries();
    return std::any_of(entries.cbegin(), entries.cend(), [family](const QNetworkAddressEntry &entry) {
        return entry.ip().protocol() == family;
    });
}

// One mDNS packet is at most 9000 bytes (RFC 6762 §17).
constexpr qint64 MaxPacketBytes = 9000;
// Bounds the datagrams one readyRead drains, so a flood cannot hold the
// event loop.
constexpr int MaxDatagramsPerRead = 64;

} // namespace

MdnsServerBrowser::MdnsServerBrowser(const QByteArray &serviceType, AdvertMapper mapper,
                                     QObject *parent)
    : LanServerBrowser(parent)
    , m_resolver(Bloom::Dns::nameFromDotted(serviceType))
    , m_mapper(std::move(mapper))
{
}

MdnsServerBrowser::~MdnsServerBrowser()
{
    stop();
}

QUdpSocket *MdnsServerBrowser::openSocket(const QHostAddress &any, const QHostAddress &group,
                                          bool ipv6)
{
    auto *socket = new QUdpSocket(this);
    // SO_REUSEADDR: share 5353 with the system's mDNS daemon.
    if (!socket->bind(any, MdnsPort,
                      QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        qCInfo(lcDiscovery) << "mDNS: cannot share UDP" << MdnsPort << "for"
                            << (ipv6 ? "IPv6" : "IPv4") << ":" << socket->errorString();
        delete socket;
        return nullptr;
    }
    socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 255);
    bool joinedAny = false;
    for (Membership &membership : m_memberships) {
        const auto family = ipv6 ? QAbstractSocket::IPv6Protocol : QAbstractSocket::IPv4Protocol;
        if (!hasAddress(membership.interface, family)) {
            continue;
        }
        // Queries go out on every interface with an address of this family;
        // sending needs no membership. Replies arrive through the join.
        const bool joined = socket->joinMulticastGroup(group, membership.interface);
        (ipv6 ? membership.ipv6 : membership.ipv4) = true;
        joinedAny = joinedAny || joined;
        if (!joined) {
            qCDebug(lcDiscovery) << "mDNS: join failed on" << membership.interface.name()
                                 << socket->errorString();
        }
    }
    if (!joinedAny) {
        qCDebug(lcDiscovery) << "mDNS: no" << (ipv6 ? "IPv6" : "IPv4")
                             << "interface joined the group";
    }
    connect(socket, &QUdpSocket::readyRead, this, [this, socket]() { readPending(socket); });
    return socket;
}

bool MdnsServerBrowser::start()
{
    stop();
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (eligible(interface)) {
            m_memberships.append(Membership{interface});
        }
    }
    if (m_memberships.isEmpty()) {
        qCInfo(lcDiscovery) << "mDNS: no multicast-capable network interface";
        return false;
    }
    m_socket4 = openSocket(QHostAddress(QHostAddress::AnyIPv4), groupIPv4(), false);
    m_socket6 = openSocket(QHostAddress(QHostAddress::AnyIPv6), groupIPv6(), true);
    if (!m_socket4 && !m_socket6) {
        m_memberships.clear();
        return false;
    }
    return true;
}

void MdnsServerBrowser::closeSocket(QPointer<QUdpSocket> &socket)
{
    if (!socket) {
        return;
    }
    QUdpSocket *closing = socket.data();
    socket.clear();
    // stop() may run from inside this socket's readyRead.
    disconnect(closing, nullptr, this, nullptr);
    closing->close();
    closing->deleteLater();
}

void MdnsServerBrowser::stop()
{
    closeSocket(m_socket4);
    closeSocket(m_socket6);
    m_memberships.clear();
    m_resolver.clear();
    m_reported.clear();
}

QByteArray MdnsServerBrowser::buildQuery() const
{
    // QM questions: Silo answers by multicast to the group either way.
    QList<Bloom::Dns::Question> questions{
        Bloom::Dns::Question{m_resolver.serviceType(), Bloom::Dns::TypePTR}};
    const auto followUps = m_resolver.missingQuestions();
    for (const Bloom::Dns::Question &question : followUps) {
        if (questions.size() == MaxQuestionsPerQuery) {
            break;
        }
        questions.append(question);
    }
    return Bloom::Dns::encodeQuery(questions);
}

void MdnsServerBrowser::query()
{
    const QByteArray packet = buildQuery();
    if (packet.isEmpty()) {
        return;
    }
    for (const Membership &membership : std::as_const(m_memberships)) {
        if (m_socket4 && membership.ipv4) {
            m_socket4->setMulticastInterface(membership.interface);
            m_socket4->writeDatagram(packet, groupIPv4(), MdnsPort);
        }
        if (m_socket6 && membership.ipv6) {
            QHostAddress group = groupIPv6();
            group.setScopeId(membership.interface.name());
            m_socket6->setMulticastInterface(membership.interface);
            m_socket6->writeDatagram(packet, group, MdnsPort);
        }
    }
}

void MdnsServerBrowser::readPending(QUdpSocket *socket)
{
    for (int read = 0; read < MaxDatagramsPerRead && socket->hasPendingDatagrams(); ++read) {
        const QNetworkDatagram datagram = socket->receiveDatagram(MaxPacketBytes);
        // mDNS responses come from port 5353 (RFC 6762 §6).
        if (datagram.isValid() && datagram.senderPort() == MdnsPort) {
            handlePacket(datagram.data());
        }
        if (socket != m_socket4 && socket != m_socket6) {
            return; // stopped while handling
        }
    }
    if (socket->hasPendingDatagrams()) {
        QMetaObject::invokeMethod(
            this,
            [this, guarded = QPointer<QUdpSocket>(socket)]() {
                if (guarded && (guarded == m_socket4 || guarded == m_socket6)) {
                    readPending(guarded);
                }
            },
            Qt::QueuedConnection);
    }
}

void MdnsServerBrowser::handlePacket(const QByteArray &packet)
{
    const auto message = Bloom::Dns::decode(packet);
    if (!message || !message->isResponse()) {
        return;
    }
    m_resolver.absorb(*message);
    const auto services = m_resolver.resolved();
    for (const Bloom::Dns::ServiceInstance &service : services) {
        const auto advert = m_mapper ? m_mapper(service) : std::nullopt;
        if (!advert) {
            continue;
        }
        QByteArray signature = advert->serverId.toUtf8();
        for (const QUrl &url : advert->urls) {
            signature += '\n' + url.toEncoded();
        }
        if (m_reported.contains(signature) || m_reported.size() >= MaxReported) {
            continue;
        }
        m_reported.insert(signature);
        emit advertFound(*advert);
    }
}
