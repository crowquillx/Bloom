.pragma library

function episodeSeasonId(episode) {
    if (!episode) {
        return ""
    }
    return episode.SeasonId || episode.ParentId || ""
}

function episodeIsPlayed(episode) {
    if (!episode) {
        return false
    }
    if (episode.isPlayed !== undefined) {
        return episode.isPlayed
    }
    return !!(episode.UserData && episode.UserData.Played)
}

function episodePlaybackPositionTicks(episode) {
    if (!episode) {
        return 0
    }
    if (episode.playbackPositionTicks !== undefined) {
        return episode.playbackPositionTicks || 0
    }
    if (episode.UserData && episode.UserData.PlaybackPositionTicks !== undefined) {
        return episode.UserData.PlaybackPositionTicks || 0
    }
    return 0
}

function resolveInitialEpisodeSelection(episodes, initialEpisodeId, targetSeasonId) {
    var result = {
        shouldApply: false,
        targetIndex: -1,
        foundInitialEpisode: false,
        usedFallback: false,
        currentSeasonId: "",
        waitingForTargetSeason: false
    }

    if (!episodes || episodes.length === 0) {
        return result
    }

    result.currentSeasonId = episodeSeasonId(episodes[0])
    if (targetSeasonId && result.currentSeasonId && result.currentSeasonId !== targetSeasonId) {
        result.waitingForTargetSeason = true
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
        if (!episodeIsPlayed(fallbackEpisode)) {
            targetIndex = j
            break
        }
        if (episodePlaybackPositionTicks(fallbackEpisode) > 0) {
            targetIndex = j
        }
    }

    result.shouldApply = true
    result.targetIndex = targetIndex
    result.usedFallback = true
    return result
}
