#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace Bloom {

enum class PosterBadgeCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

/// QML corner name: "topLeft", "topRight", "bottomLeft" or "bottomRight".
QString posterBadgeCornerName(PosterBadgeCorner corner);

/**
 * @brief One badge kind a poster shows, where, and whether with its icon.
 *
 * `kind` is a Bloom badge id such as `resolution`, `ratingImdb` or
 * `originalLanguage`; providers translate their own preference ids to it.
 */
struct PosterBadgeSlot {
    QString kind;
    PosterBadgeCorner corner = PosterBadgeCorner::TopLeft;
    bool showIcon = false;
};

/**
 * @brief The poster badges one connection's items show, in render order.
 *
 * A provider builds it from the viewer's server-side preferences. A disabled
 * layout shows nothing.
 */
struct PosterBadgeLayout {
    bool enabled = false;
    QList<PosterBadgeSlot> badges;

    bool operator==(const PosterBadgeLayout &other) const;
};

/// Badges shown per corner; later badges in the order are dropped.
inline constexpr int kMaxPosterBadgesPerCorner = 3;

/// Badge kinds `resolvePosterBadges` knows how to fill.
bool isKnownPosterBadgeKind(const QString &kind);

/**
 * @brief The badges a canonical media item shows under `layout`.
 *
 * Each entry is a map with `corner`, `kind`, `text` and `icon` (an icon id,
 * empty for none). Rating badges also carry `ratingSource` (`imdb`, `tmdb`,
 * `tomatoes` or `audience`) and `score`, the formatted score without the
 * source name. Badges whose value the item lacks are skipped before the
 * per-corner limit applies.
 */
QVariantList resolvePosterBadges(const PosterBadgeLayout &layout, const QVariantMap &item);

} // namespace Bloom

Q_DECLARE_METATYPE(Bloom::PosterBadgeLayout)
