#include "network/discovery/JellyfinServerBrowser.h"

#include "providers/jellyfin/JellyfinLanDiscovery.h"
#include "utils/BloomLogging.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QUdpSocket>

namespace {
constexpr qint64 MaxReplyBytes = 4096;
constexpr int MaxDatagramsPerRead = 64;
}

JellyfinServerBrowser::JellyfinServerBrowser(QObject *parent)
    : LanServerBrowser(parent)
{
}

JellyfinServerBrowser::~JellyfinServerBrowser()
{
    stop();
}

bool JellyfinServerBrowser::start()
{
    stop();
    auto *socket = new QUdpSocket(this);
    if (!socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        qCInfo(lcDiscovery) << "Jellyfin discovery: cannot open a UDP socket:"
                            << socket->errorString();
        delete socket;
        return false;
    }
    m_socket = socket;
    connect(socket, &QUdpSocket::readyRead, this, &JellyfinServerBrowser::readPending);
    return true;
}

void JellyfinServerBrowser::stop()
{
    if (m_socket) {
        QUdpSocket *closing = m_socket.data();
        m_socket.clear();
        // stop() may run from inside this socket's readyRead.
        disconnect(closing, nullptr, this, nullptr);
        closing->close();
        closing->deleteLater();
    }
    m_reported.clear();
}

void JellyfinServerBrowser::query()
{
    if (!m_socket) {
        return;
    }
    const QByteArray probe = JellyfinLanDiscovery::probe();
    QList<QHostAddress> targets{QHostAddress(QHostAddress::Broadcast)};
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || !flags.testFlag(QNetworkInterface::CanBroadcast)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const auto entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress broadcast = entry.broadcast();
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !broadcast.isNull()
                && !targets.contains(broadcast)) {
                targets.append(broadcast);
            }
        }
    }
    for (const QHostAddress &target : std::as_const(targets)) {
        m_socket->writeDatagram(probe, target, JellyfinLanDiscovery::DiscoveryPort);
    }
}

void JellyfinServerBrowser::readPending()
{
    QUdpSocket *socket = m_socket.data();
    for (int read = 0; socket && read < MaxDatagramsPerRead && socket->hasPendingDatagrams();
         ++read) {
        const QNetworkDatagram datagram = socket->receiveDatagram(MaxReplyBytes);
        if (datagram.isValid() && datagram.senderPort() == JellyfinLanDiscovery::DiscoveryPort) {
            handleReply(datagram.data(), datagram.senderAddress());
        }
        if (socket != m_socket) {
            return; // stopped while handling
        }
    }
    if (socket && socket->hasPendingDatagrams()) {
        QMetaObject::invokeMethod(this, &JellyfinServerBrowser::readPending,
                                  Qt::QueuedConnection);
    }
}

void JellyfinServerBrowser::handleReply(const QByteArray &reply, const QHostAddress &sender)
{
    const auto advert = JellyfinLanDiscovery::advert(reply, sender);
    if (!advert) {
        return;
    }
    QByteArray signature = advert->serverId.toUtf8();
    for (const QUrl &url : advert->urls) {
        signature += '\n' + url.toEncoded();
    }
    if (m_reported.contains(signature) || m_reported.size() >= MaxReported) {
        return;
    }
    m_reported.insert(signature);
    emit advertFound(*advert);
}
