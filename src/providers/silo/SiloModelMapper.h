#pragma once

#include "models/MediaModels.h"
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
    static QVariantList mediaItems(const QJsonArray &wireItems,
                                   const QString &connectionId);

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
};
