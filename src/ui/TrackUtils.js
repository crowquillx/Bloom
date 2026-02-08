.pragma library

// Helper functions for track formatting

function formatTrackName(track) {
    if (!track) return "Track ?"

    // Normalize properties (handle camelCase and PascalCase)
    var lang = track.language || track.Language
    var codec = track.codec || track.Codec
    var channels = track.channels || track.Channels
    var channelLayout = track.channelLayout || track.ChannelLayout
    var title = track.title || track.Title
    var isDefault = track.isDefault || track.IsDefault
    var isForced = track.isForced || track.IsForced
    var isHearingImpaired = track.isHearingImpaired || track.IsHearingImpaired
    var index = (track.index !== undefined) ? track.index : ((track.Index !== undefined) ? track.Index : "?")

    var parts = []

    // Language
    if (lang) {
        parts.push(getLanguageName(lang))
    }

    // Codec
    if (codec) {
        parts.push(codec.toUpperCase())
    }

    // Channels for audio
    if (channels) {
        parts.push(formatChannels(channels, channelLayout))
    }

    // Title if available
    if (title && !lang) {
        parts.push(title)
    }

    // Flags
    if (isDefault) parts.push("Default")
    if (isForced) parts.push("Forced")
    if (isHearingImpaired) parts.push("SDH")

    var result = parts.length > 0 ? parts.join(" • ") : ("Track " + index)
    return result
}

function formatChannels(channels, layout) {
    if (layout) return layout
    switch (channels) {
        case 1: return "Mono"
        case 2: return "Stereo"
        case 6: return "5.1"
        case 8: return "7.1"
        default: return channels + " ch"
    }
}

function getLanguageName(code) {
    if (!code) return ""
    // Common language codes to full names
    var languages = {
        "eng": "English",
        "jpn": "Japanese",
        "spa": "Spanish",
        "fre": "French",
        "fra": "French",
        "ger": "German",
        "deu": "German",
        "ita": "Italian",
        "por": "Portuguese",
        "rus": "Russian",
        "chi": "Chinese",
        "zho": "Chinese",
        "kor": "Korean",
        "ara": "Arabic",
        "hin": "Hindi",
        "und": "Unknown"
    }
    return languages[code] || code.toUpperCase()
}
