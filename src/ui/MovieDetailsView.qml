import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI
import "TrackUtils.js" as TrackUtils
import "ExtrasUtils.js" as ExtrasUtils

FocusScope {
    id: root

    property string movieId: ""
    property var pendingReturnState: null
    property bool restorePendingReturnState: false
    readonly property bool isRestoringReturnFocus: restoringFocusFromNavigation
                                                 || restoringFocusFromReturnState
                                                 || suppressHeroAutofocus
                                                 || hasPendingReturnStateForCurrentMovie()

    readonly property string movieName: MovieDetailsViewModel.title
    readonly property string movieOverview: MovieDetailsViewModel.overview
    readonly property double durationMs: MovieDetailsViewModel.durationMs
    readonly property var communityRating: MovieDetailsViewModel.communityRating
    readonly property string premiereDate: MovieDetailsViewModel.premiereDate
    readonly property int productionYear: MovieDetailsViewModel.productionYear
    readonly property bool isPlayed: MovieDetailsViewModel.isWatched
    readonly property double positionMs: MovieDetailsViewModel.positionMs
    readonly property string officialRating: MovieDetailsViewModel.officialRating
    readonly property var genres: MovieDetailsViewModel.genres
    readonly property var castAndCrew: MovieDetailsViewModel.people || []
    readonly property var chapters: MovieDetailsViewModel.chapters || []
    readonly property bool chaptersLoading: MovieDetailsViewModel.chaptersLoading
    // Chapters without images use the compact timeline at the bottom instead of cards.
    readonly property bool chaptersHaveImages: {
        for (var i = 0; i < chapters.length; ++i) {
            if (chapters[i].thumbnailUrl) {
                return true
            }
        }
        return false
    }
    // Local extras; Play Trailer shows only when one of them is a trailer.
    readonly property var extras: MovieDetailsViewModel.extras || []
    readonly property bool extrasLoading: MovieDetailsViewModel.extrasLoading
    readonly property var trailer: MovieDetailsViewModel.trailer || ({})
    readonly property bool hasTrailer: !!(trailer && trailer.itemId)
    readonly property int extraCardImageWidth: Math.round(Theme.landscapeCardWidth * Math.max(2, Screen.devicePixelRatio))
    readonly property var libraryRecommendations: MovieDetailsViewModel.similarItems || []
    readonly property bool libraryRecommendationsLoading: MovieDetailsViewModel.similarItemsLoading
    readonly property bool isLoading: MovieDetailsViewModel.isLoading
    readonly property var externalRatings: MovieDetailsViewModel.mdbListRatings || ({})

    readonly property string logoUrl: MovieDetailsViewModel.logoUrl
    readonly property string posterUrl: MovieDetailsViewModel.posterUrl
    readonly property string backdropUrl: MovieDetailsViewModel.backdropUrl

    // Hero copy keeps a fixed reading measure (about half a 16:9 screen), so
    // ultrawide screens do not stretch it.
    readonly property int heroBlockWidth: Math.min(Theme.heroCopyMaxWidth,
                                                   root.width - Theme.pageMarginX * 2)
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    // Request sharper artwork for the scaled cards at high display scaling.
    readonly property int cardImageWidth: Math.round(Theme.posterCardWidth * Math.max(2, Screen.devicePixelRatio))
    readonly property int shelfScrollOffset: Math.round(56 * Theme.layoutScale)
    /// The navigation bar recedes once the page scrolls into the shelves.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.navBarReservedHeight

    property var playbackInfo: null
    property var currentMediaSource: playbackInfo && playbackInfo.mediaSources && playbackInfo.mediaSources.length > 0
                                     ? playbackInfo.mediaSources[0] : null
    property bool playbackInfoLoading: false
    property bool playbackReturnFocusPending: false
    property bool playbackReturnFocusActivated: false
    property Item lastPlaybackRestoreFocusTarget: null
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1

    signal playRequested(var request)
    signal itemSelected(var itemData)
    signal backRequested()
    signal returnStateConsumed()
    signal personSelected(var person)

    // Cast and crew open the person's page (personSelected). Coming back,
    // focus lands on the card that opened it instead of the hero.
    property bool returnFocusToCast: false
    property int returnCastIndex: 0

    function openPerson(person) {
        if (!person || !person.personId)
            return
        returnFocusToCast = true
        returnCastIndex = Math.max(0, castList.currentIndex)
        root.personSelected(person)
    }

    function restoreCastFocus() {
        if (!returnFocusToCast || !sharedModelCurrent
                || !restoreFocusToArea("cast", returnCastIndex))
            return false
        returnFocusToCast = false
        return true
    }

    // Every movie page reads the shared MovieDetailsViewModel, and a movie
    // opened from a person page loads into it while this page waits below in
    // the stack. Handlers and actions act only while the model holds this
    // page's movie; reloadSharedDetails() puts it back when the page returns.
    readonly property bool sharedModelCurrent: movieId !== "" && MovieDetailsViewModel.movieId === movieId
    property bool awaitingSharedReload: false
    // The server's answer after a cached reload must not move focus to Play.
    property bool skipLoadedAutofocus: false

    /// Reloads this page's movie (from the detail cache when it can) if
    /// another page replaced it. Returns true when it did.
    function reloadSharedDetails() {
        if (movieId === "" || sharedModelCurrent)
            return false
        awaitingSharedReload = true
        skipLoadedAutofocus = false
        MovieDetailsViewModel.loadMovieDetails(movieId)
        // A cached movie is back already; otherwise onMovieLoaded finishes.
        if (castAndCrew.length > 0)
            Qt.callLater(function() { root.finishSharedReload(true) })
        return true
    }

    function finishSharedReload(fromCache) {
        if (!awaitingSharedReload)
            return
        awaitingSharedReload = false
        skipLoadedAutofocus = fromCache
        if (!restoreCastFocus()) {
            returnFocusToCast = false
            focusTimer.start()
        }
    }

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId) {
            return ""
        }
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 420)
    }

    function getVideoFramerate() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return 0.0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") {
                if (stream.realFrameRate && stream.realFrameRate > 0) return stream.realFrameRate
                if (stream.averageFrameRate && stream.averageFrameRate > 0) return stream.averageFrameRate
            }
        }
        return 0.0
    }

    function isVideoHDR() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return false
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video" && videoHdrLabel(stream) !== "") {
                return true
            }
        }
        return false
    }

    function primaryVideoStream() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return null
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") return stream
        }
        return null
    }

    function videoResolutionBadge() {
        var stream = primaryVideoStream()
        if (!stream) return ""
        var height = Number(stream.height || stream.Height || 0)
        var width = Number(stream.width || stream.Width || 0)
        if (height >= 2160 || width >= 3800) return qsTr("4K")
        if (height >= 1440) return qsTr("1440p")
        if (height >= 1080) return qsTr("1080p")
        if (height >= 720) return qsTr("720p")
        if (height > 0) return qsTr("%1p").arg(height)
        return ""
    }

    function videoHdrBadge() {
        return videoHdrLabel(primaryVideoStream())
    }

    function hasMediaBadges() {
        return videoResolutionBadge() !== "" || videoHdrBadge() !== ""
    }

    function videoHdrLabel(stream) {
        if (!stream) return ""
        var metadata = [
            stream.videoRange,
            stream.videoRangeType,
            stream.codecTag,
            stream.codecTagString,
            stream.codecId,
            stream.profile
        ].join(" ").toUpperCase()
        var dvProfile = Number(stream.dolbyVisionProfile || 0)
        if (dvProfile > 0 || metadata.indexOf("DOVI") >= 0 || metadata.indexOf("DOLBY VISION") >= 0
                || metadata.indexOf("DVHE") >= 0 || metadata.indexOf("DVH1") >= 0) {
            return qsTr("Dolby Vision")
        }
        if (metadata.indexOf("HDR10+") >= 0 || metadata.indexOf("HDR10PLUS") >= 0) {
            return qsTr("HDR10+")
        }
        if (metadata.indexOf("HLG") >= 0) {
            return qsTr("HLG")
        }
        var range = String(stream.videoRange || stream.videoRangeType || "").toUpperCase()
        if (range !== "" && range !== "SDR") {
            return metadata.indexOf("HDR10") >= 0 || range.indexOf("HDR") >= 0 ? qsTr("HDR10") : range
        }
        return ""
    }

    function getAudioStreams() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return []
        var audio = []
        for (var i = 0; i < currentMediaSource.mediaStreams.length; ++i) {
            if (currentMediaSource.mediaStreams[i].type === "Audio") {
                audio.push(currentMediaSource.mediaStreams[i])
            }
        }
        return audio
    }

    function getSubtitleStreams() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return []
        var subtitles = []
        for (var i = 0; i < currentMediaSource.mediaStreams.length; ++i) {
            if (currentMediaSource.mediaStreams[i].type === "Subtitle") {
                subtitles.push(currentMediaSource.mediaStreams[i])
            }
        }
        return subtitles
    }

    function shortTrackLabel(track, fallback) {
        if (!track) {
            return fallback || qsTr("Default")
        }

        var title = track.title || track.Title
        if (title) {
            return title
        }

        var language = track.language || track.Language
        if (language) {
            return TrackUtils.getLanguageName(language)
        }

        var codec = track.codec || track.Codec
        if (codec) {
            return String(codec).toUpperCase()
        }

        var index = track.index !== undefined ? track.index : track.Index
        if (index !== undefined) {
            return qsTr("Track %1").arg(index)
        }

        return fallback || qsTr("Default")
    }

    function selectedAudioSummary() {
        var tracks = getAudioStreams()
        for (var i = 0; i < tracks.length; ++i) {
            if (tracks[i].index === selectedAudioIndex) {
                return shortTrackLabel(tracks[i], qsTr("Default"))
            }
        }
        return tracks.length > 0 ? shortTrackLabel(tracks[0], qsTr("Default")) : qsTr("Unavailable")
    }

    function selectedSubtitleSummary() {
        if (selectedSubtitleIndex === -1) {
            return qsTr("Off")
        }
        var tracks = getSubtitleStreams()
        for (var i = 0; i < tracks.length; ++i) {
            if (tracks[i].index === selectedSubtitleIndex) {
                return shortTrackLabel(tracks[i], qsTr("Off"))
            }
        }
        return tracks.length > 0 ? shortTrackLabel(tracks[0], qsTr("Auto")) : qsTr("Unavailable")
    }

    function buildPlaybackRequest(startPositionOverride, restoreFocusTarget) {
        var framerate = getVideoFramerate()
        var hdr = isVideoHDR()
        var overlaySubtitle = productionYear > 0 ? String(productionYear) : ""

        return {
            itemId: movieId,
            startPositionMs: startPositionOverride !== undefined && startPositionOverride !== null
                             ? Math.round(startPositionOverride)
                             : Math.round(positionMs || 0),
            seriesId: "",
            seasonId: "",
            overlayTitle: movieName || qsTr("Now Playing"),
            overlaySubtitle: overlaySubtitle,
            overlayBackdropUrl: backdropUrl,
            overlayLogoUrl: logoUrl,
            // No index override: the controller resolves the movie's saved
            // picks against the version that plays, so a pick made in the
            // player since this page resolved its tracks still holds.
            preferredAudioIndex: -2,
            preferredSubtitleIndex: -2,
            isMovie: true,
            allowVersionPrompt: true,
            framerateHint: framerate,
            isHDRHint: hdr,
            restoreFocusTarget: restoreFocusTarget || playButton
        }
    }

    function startPlaybackWithTracks() {
        startPlaybackWithTracksAt(undefined, playButton)
    }

    function startPlaybackWithTracksAt(startPositionMs, restoreFocusTarget) {
        resetPlaybackReturnFocusState()

        if (playbackInfoLoading || !playbackInfo || !currentMediaSource) {
            if (!playbackInfoLoading && movieId !== "") {
                playbackInfoLoading = true
                PlaybackService.getPlaybackInfo(movieId)
            }
            playbackToast.show(qsTr("Playback is still preparing. Try again in a moment."))
            return
        }

        lastPlaybackRestoreFocusTarget = restoreFocusTarget || playButton
        playbackReturnFocusPending = true
        playbackReturnFocusActivated = false
        root.playRequested(buildPlaybackRequest(startPositionMs, lastPlaybackRestoreFocusTarget))
    }

    // Extras play on their own (ExtrasUtils.playbackRequest). In a merged
    // space the owning connection goes live first.
    function playExtra(extra, restoreFocusTarget) {
        if (!extra || !extra.itemId) {
            return
        }
        extrasRouter.run(extra.connectionId, function() {
            resetPlaybackReturnFocusState()
            lastPlaybackRestoreFocusTarget = restoreFocusTarget || trailerButton
            playbackReturnFocusPending = true
            playbackReturnFocusActivated = false
            root.playRequested(ExtrasUtils.playbackRequest(extra, movieName, backdropUrl, logoUrl,
                                                           lastPlaybackRestoreFocusTarget))
        })
    }

    function formatRuntime(ms) {
        if (!ms || ms === 0) return ""
        var totalMinutes = Math.round(ms / 60000)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
        if (hours > 0) {
            return qsTr("%1h %2m").arg(hours).arg(minutes)
        }
        return qsTr("%1m").arg(minutes)
    }

    function formatChapterTime(seconds) {
        var total = Math.max(0, Math.floor(seconds || 0))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var remainder = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(remainder)
                         : minutes + ":" + pad(remainder)
    }

    function calculateEndTime(ms) {
        if (!ms || ms === 0) return ""
        var now = new Date()
        var endTime = new Date(now.getTime() + ms)
        return qsTr("Ends %1").arg(endTime.toLocaleTimeString(Qt.locale(), "h:mm AP"))
    }

    function formatCommunityRating() {
        if (!communityRating || communityRating <= 0) return ""
        // The server's own IMDb (else TMDB) score is already in its ratings.
        const ratings = externalRatings["ratings"] || []
        for (let i = 0; i < ratings.length; ++i) {
            const rating = ratings[i]
            if (rating && rating.fromServer
                    && (rating.sourceKey === "imdb" || rating.sourceKey === "tmdb")) {
                return ""
            }
        }
        return "★ " + communityRating.toFixed(1)
    }

    function watchedMinutes() {
        return Math.round((positionMs || 0) / 60000)
    }

    function totalMinutes() {
        return Math.round((durationMs || 0) / 60000)
    }

    function remainingMinutes() {
        return Math.max(0, totalMinutes() - watchedMinutes())
    }

    function visibleExternalRatings() {
        const ratings = externalRatings["ratings"] || []
        const filtered = []
        for (let i = 0; i < ratings.length; ++i) {
            const rating = ratings[i]
            if (!rating) {
                continue
            }
            // A server rating carries its display text, and a score of 0
            // is a real rating there.
            if (rating.display) {
                filtered.push(rating)
                continue
            }
            const value = rating.value
            const score = rating.score
            if ((value === undefined || value === null || value === "" || value === 0 || value === "0")
                    && (score === undefined || score === null || score === "" || score === 0 || score === "0")) {
                continue
            }
            filtered.push(rating)
        }
        return filtered
    }

    function itemIsDescendant(item, ancestor) {
        let current = item
        while (current) {
            if (current === ancestor) {
                return true
            }
            current = current.parent
        }
        return false
    }

    function hasPendingReturnStateForCurrentMovie() {
        return restorePendingReturnState
                && pendingReturnState
                && String(pendingReturnState.movieId || "") === String(movieId || "")
    }

    function currentFocusArea() {
        const activeItem = root.Window.activeFocusItem
        if (!activeItem) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, playButton)
                || itemIsDescendant(activeItem, trailerButton)
                || itemIsDescendant(activeItem, markWatchedButton)
                || itemIsDescendant(activeItem, watchlistButton)
                || itemIsDescendant(activeItem, contextMenuButton)) {
            return "hero"
        }
        if (itemIsDescendant(activeItem, castList)) {
            return "cast"
        }
        if (itemIsDescendant(activeItem, chapterList)) {
            return "chapters"
        }
        if (itemIsDescendant(activeItem, extrasList)) {
            return "extras"
        }
        if (itemIsDescendant(activeItem, chapterTimeline)) {
            return "chapterTimeline"
        }
        if (itemIsDescendant(activeItem, libraryRecommendationsList)) {
            return "libraryRecommendations"
        }
        return "hero"
    }

    function currentFocusIndex() {
        const area = currentFocusArea()
        const activeItem = root.Window.activeFocusItem

        if (area === "hero") {
            if (itemIsDescendant(activeItem, markWatchedButton)) return 1
            if (itemIsDescendant(activeItem, contextMenuButton)) return 2
            if (itemIsDescendant(activeItem, watchlistButton)) return 3
            if (itemIsDescendant(activeItem, trailerButton)) return 4
            return 0
        }
        if (area === "cast") return Math.max(0, castList.currentIndex)
        if (area === "chapters") return Math.max(0, chapterList.currentIndex)
        if (area === "extras") return Math.max(0, extrasList.currentIndex)
        if (area === "chapterTimeline") return Math.max(0, chapterTimeline.currentIndex)
        if (area === "libraryRecommendations") return Math.max(0, libraryRecommendationsList.currentIndex)
        return 0
    }

    function saveReturnState() {
        return {
            movieId: movieId,
            focusArea: currentFocusArea(),
            focusIndex: currentFocusIndex(),
            contentY: contentFlickable.contentY
        }
    }

    function restoreFocusToArea(area, index) {
        const targetIndex = Math.max(0, index || 0)

        if (area === "hero") {
            if (targetIndex === 1 && markWatchedButton.visible) {
                markWatchedButton.forceActiveFocus()
            } else if (targetIndex === 2 && contextMenuButton.visible) {
                contextMenuButton.forceActiveFocus()
            } else if (targetIndex === 3 && watchlistButton.visible) {
                watchlistButton.forceActiveFocus()
            } else if (targetIndex === 4 && trailerButton.visible) {
                trailerButton.forceActiveFocus()
            } else {
                playButton.forceActiveFocus()
            }
            return true
        }

        const lists = {
            "chapters": chapterList,
            "extras": extrasList,
            "cast": castList,
            "libraryRecommendations": libraryRecommendationsList
        }
        const list = lists[area]
        if (list && list.parent.visible && list.count > 0) {
            list.currentIndex = Math.min(targetIndex, list.count - 1)
            list.forceActiveFocus()
            return true
        }

        if (area === "chapterTimeline" && chapterTimeline.visible && chapterTimeline.count > 0) {
            chapterTimeline.currentIndex = Math.min(targetIndex, chapterTimeline.count - 1)
            chapterTimeline.forceActiveFocus()
            return true
        }

        return false
    }

    function restoreReturnState(state) {
        if (!state || String(state.movieId || "") !== String(movieId || "")) {
            return false
        }

        if (state.contentY !== undefined && state.contentY >= 0) {
            const maxScroll = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(state.contentY, maxScroll)
        }

        const desiredArea = state.focusArea || "hero"
        const castReady = castList.count > 0
        const chaptersReady = chapterList.count > 0
        const recsReady = libraryRecommendationsList.count > 0
        const castPending = isLoading
        const chaptersPending = chaptersLoading
        const recsPending = libraryRecommendationsLoading

        if (desiredArea === "cast" && !castReady && !recsReady && (castPending || recsPending)) {
            return false
        }
        if ((desiredArea === "chapters" || desiredArea === "chapterTimeline") && !chaptersReady && chaptersPending) {
            return false
        }
        if (desiredArea === "libraryRecommendations" && !recsReady && !castReady && (recsPending || castPending)) {
            return false
        }
        // Extras arrive after the detail (Jellyfin); wait while they can.
        if (desiredArea === "extras" && extrasList.count <= 0 && (isLoading || extrasLoading)) {
            return false
        }

        if (restoreFocusToArea(desiredArea, state.focusIndex || 0)) {
            return true
        }

        if (restoreFocusToArea("libraryRecommendations", state.focusIndex || 0)) {
            return true
        }

        if (restoreFocusToArea("cast", state.focusIndex || 0)) {
            return true
        }
        if (restoreFocusToArea("chapters", state.focusIndex || 0)) {
            return true
        }

        if (playButton.enabled) {
            playButton.forceActiveFocus()
            return true
        }

        focusFirstLowerSection()
        return true
    }

    function tryRestorePendingReturnState() {
        if (!hasPendingReturnStateForCurrentMovie() || restoringFocusFromReturnState) {
            return
        }

        restoringFocusFromReturnState = true
        const restored = restoreReturnState(pendingReturnState)
        if (restored) {
            focusTimer.stop()
            suppressHeroAutofocus = true
            returnStateConsumed()
            heroAutofocusResetTimer.restart()
        }
        Qt.callLater(function() {
            restoringFocusFromReturnState = false
        })
    }

    // Shelves below the hero, in focus order (visible ones only).
    function shelfTargets() {
        const targets = []
        if (chapterSection.visible && chapterList.count > 0) targets.push(chapterList)
        if (extrasSection.visible && extrasList.count > 0) targets.push(extrasList)
        if (castSection.visible && castList.count > 0) targets.push(castList)
        if (libraryRecommendationsSection.visible && libraryRecommendationsList.count > 0)
            targets.push(libraryRecommendationsList)
        if (chapterTimeline.visible && chapterTimeline.count > 0) targets.push(chapterTimeline)
        return targets
    }

    function sectionFor(target) {
        return target === chapterTimeline ? chapterTimeline : (target ? target.parent : null)
    }

    function focusFirstLowerSection() {
        const targets = shelfTargets()
        if (targets.length > 0) {
            targets[0].forceActiveFocus()
        }
    }

    // Moves focus to the shelf above or below. Above the first shelf is the
    // hero's primary action.
    function moveShelfFocus(fromTarget, delta) {
        const targets = shelfTargets()
        const index = targets.indexOf(fromTarget)
        const target = index + delta
        if (index >= 0 && target >= 0 && target < targets.length) {
            targets[target].forceActiveFocus()
        } else if (target < 0 && playButton.enabled) {
            playButton.forceActiveFocus()
        }
        return true
    }

    // Up from the action row: Read More when shown, otherwise leave the key
    // unhandled so it reaches the navigation bar.
    function heroUp(event) {
        if (detailHero.readMoreButton.visible) {
            detailHero.readMoreButton.forceActiveFocus()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    function scrollTo(y) {
        const maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    function ensureTopVisible() {
        scrollTo(0)
    }

    // The first shelf scrolls only as far as it needs to sit fully in view
    // under the hero; later shelves land in the same spot near the top.
    function scrollToSection(section) {
        if (!section) {
            return
        }
        const targets = shelfTargets()
        const first = targets.length > 0 ? sectionFor(targets[0]) : null
        const top = section.mapToItem(contentColumn, 0, 0).y
        if (section === first) {
            scrollTo(top + section.height + Theme.spacingLarge - contentFlickable.height)
        } else {
            scrollTo(top - root.shelfScrollOffset)
        }
    }

    function remainingText() {
        const minutes = Math.max(1, remainingMinutes())
        return minutes >= 60
               ? qsTr("%1h %2m left").arg(Math.floor(minutes / 60)).arg(minutes % 60)
               : qsTr("%1 min left").arg(minutes)
    }

    function posterSubtitle(item) {
        if (!item) {
            return ""
        }
        if (item.productionYear) {
            return String(item.productionYear)
        }
        if (item.premiereDate) {
            const date = new Date(item.premiereDate)
            if (!isNaN(date.getTime())) {
                return String(date.getFullYear())
            }
        }
        return ""
    }

    function resetPlaybackReturnFocusState() {
        playbackReturnFocusPending = false
        playbackReturnFocusActivated = false
        lastPlaybackRestoreFocusTarget = null
    }

    function restoreFocusAfterPlaybackExit() {
        if (!root.visible
                || !root.playbackReturnFocusPending
                || !root.playbackReturnFocusActivated
                || PlayerController.awaitingNextEpisodeResolution) {
            return
        }

        root.playbackReturnFocusPending = false
        root.playbackReturnFocusActivated = false

        root.forceActiveFocus()
        Qt.callLater(function() {
            if (lastPlaybackRestoreFocusTarget
                    && lastPlaybackRestoreFocusTarget.parent
                    && typeof lastPlaybackRestoreFocusTarget.forceActiveFocus === "function") {
                lastPlaybackRestoreFocusTarget.forceActiveFocus()
            } else if (playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
            }
        })
    }

    Keys.onPressed: (event) => {
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            if (contextMenu.opened) {
                contextMenu.close()
                event.accepted = true
                return
            }
            root.backRequested()
            event.accepted = true
        }
    }

    focus: true

    onActiveFocusChanged: {
        if (!activeFocus) {
            return
        }
        if (returnFocusToCast) {
            if (restoreCastFocus())
                return
            // The reloaded movie brings its cast; finishSharedReload focuses it.
            if (awaitingSharedReload || !sharedModelCurrent)
                return
            returnFocusToCast = false
        }
        if (restoringFocusFromNavigation
                || restoringFocusFromReturnState
                || suppressHeroAutofocus
                || hasPendingReturnStateForCurrentMovie()) {
            return
        }
        Qt.callLater(function() {
            if (playButton && playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
            }
        })
    }

    onMovieIdChanged: {
        if (movieId !== "") {
            MovieDetailsViewModel.loadMovieDetails(movieId)
            playbackInfoLoading = true
            playbackInfo = null
            selectedAudioIndex = -1
            selectedSubtitleIndex = -1
            PlaybackService.getPlaybackInfo(movieId)
        } else {
            MovieDetailsViewModel.clear()
            playbackInfo = null
            playbackInfoLoading = false
            selectedAudioIndex = -1
            selectedSubtitleIndex = -1
        }
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onPendingReturnStateChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onRestorePendingReturnStateChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onCastAndCrewChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onLibraryRecommendationsChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onChaptersChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onChaptersLoadingChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onExtrasChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    onExtrasLoadingChanged: {
        Qt.callLater(root.tryRestorePendingReturnState)
    }

    // Stage: the movie's backdrop, sharp behind the hero copy. A poster
    // fallback is low resolution, so it is shown softened instead.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        z: -1
        mode: backdropUrl !== "" ? "hero" : "ambient"
        source: backdropUrl !== "" ? backdropUrl : posterUrl
        // Quiet the artwork as the page scrolls into the shelves.
        dim: Math.min(0.72, contentFlickable.contentY / Math.max(1, heroArea.height) * 0.72)
    }

    StateMessage {
        anchors.centerIn: parent
        visible: isLoading && movieName === ""
        z: 100
        kind: "loading"
        message: qsTr("Loading movie details…")
    }

    NumberAnimation {
        id: scrollAnimation
        target: contentFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
    }

    // The hero and the shelves scroll together, like Home.
    Flickable {
        id: contentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            width: contentFlickable.width
            spacing: Theme.sectionSpacing

            // Hero: about the top 60% of the screen. The copy sits at its foot
            // and grows upwards, never under the navigation bar.
            Item {
                id: heroArea
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Math.round(root.height * 0.60),
                                                 Theme.navBarReservedHeight + detailHero.height)

                DetailHero {
                    id: detailHero
                    x: Theme.pageMarginX
                    anchors.bottom: parent.bottom
                    width: root.heroBlockWidth
                    eyebrow: qsTr("Movie")
                    logoSource: logoUrl
                    title: movieName
                    metadataItems: [productionYear > 0 ? String(productionYear) : "", officialRating,
                                    formatRuntime(durationMs), calculateEndTime(durationMs), formatCommunityRating()]
                    qualityBadges: [root.videoResolutionBadge(), root.videoHdrBadge()]
                    ratings: root.visibleExternalRatings()
                    // Playback info and ratings load after the details.
                    reserveChipRow: true
                    detailItems: genres || []
                    synopsis: movieOverview
                    synopsisPlaceholder: isLoading ? "" : qsTr("No description available.")
                    synopsisExpandedLines: Math.max(4, Math.floor(root.height * 0.3 / (Theme.fontSizeBody * 1.45)))
                    primaryAction: playButton
                    progress: positionMs > 0 && !isPlayed && durationMs > 0 ? Math.min(1, positionMs / durationMs) : 0
                    footnote: progress > 0 ? root.remainingText() : ""

                    ActionButton {
                        id: playButton
                        variant: "primary"
                        text: positionMs > 0 ? qsTr("Resume") : qsTr("Play")
                        iconGlyph: Icons.playArrow
                        busy: playbackInfoLoading
                        enabled: movieId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(40 * Theme.layoutScale)

                        Accessible.name: text

                        KeyNavigation.right: trailerButton.visible ? trailerButton : markWatchedButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat && enabled) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat && enabled) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        onClicked: root.startPlaybackWithTracks()
                    }

                    // Plays the movie's local trailer; remote trailers are not offered.
                    SecondaryActionButton {
                        id: trailerButton
                        visible: root.hasTrailer
                        text: qsTr("Play Trailer")
                        iconGlyph: Icons.movie
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: playButton
                        KeyNavigation.right: markWatchedButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        onClicked: root.playExtra(root.trailer, trailerButton)
                    }

                    SecondaryActionButton {
                        id: markWatchedButton
                        text: isPlayed ? qsTr("Mark Unwatched") : qsTr("Mark Watched")
                        iconGlyph: isPlayed ? Icons.visibilityOff : Icons.visibility
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: trailerButton.visible ? trailerButton : playButton
                        KeyNavigation.right: watchlistButton.visible ? watchlistButton : contextMenuButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        onClicked: {
                            if (!root.sharedModelCurrent)
                                return
                            if (isPlayed) {
                                MovieDetailsViewModel.markAsUnwatched()
                            } else {
                                MovieDetailsViewModel.markAsWatched()
                            }
                        }
                    }

                    // Only providers with a watchlist (Silo) show this.
                    SecondaryActionButton {
                        id: watchlistButton
                        visible: MovieDetailsViewModel.watchlistSupported
                        text: MovieDetailsViewModel.inWatchlist ? qsTr("On Watchlist") : qsTr("Add to Watchlist")
                        iconGlyph: MovieDetailsViewModel.inWatchlist ? Icons.bookmark : Icons.bookmarkBorder
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: markWatchedButton
                        KeyNavigation.right: contextMenuButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                                event.accepted = true
                            }
                        }

                        onClicked: {
                            if (root.sharedModelCurrent)
                                MovieDetailsViewModel.toggleWatchlist()
                        }
                    }

                    SecondaryActionButton {
                        id: contextMenuButton
                        text: ""
                        iconGlyph: Icons.moreHoriz
                        accessibleLabel: qsTr("More options")
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: watchlistButton.visible ? watchlistButton : markWatchedButton

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.ensureTopVisible()
                            }
                        }

                        Keys.onUpPressed: (event) => root.heroUp(event)

                        Keys.onDownPressed: (event) => {
                            root.focusFirstLowerSection()
                            event.accepted = true
                        }

                        Keys.onRightPressed: (event) => { event.accepted = true }
                        Keys.onReturnPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                        Keys.onEnterPressed: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                        onClicked: contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                    }
                }
            }

            ColumnLayout {
                id: chapterSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                // Chapters without images use the compact timeline further down.
                visible: movieId !== "" && chapters.length > 0 && chaptersHaveImages

                SectionHeader {
                    text: qsTr("Chapters")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: chapterList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    model: chapters

                    delegate: MediaCard {
                        id: chapterCard
                        required property var modelData
                        required property int index
                        variant: "landscape"
                        artworkSources: [modelData.thumbnailUrl || ""]
                        title: modelData.name || qsTr("Chapter %1").arg(index + 1)
                        subtitle: root.formatChapterTime((modelData.startMs || 0) / 1000)
                        placeholderIcon: Icons.movie
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            chapterList.currentIndex = index
                            chapterList.forceActiveFocus()
                            root.startPlaybackWithTracksAt(modelData.startMs || 0, chapterList)
                        }
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(chapterSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(chapterList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(chapterList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < chapters.length)
                            root.startPlaybackWithTracksAt(chapters[currentIndex].startMs || 0, chapterList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < chapters.length)
                            root.startPlaybackWithTracksAt(chapters[currentIndex].startMs || 0, chapterList)
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: extrasSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: movieId !== "" && extras.length > 0

                SectionHeader {
                    text: qsTr("Extras")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: extrasList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    model: extras

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "landscape"
                        artworkSources: [root.artworkUrlFromRef(modelData.primaryArtwork, root.extraCardImageWidth)]
                        title: ExtrasUtils.title(modelData)
                        subtitle: ExtrasUtils.subtitle(modelData)
                        placeholderIcon: Icons.movie
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            extrasList.currentIndex = index
                            extrasList.forceActiveFocus()
                            root.playExtra(modelData, extrasList)
                        }
                        Accessible.description: qsTr("Play %1").arg(ExtrasUtils.title(modelData))
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(extrasSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(extrasList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(extrasList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < extras.length)
                            root.playExtra(extras[currentIndex], extrasList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < extras.length)
                            root.playExtra(extras[currentIndex], extrasList)
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: castSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: movieId !== "" && castAndCrew.length > 0

                SectionHeader {
                    text: qsTr("Cast & Crew")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: castList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: root.peopleCardHeight
                    model: castAndCrew

                    delegate: PersonCard {
                        required property var modelData
                        required property int index
                        width: root.peopleCardWidth
                        height: root.peopleCardHeight
                        itemData: modelData
                        isFocused: ListView.isCurrentItem && ListView.view.activeFocus
                        onActiveFocusChanged: if (activeFocus) castList.currentIndex = index
                        onClicked: {
                            castList.currentIndex = index
                            castList.forceActiveFocus()
                            root.openPerson(modelData)
                        }
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(castSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(castList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(castList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < castAndCrew.length)
                            root.openPerson(castAndCrew[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < castAndCrew.length)
                            root.openPerson(castAndCrew[currentIndex])
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: libraryRecommendationsSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: libraryRecommendations.length > 0

                SectionHeader {
                    text: qsTr("Recommended From Your Library")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: libraryRecommendationsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: libraryRecommendations

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "poster"
                        artworkSources: [root.artworkUrlFromRef(modelData.primaryArtwork, root.cardImageWidth)]
                        title: modelData.name || ""
                        subtitle: root.posterSubtitle(modelData)
                        placeholderIcon: modelData.mediaType === "Series" ? Icons.tvShows : Icons.movie
                        item: modelData
                        showWatchState: modelData.mediaType === "Series" || !!modelData.watched
                        unplayedCount: modelData.mediaType === "Series" ? (modelData.unplayedItemCount || 0) : 0
                        watched: !!modelData.watched
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            libraryRecommendationsList.currentIndex = index
                            libraryRecommendationsList.forceActiveFocus()
                            root.itemSelected(modelData)
                        }
                        Accessible.description: qsTr("Details for %1").arg(modelData.name || "")
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(libraryRecommendationsSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(libraryRecommendationsList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(libraryRecommendationsList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < libraryRecommendations.length)
                            root.itemSelected(libraryRecommendations[currentIndex])
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < libraryRecommendations.length)
                            root.itemSelected(libraryRecommendations[currentIndex])
                        event.accepted = true
                    }
                }
            }

            ChapterTimeline {
                id: chapterTimeline
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.preferredHeight: implicitHeight
                visible: movieId !== "" && chapters.length > 0 && !chaptersHaveImages
                chapters: root.chapters
                durationMs: root.durationMs
                positionMs: root.positionMs

                onActiveFocusChanged: if (activeFocus) root.scrollToSection(chapterTimeline)
                onChapterActivated: (startMs) => root.startPlaybackWithTracksAt(startMs, chapterTimeline)

                Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(chapterTimeline, -1) }
                Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(chapterTimeline, 1) }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(140 * Theme.layoutScale)
    }

    BloomMenu {
        id: contextMenu



        onOpened: {
            currentIndex = 0
            forceActiveFocus()
            if (currentMediaSource) {
                var resolved = PlayerController.resolveTrackSelectionForMediaSource(currentMediaSource, root.movieId, true)
                selectedAudioIndex = resolved.audioIndex
                selectedSubtitleIndex = resolved.subtitleIndex
            }
        }

        onClosed: {
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }

        BloomMenuItem {
            id: watchedMenuItem
            text: isPlayed ? qsTr("Mark as Unwatched") : qsTr("Mark as Watched")



            onTriggered: {
                if (!root.sharedModelCurrent) {
                    contextMenu.close()
                    return
                }
                if (isPlayed) {
                    MovieDetailsViewModel.markAsUnwatched()
                } else {
                    MovieDetailsViewModel.markAsWatched()
                }
                contextMenu.close()
            }
        }

        // Only where the provider offers it to the profile (Silo).
        BloomMenuItem {
            id: notInterestedMenuItem
            visible: MovieDetailsViewModel.notInterestedSupported
            enabled: visible
            height: visible ? implicitHeight : 0
            text: MovieDetailsViewModel.notInterested ? qsTr("Interested Again") : qsTr("Not Interested")

            onTriggered: {
                MovieDetailsViewModel.toggleNotInterested()
                contextMenu.close()
            }
        }

        // The viewer's own rating, where the provider has one: stars on
        // Silo, like or dislike on Jellyfin.
        BloomMenuItem {
            id: ratingMenuItem
            readonly property var ratingState: MovieDetailsViewModel.userRating
            visible: !!ratingState && ratingState.supported
            enabled: visible
            height: visible ? implicitHeight : 0
            text: {
                if (!ratingState || ratingState.value === 0)
                    return qsTr("Rate…")
                if (ratingState.scale === "likes")
                    return ratingState.value > 0 ? qsTr("Your Rating: Liked") : qsTr("Your Rating: Disliked")
                return ratingState.value === 1 ? qsTr("Your Rating: 1 Star")
                                               : qsTr("Your Rating: %1 Stars").arg(ratingState.value)
            }

            onTriggered: {
                contextMenu.close()
                if (!root.sharedModelCurrent)
                    return
                Qt.callLater(function() { userRatingDialog.openFor(contextMenuButton) })
            }
        }

        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.borderLight
            }
        }

        BloomMenu {
            id: audioMenu
            title: qsTr("Audio: %1").arg(root.selectedAudioSummary())
            enabled: getAudioStreams().length > 0


            Repeater {
                model: getAudioStreams()

                BloomMenuItem {
                    id: audioMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedAudioIndex
                    indicator: Item {}

                    marked: audioMenuItem.checked


                    onTriggered: {
                        selectedAudioIndex = modelData.index
                        if (root.movieId) {
                            PlayerController.chooseMovieAudioTrack(root.movieId, currentMediaSource || ({}),
                                                                   selectedAudioIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }

        BloomMenu {
            id: subtitleMenu
            title: qsTr("Subtitles: %1").arg(root.selectedSubtitleSummary())
            enabled: getSubtitleStreams().length > 0 || selectedSubtitleIndex === -1


            BloomMenuItem {
                text: qsTr("Off")
                checkable: true
                checked: selectedSubtitleIndex === -1
                indicator: Item {}



                onTriggered: {
                    selectedSubtitleIndex = -1
                    if (root.movieId) {
                        PlayerController.chooseMovieSubtitleTrack(root.movieId, currentMediaSource || ({}),
                                                                  selectedSubtitleIndex)
                    }
                    contextMenu.close()
                }
            }

            Repeater {
                model: getSubtitleStreams()

                BloomMenuItem {
                    id: subtitleMenuItem
                    required property var modelData

                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedSubtitleIndex
                    indicator: Item {}

                    marked: subtitleMenuItem.checked


                    onTriggered: {
                        selectedSubtitleIndex = modelData.index
                        if (root.movieId) {
                            PlayerController.chooseMovieSubtitleTrack(root.movieId, currentMediaSource || ({}),
                                                                      selectedSubtitleIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
    }

    Connections {
        target: PlaybackService

        function onPlaybackInfoLoaded(itemId, info) {
            if (itemId === root.movieId) {
                root.playbackInfo = info
                root.playbackInfoLoading = false

                if (info.mediaSources && info.mediaSources.length > 0) {
                    var source = info.mediaSources[0]
                    var resolved = PlayerController.resolveTrackSelectionForMediaSource(source, root.movieId, true)
                    root.selectedAudioIndex = resolved.audioIndex
                    root.selectedSubtitleIndex = resolved.subtitleIndex
                }
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint !== "getPlaybackInfo") {
                return
            }

            root.playbackInfoLoading = false
            root.resetPlaybackReturnFocusState()
        }
    }

    Connections {
        target: PlayerController

        function onIsPlaybackActiveChanged() {
            if (PlayerController.isPlaybackActive) {
                if (root.playbackReturnFocusPending) {
                    root.playbackReturnFocusActivated = true
                }
                return
            }

            if (!root.playbackReturnFocusPending
                    || !root.playbackReturnFocusActivated
                    || PlayerController.awaitingNextEpisodeResolution) {
                return
            }

            Qt.callLater(root.restoreFocusAfterPlaybackExit)
        }

        function onAwaitingNextEpisodeResolutionChanged() {
            if (PlayerController.awaitingNextEpisodeResolution) {
                root.resetPlaybackReturnFocusState()
            }
        }
    }

    Timer {
        id: focusTimer
        interval: 50
        repeat: false
        onTriggered: {
            if (playButton && playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                focusFirstLowerSection()
            }
        }
    }

    Timer {
        id: heroAutofocusResetTimer
        interval: 200
        repeat: false

        onTriggered: {
            root.suppressHeroAutofocus = false
        }
    }

    ToastNotification {
        id: playbackToast
        z: 300
    }

    SourceRouter {
        id: extrasRouter
        onFailed: (message) => playbackToast.show(message || qsTr("Couldn't reach the server for this extra."))
    }

    Connections {
        target: MovieDetailsViewModel
        function onMovieLoaded() {
            // Another movie page loaded; this one is hidden in the stack.
            if (!root.sharedModelCurrent)
                return
            if (root.awaitingSharedReload) {
                Qt.callLater(function() { root.finishSharedReload(false) })
                return
            }
            if (root.skipLoadedAutofocus) {
                root.skipLoadedAutofocus = false
                return
            }
            if (!hasPendingReturnStateForCurrentMovie() && !suppressHeroAutofocus) {
                focusTimer.start()
            }
            Qt.callLater(root.tryRestorePendingReturnState)
        }
        function onLoadError(error) {
            if (root.awaitingSharedReload && root.sharedModelCurrent)
                Qt.callLater(function() { root.finishSharedReload(false) })
        }
        function onWatchlistUpdateFailed(message) {
            if (root.sharedModelCurrent)
                playbackToast.show(message)
        }
        function onNotInterestedUpdateFailed(message) {
            playbackToast.show(message)
        }
    }

    Connections {
        target: MovieDetailsViewModel.userRating
        function onUpdateFailed(message) {
            if (root.sharedModelCurrent)
                playbackToast.show(message)
        }
    }

    UserRatingDialog {
        id: userRatingDialog
        ratingState: MovieDetailsViewModel.userRating
        itemTitle: root.movieName
        onRatingFailed: (message) => playbackToast.show(message)
    }

    Component.onCompleted: {
        if (movieId) {
            MovieDetailsViewModel.loadMovieDetails(movieId)
            playbackInfoLoading = true
            PlaybackService.getPlaybackInfo(movieId)
            Qt.callLater(root.tryRestorePendingReturnState)
        }
    }

    property var savedFocusItem: null
    property string savedFocusArea: "hero"
    property int savedFocusIndex: 0
    property bool restoringFocusFromNavigation: false
    property bool restoringFocusFromReturnState: false
    property bool suppressHeroAutofocus: false

    function saveFocusForNavigation() {
        savedFocusItem = root.Window.activeFocusItem
        savedFocusArea = currentFocusArea()
        savedFocusIndex = currentFocusIndex()
    }

    function restoreFocusFromNavigation() {
        restoringFocusFromNavigation = true
        Qt.callLater(root.restoreSavedSidebarFocus)
    }

    function restoreSavedSidebarFocus() {
        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === "function") {
            savedFocusItem.forceActiveFocus()
        } else if (restoreFocusToArea(savedFocusArea, savedFocusIndex)) {
        } else if (playButton.enabled) {
            playButton.forceActiveFocus()
        } else {
            root.focusFirstLowerSection()
        }

        savedFocusItem = null
        savedFocusArea = "hero"
        savedFocusIndex = 0
        Qt.callLater(function() {
            restoringFocusFromNavigation = false
        })
    }
}
