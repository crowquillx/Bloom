#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace NextEpisodeResolver {

QVariantMap resolveBestNextEpisode(const QVariantList &episodes,
                                   const QString &excludeItemId = QString(),
                                   const QVariantMap &preferredEpisode = QVariantMap());

}
