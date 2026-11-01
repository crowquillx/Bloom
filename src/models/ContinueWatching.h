#pragma once

#include <QVariantList>

/**
 * @brief Orders Home's Continue Watching and Next Up items.
 *
 * Jellyfin answers in-progress movies and episodes (the resume list) apart
 * from Next Up; Silo sends both in one Home layout. Home shows one list per
 * connection or space, split by `positionMs` into Continue Watching and Next
 * Up. Items are canonical maps; a MediaRef (`connectionId`, `itemId`)
 * identifies each one.
 */
namespace ContinueWatching {

/// Each item once (the first occurrence wins): in-progress items
/// (`positionMs > 0`) first, most recently played first, then the rest in
/// their given order. In-progress items without a play date follow the
/// dated ones in their given order.
QVariantList order(const QVariantList &items);

/// The resume list and Next Up as one Home list, ordered as order().
QVariantList combine(const QVariantList &resume, const QVariantList &nextUp);

} // namespace ContinueWatching
