#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace NextEpisodeResolver {

QJsonObject resolveBestNextEpisode(const QJsonArray &episodes,
                                   const QString &excludeItemId = QString(),
                                   const QJsonObject &preferredEpisode = QJsonObject());

}
