#pragma once

#include "network/Types.h"
#include "providers/ServerConnection.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

class IPlaybackProvider;
class ICatalogProvider;
class IProviderAuthenticator;
class IProviderRequestFactory;

/**
 * @brief Provider implementation bundle consumed by stable application façades.
 *
 * The adapter owns provider wire behavior while Bloom's QML-facing services own
 * UI-compatible state and signals.
 */
using ProviderItemsResponseParser =
    std::function<ParsedItemsResult(const QByteArray &, const QString &)>;

enum class ProviderRoute {
    Profiles,
    VerifyProfilePin,
    AuthSessions,
    RevokeAuthSession,
    CatalogItems,
    CatalogItem,
    NativeState,
    UpdateNativeState,
    MediaSegments,
    PlaybackInfo,
    PlaybackCapability,
    Health,
    CallerLogout
};

struct ProviderRouteContext {
    QString accountId;
    QString profileId;
    QString itemId;
    QString sessionId;
};

class IProviderAdapter
{
public:
    virtual ~IProviderAdapter() = default;

    virtual ProviderKind providerKind() const = 0;
    virtual ProtocolMode protocolMode() const = 0;
    virtual const IProviderAuthenticator *authenticator() const = 0;
    virtual const IProviderRequestFactory *requestFactory() const = 0;
    virtual const IPlaybackProvider *playbackProvider() const = 0;
    virtual const ICatalogProvider *catalogProvider() const { return nullptr; }
    virtual PlaybackInfoResponse mapPlaybackInfo(
        const QJsonObject &wirePlaybackInfo) const = 0;
    virtual ProviderItemsResponseParser itemsResponseParser() const = 0;
    virtual TrickplayTileInfoMap mapTrickplayInfo(
        const QJsonObject &wireItem) const = 0;
    virtual QList<MediaSegmentInfo> mapIntroSkipperSegments(
        const QString &itemId, const QJsonObject &wireSegments) const = 0;
    virtual QVariantList mapRemoteSessions(const QJsonArray &wireSessions,
                                           const QString &connectionId) const = 0;
    virtual QString mapLibraryIdFromAncestors(
        const QJsonArray &wireAncestors) const = 0;
    virtual QVariantMap mapFilterOptions(const QJsonObject &wireFilters) const = 0;
    virtual QStringList mapNamedItems(const QJsonObject &wireItems) const = 0;
    virtual QVariantMap mapMediaItem(const QJsonObject &wireItem,
                                     const QString &connectionId) const = 0;
    virtual QVariantList mapMediaItems(const QJsonArray &wireItems,
                                       const QString &connectionId) const = 0;
    virtual QVariantList mapChaptersFromItem(const QJsonObject &wireItem,
                                             const QString &connectionId,
                                             const QString &itemId) const = 0;

    // Optional route discovery is explicit. Callers must not interpret an
    // empty URL, empty mapping, or HTTP success as feature support.
    virtual bool supportsCapability(ProviderCapability capability) const
    {
        Q_UNUSED(capability)
        return false;
    }
    virtual std::optional<QString> endpointFor(
        ProviderRoute route, const ProviderRouteContext &context) const
    {
        Q_UNUSED(route)
        Q_UNUSED(context)
        return std::nullopt;
    }

    std::optional<QString> endpointFor(ProviderRoute route) const
    {
        return endpointFor(route, ProviderRouteContext{});
    }

    virtual std::optional<ProviderDetectionResult> mapDetectionResult(
        const QJsonObject &wireResult) const
    {
        Q_UNUSED(wireResult)
        return std::nullopt;
    }
    virtual std::optional<QList<ProviderProfile>> mapProfiles(
        const QJsonArray &wireProfiles) const
    {
        Q_UNUSED(wireProfiles)
        return std::nullopt;
    }
    virtual std::optional<QList<ProviderAuthSession>> mapAuthSessions(
        const QJsonArray &wireSessions) const
    {
        Q_UNUSED(wireSessions)
        return std::nullopt;
    }
    virtual std::optional<QVariantMap> mapNativeState(
        const QJsonObject &wireState, const QString &connectionId) const
    {
        Q_UNUSED(wireState)
        Q_UNUSED(connectionId)
        return std::nullopt;
    }
    virtual std::optional<QList<MediaSegmentInfo>> mapMediaSegments(
        const QString &itemId, const QJsonObject &wireSegments) const
    {
        Q_UNUSED(itemId)
        Q_UNUSED(wireSegments)
        return std::nullopt;
    }
};
