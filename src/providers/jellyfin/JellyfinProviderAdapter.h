#pragma once

#include "providers/IProviderAdapter.h"
#include "providers/jellyfin/JellyfinAuthenticator.h"
#include "providers/jellyfin/JellyfinRequestFactory.h"

class JellyfinProviderAdapter final : public IProviderAdapter
{
public:
    ProviderKind providerKind() const override { return ProviderKind::Jellyfin; }
    ProtocolMode protocolMode() const override { return ProtocolMode::Native; }
    const IProviderAuthenticator *authenticator() const override { return &m_authenticator; }
    const IProviderRequestFactory *requestFactory() const override { return &m_requestFactory; }

private:
    JellyfinAuthenticator m_authenticator;
    JellyfinRequestFactory m_requestFactory;
};
