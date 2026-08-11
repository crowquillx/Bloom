#pragma once

#include <QUrl>

class UpdateNetworkPolicy
{
  public:
    static bool isAllowedManifestUrl(const QUrl &url);
    static bool isAllowedAssetUrl(const QUrl &url, bool initialRequest);
    static QUrl resolvedRedirect(const QUrl &source, const QUrl &target);

  private:
    static bool isStrictHttpsUrl(const QUrl &url);
};
