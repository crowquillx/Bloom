#include "providers/LanServerAdvert.h"

namespace LanAddress {

bool isUsableAddress(const QHostAddress &address)
{
    bool isIPv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIPv4);
    const QHostAddress normalized = isIPv4 ? QHostAddress(ipv4) : address;
    if (normalized.isNull() || normalized.isLoopback()) {
        return false;
    }
    if (normalized.protocol() == QAbstractSocket::IPv4Protocol) {
        return normalized != QHostAddress(QHostAddress::AnyIPv4);
    }
    return normalized != QHostAddress(QHostAddress::AnyIPv6) && !normalized.isLinkLocal();
}

bool isUsableHost(const QString &host)
{
    QString name = host.trimmed();
    if (name.startsWith(QLatin1Char('[')) && name.endsWith(QLatin1Char(']'))) {
        name = name.mid(1, name.size() - 2);
    }
    const QHostAddress literal(name);
    if (!literal.isNull()) {
        return isUsableAddress(literal);
    }
    while (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    name = name.toLower();
    return !name.isEmpty() && name != QStringLiteral("localhost")
        && !name.endsWith(QStringLiteral(".localhost"));
}

} // namespace LanAddress
