.pragma library

// Labels and playback requests for the local extras shelf and Play Trailer
// on the movie and series detail pages (canonical extras: models/MediaExtras.h).

function kindLabel(kind) {
    switch (kind) {
    case "trailer": return qsTr("Trailer")
    case "teaser": return qsTr("Teaser")
    case "featurette": return qsTr("Featurette")
    case "clip": return qsTr("Clip")
    case "behindTheScenes": return qsTr("Behind the Scenes")
    case "bloopers": return qsTr("Bloopers")
    case "deletedScene": return qsTr("Deleted Scene")
    case "interview": return qsTr("Interview")
    case "scene": return qsTr("Scene")
    case "short": return qsTr("Short")
    default: return qsTr("Extra")
    }
}

function title(extra) {
    return extra && extra.name ? extra.name : kindLabel(extra ? extra.extraKind : "")
}

function formatDuration(ms) {
    var total = Math.max(0, Math.floor((ms || 0) / 1000))
    var hours = Math.floor(total / 3600)
    var minutes = Math.floor((total % 3600) / 60)
    var seconds = total % 60
    function pad(value) { return value < 10 ? "0" + value : "" + value }
    return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                     : minutes + ":" + pad(seconds)
}

// "Trailer · 2:31"; the kind is left out when it is already the title.
function subtitle(extra) {
    if (!extra) {
        return ""
    }
    var parts = []
    if (extra.name) {
        parts.push(kindLabel(extra.extraKind))
    }
    if (extra.durationMs > 0) {
        parts.push(formatDuration(extra.durationMs))
    }
    return parts.join(" · ")
}

// Extras play on their own: from the start, without the parent's resume
// point, series context, track choices or Up Next (PlayerPolicy content
// rules).
function playbackRequest(extra, overlayTitle, backdropUrl, logoUrl, restoreFocusTarget) {
    return {
        itemId: extra.itemId,
        contentKind: "extra",
        startPositionMs: 0,
        seriesId: "",
        seasonId: "",
        overlayTitle: overlayTitle || qsTr("Now Playing"),
        overlaySubtitle: title(extra),
        overlayBackdropUrl: backdropUrl,
        overlayLogoUrl: logoUrl,
        preferredAudioIndex: -2,
        preferredSubtitleIndex: -2,
        isMovie: true,
        allowVersionPrompt: false,
        restoreFocusTarget: restoreFocusTarget
    }
}
