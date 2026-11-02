#include "providers/silo/SiloLanDiscovery.h"

namespace SiloLanDiscovery {

namespace {
constexpr int MaxCandidateUrls = 4;
}

QByteArray serviceType()
{
    return QByteArrayLiteral("_silo._tcp.local");
}

std::optional<LanServerAdvert> advert(const QString &instanceLabel,
                                      const QMap<QByteArray, QByteArray> &txt,
                                      quint16 port,
                                      const QList<QHostAddress> &addresses)
{
    // `v` changes only when an existing key changes meaning; new keys are
    // added without a bump and ignored here.
    if (txt.value(QByteArrayLiteral("v")) != QByteArrayLiteral("1") || port == 0) {
        return std::nullopt;
    }
    const QString serverId =
        QString::fromUtf8(txt.value(QByteArrayLiteral("id"))).trimmed();
    if (serverId.isEmpty()) {
        return std::nullopt;
    }
    LanServerAdvert result;
    result.providerKind = ProviderKind::Silo;
    result.serverId = serverId;
    result.name = instanceLabel.trimmed();
    for (const QHostAddress &address : addresses) {
        if (result.urls.size() == MaxCandidateUrls) {
            break;
        }
        // Only resolved A/AAAA addresses become URLs, never the SRV host
        // name, so a "localhost" target cannot reach this machine by name.
        if (!LanAddress::isUsableAddress(address)) {
            continue;
        }
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        // QUrl brackets an IPv6 host itself.
        url.setHost(address.toString());
        url.setPort(port);
        if (url.isValid() && !result.urls.contains(url)) {
            result.urls.append(url);
        }
    }
    if (result.urls.isEmpty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<ProviderServerIdentity> serverIdentity(const QJsonObject &wireIdentity)
{
    const QString serverId = wireIdentity.value(QStringLiteral("server_id")).toString().trimmed();
    if (serverId.isEmpty()) {
        return std::nullopt;
    }
    return ProviderServerIdentity{serverId, QString()};
}

QString brandingServerName(const QJsonObject &wireBranding)
{
    return wireBranding.value(QStringLiteral("server_name")).toString().trimmed();
}

} // namespace SiloLanDiscovery
