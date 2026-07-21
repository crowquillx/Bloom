#pragma once

#include "models/MediaModels.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

class JellyfinModelMapper
{
public:
    static qint64 ticksToMilliseconds(qint64 ticks);
    static qint64 millisecondsToTicks(qint64 milliseconds);

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
