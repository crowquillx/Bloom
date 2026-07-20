#pragma once

#include <QByteArray>
#include <QString>

struct ProviderAuthenticationRequest {
    QString endpoint;
    QByteArray body;
};

struct ProviderAuthenticationResult {
    QString accessToken;
    QString accountId;
    QString username;

    bool isValid() const
    {
        return !accessToken.isEmpty() && !accountId.isEmpty();
    }
};

/**
 * @brief Provider-owned authentication wire contract.
 *
 * Implementations own login/validation routes, request payloads, and response
 * parsing while AuthenticationService remains the stable QML-facing façade.
 */
class IProviderAuthenticator
{
public:
    virtual ~IProviderAuthenticator() = default;

    virtual ProviderAuthenticationRequest createLoginRequest(const QString &username,
                                                              const QString &password) const = 0;
    virtual QString sessionValidationEndpoint(const QString &accountId) const = 0;
    virtual ProviderAuthenticationResult parseLoginResponse(const QByteArray &response) const = 0;
};
