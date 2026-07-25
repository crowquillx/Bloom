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
    PlaybackInfoResponse mapPlaybackInfo(
        const QJsonObject &wirePlaybackInfo) const override
    {
        return JellyfinModelMapper::playbackInfo(wirePlaybackInfo);
    }
    ProviderItemsResponseParser itemsResponseParser() const override
    {
        return [](const QByteArray &wireResponse, const QString &parentId) {
            return JellyfinModelMapper::itemsResponse(wireResponse, parentId);
        };
    }
    TrickplayTileInfoMap mapTrickplayInfo(
        const QJsonObject &wireItem) const override
    {
        return JellyfinModelMapper::trickplayInfo(wireItem);
    }
    QList<MediaSegmentInfo> mapIntroSkipperSegments(
        const QString &itemId, const QJsonObject &wireSegments) const override
    {
        return JellyfinModelMapper::introSkipperSegments(itemId, wireSegments);
    }
    QVariantList mapRemoteSessions(const QJsonArray &wireSessions,
                                   const QString &connectionId) const override
    {
        return JellyfinModelMapper::remoteSessions(wireSessions, connectionId);
    }
    QString mapLibraryIdFromAncestors(
        const QJsonArray &wireAncestors) const override
    {
        return JellyfinModelMapper::libraryIdFromAncestors(wireAncestors);
    }
    QVariantMap mapFilterOptions(const QJsonObject &wireFilters) const override
    {
        return JellyfinModelMapper::filterOptions(wireFilters);
    }
    QStringList mapNamedItems(const QJsonObject &wireItems) const override
    {
        return JellyfinModelMapper::namedItems(wireItems);
    }
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
    QVariantList mapChaptersFromItem(const QJsonObject &wireItem,
                                     const QString &connectionId,
                                     const QString &itemId) const override
    {
        return JellyfinModelMapper::chapters(
            wireItem.value(QStringLiteral("Chapters")).toArray(), connectionId, itemId);
    }

private:
    JellyfinAuthenticator m_authenticator;
    JellyfinPlaybackProvider m_playbackProvider;
    JellyfinRequestFactory m_requestFactory;
};
