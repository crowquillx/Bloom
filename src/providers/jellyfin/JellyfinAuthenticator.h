#pragma once

#include "providers/IProviderAuthenticator.h"

class JellyfinAuthenticator final : public IProviderAuthenticator
{
public:
    ProviderAuthenticationRequest createLoginRequest(const QString &username,
                                                      const QString &password) const override;
    QString sessionValidationEndpoint(const QString &accountId) const override;
    ProviderAuthenticationResult parseLoginResponse(const QByteArray &response) const override;
};
