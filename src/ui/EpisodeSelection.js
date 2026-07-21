.pragma library

// This helper consumes only Bloom canonical episode maps from EpisodesModel.
function episodeSeasonId(episode) {
    if (!episode) {
        return ""
    }
    return episode.seasonId || episode.parentId || ""
}

function episodeIsPlayed(episode) {
    if (!episode) {
        return false
    }
    return !!episode.watched
}

function episodePositionMs(episode) {
    if (!episode) {
        return 0
    }
    return episode.positionMs || 0
}

function firstValidEpisodeIndex(episodes) {
    for (var i = 0; i < episodes.length; i++) {
        if (episodes[i]) {
            return i
        }
    }
    return -1
}

function resolveInitialEpisodeSelection(episodes, initialEpisodeId, targetSeasonId) {
    var result = {
        shouldApply: false,
        targetIndex: -1,
        foundInitialEpisode: false,
        usedFallback: false,
        currentSeasonId: null,
        waitingForTargetSeason: false
    }

    if (!episodes || episodes.length === 0) {
        return result
    }

    var firstEpisodeIndex = firstValidEpisodeIndex(episodes)
    if (firstEpisodeIndex < 0) {
        return result
    }

    result.currentSeasonId = episodeSeasonId(episodes[firstEpisodeIndex])
    if (targetSeasonId && result.currentSeasonId && result.currentSeasonId !== targetSeasonId) {
        result.waitingForTargetSeason = true
    }

    if (initialEpisodeId) {
        for (var i = 0; i < episodes.length; i++) {
            var episode = episodes[i]
            if (episode && episode.itemId === initialEpisodeId) {
                result.shouldApply = true
                result.targetIndex = i
                result.foundInitialEpisode = true
                return result
            }
        }
        return result
    }

    var targetIndex = firstEpisodeIndex
    for (var j = firstEpisodeIndex; j < episodes.length; j++) {
        var fallbackEpisode = episodes[j]
        if (!fallbackEpisode) {
            continue
        }
        if (!episodeIsPlayed(fallbackEpisode)) {
            targetIndex = j
            break
        }
        if (episodePositionMs(fallbackEpisode) > 0) {
            targetIndex = j
        }
    }

    result.shouldApply = true
    result.targetIndex = targetIndex
    result.usedFallback = true
    return result
}
