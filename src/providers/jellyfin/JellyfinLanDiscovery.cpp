#include "providers/jellyfin/JellyfinLanDiscovery.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

namespace JellyfinLanDiscovery {

namespace {

constexpr int MaxReplyBytes = 4096;
constexpr int MaxCandidateUrls = 4;

// An advertised address as a base URL: a full http(s) URL keeps its scheme,
// host, port and base path (Jellyfin's configurable BaseUrl); a bare
// "host[:port]" is plain HTTP on that port or 8096. A loopback, unspecified
// or localhost host is refused: the reply is only a claim, and Bloom would
// otherwise send its identity read to a service on this machine.
std::optional<QUrl> advertisedUrl(const QString &address)
{
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    QUrl url = QUrl(trimmed.contains(QStringLiteral("://")) ? trimmed
                                                             : QStringLiteral("http://") + trimmed,
                    QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || !LanAddress::isUsableHost(url.host())
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        || !url.userInfo().isEmpty()) {
        return std::nullopt;
    }
    QUrl base;
    base.setScheme(scheme);
    base.setHost(url.host());
    const int defaultPort = trimmed.contains(QStringLiteral("://")) ? -1 : DefaultHttpPort;
    base.setPort(url.port(defaultPort));
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    base.setPath(path);
    return base;
}

} // namespace

QByteArray probe()
{
    return QByteArrayLiteral("Who is JellyfinServer?");
}

std::optional<LanServerAdvert> advert(const QByteArray &reply, const QHostAddress &sender)
{
    if (reply.size() > MaxReplyBytes) {
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(reply, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject info = document.object();
    const QString serverId = info.value(QStringLiteral("Id")).toString().trimmed();
    if (serverId.isEmpty()) {
        return std::nullopt;
    }
    LanServerAdvert result;
    result.providerKind = ProviderKind::Jellyfin;
    result.serverId = serverId;
    result.name = info.value(QStringLiteral("Name")).toString().trimmed();

    auto add = [&result](const QUrl &url) {
        if (result.urls.size() < MaxCandidateUrls && !result.urls.contains(url)) {
            result.urls.append(url);
        }
    };
    std::optional<QUrl> advertised =
        advertisedUrl(info.value(QStringLiteral("Address")).toString());
    if (advertised) {
        add(*advertised);
    }
    if (auto endpoint = advertisedUrl(info.value(QStringLiteral("EndpointAddress")).toString())) {
        add(*endpoint);
    }
    // The address the reply came from reaches the server from here, on the
    // advertised port when Address is plain HTTP (an HTTPS port does not
    // serve HTTP, so 8096 then), always under the advertised base path.
    bool isIPv4 = false;
    const quint32 senderIPv4 = sender.toIPv4Address(&isIPv4);
    const QHostAddress source = isIPv4 ? QHostAddress(senderIPv4) : sender;
    if (LanAddress::isUsableAddress(source)
        && source.protocol() == QAbstractSocket::IPv4Protocol) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(source.toString());
        const bool plainHttp = advertised && advertised->scheme() == QStringLiteral("http");
        url.setPort(plainHttp ? advertised->port(DefaultHttpPort) : DefaultHttpPort);
        if (advertised) {
            url.setPath(advertised->path());
        }
        add(url);
    }
    if (result.urls.isEmpty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<ProviderServerIdentity> publicSystemInfoIdentity(const QJsonObject &wireInfo)
{
    const QString serverId = wireInfo.value(QStringLiteral("Id")).toString().trimmed();
    if (serverId.isEmpty()) {
        return std::nullopt;
    }
    return ProviderServerIdentity{
        serverId, wireInfo.value(QStringLiteral("ServerName")).toString().trimmed()};
}

} // namespace JellyfinLanDiscovery
