#pragma once

#include "providers/IProviderAdapter.h"
#include "providers/jellyfin/JellyfinAuthenticator.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "providers/jellyfin/JellyfinRequestFactory.h"

class JellyfinProviderAdapter final : public IProviderAdapter
{
public:
    ProviderKind providerKind() const override { return ProviderKind::Jellyfin; }
    ProtocolMode protocolMode() const override { return ProtocolMode::Native; }
    const IProviderAuthenticator *authenticator() const override { return &m_authenticator; }
    const IProviderRequestFactory *requestFactory() const override { return &m_requestFactory; }
    const IPlaybackProvider *playbackProvider() const override { return &m_playbackProvider; }
    QVariantMap mapMediaItem(const QJsonObject &wireItem,
                             const QString &connectionId) const override
    {
        return JellyfinModelMapper::mediaItem(wireItem, connectionId);
    }
    QVariantList mapMediaItems(const QJsonArray &wireItems,
                               const QString &connectionId) const override
    {
        return JellyfinModelMapper::mediaItems(wireItems, connectionId);
    }

private:
    JellyfinAuthenticator m_authenticator;
    JellyfinPlaybackProvider m_playbackProvider;
    JellyfinRequestFactory m_requestFactory;
};
