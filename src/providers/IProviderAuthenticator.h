#pragma once

#include <QByteArray>
#include <QDateTime>
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

    // Refresh responses may omit refresh_token when the existing token remains valid.
    [[nodiscard]] bool isValidRefresh() const
    {
        return !accessToken.isEmpty();
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

/** Which sign-in methods a server offers before anyone signs in. */
struct ProviderSignInMethods {
    // Absent when the server does not say; callers then keep the password form.
    std::optional<bool> passwordSignIn;
};

/** Whether a server serves device authorization ("sign in with a code"). */
struct ProviderDeviceAuthorizationCapability {
    bool available = false;
    // The device may withdraw its own pending request.
    bool cancel = false;
};

/** A started device authorization: the code a person approves elsewhere. */
struct ProviderDeviceAuthorization {
    // Secret the device polls with. Never shown, logged, or persisted.
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    // verificationUri with the user code prefilled, when the provider has one.
    QString verificationUriComplete;
    qint64 expiresInSeconds = -1;
    qint64 intervalSeconds = -1;
    // Server clock; only used with expiresInSeconds to correct for skew.
    QDateTime expiresAt;

    [[nodiscard]] bool isValid() const
    {
        return !deviceCode.isEmpty() && !userCode.isEmpty() && !verificationUri.isEmpty()
            && expiresInSeconds > 0 && intervalSeconds > 0;
    }
};

struct ProviderDeviceAuthorizationPoll {
    enum class Status {
        Invalid,
        Pending,
        Approved,
        Denied,
        Expired,
        Canceled,
        // The tokens were already collected (for example a lost response).
        Consumed
    };

    Status status = Status::Invalid;
    // An approver has looked the request up and is deciding.
    bool opened = false;
    qint64 pollAfterSeconds = -1;
    // Server clock; a pending request's current expiry when the provider moves it.
    QDateTime expiresAt;
    // Present only with Approved; validated like a password login response.
    ProviderAuthenticationResult authentication;
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
    virtual ProviderAuthenticationRequest createLoginRequest(
        const QString &username,
        const QString &password,
        const QString &provider) const
    {
        Q_UNUSED(provider)
        return createLoginRequest(username, password);
    }

    virtual ProviderAuthenticationResult parseSessionValidationResponse(
        const QByteArray &response) const
    {
        Q_UNUSED(response)
        return {};
    }
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

    // Public sign-in discovery, read before anyone signs in (GET, no bearer).
    virtual std::optional<QString> signInMethodsEndpoint() const { return std::nullopt; }
    virtual std::optional<ProviderSignInMethods> parseSignInMethods(
        const QByteArray &response) const
    {
        Q_UNUSED(response)
        return std::nullopt;
    }

    // Optional device authorization ("sign in with a code"). Requests are
    // unauthenticated POSTs; a provider whose poll is a different shape (for
    // example Jellyfin Quick Connect's separate token exchange) extends this
    // capability rather than reusing the login flow.
    virtual std::optional<QString> deviceAuthorizationCapabilityEndpoint() const
    {
        return std::nullopt;
    }
    virtual std::optional<ProviderDeviceAuthorizationCapability>
    parseDeviceAuthorizationCapability(const QByteArray &response) const
    {
        Q_UNUSED(response)
        return std::nullopt;
    }
    virtual std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationRequest(
        const QString &deviceName, const QString &devicePlatform) const
    {
        Q_UNUSED(deviceName)
        Q_UNUSED(devicePlatform)
        return std::nullopt;
    }
    virtual std::optional<ProviderDeviceAuthorization> parseDeviceAuthorizationResponse(
        const QByteArray &response) const
    {
        Q_UNUSED(response)
        return std::nullopt;
    }
    virtual std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationPollRequest(
        const QString &deviceCode) const
    {
        Q_UNUSED(deviceCode)
        return std::nullopt;
    }
    virtual ProviderDeviceAuthorizationPoll parseDeviceAuthorizationPollResponse(
        const QByteArray &response) const
    {
        Q_UNUSED(response)
        return {};
    }
    virtual std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationCancelRequest(
        const QString &deviceCode) const
    {
        Q_UNUSED(deviceCode)
        return std::nullopt;
    }
};
