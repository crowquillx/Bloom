#include "UpdateNetworkPolicy.h"

#include <QSet>

namespace
{

const QSet<QString> &assetRedirectHosts()
{
    static const QSet<QString> hosts{
        QStringLiteral("release-assets.githubusercontent.com"),
        QStringLiteral("objects.githubusercontent.com"),
        QStringLiteral("github-releases.githubusercontent.com"),
    };
    return hosts;
}

} // namespace

bool UpdateNetworkPolicy::isStrictHttpsUrl(const QUrl &url)
{
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment() &&
           (url.port() == -1 || url.port() == 443);
}

bool UpdateNetworkPolicy::isAllowedManifestUrl(const QUrl &url)
{
    return isStrictHttpsUrl(url) &&
           url.host().compare(QStringLiteral("raw.githubusercontent.com"), Qt::CaseInsensitive) == 0 &&
           url.path().startsWith(QStringLiteral("/crowquillx/Bloom/"));
}

bool UpdateNetworkPolicy::isAllowedAssetUrl(const QUrl &url, bool initialRequest)
{
    if (!isStrictHttpsUrl(url))
    {
        return false;
    }

    const QString host = url.host().toLower();
    if (host == QStringLiteral("github.com"))
    {
        return url.path().startsWith(QStringLiteral("/crowquillx/Bloom/releases/download/"));
    }
    return !initialRequest && assetRedirectHosts().contains(host);
}

QUrl UpdateNetworkPolicy::resolvedRedirect(const QUrl &source, const QUrl &target)
{
    if (target.isEmpty())
    {
        return {};
    }
    return source.resolved(target);
}
