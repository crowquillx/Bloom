#pragma once

#include "providers/IProviderAdapter.h"
#include "providers/jellyfin/JellyfinCatalogProvider.h"
#include "providers/jellyfin/JellyfinAuthenticator.h"
#include "providers/jellyfin/JellyfinLanDiscovery.h"
#include "providers/jellyfin/JellyfinModelMapper.h"
#include "providers/jellyfin/JellyfinPlaybackProvider.h"
#include "providers/jellyfin/JellyfinRequestFactory.h"

class JellyfinProviderAdapter final : public IProviderAdapter
{
public:
    using IProviderAdapter::endpointFor;
    ProviderKind providerKind() const override { return ProviderKind::Jellyfin; }
    ProtocolMode protocolMode() const override { return ProtocolMode::Native; }
    const IProviderAuthenticator *authenticator() const override { return &m_authenticator; }
    const IProviderRequestFactory *requestFactory() const override { return &m_requestFactory; }
    const IPlaybackProvider *playbackProvider() const override { return &m_playbackProvider; }
    const ICatalogProvider *catalogProvider() const override { return &m_catalogProvider; }
    bool supportsCapability(ProviderCapability capability) const override
    {
        // Jellyfin's Next Up takes rewatching and a last-watched cutoff, its
        // resume list is a separate read, it recommends movies per library,
        // its personal rating is a like or a dislike, its played items are
        // read one library at a time, Bloom shuffles from its random item
        // reads, box sets are its collections, and its upcoming episodes
        // (/Shows/Upcoming) are its airing calendar.
        return capability == ProviderCapability::NextUpOptions
            || capability == ProviderCapability::ResumeItems
            || capability == ProviderCapability::MovieRecommendations
            || capability == ProviderCapability::LikeRatings
            || capability == ProviderCapability::LibraryScopedHistory
            || capability == ProviderCapability::LocalShuffle
            || capability == ProviderCapability::Collections
            || capability == ProviderCapability::AiringCalendar;
    }
    std::optional<QString> endpointFor(
        ProviderRoute route, const ProviderRouteContext &context) const override
    {
        Q_UNUSED(context)
        switch (route) {
        case ProviderRoute::Health:
        case ProviderRoute::ServerIdentity:
            return QStringLiteral("/System/Info/Public");
        case ProviderRoute::CallerLogout:
            return QStringLiteral("/Sessions/Logout");
        default:
            return std::nullopt;
        }
    }
    std::optional<ProviderServerIdentity> mapServerIdentity(
        const QJsonObject &wireIdentity) const override
    {
        return JellyfinLanDiscovery::publicSystemInfoIdentity(wireIdentity);
    }
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
    std::optional<QList<MediaSegmentInfo>> mapMediaSegments(
        const QString &itemId, const QJsonObject &wireSegments) const override
    {
        return JellyfinModelMapper::mediaSegments(itemId, wireSegments);
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

    QVariantMap mapPerson(const QJsonObject &wirePerson,
                          const QString &connectionId) const override
    {
        return JellyfinModelMapper::person(wirePerson, connectionId);
    }

    QVariantList mapPeople(const QJsonArray &wirePeople,
                           const QString &connectionId) const override
    {
        return JellyfinModelMapper::personList(wirePeople, connectionId);
    }
    QVariantList mapCollections(const QJsonArray &wireCollections,
                                const QString &connectionId) const override
    {
        return JellyfinModelMapper::collections(wireCollections, connectionId);
    }
    QVariantList mapAirings(const QJsonArray &wireAirings,
                            const QString &connectionId) const override
    {
        return JellyfinModelMapper::upcomingAirings(wireAirings, connectionId);
    }
    // /Shows/Upcoming lists only virtual episodes a metadata provider
    // created (TMDb's "Create unaired (upcoming) episodes"), off by default.
    bool airingCalendarNeedsProbe() const override { return true; }
    QVariantList mapChaptersFromItem(const QJsonObject &wireItem,
                                     const QString &connectionId,
                                     const QString &itemId) const override
    {
        return JellyfinModelMapper::chapters(
            wireItem.value(QStringLiteral("Chapters")).toArray(), connectionId, itemId);
    }

    QVariantList mapExtras(const QJsonArray &wireExtras,
                           const QString &connectionId,
                           const QString &parentId) const override
    {
        return JellyfinModelMapper::extras(wireExtras, connectionId, parentId);
    }

private:
    JellyfinCatalogProvider m_catalogProvider;
    JellyfinAuthenticator m_authenticator;
    JellyfinPlaybackProvider m_playbackProvider;
    JellyfinRequestFactory m_requestFactory;
};
