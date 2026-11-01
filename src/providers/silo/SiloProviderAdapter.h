#pragma once

#include "providers/IProviderAdapter.h"
#include "providers/silo/SiloAuthenticator.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloModelMapper.h"
#include "providers/silo/SiloPlaybackProvider.h"
#include "providers/silo/SiloRequestFactory.h"
#include <QUrl>

class SiloProviderAdapter final : public IProviderAdapter
{
public:
    using IProviderAdapter::endpointFor;
    ProviderKind providerKind() const override { return ProviderKind::Silo; }
    ProtocolMode protocolMode() const override { return ProtocolMode::Native; }
    const IProviderAuthenticator *authenticator() const override { return &m_authenticator; }
    const IProviderRequestFactory *requestFactory() const override { return &m_requestFactory; }

    const IPlaybackProvider *playbackProvider() const override { return &m_playbackProvider; }
    const ICatalogProvider *catalogProvider() const override { return &m_catalogProvider; }

    PlaybackInfoResponse mapPlaybackInfo(const QJsonObject &wirePlaybackInfo) const override
    {
        return SiloModelMapper::playbackInfo(wirePlaybackInfo);
    }

    ProviderItemsResponseParser itemsResponseParser() const override
    {
        return [](const QByteArray &wireResponse, const QString &parentId) {
            return SiloModelMapper::itemsResponse(wireResponse, parentId);
        };
    }

    TrickplayTileInfoMap mapTrickplayInfo(const QJsonObject &wireItem) const override
    {
        return SiloModelMapper::trickplayInfo(wireItem);
    }

    QList<MediaSegmentInfo> mapIntroSkipperSegments(
        const QString &itemId, const QJsonObject &wireSegments) const override
    {
        return SiloModelMapper::mediaSegments(itemId, wireSegments);
    }

    QVariantList mapRemoteSessions(const QJsonArray &wireSessions,
                                   const QString &connectionId) const override
    {
        Q_UNUSED(wireSessions)
        Q_UNUSED(connectionId)
        // Auth sessions are mapped through mapAuthSessions; playback sessions are unsupported.
        return {};
    }

    QString mapLibraryIdFromAncestors(const QJsonArray &wireAncestors) const override
    {
        Q_UNUSED(wireAncestors)
        return {};
    }

    QVariantMap mapFilterOptions(const QJsonObject &wireFilters) const override
    {
        return SiloModelMapper::filterOptions(wireFilters);
    }

    QStringList mapNamedItems(const QJsonObject &wireItems) const override
    {
        return SiloModelMapper::namedItems(wireItems);
    }

    QVariantMap mapMediaItem(const QJsonObject &wireItem,
                             const QString &connectionId) const override
    {
        // A single item is always a catalog item detail.
        return SiloModelMapper::mediaDetail(wireItem, connectionId);
    }

    QVariantList mapMediaItems(const QJsonArray &wireItems,
                               const QString &connectionId) const override
    {
        return SiloModelMapper::mediaItems(wireItems, connectionId);
    }

    QVariantMap mapPerson(const QJsonObject &wirePerson,
                          const QString &connectionId) const override
    {
        return SiloModelMapper::person(wirePerson, connectionId);
    }

    QVariantList mapPeople(const QJsonArray &wirePeople,
                           const QString &connectionId) const override
    {
        return SiloModelMapper::personList(wirePeople, connectionId);
    }

    QVariantList mapChaptersFromItem(const QJsonObject &wireItem,
                                     const QString &connectionId,
                                     const QString &itemId) const override
    {
        return SiloModelMapper::chaptersFromItem(wireItem, connectionId, itemId);
    }

    QVariantList mapExtras(const QJsonArray &wireExtras,
                           const QString &connectionId,
                           const QString &parentId) const override
    {
        return SiloModelMapper::extras(wireExtras, connectionId, parentId);
    }

    bool supportsCapability(ProviderCapability capability) const override
    {
        return implementedCapabilities().testFlag(capability);
    }

    std::optional<QString> endpointFor(
        ProviderRoute route, const ProviderRouteContext &context) const override
    {
        switch (route) {
        case ProviderRoute::Health:
            return QStringLiteral("/health");
        case ProviderRoute::Ready:
            return QStringLiteral("/ready");
        case ProviderRoute::SystemInfo:
            return QStringLiteral("/api/v2/system/info");
        case ProviderRoute::CallerLogout:
            return QStringLiteral("/api/v2/auth/logout");
        case ProviderRoute::Profiles:
            return QStringLiteral("/api/v2/profiles");
        case ProviderRoute::VerifyProfilePin:
            return endpointWithId(QStringLiteral("/api/v2/profiles/%1/verify-pin"),
                                  context.profileId);
        case ProviderRoute::AuthSessions:
            return QStringLiteral("/api/v2/auth/sessions");
        case ProviderRoute::RevokeAuthSession:
            return endpointWithId(QStringLiteral("/api/v2/auth/sessions/%1"),
                                  context.sessionId);
        case ProviderRoute::CatalogItems:
            return QStringLiteral("/api/v2/catalog");
        case ProviderRoute::CatalogItem:
            return endpointWithId(QStringLiteral("/api/v2/catalog/items/%1"),
                                  context.itemId);
        case ProviderRoute::NativeState:
            // Detail is the authoritative profile-scoped read for user state.
            return endpointWithId(QStringLiteral("/api/v2/catalog/items/%1"),
                                  context.itemId);
        case ProviderRoute::UpdateNativeState:
            return endpointWithId(QStringLiteral("/api/v2/watched/%1"),
                                  context.itemId);
        case ProviderRoute::MediaSegments:
            return context.fileId.isEmpty()
                ? endpointWithId(QStringLiteral("/api/v2/markers/items/%1"),
                                 context.itemId)
                : endpointWithId(QStringLiteral("/api/v2/markers/files/%1"),
                                 context.fileId);
        case ProviderRoute::PlaybackInfo:
            return endpointWithId(QStringLiteral("/api/v2/watch/%1"),
                                  context.itemId);
        case ProviderRoute::PlaybackCapability:
            return QStringLiteral("/api/v2/playback/capabilities");
        }
        return std::nullopt;
    }

    std::optional<ProviderDetectionResult> mapDetectionResult(
        const QJsonObject &wireResult) const override
    {
        if (wireResult.value(QStringLiteral("api_major")).toInt() != 2
            || !wireResult.value(QStringLiteral("links")).isObject()) {
            return std::nullopt;
        }

        ProviderDetectionResult result;
        result.providerKind = ProviderKind::Silo;
        result.protocolMode = ProtocolMode::Native;
        result.capabilities = implementedCapabilities();
        return result;
    }

    std::optional<QList<ProviderProfile>> mapProfiles(
        const QJsonArray &wireProfiles) const override
    {
        return SiloModelMapper::profiles(wireProfiles);
    }

    std::optional<QList<ProviderAuthSession>> mapAuthSessions(
        const QJsonArray &wireSessions) const override
    {
        return SiloModelMapper::authSessions(wireSessions);
    }

    std::optional<QVariantMap> mapNativeState(
        const QJsonObject &wireState, const QString &connectionId) const override
    {
        const QVariantMap state = SiloModelMapper::nativeState(wireState, connectionId);
        return state.isEmpty() ? std::nullopt : std::optional<QVariantMap>(state);
    }

    std::optional<QList<MediaSegmentInfo>> mapMediaSegments(
        const QString &itemId, const QJsonObject &wireSegments) const override
    {
        return SiloModelMapper::mediaSegments(itemId, wireSegments);
    }

    std::optional<Bloom::PosterBadgeLayout> mapPosterBadgeLayout(
        const QJsonObject &wireSettings, const QJsonObject &wireDefaults) const override
    {
        return SiloModelMapper::posterBadgeLayout(wireSettings, wireDefaults);
    }

private:
    static ProviderCapabilities implementedCapabilities()
    {
        ProviderCapabilities capabilities;
        capabilities |= ProviderCapability::RefreshAuthentication;
        capabilities |= ProviderCapability::Profiles;
        capabilities |= ProviderCapability::ProfilePin;
        capabilities |= ProviderCapability::AuthSessions;
        capabilities |= ProviderCapability::Catalog;
        capabilities |= ProviderCapability::NativeState;
        capabilities |= ProviderCapability::Playback;
        capabilities |= ProviderCapability::PlaybackReporting;
        capabilities |= ProviderCapability::MediaSegments;
        capabilities |= ProviderCapability::ThemeSongs;
        capabilities |= ProviderCapability::Trickplay;
        capabilities |= ProviderCapability::Watchlist;
        capabilities |= ProviderCapability::HomeSections;
        capabilities |= ProviderCapability::MediaRequests;
        return capabilities;
    }

    static std::optional<QString> endpointWithId(const QString &format,
                                                 const QString &id)
    {
        const QString normalized = id.trimmed();
        if (normalized.isEmpty()) {
            return std::nullopt;
        }
        return format.arg(QString::fromLatin1(QUrl::toPercentEncoding(normalized)));
    }

    SiloCatalogProvider m_catalogProvider;
    SiloAuthenticator m_authenticator;
    SiloPlaybackProvider m_playbackProvider;
    SiloRequestFactory m_requestFactory;
};
