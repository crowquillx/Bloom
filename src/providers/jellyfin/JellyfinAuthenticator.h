#pragma once

#include "providers/IProviderAuthenticator.h"

/**
 * Jellyfin password login, session validation, and Quick Connect, which is
 * Jellyfin's device authorization: the device shows a code, a signed-in
 * Jellyfin app authorizes it, and the device exchanges its secret once for an
 * ordinary login response.
 */
class JellyfinAuthenticator final : public IProviderAuthenticator
{
public:
    // Jellyfin forgets a Quick Connect request ten minutes after it starts.
    static constexpr qint64 kQuickConnectLifetimeSeconds = 10 * 60;
    static constexpr qint64 kQuickConnectPollSeconds = 5;

    ProviderAuthenticationRequest createLoginRequest(const QString &username,
                                                      const QString &password) const override;
    QString sessionValidationEndpoint(const QString &accountId) const override;
    ProviderAuthenticationResult parseLoginResponse(const QByteArray &response) const override;
    ProviderAuthenticationResult parseSessionValidationResponse(
        const QByteArray &response) const override;

    std::optional<QString> deviceAuthorizationCapabilityEndpoint() const override;
    std::optional<ProviderDeviceAuthorizationCapability> parseDeviceAuthorizationCapability(
        const QByteArray &response) const override;
    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationRequest(
        const QString &deviceName, const QString &devicePlatform) const override;
    std::optional<ProviderDeviceAuthorization> parseDeviceAuthorizationResponse(
        const QByteArray &response) const override;
    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationPollRequest(
        const QString &deviceCode) const override;
    ProviderDeviceAuthorizationPoll parseDeviceAuthorizationPollResponse(
        const QByteArray &response) const override;
    std::optional<ProviderAuthenticationRequest> createDeviceAuthorizationExchangeRequest(
        const QString &deviceCode) const override;
};
