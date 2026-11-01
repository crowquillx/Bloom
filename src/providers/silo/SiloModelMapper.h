#pragma once

#include "models/MediaModels.h"
#include "models/PosterBadges.h"
#include "network/Types.h"
#include "providers/ICatalogProvider.h"
#include "providers/ServerConnection.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

class SiloModelMapper
{
public:
    static TrickplayTileInfoMap trickplayInfo(const QJsonObject &wire);
    static qint64 secondsToMilliseconds(double seconds);
    static PlaybackInfoResponse playbackInfo(const QJsonObject &wireItem);
    static PlaybackInfoResponse playbackInfoFromVersions(const QJsonArray &wireVersions);

    static ParsedItemsResult itemsResponse(const QByteArray &wireResponse,
                                           const QString &parentId);
    static ProviderCatalogResponse catalogResponse(
        ProviderCatalogOperation operation,
        const QByteArray &wireResponse,
        const QHash<QByteArray, QByteArray> &responseHeaders = {});
    static QVariantMap library(const QJsonObject &wireLibrary,
                               const QString &connectionId);
    static QVariantList libraries(const QJsonArray &wireLibraries,
                                  const QString &connectionId);

    static MediaStreamInfo mediaStream(const QJsonObject &wireTrack,
                                       const QString &kind,
                                       int index);
    static QVariantMap mediaVersion(const QJsonObject &wireVersion,
                                    const QString &connectionId,
                                    const QString &itemId);
    static QVariantList mediaVersions(const QJsonArray &wireVersions,
                                      const QString &connectionId,
                                      const QString &itemId);
    static QVariantMap mediaItem(const QJsonObject &wireItem,
                                 const QString &connectionId);
    /// A catalog item detail: mediaItem() plus an `extras` list that is
    /// always present. Silo omits `extras` when the item has none, so an
    /// empty list here replaces extras cached from an earlier detail.
    static QVariantMap mediaDetail(const QJsonObject &wireItem,
                                   const QString &connectionId);
    static QVariantList mediaItems(const QJsonArray &wireItems,
                                   const QString &connectionId);
    /// A catalog Person (getPerson, listPeople) as a canonical person; empty
    /// when it has no ID or name.
    static QVariantMap person(const QJsonObject &wirePerson,
                              const QString &connectionId);
    static QVariantList personList(const QJsonArray &wirePeople,
                                   const QString &connectionId);
    /// The token-free tag of a signed artwork URL: a hash of its path, which
    /// names the stored size variant and its content.
    static QString artworkTag(const QString &url);

    // ItemExtraInfo entries of a catalog item detail as canonical extras
    // (models/MediaExtras.h) of `parentId`. The still is owned by the parent
    // (ArtworkOwnerKind::Extra), since an extra has no catalog route.
    static QVariantList extras(const QJsonArray &wireExtras,
                               const QString &connectionId,
                               const QString &parentId);
    static QVariantMap chapter(const QJsonObject &wireChapter,
                               const QString &connectionId,
                               const QString &itemId,
                               const QString &fileId,
                               int fallbackIndex);
    static QVariantList chapters(const QJsonArray &wireChapters,
                                 const QString &connectionId,
                                 const QString &itemId,
                                 const QString &fileId = {});
    static QVariantList chaptersFromItem(const QJsonObject &wireItem,
                                         const QString &connectionId,
                                         const QString &itemId);

    static QVariantMap filterOptions(const QJsonObject &wireFilters);
    static QStringList namedItems(const QJsonObject &wireItems);
    static QList<ProviderProfile> profiles(const QJsonArray &wireProfiles);
    static QList<ProviderAuthSession> authSessions(const QJsonArray &wireSessions);
    static QVariantMap nativeState(const QJsonObject &wireState,
                                   const QString &connectionId);
    static QList<MediaSegmentInfo> mediaSegments(const QString &itemId,
                                                 const QJsonObject &wireSegments);

    /// The viewer's poster badges from the effective `ui.card_overlays` and
    /// `ui.card_overlays_enabled` settings, falling back to the server-wide
    /// overlay defaults. Badge styling (preset shape, accent colours) is not
    /// carried over; Bloom draws badges in its own theme.
    static Bloom::PosterBadgeLayout posterBadgeLayout(const QJsonObject &wireEffectiveSettings,
                                                      const QJsonObject &wireOverlayConfig);
};
