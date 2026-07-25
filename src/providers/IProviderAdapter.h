#pragma once

#include "network/Types.h"
#include "providers/ServerConnection.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class IPlaybackProvider;
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

class IProviderAdapter
{
public:
    virtual ~IProviderAdapter() = default;

    virtual ProviderKind providerKind() const = 0;
    virtual ProtocolMode protocolMode() const = 0;
    virtual const IProviderAuthenticator *authenticator() const = 0;
    virtual const IProviderRequestFactory *requestFactory() const = 0;
    virtual const IPlaybackProvider *playbackProvider() const = 0;
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
};
