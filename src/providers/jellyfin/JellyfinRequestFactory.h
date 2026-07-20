#pragma once

#include "providers/IProviderRequestFactory.h"

class JellyfinRequestFactory final : public IProviderRequestFactory
{
public:
    QNetworkRequest createRequest(const ProviderRequestContext &context,
                                  const QString &endpoint) const override;
    QString redactedUrl(const QUrl &url) const override;
};
