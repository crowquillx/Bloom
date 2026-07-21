#pragma once

#include "providers/ServerConnection.h"

class IPlaybackProvider;
class IProviderAuthenticator;
class IProviderRequestFactory;

/**
 * @brief Provider implementation bundle consumed by stable application façades.
 *
 * The adapter owns provider wire behavior while Bloom's QML-facing services own
 * UI-compatible state and signals.
 */
class IProviderAdapter
{
public:
    virtual ~IProviderAdapter() = default;

    virtual ProviderKind providerKind() const = 0;
    virtual ProtocolMode protocolMode() const = 0;
    virtual const IProviderAuthenticator *authenticator() const = 0;
    virtual const IProviderRequestFactory *requestFactory() const = 0;
    virtual const IPlaybackProvider *playbackProvider() const = 0;
};
