.pragma library

function resolveInitialEpisodeSelection(episodes, initialEpisodeId) {
    var result = {
        shouldApply: false,
        targetIndex: -1,
        foundInitialEpisode: false,
        usedFallback: false
    }

    if (!episodes || episodes.length === 0) {
        return result
    }

    if (initialEpisodeId) {
        for (var i = 0; i < episodes.length; i++) {
            var episode = episodes[i]
            if (episode && (episode.itemId === initialEpisodeId || episode.Id === initialEpisodeId)) {
                result.shouldApply = true
                result.targetIndex = i
                result.foundInitialEpisode = true
                return result
            }
        }
        return result
    }

    var targetIndex = 0
    for (var j = 0; j < episodes.length; j++) {
        var fallbackEpisode = episodes[j]
        if (!fallbackEpisode) {
            continue
        }
        if (!fallbackEpisode.isPlayed) {
            targetIndex = j
            break
        }
        if (fallbackEpisode.playbackPositionTicks > 0) {
            targetIndex = j
        }
    }

    result.shouldApply = true
    result.targetIndex = targetIndex
    result.usedFallback = true
    return result
}
