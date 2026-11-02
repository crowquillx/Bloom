#pragma once

#include <QVariantList>
#include <QVariantMap>

// The backdrops a detail page's hero cycles through. A canonical movie or
// series map names its main backdrop in `backdropArtwork` and lists every
// artwork reference in `artwork`; providers that know more than one backdrop
// for the title (Jellyfin BackdropImageTags, Silo backdrop_urls) list each
// one there with kind `backdrop` and its position as `index`.
namespace Bloom::HeroBackdrops {

/// Most backdrops one hero cycles through.
inline constexpr int kMaxBackdrops = 8;

/// The item's own backdrop references (ArtworkRef maps), main backdrop
/// first, then the others by index, each once and at most @p limit. A
/// backdrop owned by another item (an episode's series backdrop) is not the
/// title's own and is left out. Empty when the item has no main backdrop.
[[nodiscard]] QVariantList refs(const QVariantMap &item, int limit = kMaxBackdrops);

} // namespace Bloom::HeroBackdrops
