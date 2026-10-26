import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQml

import BloomUI
import "EpisodeSelection.js" as EpisodeSelection
import "TrackUtils.js" as TrackUtils

FocusScope {
    id: root
    
    // Input properties
    property string seriesId: ""
    property string seriesName: ""
    property string initialSeasonId: ""  // Season to load on entry
    property int initialSeasonIndex: -1  // Optional: season index to highlight
    property string initialEpisodeId: ""  // Optional: specific episode ID to highlight on load
    property int pendingAudioTrackIndex: -2
    property int pendingSubtitleTrackIndex: -2
    property bool pendingTrackOverrideConsumed: false
    property bool appliedPendingTrackOverride: false
    property bool userMadeAudioSelection: false
    property bool userMadeSubtitleSelection: false
    // LibraryScreen binds this from its own StackView attachment (not the nested view's).
    property bool screenStackActive: false
    readonly property bool activeVisibleDetailView: root.visible && screenStackActive

    onScreenStackActiveChanged: {
        if (!screenStackActive || !root.visible) {
            return
        }
        schedulePlaybackInfoPreload()
        if (!hasPendingPlayback || pendingPlaybackRequestEpisodeId !== selectedEpisodeId) {
            return
        }
        var fromBeginning = pendingPlaybackFromBeginning
        var restoreTarget = pendingPlaybackRestoreFocusTarget
        hasPendingPlayback = false
        pendingPlaybackRequestEpisodeId = ""
        pendingPlaybackFromBeginning = false
        pendingPlaybackRestoreFocusTarget = null
        Qt.callLater(function() {
            if (root.visible && screenStackActive && !PlayerController.awaitingNextEpisodeResolution) {
                performPlayback(fromBeginning, restoreTarget)
            }
        })
    }
    readonly property int heroPosterWidth: Math.round(320 * Theme.layoutScale)
    readonly property int heroPosterHeight: Math.round(heroPosterWidth * 1.5)
    readonly property int heroPanelPadding: Theme.spacingXLarge
    readonly property int heroActionsBottomSpacing: Theme.spacingMedium
    // The hero is pinned, so an expanded overview is capped to keep room for the
    // scrolling shelves below it.
    readonly property int overviewExpandedMaxLines: Math.max(4, Math.floor(root.height * 0.3 / (Theme.fontSizeBody * 1.45)))
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    // Chapter cards match the episode card height so both shelves fill the
    // viewport below the pinned header.
    readonly property int chapterRowHeight: Math.max(Math.round(224 * Theme.layoutScale),
                                                     episodesList.responsiveHeight - Math.round(50 * Theme.layoutScale))
    readonly property int chapterTextAllowance: Math.round((Theme.fontSizeBody + Theme.fontSizeCaption) * 1.4)
                                                + Theme.spacingSmall * 4
    readonly property int chapterCardWidth: Math.round((chapterRowHeight - chapterTextAllowance) * 16 / 9)
                                            + Theme.spacingSmall * 2
    readonly property var focusedEpisodePeople: SeriesDetailsViewModel.focusedEpisodePeople || []
    readonly property bool focusedEpisodeDetailsLoading: SeriesDetailsViewModel.focusedEpisodeDetailsLoading
    readonly property var focusedEpisodeChapters: SeriesDetailsViewModel.focusedEpisodeChapters || []
    readonly property bool focusedEpisodeChaptersLoading: SeriesDetailsViewModel.focusedEpisodeChaptersLoading
    // Chapters without images use the compact timeline at the bottom instead of cards.
    readonly property bool focusedEpisodeChaptersHaveImages: {
        for (var i = 0; i < focusedEpisodeChapters.length; ++i) {
            if (focusedEpisodeChapters[i].thumbnailUrl) {
                return true
            }
        }
        return false
    }
    // Remembers whether the last loaded episode had chapter cards so the section does
    // not flicker in and out while moving between episodes of a season without them.
    property bool lastEpisodeHadChapterCards: false
    onFocusedEpisodeChaptersChanged: {
        if (focusedEpisodeChapters.length > 0)
            lastEpisodeHadChapterCards = focusedEpisodeChaptersHaveImages
    }
    onFocusedEpisodeChaptersLoadingChanged: {
        if (!focusedEpisodeChaptersLoading)
            lastEpisodeHadChapterCards = focusedEpisodeChapters.length > 0 && focusedEpisodeChaptersHaveImages
    }
    readonly property string selectedEpisodeImageUrl: selectedEpisodeData
                                                     ? (selectedEpisodeData.imageUrl || "")
                                                     : ""
    // Prefer season poster, then series poster. Episode thumbs are landscape and only
    // used as a last-resort fill for the portrait hero frame.
    readonly property string selectedSeasonImageUrl: {
        if (SeriesDetailsViewModel.selectedSeasonIndex >= 0) {
            var season = SeriesDetailsViewModel.seasonsModel.getItem(SeriesDetailsViewModel.selectedSeasonIndex)
            if (season && season.imageUrl) {
                return season.imageUrl
            }
        }
        return SeriesDetailsViewModel.posterUrl || ""
    }
    readonly property string heroArtworkUrl: selectedSeasonImageUrl || selectedEpisodeImageUrl || ""
    readonly property bool heroArtworkUsesFit: selectedSeasonImageUrl === ""
                                               && selectedEpisodeImageUrl !== ""
    readonly property string selectedEpisodeLabel: {
        if (!selectedEpisodeData) {
            return ""
        }
        if (selectedSeasonNumber <= 0 || selectedEpisodeNumber <= 0) {
            return qsTr("Special")
        }
        return qsTr("S%1 E%2").arg(selectedSeasonNumber).arg(selectedEpisodeNumber)
    }
    
    // Signals for navigation and actions
    signal backRequested()
    signal playRequested(var request)
    signal autoplayOverridesConsumed()
    signal seriesDetailsRequested(string episodeId)
    
    Connections {
        target: SeriesDetailsViewModel
        function onSeriesLoaded() {
            // [FIX] When series details are loaded, ensure we enforce the initialSeasonId
            // This prevents the view from defaulting to the first season/specials if the series
            // load completes after the view is shown.
            Qt.callLater(function() {
                if (initialSeasonId !== "") {
                    console.log("[SeriesSeasonEpisodeView] Series loaded signal (delayed), checking enforcement. Target:", initialSeasonId, "Current:", SeriesDetailsViewModel.selectedSeasonId)
                    enforceInitialSeasonSelection()
                }
            })
        }
    }
    
    Connections {
        target: SeriesDetailsViewModel.seasonsModel
        function onModelReset() {
            // [FIX] When the seasons list is populated, the view model might default to the first season.
            // We need to re-enforce our desired season ID here.
            console.log("[SeriesSeasonEpisodeView] Seasons model reset, count:", SeriesDetailsViewModel.seasonsModel.rowCount())
            Qt.callLater(function() {
                if (initialSeasonId !== "") {
                    console.log("[SeriesSeasonEpisodeView] Checking season enforcement after model reset. Target:", initialSeasonId, "Current:", SeriesDetailsViewModel.selectedSeasonId)
                    enforceInitialSeasonSelection()
                }
            })
        }
    }
    
    // Currently selected episode (from ListView currentIndex)
    property var selectedEpisodeData: null
    property string selectedEpisodeId: selectedEpisodeData ? (selectedEpisodeData.itemId || "") : ""
    property string selectedEpisodeName: selectedEpisodeData ? (selectedEpisodeData.name || "") : ""
    property int selectedEpisodeNumber: selectedEpisodeData ? (selectedEpisodeData.indexNumber || 0) : 0
    property int selectedSeasonNumber: selectedEpisodeData ? (selectedEpisodeData.parentIndexNumber || 0) : 0
    property string selectedEpisodeOverview: selectedEpisodeData ? (selectedEpisodeData.overview || "") : ""
    property real selectedEpisodeDurationMs: selectedEpisodeData ? (selectedEpisodeData.durationMs || 0) : 0
    property real selectedEpisodeCommunityRating: selectedEpisodeData ? (selectedEpisodeData.communityRating || 0) : 0
    property string selectedEpisodePremiereDate: selectedEpisodeData ? (selectedEpisodeData.premiereDate || "") : ""
    property bool selectedEpisodeIsPlayed: episodesList.currentItem ? episodesList.currentItem.isPlayed : false
    property real selectedEpisodePlaybackPosition: episodesList.currentItem ? episodesList.currentItem.playbackPosition : 0
    property bool selectedEpisodeIsFavorite: episodesList.currentItem ? episodesList.currentItem.isFavorite : false
    
    property bool overviewExpanded: false
    onSelectedEpisodeIdChanged: {
        overviewExpanded = false
        clearEpisodePlaybackState()
        chapterPreloadTimer.stop()
        SeriesDetailsViewModel.clearFocusedEpisodeChapters()
        chapterPreloadTimer.start()
    }

    // Guard initial episode focusing/selection so async reloads do not override user input.
    property bool initialEpisodeSelectionPending: true
    property bool userHasInteracted: false
    property bool suppressInteractionTracking: false
    property bool playbackSelectionRestorePending: false
    property string playbackAnchorEpisodeId: ""
    property string pendingPlaybackSelectionEpisodeId: ""

    function resetInitialSelectionState() {
        initialEpisodeSelectionPending = true
        userHasInteracted = false
        playbackSelectionRestorePending = false
        playbackAnchorEpisodeId = ""
        pendingPlaybackSelectionEpisodeId = ""
        pendingTrackOverrideConsumed = false
        appliedPendingTrackOverride = false
        userMadeAudioSelection = false
        userMadeSubtitleSelection = false
    }

    function clearEpisodePlaybackState() {
        playbackInfoPreloadTimer.stop()
        playbackInfo = null
        playbackInfoOwnerId = ""
        playbackInfoLoadingItemId = ""
        pendingPlaybackInfo = null
        pendingPlaybackInfoOwnerId = ""
        pendingPlaybackRequestEpisodeId = ""
        hasPendingPlayback = false
        pendingPlaybackFromBeginning = false
        pendingPlaybackRestoreFocusTarget = null
        waitingForContextInfo = false
        lastLoadedPlaybackInfo = null
    }

    Timer {
        id: chapterPreloadTimer
        interval: 300
        repeat: false
        onTriggered: {
            if (selectedEpisodeId) {
                SeriesDetailsViewModel.loadFocusedEpisodeChapters(selectedEpisodeId)
            } else {
                SeriesDetailsViewModel.clearFocusedEpisodeChapters()
            }
        }
    }
    
    // Playback info storage - keeps the last loaded playback info
    property var lastLoadedPlaybackInfo: null
    
    // Helper functions to filter streams
    function getAudioStreams() {
        var pbInfo = playbackInfo || lastLoadedPlaybackInfo
        if (!pbInfo || !pbInfo.mediaSources || pbInfo.mediaSources.length === 0) return []
        var ms = pbInfo.mediaSources[0]
        if (!ms.mediaStreams) return []
        var audio = []
        for (var i = 0; i < ms.mediaStreams.length; i++) {
            var stream = ms.mediaStreams[i]
            if (stream.type === "Audio") {
                audio.push(stream)
            }
        }
        return audio
    }
    
    function getSubtitleStreams() {
        var pbInfo = playbackInfo || lastLoadedPlaybackInfo
        if (!pbInfo || !pbInfo.mediaSources || pbInfo.mediaSources.length === 0) return []
        var ms = pbInfo.mediaSources[0]
        if (!ms.mediaStreams) return []
        var subs = []
        for (var i = 0; i < ms.mediaStreams.length; i++) {
            var stream = ms.mediaStreams[i]
            if (stream.type === "Subtitle") {
                subs.push(stream)
            }
        }
        return subs
    }
    
    // Playback info for track selection
    property var playbackInfo: null
    property string playbackInfoOwnerId: ""
    property string playbackInfoLoadingItemId: ""
    property bool playbackReturnFocusPending: false
    property bool playbackReturnFocusActivated: false
    property Item lastPlaybackRestoreFocusTarget: null
    property var currentMediaSource: {
        var info = playbackInfoOwnerId === selectedEpisodeId ? playbackInfo : null
        if (!info && pendingPlaybackInfoOwnerId === selectedEpisodeId) {
            info = pendingPlaybackInfo
        }
        return info && info.mediaSources && info.mediaSources.length > 0 ? info.mediaSources[0] : null
    }
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1
    
    // Logo priority: Season logo -> Series logo -> Text fallback
    readonly property string displayLogoUrl: {
        // Try season-specific logo
        if (SeriesDetailsViewModel.selectedSeasonIndex >= 0) {
            var season = SeriesDetailsViewModel.seasonsModel.getItem(SeriesDetailsViewModel.selectedSeasonIndex)
            const artwork = season ? season.logoArtwork : null
            if (artwork && artwork.itemId) {
                return LibraryService.getCachedArtworkUrlFromRef(artwork, 600)
            }
        }
        // Fallback to series logo
        return SeriesDetailsViewModel.logoUrl
    }
    
    readonly property string displayName: {
        if (displayLogoUrl) return ""  // Hide text when logo available
        return seriesName
    }

    function seasonIndexForId(seasonId) {
        if (!seasonId) {
            return -1
        }

        for (var i = 0; i < SeriesDetailsViewModel.seasonsModel.rowCount(); ++i) {
            var season = SeriesDetailsViewModel.seasonsModel.getItem(i)
            if (season && season.itemId === seasonId) {
                return i
            }
        }

        return -1
    }

    function enforceInitialSeasonSelection() {
        if (initialSeasonId === "") {
            return
        }

        var targetIndex = initialSeasonIndex
        if (targetIndex >= 0 && targetIndex < SeriesDetailsViewModel.seasonsModel.rowCount()) {
            var targetSeason = SeriesDetailsViewModel.seasonsModel.getItem(targetIndex)
            var targetSeasonId = targetSeason ? (targetSeason.itemId || "") : ""
            if (targetSeasonId !== initialSeasonId) {
                targetIndex = -1
            }
        }

        if (targetIndex < 0 || targetIndex >= SeriesDetailsViewModel.seasonsModel.rowCount()) {
            targetIndex = seasonIndexForId(initialSeasonId)
        }

        if (targetIndex >= 0) {
            if (SeriesDetailsViewModel.selectedSeasonIndex !== targetIndex
                    || SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                console.log("[SeriesSeasonEpisodeView] Selecting initial season index:", targetIndex, "ID:", initialSeasonId)
                SeriesDetailsViewModel.selectSeason(targetIndex)
            } else {
                selectInitialEpisodeIfAlreadyLoaded()
            }
            return
        }

        if (SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
            console.log("[SeriesSeasonEpisodeView] Initial season index not available yet, loading by ID:", initialSeasonId)
            SeriesDetailsViewModel.loadSeasonEpisodes(initialSeasonId)
        } else {
            selectInitialEpisodeIfAlreadyLoaded()
        }
    }

    // The series page preloads the selected season, so opening that season emits no
    // episodesLoaded signal. Resolve the initial (next unwatched) episode from the cached model.
    function selectInitialEpisodeIfAlreadyLoaded() {
        Qt.callLater(function() {
            if (initialEpisodeSelectionPending && !userHasInteracted && episodesList.count > 0) {
                selectInitialEpisode()
            }
        })
    }
    
    // Key handling for back navigation
    Keys.onPressed: (event) => {
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            if (contextMenu.opened) {
                console.log("[SeriesSeasonEpisodeView] Ignoring Back/Escape - context menu is open")
                event.accepted = true
                return
            }
            console.log("[SeriesSeasonEpisodeView] Back key pressed")
            root.backRequested()
            event.accepted = true
        }
    }
    
    // Focus delegation: when this view receives focus, delegate to the episode list
    onActiveFocusChanged: {
        if (activeFocus) {
            console.log("[SeriesSeasonEpisodeView] View received focus, delegating to episodesList")
            episodesList.forceActiveFocus()
        }
    }
    
    focus: true
    
    // Load series details and initial season
    Component.onCompleted: {
        resetInitialSelectionState()
        if (seriesId !== "" && SeriesDetailsViewModel.seriesId !== seriesId) {
            console.log("[SeriesSeasonEpisodeView] Loading series details needed:", seriesId)
            SeriesDetailsViewModel.loadSeriesDetails(seriesId)
        } else {
            console.log("[SeriesSeasonEpisodeView] Series details already loaded/matching for:", seriesId)
        }
        // Initial season loading is handled via onInitialSeasonIdChanged
    }
    
    onInitialSeasonIdChanged: {
        if (initialSeasonId !== "") {
            resetInitialSelectionState()
            console.log("[SeriesSeasonEpisodeView] Loading initial season:", initialSeasonId)
            enforceInitialSeasonSelection()
        }
    }

    onInitialEpisodeIdChanged: {
        resetInitialSelectionState()
        if (initialEpisodeId === "") {
            return
        }

        Qt.callLater(function() {
            if (initialSeasonId !== "" && SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                enforceInitialSeasonSelection()
            }

            if (SeriesDetailsViewModel.selectedSeasonId) {
                SeriesDetailsViewModel.refreshSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
            } else if (initialSeasonId !== "") {
                SeriesDetailsViewModel.refreshSeasonEpisodes(initialSeasonId)
            } else if (episodesList.count > 0) {
                selectInitialEpisode()
            }
        })
    }

    function restoreEpisodeSelection(episodeId) {
        resetInitialSelectionState()
        initialEpisodeSelectionPending = true
        userHasInteracted = false

        Qt.callLater(function() {
            if (initialSeasonId !== "" && SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                enforceInitialSeasonSelection()
            }

            if (!selectInitialEpisode(episodeId)) {
                if (initialSeasonId !== "") {
                    SeriesDetailsViewModel.refreshSeasonEpisodes(initialSeasonId)
                } else if (SeriesDetailsViewModel.selectedSeasonId) {
                    SeriesDetailsViewModel.refreshSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
                }
            }

            episodesList.forceActiveFocus()
        })
    }
    
    // When episodes are loaded, find next-up/partially-watched episode
    Connections {
        target: SeriesDetailsViewModel
        function onEpisodesLoaded() {
            Qt.callLater(function() {
                if (initialEpisodeSelectionPending && !userHasInteracted) {
                    selectInitialEpisode()
                }
            })
        }
    }
    
    function selectInitialEpisode(episodeIdOverride) {
        if (episodesList.count === 0) {
            console.log("[SeriesSeasonEpisodeView] selectInitialEpisode: No episodes, skipping")
            return false
        }

        var episodes = []
        for (var i = 0; i < episodesList.count; i++) {
            episodes.push(SeriesDetailsViewModel.episodesModel.getItem(i))
        }

        var targetEpisodeId = episodeIdOverride || initialEpisodeId
        if (targetEpisodeId !== "") {
            console.log("[SeriesSeasonEpisodeView] Looking for initial episode ID:", targetEpisodeId, "in count:", episodesList.count, "for season:", SeriesDetailsViewModel.selectedSeasonId)
        }

        var selection = EpisodeSelection.resolveInitialEpisodeSelection(episodes, targetEpisodeId, initialSeasonId)
        if (!selection.shouldApply && targetEpisodeId !== "") {
            if (selection.waitingForTargetSeason) {
                console.log("[SeriesSeasonEpisodeView] Episode model still belongs to season:", selection.currentSeasonId,
                            "waiting for target season:", initialSeasonId)
                return false
            }

            if (initialSeasonId !== "" && SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                console.log("[SeriesSeasonEpisodeView] Initial episode not in current season yet, waiting for season:", initialSeasonId)
                return false
            }

            console.warn("[SeriesSeasonEpisodeView] Initial episode ID not found after loading target season:", targetEpisodeId)
            initialEpisodeSelectionPending = false
            return false
        }

        var targetIndex = selection.targetIndex
        console.log("[SeriesSeasonEpisodeView] Setting currentIndex to:", targetIndex)
        suppressInteractionTracking = true
        episodesList.currentIndex = targetIndex
        suppressInteractionTracking = false
        episodesList.positionViewAtIndex(targetIndex, ListView.Center)
        updateSelectedEpisode(targetIndex)
        initialEpisodeSelectionPending = false
        
        // Don't request playback info on initial load - only request when user presses play
        
        // Focus the episode list with a slight delay to ensure UI is ready
        Qt.callLater(function() {
            console.log("[SeriesSeasonEpisodeView] Restoring focus to episodesList")
            episodesList.forceActiveFocus()
        })
        return true
    }

    function episodeIndexById(episodeId) {
        if (!episodeId) {
            return -1
        }

        for (var i = 0; i < SeriesDetailsViewModel.episodesModel.rowCount(); ++i) {
            var episode = SeriesDetailsViewModel.episodesModel.getItem(i)
            if (episode && episode.itemId === episodeId) {
                return i
            }
        }

        return -1
    }

    function applyEpisodeSelection(index, alignmentMode) {
        if (index < 0 || index >= episodesList.count) {
            return false
        }

        suppressInteractionTracking = true
        episodesList.currentIndex = index
        suppressInteractionTracking = false
        episodesList.positionViewAtIndex(index, alignmentMode || ListView.Center)
        updateSelectedEpisode(index)
        return true
    }

    function resolveNextUnplayedIndexAfter(anchorEpisodeId) {
        var episodes = []
        for (var i = 0; i < episodesList.count; ++i) {
            episodes.push(SeriesDetailsViewModel.episodesModel.getItem(i))
        }

        if (episodes.length === 0) {
            return -1
        }

        var anchorIndex = episodeIndexById(anchorEpisodeId)
        if (anchorIndex >= 0) {
            for (var afterIndex = anchorIndex + 1; afterIndex < episodes.length; ++afterIndex) {
                var afterEpisode = episodes[afterIndex]
                if (afterEpisode && !EpisodeSelection.episodeIsPlayed(afterEpisode)) {
                    return afterIndex
                }
            }
        }

        var fallback = EpisodeSelection.resolveInitialEpisodeSelection(episodes, "", initialSeasonId)
        return fallback.shouldApply ? fallback.targetIndex : -1
    }

    function applyPendingPlaybackSelection() {
        if (!playbackSelectionRestorePending || episodesList.count === 0) {
            return false
        }

        var targetIndex = episodeIndexById(pendingPlaybackSelectionEpisodeId)
        if (targetIndex < 0) {
            targetIndex = resolveNextUnplayedIndexAfter(playbackAnchorEpisodeId)
        }
        if (targetIndex < 0 && episodesList.currentIndex >= 0) {
            targetIndex = Math.min(episodesList.currentIndex, episodesList.count - 1)
        }

        playbackSelectionRestorePending = false
        playbackAnchorEpisodeId = ""
        pendingPlaybackSelectionEpisodeId = ""

        return applyEpisodeSelection(targetIndex, ListView.Center)
    }
    
    function updateSelectedEpisode(index) {
        if (index >= 0 && index < SeriesDetailsViewModel.episodesModel.rowCount()) {
            selectedEpisodeData = SeriesDetailsViewModel.episodesModel.getItem(index)
            SeriesDetailsViewModel.loadFocusedEpisodeDetails(selectedEpisodeData.itemId || "")
            console.log("[SeriesSeasonEpisodeView] Selected episode:", selectedEpisodeName, "ID:", selectedEpisodeId,
                        "Played:", selectedEpisodeIsPlayed)
        } else {
            selectedEpisodeData = null
            SeriesDetailsViewModel.clearFocusedEpisodeDetails()
            SeriesDetailsViewModel.clearFocusedEpisodeChapters()
        }
    }
    
    // Refresh episode progress when playback stops
    Connections {
        target: PlayerController
        function onPlaybackStopped() {
            if (SeriesDetailsViewModel.selectedSeasonId) {
                playbackSelectionRestorePending = true
                playbackAnchorEpisodeId = selectedEpisodeId
                pendingPlaybackSelectionEpisodeId = ""
                if (PlayerController.awaitingNextEpisodeResolution) {
                    return
                }
                refreshEpisodesTimer.start()
            }
        }
    }
    
    Timer {
        id: refreshEpisodesTimer
        interval: 200  // Minimal delay to ensure playback stop event is processed before refresh
        repeat: false
        onTriggered: {
            if (!root.activeVisibleDetailView || PlayerController.awaitingNextEpisodeResolution) {
                return
            }
            console.log("[SeriesSeasonEpisodeView] Refreshing episodes immediately after playback stop")
            if (SeriesDetailsViewModel.selectedSeasonId) {
                SeriesDetailsViewModel.refreshSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
            }
        }
    }
    
    // Update selected episode data when episodes are reloaded
    Connections {
        target: SeriesDetailsViewModel
        function onEpisodesLoaded() {
            if (playbackSelectionRestorePending && applyPendingPlaybackSelection()) {
                return
            }

            if (initialEpisodeSelectionPending && !userHasInteracted) {
                return
            }

            if (!initialEpisodeSelectionPending) {
                updateSelectedEpisode(episodesList.currentIndex)
            }
        }
    }

    // Update selected episode data when played status changes
    Connections {
        target: LibraryService
        function onItemPlayedStatusChanged(itemId, isPlayed) {
            if (itemId === selectedEpisodeId) {
                // Refresh the episodes model to get updated UserData
                if (SeriesDetailsViewModel.selectedSeasonId) {
                    playbackSelectionRestorePending = true
                    playbackAnchorEpisodeId = selectedEpisodeId
                    pendingPlaybackSelectionEpisodeId = selectedEpisodeId
                    SeriesDetailsViewModel.refreshSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
                }
            }
        }
    }
    
    // Helper functions for episode info display
    function formatRuntime(durationMs) {
        if (!durationMs || durationMs === 0) return ""
        const totalMinutes = Math.round(durationMs / 60000)
        const hours = Math.floor(totalMinutes / 60)
        const minutes = totalMinutes % 60
        if (hours > 0) {
            return hours + "h " + minutes + "m"
        }
        return minutes + "m"
    }
    
    function formatRating(rating) {
        if (!rating || rating === 0) return ""
        return "★ " + rating.toFixed(1)
    }
    
    function formatPremiereDate(dateStr) {
        if (!dateStr) return ""
        var date = new Date(dateStr)
        return date.toLocaleDateString(Qt.locale(), "MMMM d, yyyy")
    }
    
    function calculateEndTime(durationMs) {
        if (!durationMs || durationMs === 0) return ""
        const now = new Date()
        const endTime = new Date(now.getTime() + durationMs)
        return "Ends at " + endTime.toLocaleTimeString(Qt.locale(), "h:mm AP")
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
    
    // Debounce timer for preloading playback info on episode selection
    // Prevents excessive API calls when user rapidly navigates episodes
    Timer {
        id: playbackInfoPreloadTimer
        interval: 300
        repeat: false
        onTriggered: {
            if (root.visible
                    && screenStackActive
                    && !PlayerController.awaitingNextEpisodeResolution
                    && selectedEpisodeId
                    && playbackInfoOwnerId !== selectedEpisodeId
                    && playbackInfoLoadingItemId !== selectedEpisodeId) {
                playbackInfoLoadingItemId = selectedEpisodeId
                console.log("[SeriesSeasonEpisodeView] Preloading playback info for episode:", selectedEpisodeId)
                PlaybackService.getPlaybackInfo(selectedEpisodeId)
            }
        }
    }
    
    // Playback info request
    function requestPlaybackInfo() {
        if (!selectedEpisodeId
                || playbackInfoLoadingItemId === selectedEpisodeId
                || !root.visible
                || !screenStackActive
                || PlayerController.awaitingNextEpisodeResolution) {
            return
        }
        
        playbackInfoLoadingItemId = selectedEpisodeId
        console.log("[SeriesSeasonEpisodeView] Requesting playback info for episode:", selectedEpisodeId)
        PlaybackService.getPlaybackInfo(selectedEpisodeId)
    }
    
    // Trigger debounced playback info preload when episode is selected
    function schedulePlaybackInfoPreload() {
        if (PlayerController.awaitingNextEpisodeResolution || !selectedEpisodeId) {
            return
        }
        playbackInfoPreloadTimer.restart()
    }

    // Open track selector with fresh playback info
    property bool waitingForContextInfo: false
    
    function openTrackSelector() {
        if (!selectedEpisodeId) {
            console.warn("[SeriesSeasonEpisodeView] No episode selected, cannot open track selector")
            return
        }
        
        console.log("[SeriesSeasonEpisodeView] Opening track selector for episode:", selectedEpisodeId, selectedEpisodeName)
        
        // If we already have playback info for this episode, open immediately
        if (playbackInfoOwnerId === selectedEpisodeId
                && playbackInfo
                && playbackInfo.mediaSources
                && playbackInfo.mediaSources.length > 0) {
            console.log("[SeriesSeasonEpisodeView] Opening track selector with cached playback info")
            contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
            return
        }
        
        // Otherwise, request playback info and open when ready
        waitingForContextInfo = true
        playbackInfoLoadingItemId = selectedEpisodeId
        PlaybackService.getPlaybackInfo(selectedEpisodeId)
        // The response will come through the Connections handler below
    }
    
    // Connection to receive playback info
    Connections {
        target: PlaybackService
        
        function onPlaybackInfoLoaded(itemId, info) {
            if (itemId === selectedEpisodeId) {
                playbackInfo = info
                playbackInfoOwnerId = itemId
                playbackInfoLoadingItemId = ""
                lastLoadedPlaybackInfo = info  // Persist for track selector access
                pendingPlaybackInfo = info  // Store for deferred playback
                pendingPlaybackInfoOwnerId = itemId
                
                // Apply track preferences
                applyTrackPreferences()
                
                // If there's a pending playback request, execute it now
                if (hasPendingPlayback && pendingPlaybackRequestEpisodeId === itemId) {
                    hasPendingPlayback = false
                    var requestEpisodeId = pendingPlaybackRequestEpisodeId
                    var fromBeginning = pendingPlaybackFromBeginning
                    var restoreFocusTarget = pendingPlaybackRestoreFocusTarget
                    console.log("[SeriesSeasonEpisodeView] Executing pending playback, fromBeginning:", fromBeginning, "playbackInfo available:", playbackInfo !== null)
                    // Use callLater to ensure property bindings have updated
                    Qt.callLater(function() {
                        if (selectedEpisodeId === requestEpisodeId
                                && root.visible
                                && screenStackActive
                                && !PlayerController.awaitingNextEpisodeResolution) {
                            performPlayback(fromBeginning, restoreFocusTarget)
                        }
                    })
                }
                
                // Open the popup if we were waiting for this info
                if (waitingForContextInfo) {
                    waitingForContextInfo = false
                    contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                }
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint !== "getPlaybackInfo") {
                return
            }

            playbackInfoLoadingItemId = ""
            waitingForContextInfo = false
            hasPendingPlayback = false
            pendingPlaybackRequestEpisodeId = ""
            pendingPlaybackRestoreFocusTarget = null
        }
    }
    
    function applyTrackPreferences() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return

        var preferredAudio = pendingTrackOverrideConsumed ? -2 : pendingAudioTrackIndex
        var preferredSubtitle = pendingTrackOverrideConsumed ? -2 : pendingSubtitleTrackIndex
        var hasPendingOverride = preferredAudio >= 0 || preferredSubtitle >= -1
        var resolved = PlayerController.resolveTrackSelectionForMediaSource(
                    currentMediaSource,
                    SeriesDetailsViewModel.selectedSeasonId,
                    false,
                    preferredAudio,
                    preferredSubtitle)

        selectedAudioIndex = resolved.audioIndex
        selectedSubtitleIndex = resolved.subtitleIndex
        pendingTrackOverrideConsumed = true
        appliedPendingTrackOverride = hasPendingOverride
        if (hasPendingOverride) {
            root.autoplayOverridesConsumed()
        }

        console.log("[SeriesSeasonEpisodeView] applyTrackPreferences - seasonId:",
                    SeriesDetailsViewModel.selectedSeasonId,
                    "audio:", resolved.audioIndex, resolved.audioSource,
                    "subtitle:", resolved.subtitleIndex, resolved.subtitleSource)
    }

    function getVideoFramerate() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return 0.0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") {
                if (stream.realFrameRate && stream.realFrameRate > 0) {
                    return stream.realFrameRate
                }
                if (stream.averageFrameRate && stream.averageFrameRate > 0) {
                    return stream.averageFrameRate
                }
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
    
    // Track pending playback request when waiting for playback info
    property bool pendingPlaybackFromBeginning: false
    property bool hasPendingPlayback: false
    property var pendingPlaybackInfo: null  // Store playback info for deferred playback
    property string pendingPlaybackInfoOwnerId: ""
    property string pendingPlaybackRequestEpisodeId: ""
    property Item pendingPlaybackRestoreFocusTarget: null

    function showPlaybackInfoNotReadyToast() {
        playbackToast.show(qsTr("Playback is still preparing. Try again in a moment."))
    }

    function resetPlaybackReturnFocusState() {
        playbackReturnFocusPending = false
        playbackReturnFocusActivated = false
        lastPlaybackRestoreFocusTarget = null
    }

    function restoreFocusAfterPlaybackExit() {
        if (!root.visible
                || !root.activeVisibleDetailView
                || !root.playbackReturnFocusPending
                || !root.playbackReturnFocusActivated
                || PlayerController.awaitingNextEpisodeResolution) {
            return
        }

        root.playbackReturnFocusPending = false
        root.playbackReturnFocusActivated = false

        root.forceActiveFocus()
        Qt.callLater(function() {
            const target = lastPlaybackRestoreFocusTarget
            if (target && target.parent && typeof target.forceActiveFocus === "function") {
                target.forceActiveFocus()
            } else if (playResumeButton && playResumeButton.enabled) {
                playResumeButton.forceActiveFocus()
            } else if (episodesList && episodesList.count > 0) {
                episodesList.forceActiveFocus()
            } else {
                root.forceActiveFocus()
            }
        })
    }
    
    // Playback actions
    function startPlayback(fromBeginning, restoreFocusTarget, chapterStartMs) {
        if (!selectedEpisodeId || PlayerController.awaitingNextEpisodeResolution) {
            return
        }
        if (!screenStackActive) {
            console.log("[SeriesSeasonEpisodeView] startPlayback deferred until library screen is active")
            hasPendingPlayback = true
            pendingPlaybackRequestEpisodeId = selectedEpisodeId
            pendingPlaybackFromBeginning = fromBeginning
            pendingPlaybackRestoreFocusTarget = restoreFocusTarget || null
            return
        }

        console.log("[SeriesSeasonEpisodeView] startPlayback - Episode:", selectedEpisodeName,
                    "ID:", selectedEpisodeId,
                    "fromBeginning:", fromBeginning,
                    "hasPlaybackInfo:", playbackInfo !== null)

        if (playbackInfoOwnerId !== selectedEpisodeId && playbackInfoLoadingItemId !== selectedEpisodeId) {
            schedulePlaybackInfoPreload()
        }

        performPlayback(fromBeginning, restoreFocusTarget, chapterStartMs)
    }
    
    function performPlayback(fromBeginning, restoreFocusTarget, chapterStartMs) {
        if (!selectedEpisodeId || PlayerController.awaitingNextEpisodeResolution) {
            return
        }
        if (!screenStackActive) {
            return
        }
        
        var hasChapterStart = chapterStartMs !== undefined && chapterStartMs !== null
        var startPositionMs = hasChapterStart ? chapterStartMs : (fromBeginning ? 0 : selectedEpisodePlaybackPosition)
        var framerate = getVideoFramerate()
        var isHDR = isVideoHDR()
        
        // Use pendingPlaybackInfo if playbackInfo is not set
        var info = playbackInfoOwnerId === selectedEpisodeId ? playbackInfo : null
        if (!info && pendingPlaybackInfoOwnerId === selectedEpisodeId) {
            info = pendingPlaybackInfo
        }
        var mediaSource = currentMediaSource  // Use the same source as applyTrackPreferences
        
        console.log("[SeriesSeasonEpisodeView] performPlayback - Episode:", selectedEpisodeName,
                    "ID:", selectedEpisodeId,
                    "fromBeginning:", fromBeginning,
                    "startPositionMs:", startPositionMs,
                    "playbackInfo:", playbackInfo !== null,
                    "pendingPlaybackInfo:", pendingPlaybackInfo !== null,
                    "currentMediaSource:", currentMediaSource !== null,
                    "mediaSource:", mediaSource !== null)
        console.log("[SeriesSeasonEpisodeView] performPlayback - selectedAudioIndex:", selectedAudioIndex, "selectedSubtitleIndex:", selectedSubtitleIndex)

        var overlayTitle = seriesName || qsTr("Now Playing")
        var episodePrefix = qsTr("S%1 E%2").arg(selectedSeasonNumber).arg(selectedEpisodeNumber)
        var overlaySubtitle = selectedEpisodeName ? (episodePrefix + " - " + selectedEpisodeName) : episodePrefix
        lastPlaybackRestoreFocusTarget = restoreFocusTarget || pendingPlaybackRestoreFocusTarget || playResumeButton
        playbackReturnFocusPending = true
        playbackReturnFocusActivated = false
        root.playRequested({
            itemId: selectedEpisodeId,
            startPositionMs: Math.round(startPositionMs),
            seriesId: seriesId,
            seasonId: SeriesDetailsViewModel.selectedSeasonId,
            overlayTitle: overlayTitle,
            overlaySubtitle: overlaySubtitle,
            overlayBackdropUrl: SeriesDetailsViewModel.backdropUrl || SeriesDetailsViewModel.posterUrl,
            overlayLogoUrl: displayLogoUrl,
            preferredAudioIndex: selectedAudioIndex,
            preferredSubtitleIndex: selectedSubtitleIndex,
            isMovie: false,
            allowVersionPrompt: true,
            framerateHint: framerate,
            isHDRHint: isHDR,
            restoreFocusTarget: lastPlaybackRestoreFocusTarget
        })

        pendingPlaybackInfo = null
        pendingPlaybackInfoOwnerId = ""
        pendingPlaybackRequestEpisodeId = ""
        pendingPlaybackRestoreFocusTarget = null
    }
    
    function toggleWatchedStatus() {
        if (!selectedEpisodeId) return
        
        if (selectedEpisodeIsPlayed) {
            LibraryService.markItemUnplayed(selectedEpisodeId)
        } else {
            LibraryService.markItemPlayed(selectedEpisodeId)
        }
    }
    
    function toggleFavorite() {
        if (!selectedEpisodeId) return
        LibraryService.toggleFavorite(selectedEpisodeId, !selectedEpisodeIsFavorite)
    }
    
    // Backdrop with fade. Full-bleed under the floating rail, sharp artwork
    // with a leading-edge scrim and a floor for the episode shelf.
    Item {
        id: backdropContainer
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        z: 0
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundPrimary
        }

        Image {
            id: backdropImage
            anchors.fill: parent
            source: SeriesDetailsViewModel.backdropUrl || SeriesDetailsViewModel.posterUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

            // Poster fallbacks are low resolution; soften them.
            layer.enabled: !SeriesDetailsViewModel.backdropUrl
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.8
                blurMax: 48
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.90) }
                GradientStop { position: 0.45; color: Theme.withAlpha(Theme.backgroundPrimary, 0.55) }
                GradientStop { position: 0.85; color: Theme.withAlpha(Theme.backgroundPrimary, 0.10) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
                GradientStop { position: 0.45; color: Theme.withAlpha(Theme.backgroundPrimary, 0.25) }
                GradientStop { position: 0.75; color: Theme.withAlpha(Theme.backgroundPrimary, 0.82) }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.96) }
            }
        }
    }
    
    // Pinned header: the season tabs and episode hero stay in place while the
    // shelves below scroll.
    ColumnLayout {
        id: pinnedHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: Theme.pageMarginTop
        spacing: Theme.sectionSpacing - Theme.spacingMedium
        z: 2

        ListView {
            id: seasonsTabList
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightSmall + Theme.spacingSmall
            orientation: ListView.Horizontal
            spacing: Theme.spacingSmall
            clip: false
            model: SeriesDetailsViewModel.seasonsModel
            currentIndex: Math.max(0, SeriesDetailsViewModel.selectedSeasonIndex)

            onActiveFocusChanged: {
                if (activeFocus) {
                    mainContentFlickable.contentY = 0
                }
            }

            delegate: ItemDelegate {
                width: seasonTabText.implicitWidth + Theme.spacingXLarge
                height: Theme.buttonHeightSmall
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                property bool isSelected: SeriesDetailsViewModel.selectedSeasonId === model.itemId

                KeyNavigation.down: playResumeButton

                // Season tabs: selected = accent underline, focus = ring.
                background: Rectangle {
                    id: seasonTabBackground
                    radius: Theme.radiusSmall
                    color: parent.down ? Theme.surfaceInteractivePressed
                         : (parent.hovered || parent.activeFocus ? Theme.surfaceInteractive : "transparent")

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        width: parent.width - Theme.spacingLarge
                        height: Theme.selectionIndicatorWidth
                        radius: height / 2
                        color: Theme.selectionIndicator
                        visible: isSelected
                    }

                    FocusRing {
                        inset: true
                        active: seasonTabBackground.parent.activeFocus
                        radius: seasonTabBackground.radius
                    }
                }

                contentItem: Text {
                    id: seasonTabText
                    text: model.name || (model.indexNumber > 0 ? qsTr("Season %1").arg(model.indexNumber) : qsTr("Specials"))
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: isSelected ? Theme.fontWeightStrong : Theme.fontWeightRegular
                    color: isSelected ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    SeriesDetailsViewModel.selectSeason(index)
                }

                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    clicked()
                    event.accepted = true
                }
            }
        }

        GlassPanel {
            id: heroPanel
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(heroPosterHeight + root.heroPanelPadding * 2,
                                             heroContent.implicitHeight + root.heroPanelPadding * 2)

            RowLayout {
                id: heroContent
                anchors.fill: parent
                anchors.margins: root.heroPanelPadding
                spacing: Theme.spacingXLarge + Theme.spacingSmall

                Item {
                    Layout.preferredWidth: heroPosterWidth
                    Layout.preferredHeight: heroPosterHeight

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        color: root.heroArtworkUsesFit ? Theme.surfaceSunken : "transparent"

                        Image {
                            id: heroPosterImage
                            anchors.fill: parent
                            anchors.margins: heroArtworkUsesFit ? Theme.spacingSmall : 0
                            source: heroArtworkUrl
                            fillMode: root.heroArtworkUsesFit ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true

                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: heroPosterMask
                            }
                        }

                        Rectangle {
                            id: heroPosterMask
                            anchors.fill: parent
                            radius: Theme.imageRadius
                            visible: false
                            layer.enabled: true
                            layer.smooth: true
                        }

                        ArtworkPlaceholder {
                            anchors.fill: parent
                            visible: heroPosterImage.status !== Image.Ready
                            loading: heroPosterImage.status === Image.Loading
                            iconGlyph: Icons.tvShows
                        }

                        Row {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: Theme.spacingSmall
                            spacing: Theme.spacingXSmall
                            opacity: root.hasMediaBadges() ? 1.0 : 0.0

                            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }

                            MetadataChip {
                                text: root.videoResolutionBadge()
                                onArtwork: true
                            }
                            MetadataChip {
                                text: root.videoHdrBadge()
                                onArtwork: true
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.spacingMedium
                            radius: Theme.radiusMedium
                            color: Theme.overlayTextBackground
                            border.width: 1
                            border.color: Theme.overlayTextBorder
                            visible: selectedEpisodeLabel !== ""
                            implicitHeight: episodeLabelText.implicitHeight + Math.round(14 * Theme.layoutScale)
                            implicitWidth: episodeLabelText.implicitWidth + Math.round(26 * Theme.layoutScale)

                            Text {
                                id: episodeLabelText
                                anchors.centerIn: parent
                                text: selectedEpisodeLabel
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                font.weight: Theme.fontWeightHeavy
                                color: Theme.textOnDarkOverlay
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingMedium

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: displayLogoUrl !== "" ? Theme.detailViewLogoHeight : titleFallback.implicitHeight

                        ArtworkLogo {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(Theme.seriesLogoMaxWidth, parent.width)
                            height: parent.height
                            source: displayLogoUrl
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                            cache: true
                            visible: displayLogoUrl !== ""
                            opacity: status === Image.Ready ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                        }

                        Text {
                            id: titleFallback
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            text: displayName || seriesName
                            visible: displayLogoUrl === ""
                            font.pixelSize: Theme.fontSizeHero
                            font.family: Theme.fontPrimary
                            font.weight: Theme.fontWeightHeavy
                            color: Theme.textPrimary
                            wrapMode: Text.WordWrap
                        }
                    }

                    Text {
                        text: selectedEpisodeName !== "" ? selectedEpisodeName : qsTr("Select an episode")
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }

                    MetadataLine {
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontSizeBody
                        items: [selectedEpisodeLabel,
                                selectedEpisodeDurationMs > 0 ? formatRuntime(selectedEpisodeDurationMs) : "",
                                selectedEpisodeDurationMs > 0 ? calculateEndTime(selectedEpisodeDurationMs) : "",
                                selectedEpisodeCommunityRating > 0 ? formatRating(selectedEpisodeCommunityRating) : "",
                                selectedEpisodePremiereDate !== "" ? formatPremiereDate(selectedEpisodePremiereDate) : "",
                                selectedEpisodeIsPlayed ? qsTr("Watched")
                                                        : (selectedEpisodePlaybackPosition > 0 ? qsTr("In Progress") : "")]
                    }

                    Item {
                        id: overviewContainer
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.overviewExpanded
                                               ? overviewColumn.implicitHeight
                                               : overviewContainer.collapsedLayoutHeight

                        // Collapsed height is fixed so the hero does not jump while browsing
                        // episodes with different overview lengths.
                        readonly property int collapsedLines: 3
                        readonly property int collapsedHeight: Math.round(Theme.fontSizeBody * 1.45 * collapsedLines)
                        readonly property int buttonRowHeight: Theme.buttonHeightSmall
                        readonly property int collapsedLayoutHeight: collapsedHeight + buttonRowHeight + overviewColumn.spacing
                        property bool hasOverflow: overviewText.truncated || root.overviewExpanded

                        ColumnLayout {
                            id: overviewColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Theme.spacingSmall

                            Text {
                                id: overviewText
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.overviewExpanded ? implicitHeight : overviewContainer.collapsedHeight
                                verticalAlignment: Text.AlignTop
                                text: selectedEpisodeOverview || qsTr("No description available.")
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: selectedEpisodeOverview ? Theme.textPrimary : Theme.textSecondary
                                opacity: 0.9
                                lineHeight: 1.12
                                wrapMode: Text.WordWrap
                                maximumLineCount: root.overviewExpanded ? root.overviewExpandedMaxLines : overviewContainer.collapsedLines
                                elide: Text.ElideRight
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: overviewContainer.buttonRowHeight

                                ActionButton {
                                    id: readMoreButton
                                    variant: "ghost"
                                    visible: overviewContainer.hasOverflow
                                    anchors.left: parent.left
                                    anchors.leftMargin: -Theme.spacingMedium
                                    anchors.verticalCenter: parent.verticalCenter
                                    implicitHeight: overviewContainer.buttonRowHeight
                                    sidePadding: Theme.spacingMedium
                                    labelPixelSize: Theme.fontSizeSmall
                                    text: root.overviewExpanded ? qsTr("Show Less") : qsTr("Read More")
                                    iconGlyph: root.overviewExpanded ? Icons.expandLess : Icons.expandMore

                                    KeyNavigation.up: seasonsTabList
                                    KeyNavigation.down: playResumeButton

                                    Keys.onReturnPressed: (event) => {
                                        if (event.isAutoRepeat) {
                                            event.accepted = true
                                            return
                                        }
                                        clicked()
                                        event.accepted = true
                                    }

                                    Keys.onEnterPressed: (event) => {
                                        if (event.isAutoRepeat) {
                                            event.accepted = true
                                            return
                                        }
                                        clicked()
                                        event.accepted = true
                                    }

                                    onClicked: root.overviewExpanded = !root.overviewExpanded
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: root.heroActionsBottomSpacing
                        spacing: Theme.spacingMedium

                        ActionButton {
                            id: playResumeButton
                            variant: "primary"
                            readonly property bool loadingPlayback: selectedEpisodeId !== "" && playbackInfoLoadingItemId === selectedEpisodeId
                            text: selectedEpisodePlaybackPosition > 0 ? qsTr("Resume Episode") : qsTr("Play Episode")
                            iconGlyph: Icons.playArrow
                            busy: loadingPlayback
                            enabled: selectedEpisodeId !== ""
                            implicitHeight: Theme.buttonHeightLarge
                            Layout.minimumWidth: Math.round(260 * Theme.layoutScale)

                            Accessible.name: selectedEpisodePlaybackPosition > 0 ? qsTr("Resume Episode") : qsTr("Play Episode")

                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList
                            KeyNavigation.right: playFromBeginningButton

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
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

                            onClicked: startPlayback(false, playResumeButton)
                        }

                        SecondaryActionButton {
                            id: playFromBeginningButton
                            text: qsTr("From Start")
                            iconGlyph: Icons.replay
                            showLabel: false
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            Layout.preferredWidth: Theme.buttonHeightLarge

                            KeyNavigation.left: playResumeButton
                            KeyNavigation.right: watchedToggleButton
                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
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

                            onClicked: startPlayback(true, playFromBeginningButton)
                        }

                        SecondaryActionButton {
                            id: watchedToggleButton
                            text: selectedEpisodeIsPlayed ? qsTr("Mark Unwatched") : qsTr("Mark Watched")
                            iconGlyph: selectedEpisodeIsPlayed ? Icons.visibilityOff : Icons.visibility
                            iconColor: selectedEpisodeIsPlayed ? Theme.accentPrimary : Theme.textPrimary
                            showLabel: false
                            enabled: selectedEpisodeId !== ""
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            Layout.preferredWidth: Theme.buttonHeightLarge

                            KeyNavigation.left: playFromBeginningButton
                            KeyNavigation.right: favoriteButton
                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
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

                            onClicked: toggleWatchedStatus()
                        }

                        SecondaryActionButton {
                            id: favoriteButton
                            text: selectedEpisodeIsFavorite ? qsTr("Favorited") : qsTr("Favorite")
                            iconGlyph: selectedEpisodeIsFavorite ? Icons.favorite : Icons.favoriteBorder
                            iconColor: selectedEpisodeIsFavorite ? Theme.favoriteColor : Theme.textPrimary
                            showLabel: false
                            enabled: selectedEpisodeId !== ""
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            Layout.preferredWidth: Theme.buttonHeightLarge

                            KeyNavigation.left: watchedToggleButton
                            KeyNavigation.right: goToSeriesButton
                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
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

                            onClicked: toggleFavorite()
                        }

                        SecondaryActionButton {
                            id: goToSeriesButton
                            text: qsTr("Go to Series")
                            iconGlyph: Icons.tvShows
                            showLabel: true
                            enabled: seriesId !== ""
                            Layout.preferredHeight: Theme.buttonHeightLarge

                            KeyNavigation.left: favoriteButton
                            KeyNavigation.right: contextMenuButton
                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
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

                            onClicked: root.seriesDetailsRequested(selectedEpisodeId)
                        }

                        SecondaryActionButton {
                            id: contextMenuButton
                            text: ""
                            iconGlyph: Icons.moreHoriz
                            accessibleLabel: qsTr("Audio and subtitle options")
                            enabled: selectedEpisodeId !== ""
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            Layout.preferredWidth: Theme.buttonHeightLarge

                            KeyNavigation.left: goToSeriesButton
                            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    mainContentFlickable.contentY = 0
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (episodesList.count > 0) {
                                    episodesList.forceActiveFocus()
                                } else if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                }
                                event.accepted = true
                            }

                            Keys.onReturnPressed: if (enabled) openTrackSelector()
                            Keys.onEnterPressed: if (enabled) openTrackSelector()
                            onClicked: openTrackSelector()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    // Episodes, chapters and cast scroll beneath the pinned header.
    Flickable {
        id: mainContentFlickable
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: pinnedHeader.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: Theme.sectionSpacing - Theme.spacingMedium
        contentWidth: width
        contentHeight: mainContentColumn.implicitHeight + bottomMargin
        
        // Bottom margin to ensure last item is fully visible
        readonly property int bottomMargin: Math.round(150 * Theme.layoutScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        z: 1
        
        // The header is pinned and the shelf viewport is short, so the focused
        // section snaps its header to the top of the viewport.
        function scrollSectionToTop(item) {
            if (!item) return
            var itemTop = item.mapToItem(mainContentColumn, 0, 0).y
            contentY = Math.max(0, Math.min(itemTop, contentHeight - height))
        }
        
        Behavior on contentY {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
        
        ColumnLayout {
            id: mainContentColumn
            width: mainContentFlickable.width
            spacing: Theme.sectionSpacing - Theme.spacingMedium

            ColumnLayout {
                id: episodesSection
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    id: episodesHeader
                    text: qsTr("Episodes")
                    font.pixelSize: Theme.fontSizeHeader
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightHeavy
                    color: Theme.textPrimary
                }

                ListView {
                    id: episodesList
                    Layout.fillWidth: true
                    // Fit one full row of cards in the viewport below the pinned header.
                    readonly property int responsiveHeight: Math.max(
                        Theme.episodeListMinHeight,
                        Math.min(Theme.episodeListHeight, root.height * 0.4,
                                 mainContentFlickable.height - episodesHeader.height - episodesSection.spacing))
                    Layout.preferredHeight: responsiveHeight
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingLarge
                    clip: true
                    highlightMoveDuration: Theme.durationNormal
                    highlightMoveVelocity: -1
                    model: SeriesDetailsViewModel.episodesModel

                    StateMessage {
                        parent: episodesList
                        anchors.centerIn: parent
                        width: parent.width
                        compact: true
                        visible: episodesList.count === 0
                        kind: SeriesDetailsViewModel.isLoading ? "loading" : "empty"
                        iconGlyph: SeriesDetailsViewModel.isLoading ? "" : Icons.tvShows
                        title: SeriesDetailsViewModel.isLoading ? qsTr("Loading episodes") : qsTr("No episodes in this season")

                        // Focus can still land on the empty list; keep it visible.
                        FocusRing {
                            active: episodesList.activeFocus
                            inset: true
                            radius: Theme.radiusLarge
                        }
                    }

                    KeyNavigation.up: playResumeButton
                    KeyNavigation.down: chapterSection.visible && chapterList.count > 0
                                        ? chapterList
                                        : (castSection.visible && castList.count > 0
                                           ? castList
                                           : (chapterTimeline.visible ? chapterTimeline : null))

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            mainContentFlickable.scrollSectionToTop(episodesSection)
                        }
                    }

                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && currentIndex < count) {
                            if (!suppressInteractionTracking && activeFocus) {
                                userHasInteracted = true
                            }
                            updateSelectedEpisode(currentIndex)
                            schedulePlaybackInfoPreload()
                        }
                    }

                    Keys.onDownPressed: (event) => {
                        if (chapterSection.visible && chapterList.count > 0) {
                            chapterSection.focusCurrentOrFirst()
                            event.accepted = true
                            return
                        }
                        if (castSection.visible && castList.count > 0) {
                            castSection.focusCurrentOrFirst()
                            event.accepted = true
                            return
                        }
                        if (chapterTimeline.visible) {
                            chapterTimeline.forceActiveFocus()
                            event.accepted = true
                            return
                        }
                        event.accepted = false
                    }

                    delegate: ItemDelegate {
                        id: episodeDelegate
                        readonly property int textAllowance: Math.round(50 * Theme.layoutScale)
                        readonly property int availableThumbHeight: Math.max(1, episodesList.responsiveHeight - textAllowance)
                        readonly property bool isPlayed: model.isPlayed || false
                        readonly property bool isFavorite: model.isFavorite || false
                        readonly property string itemId: model.itemId || ""
                        readonly property var playbackPosition: model.positionMs || 0

                        width: Math.round(availableThumbHeight * 16 / 9)
                        height: availableThumbHeight
                        implicitHeight: availableThumbHeight + textAllowance
                        implicitWidth: width

                        background: Rectangle {
                            radius: Theme.radiusMedium
                            color: Theme.cardBackground
                            border.width: episodeDelegate.ListView.isCurrentItem && !episodeDelegate.activeFocus ? Theme.selectionIndicatorWidth : 0
                            border.color: Theme.selectionIndicator
                            FocusRing { inset: true; active: episodeDelegate.activeFocus; radius: parent.radius }

                            Image {
                                id: episodeThumbnailSource
                                anchors.fill: parent
                                anchors.margins: Math.round(2 * Theme.layoutScale)
                                source: model.imageUrl
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                visible: true
                                opacity: 0.9

                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    maskEnabled: true
                                    maskSource: episodeThumbnailMask
                                }
                            }

                            Rectangle {
                                id: episodeThumbnailMask
                                anchors.fill: parent
                                anchors.margins: Math.round(2 * Theme.layoutScale)
                                radius: Theme.radiusMedium
                                visible: false
                                layer.enabled: true
                                layer.smooth: true
                            }

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: Math.round(2 * Theme.layoutScale)
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
                                    GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.3) }
                                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.8) }
                                }
                            }

                            MediaProgressBar {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: Math.round(2 * Theme.layoutScale)
                                anchors.rightMargin: Math.round(2 * Theme.layoutScale)
                                anchors.bottomMargin: Math.round(2 * Theme.layoutScale)
                                positionMs: model.positionMs || 0
                                durationMs: model.durationMs || 0
                            }

                            Text {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: Theme.spacingMedium
                                text: Icons.checkCircle
                                font.family: Theme.fontIcon
                                font.pixelSize: Theme.fontSizeIcon
                                color: Theme.accentPrimary
                                visible: model.isPlayed || false

                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    shadowEnabled: true
                                    shadowHorizontalOffset: 0
                                    shadowVerticalOffset: 1
                                    shadowBlur: 0.2
                                    shadowColor: "black"
                                }
                            }
                        }

                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            // Keep the text clear of the progress line on in-progress episodes.
                            anchors.bottomMargin: (model.positionMs || 0) > 0 ? Theme.progressHeight + Theme.spacingMedium * 2 : Theme.spacingMedium
                            spacing: Math.round(2 * Theme.layoutScale)

                            Item { Layout.fillHeight: true }

                            Text {
                                text: model.indexNumber > 0 ? ("E" + model.indexNumber) : qsTr("Special")
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontPrimary
                                color: Theme.accentPrimary
                            }

                            Text {
                                Layout.fillWidth: true
                                text: model.name || ""
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                font.bold: true
                                color: Theme.textPrimary
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: model.durationMs > 0
                                text: formatRuntime(model.durationMs)
                                font.pixelSize: Theme.fontSizeCaption
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                            }
                        }

                        onClicked: {
                            userHasInteracted = true
                            episodesList.currentIndex = index
                            episodesList.forceActiveFocus()
                        }

                        Keys.onReturnPressed: (event) => {
                            if (event.isAutoRepeat) {
                                event.accepted = true
                                return
                            }
                            userHasInteracted = true
                            episodesList.currentIndex = index
                            startPlayback(false, episodesList)
                            event.accepted = true
                        }
                        Keys.onEnterPressed: (event) => {
                            if (event.isAutoRepeat) {
                                event.accepted = true
                                return
                            }
                            userHasInteracted = true
                            episodesList.currentIndex = index
                            startPlayback(false, episodesList)
                            event.accepted = true
                        }
                    }
                }
            }

            FocusScope {
                id: chapterSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: selectedEpisodeId !== ""
                         && ((focusedEpisodeChapters.length > 0 && focusedEpisodeChaptersHaveImages)
                             || ((focusedEpisodeChaptersLoading || chapterPreloadTimer.running)
                                 && focusedEpisodeChapters.length === 0 && lastEpisodeHadChapterCards))
                implicitHeight: chapterSectionContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (chapterList.count <= 0) {
                        if (castSection.visible && castList.count > 0) {
                            castSection.focusCurrentOrFirst()
                        } else {
                            episodesList.forceActiveFocus()
                        }
                        return
                    }
                    chapterList.currentIndex = Math.max(0, chapterList.currentIndex)
                    chapterList.forceActiveFocus()
                    Qt.callLater(function() {
                        if (chapterList.currentItem) {
                            chapterList.currentItem.forceActiveFocus()
                        }
                    })
                }

                ColumnLayout {
                    id: chapterSectionContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Chapters")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        visible: (focusedEpisodeChaptersLoading || chapterPreloadTimer.running) && focusedEpisodeChapters.length === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.chapterRowHeight
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: root.chapterCardWidth
                                    height: parent.height
                                    radius: Theme.radiusMedium
                                    color: Theme.surfaceInteractive
                                    opacity: 0.72
                                    SequentialAnimation on opacity {
                                        running: Theme.uiAnimationsEnabled && parent.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.42; duration: Theme.durationNormal }
                                        NumberAnimation { to: 0.72; duration: Theme.durationNormal }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: chapterList
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.chapterRowHeight
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: focusedEpisodeChapters
                        visible: focusedEpisodeChapters.length > 0
                        clip: true
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                mainContentFlickable.scrollSectionToTop(chapterSection)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        Keys.onUpPressed: (event) => {
                            episodesList.forceActiveFocus()
                            event.accepted = true
                        }

                        Keys.onDownPressed: (event) => {
                            if (castSection.visible && castList.count > 0) {
                                castSection.focusCurrentOrFirst()
                                event.accepted = true
                            } else {
                                event.accepted = false
                            }
                        }

                        delegate: FocusScope {
                            id: chapterDelegate
                            required property int index
                            required property var modelData
                            width: root.chapterCardWidth
                            height: chapterList.height
                            focus: chapterList.activeFocus && chapterList.currentIndex === index

                            Keys.onLeftPressed: (event) => {
                                if (index > 0) {
                                    chapterList.currentIndex = index - 1
                                    Qt.callLater(function() {
                                        if (chapterList.currentItem) chapterList.currentItem.forceActiveFocus()
                                    })
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onRightPressed: (event) => {
                                if (index + 1 < chapterList.count) {
                                    chapterList.currentIndex = index + 1
                                    Qt.callLater(function() {
                                        if (chapterList.currentItem) chapterList.currentItem.forceActiveFocus()
                                    })
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onUpPressed: (event) => {
                                episodesList.forceActiveFocus()
                                event.accepted = true
                            }

                            Keys.onDownPressed: (event) => {
                                if (castSection.visible && castList.count > 0) {
                                    castSection.focusCurrentOrFirst()
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onReturnPressed: (event) => {
                                if (!event.isAutoRepeat) {
                                    startPlayback(false, chapterDelegate, modelData.startMs || 0)
                                }
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (!event.isAutoRepeat) {
                                    startPlayback(false, chapterDelegate, modelData.startMs || 0)
                                }
                                event.accepted = true
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMedium
                                color: Theme.cardBackground
                                FocusRing { inset: true; active: chapterDelegate.activeFocus; radius: parent.radius }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacingSmall

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: Theme.radiusMedium
                                        color: Theme.cardBackgroundHover
                                        clip: true

                                        Image {
                                            id: chapterThumbnail
                                            anchors.fill: parent
                                            source: modelData.thumbnailUrl || ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            visible: source.toString().length > 0 && status === Image.Ready
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            visible: chapterThumbnail.status !== Image.Ready
                                            text: Icons.movie
                                            font.family: Theme.fontIcon
                                            font.pixelSize: Math.round(48 * Theme.layoutScale)
                                            color: Theme.textSecondary
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name || qsTr("Chapter")
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                        font.bold: true
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: formatChapterTime((modelData.startMs || 0) / 1000)
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontPrimary
                                        color: Theme.textSecondary
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.chapterRowHeight
                        visible: !focusedEpisodeChaptersLoading && !chapterPreloadTimer.running && focusedEpisodeChapters.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("No chapters available.")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                        }
                    }
                }

                WheelStepScroller {
                    anchors.fill: parent
                    target: chapterList
                    orientation: Qt.Horizontal
                    stepPx: root.chapterCardWidth + Theme.spacingMedium
                }
            }

            FocusScope {
                id: castSection
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: selectedEpisodeId !== ""
                implicitHeight: castSectionContent.implicitHeight

                function focusCurrentOrFirst() {
                    if (castList.count <= 0) {
                        episodesList.forceActiveFocus()
                        return
                    }
                    castList.currentIndex = Math.max(0, castList.currentIndex)
                    castList.forceActiveFocus()
                }

                ColumnLayout {
                    id: castSectionContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Theme.spacingMedium

                    Text {
                        text: qsTr("Cast & Crew")
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    Rectangle {
                        visible: focusedEpisodeDetailsLoading && focusedEpisodePeople.length === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium
                            Repeater {
                                model: 4
                                Rectangle {
                                    width: root.peopleCardWidth
                                    height: root.peopleCardHeight
                                    radius: Theme.radiusMedium
                                    color: Theme.surfaceInteractive
                                    opacity: 0.72
                                    SequentialAnimation on opacity {
                                        running: Theme.uiAnimationsEnabled && parent.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.42; duration: Theme.durationNormal }
                                        NumberAnimation { to: 0.72; duration: Theme.durationNormal }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: castList
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingMedium
                        model: focusedEpisodePeople
                        visible: focusedEpisodePeople.length > 0
                        clip: false
                        interactive: false
                        boundsBehavior: Flickable.StopAtBounds
                        leftMargin: Math.round(8 * Theme.layoutScale)
                        rightMargin: Math.round(8 * Theme.layoutScale)
                        topMargin: Math.round(8 * Theme.layoutScale)
                        bottomMargin: Math.round(8 * Theme.layoutScale)

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                mainContentFlickable.scrollSectionToTop(castSection)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        delegate: FocusScope {
                            id: castDelegate
                            required property int index
                            required property var modelData
                            width: root.peopleCardWidth
                            height: root.peopleCardHeight

                            Keys.onLeftPressed: (event) => {
                                if (index > 0) {
                                    castList.currentIndex = index - 1
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }

                            Keys.onRightPressed: {
                                if (index + 1 < castList.count) {
                                    castList.currentIndex = index + 1
                                }
                            }

                            Keys.onUpPressed: {
                                if (chapterSection.visible && chapterList.count > 0) {
                                    chapterSection.focusCurrentOrFirst()
                                } else {
                                    episodesList.forceActiveFocus()
                                }
                            }

                            Keys.onDownPressed: (event) => {
                                if (chapterTimeline.visible) {
                                    chapterTimeline.forceActiveFocus()
                                }
                                event.accepted = true
                            }

                            Keys.onReturnPressed: (event) => {
                                event.accepted = true
                            }

                            Keys.onEnterPressed: (event) => {
                                event.accepted = true
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    castList.currentIndex = index
                                }
                            }

                            PersonCard {
                                anchors.fill: parent
                                itemData: modelData
                                isFocused: castList.activeFocus && castList.currentIndex === index
                            }
                        }

                        WheelStepScroller {
                            anchors.fill: parent
                            target: castList
                            orientation: Qt.Horizontal
                            stepPx: root.peopleCardWidth + Theme.spacingMedium
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.peopleCardHeight + Math.round(16 * Theme.layoutScale)
                        visible: !focusedEpisodeDetailsLoading && focusedEpisodePeople.length === 0
                        radius: Theme.radiusMedium
                        color: Theme.surfaceSunken
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("No cast or crew listed.")
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                        }
                    }
                }
            }

            ChapterTimeline {
                id: chapterTimeline
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: selectedEpisodeId !== "" && focusedEpisodeChapters.length > 0 && !focusedEpisodeChaptersHaveImages
                chapters: focusedEpisodeChapters
                durationMs: selectedEpisodeDurationMs
                positionMs: selectedEpisodePlaybackPosition

                onActiveFocusChanged: {
                    if (activeFocus) {
                        mainContentFlickable.scrollSectionToTop(chapterTimeline)
                    }
                }
                onChapterActivated: (startMs) => startPlayback(false, chapterTimeline, startMs)

                Keys.onUpPressed: (event) => {
                    if (castSection.visible && castList.count > 0) {
                        castSection.focusCurrentOrFirst()
                    } else {
                        episodesList.forceActiveFocus()
                    }
                    event.accepted = true
                }
                Keys.onDownPressed: (event) => event.accepted = true
            }
        }
    }  // End of mainContentFlickable

    WheelStepScroller {
        anchors.fill: mainContentFlickable
        target: mainContentFlickable
        stepPx: Math.round(130 * Theme.layoutScale)
    }
    
    // Context Menu for Audio/Subtitle Selection
    BloomMenu {
        id: contextMenu
        
        
        
        onOpened: {
            // Re-fetch tracks when menu opens
            console.log("[ContextMenu] Menu opened")
            
            // Focus the first item
            currentIndex = 0
            forceActiveFocus()
            
            if (currentMediaSource && !appliedPendingTrackOverride) {
                var resolved = PlayerController.resolveTrackSelectionForMediaSource(
                            currentMediaSource,
                            SeriesDetailsViewModel.selectedSeasonId,
                            false)
                selectedAudioIndex = resolved.audioIndex
                selectedSubtitleIndex = resolved.subtitleIndex
            }
        }
        
        onClosed: {
            console.log("[ContextMenu] Menu closed, restoring focus to contextMenuButton")
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }
        
        BloomMenu {
            id: audioMenu
            title: "Audio Tracks"
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
                        userMadeAudioSelection = true
                        appliedPendingTrackOverride = false
                        console.log("[ContextMenu] Selected audio:", selectedAudioIndex)
                        var seasonId = SeriesDetailsViewModel.selectedSeasonId
                        if (seasonId) {
                             PlayerController.setExplicitSeasonAudioPreference(seasonId, selectedAudioIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
        
        BloomMenu {
            id: subtitleMenu
            title: "Subtitle Tracks"
            
            
            BloomMenuItem {
                text: "None"
                checkable: true
                checked: selectedSubtitleIndex === -1
                indicator: Item {}
                
                
                
                onTriggered: {
                    selectedSubtitleIndex = -1
                    userMadeSubtitleSelection = true
                    appliedPendingTrackOverride = false
                    console.log("[ContextMenu] Selected subtitle: None")
                    var seasonId = SeriesDetailsViewModel.selectedSeasonId
                    if (seasonId) {
                         PlayerController.setExplicitSeasonSubtitlePreference(seasonId, selectedSubtitleIndex)
                    }
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
                        userMadeSubtitleSelection = true
                        appliedPendingTrackOverride = false
                        console.log("[ContextMenu] Selected subtitle:", selectedSubtitleIndex)
                        var seasonId = SeriesDetailsViewModel.selectedSeasonId
                        if (seasonId) {
                             PlayerController.setExplicitSeasonSubtitlePreference(seasonId, selectedSubtitleIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
    }

    ToastNotification {
        id: playbackToast
        z: 300
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
            if (!PlayerController.awaitingNextEpisodeResolution) {
                if (root.playbackSelectionRestorePending && SeriesDetailsViewModel.selectedSeasonId) {
                    refreshEpisodesTimer.start()
                }
                if (root.playbackReturnFocusPending
                        && root.playbackReturnFocusActivated
                        && !PlayerController.isPlaybackActive) {
                    Qt.callLater(root.restoreFocusAfterPlaybackExit)
                }
            }
        }
    }
}
