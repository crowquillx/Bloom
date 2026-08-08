#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

struct ProviderAuthenticationRequest {
    QString endpoint;
    QByteArray body;
    QString refreshToken;
    QString profileId;
    QString profileToken;

    [[nodiscard]] bool isValid() const { return !endpoint.trimmed().isEmpty(); }
};

struct ProviderAuthenticationResult {
    QString accessToken;
    QString refreshToken;
    QString accountId;
    QString profileId;
    QString profileToken;
    QString username;
    // Relative lifetime reported by the provider. Negative means unspecified.
    qint64 expiresInSeconds = -1;

    // Full login responses must bind the token to a provider account.
    bool isValid() const
    {
        return !accessToken.isEmpty() && !accountId.isEmpty();
    }

    // Refresh responses rotate both credentials and do not need to repeat the account ID.
    [[nodiscard]] bool isValidRefresh() const
    {
        return !accessToken.isEmpty() && !refreshToken.isEmpty();
    }
};

struct ProviderProfileAuthenticationResult {
    // Parsed distinguishes a completed wrong-PIN response from malformed or unsupported data.
    bool responseParsed = false;
    bool valid = false;
    QString profileToken;

    [[nodiscard]] bool isValid() const
    {
        return responseParsed && valid && !profileToken.isEmpty();
    }

    [[nodiscard]] bool isIncorrectPin() const
    {
        return responseParsed && !valid;
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

    // Optional authentication flows return nullopt when the provider does not
    // implement the operation; an empty request must never mean success.
    virtual std::optional<ProviderAuthenticationRequest> createRefreshRequest(
        const QString &refreshToken) const
    {
        Q_UNUSED(refreshToken)
        return std::nullopt;
    }
    virtual ProviderAuthenticationResult parseRefreshResponse(const QByteArray &response) const
    {
        Q_UNUSED(response)
        return {};
    }
    virtual std::optional<ProviderAuthenticationRequest> createProfileLoginRequest(
        const QString &profileId, const QString &pin) const
    {
        Q_UNUSED(profileId)
        Q_UNUSED(pin)
        return std::nullopt;
    }
    virtual ProviderProfileAuthenticationResult parseProfileLoginResponse(
        const QByteArray &response) const
    {
        Q_UNUSED(response)
        return {};
    }
};
