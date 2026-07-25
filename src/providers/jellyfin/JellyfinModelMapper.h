#pragma once

#include "models/MediaModels.h"
#include "network/Types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

class JellyfinModelMapper
{
public:
    static qint64 ticksToMilliseconds(qint64 ticks);
    static qint64 millisecondsToTicks(qint64 milliseconds);

    static MediaStreamInfo mediaStream(const QJsonObject &wireStream);
    static MediaSourceInfo mediaSource(const QJsonObject &wireSource);
    static PlaybackInfoResponse playbackInfo(const QJsonObject &wirePlaybackInfo);
    static TrickplayTileInfo trickplayTile(const QJsonObject &wireTile);
    static TrickplayTileInfoMap trickplayInfo(const QJsonObject &wireItem);
    static QList<MediaSegmentInfo> introSkipperSegments(
        const QString &itemId, const QJsonObject &wireSegments);
    static QVariantList remoteSessions(const QJsonArray &wireSessions,
                                       const QString &connectionId);

    static QVariantMap mediaItem(const QJsonObject &wireItem,
                                 const QString &connectionId);
    static QVariantList mediaItems(const QJsonArray &wireItems,
                                   const QString &connectionId);
    static Bloom::Chapter chapter(const QJsonObject &wireChapter,
                                  const QString &connectionId,
                                  const QString &itemId,
                                  int chapterIndex);
    static QVariantList chapters(const QJsonArray &wireChapters,
                                 const QString &connectionId,
                                 const QString &itemId);
    static QString artworkEndpoint(const Bloom::ArtworkRef &artwork);
};
