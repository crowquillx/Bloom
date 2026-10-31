import QtQuick

import BloomUI

/**
 * DiscoveryCard - a poster MediaCard for a request discovery item
 * (docs/seerr.md): TMDB artwork, the year and request status as subtitle.
 *
 * Callers own focus, keys and what activation does.
 */
MediaCard {
    id: card

    /// A discovery map: name, mediaType, productionYear, imageUrl,
    /// requestStatus, libraryItemId.
    property var entry: ({})

    variant: "poster"
    artworkSources: [card.entry.imageUrl || ""]
    title: card.entry.name || ""
    subtitle: {
        var parts = []
        if (card.entry.productionYear)
            parts.push(String(card.entry.productionYear))
        var status = MediaRequests.statusLabel(card.entry.requestStatus || "")
        if (status !== "")
            parts.push(status)
        return parts.join("  ·  ")
    }
    placeholderIcon: card.entry.mediaType === "Series" ? Icons.tvShows : Icons.movie
    item: null
    Accessible.description: (card.entry.libraryItemId || "") !== ""
                            ? qsTr("Details for %1").arg(card.entry.name || "")
                            : qsTr("Request %1 with %2").arg(card.entry.name || "")
                                                        .arg(MediaRequests.backendName)
}
