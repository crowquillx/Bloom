#pragma once

#include <QString>

// Silo answers artwork-bearing reads with signed URLs to one stored variant
// per image: the narrowest variant at least as wide as the request's per-kind
// width, else the original. Without widths it uses each surface's default
// (500 px posters, 1920 px backdrops, 500 px logos on details, 300 px stills on
// episode rows), which a 4K screen upscales. Bloom asks for what it draws on a
// 4K 10-foot screen, as it asks Jellyfin for maxWidth. Stills get the 1280 px
// rung, which covers a landscape episode card at 4K. Photos keep their 500 px
// default, which already covers the cards Bloom draws them on.
//
// Silo rejects undeclared query parameters with 422, so only operations whose
// contract declares the *_width parameters may carry them.
namespace SiloArtworkSizes {

inline QString withWidths(const QString &endpoint)
{
    return endpoint + (endpoint.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?'))
        + QStringLiteral("poster_width=1000&backdrop_width=3840&logo_width=1280&still_width=1280");
}

} // namespace SiloArtworkSizes
