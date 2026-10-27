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
    // Hero copy keeps a fixed reading measure (about half a 16:9 screen), so
    // ultrawide screens do not stretch it.
    readonly property int heroBlockWidth: Math.min(Theme.heroCopyMaxWidth,
                                                   root.width - Theme.pageMarginX * 2)
    readonly property int peopleCardWidth: Math.round(176 * Theme.layoutScale)
    readonly property int peopleCardHeight: Math.round(320 * Theme.layoutScale)
    // Request sharper stills for the scaled cards at high display scaling.
    readonly property int cardImageWidth: Math.round(Theme.landscapeCardWidth * Math.max(2, Screen.devicePixelRatio))
    readonly property int shelfScrollOffset: Math.round(56 * Theme.layoutScale)
    /// The navigation bar recedes once the page scrolls into the shelves.
    readonly property bool navigationRecessed: mainContentFlickable.contentY > Theme.navBarReservedHeight
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
    
    property alias overviewExpanded: detailHero.synopsisExpanded
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
    
    // ---- Navigation and scrolling ----

    // Shelves below the hero, in focus order (visible ones only). The season
    // tabs head the episode shelf and only take focus with several seasons.
    function shelfTargets() {
        const targets = []
        if (seasonsTabList.count > 1) targets.push(seasonsTabList)
        targets.push(episodesList)
        if (chapterSection.visible && chapterList.count > 0) targets.push(chapterList)
        if (castSection.visible && castList.count > 0) targets.push(castList)
        if (chapterTimeline.visible && chapterTimeline.count > 0) targets.push(chapterTimeline)
        return targets
    }

    function sectionFor(target) {
        if (target === seasonsTabList || target === episodesList) return episodesSection
        if (target === chapterTimeline) return chapterTimeline
        return target ? target.parent : null
    }

    function focusTarget(target) {
        if (target === seasonsTabList) {
            seasonsTabList.currentIndex = Math.max(0, SeriesDetailsViewModel.selectedSeasonIndex)
        }
        target.forceActiveFocus()
    }

    // Moves focus to the row above or below. Above the first row is the
    // hero's primary action; below the last row nothing happens.
    function moveShelfFocus(fromTarget, delta) {
        const targets = shelfTargets()
        const index = targets.indexOf(fromTarget)
        const target = index + delta
        if (index >= 0 && target >= 0 && target < targets.length) {
            focusTarget(targets[target])
        } else if (target < 0 && playResumeButton.enabled) {
            playResumeButton.forceActiveFocus()
        }
        return true
    }

    function heroDown(event) {
        const targets = shelfTargets()
        if (targets.length > 0) {
            focusTarget(targets[0])
        }
        event.accepted = true
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
        const maxY = Math.max(0, mainContentFlickable.contentHeight - mainContentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    // The episode shelf scrolls only as far as it needs to sit fully in view
    // under the hero, so the hero keeps describing the focused episode; later
    // shelves land in the same spot near the top.
    function scrollToSection(section) {
        if (!section) {
            return
        }
        const top = section.mapToItem(mainContentColumn, 0, 0).y
        if (section === episodesSection) {
            scrollTo(top + section.height + Theme.spacingLarge - mainContentFlickable.height)
        } else {
            scrollTo(top - root.shelfScrollOffset)
        }
    }

    function remainingLabel(ms) {
        const minutes = Math.max(1, Math.round(ms / 60000))
        return minutes >= 60
               ? qsTr("%1h %2m left").arg(Math.floor(minutes / 60)).arg(minutes % 60)
               : qsTr("%1 min left").arg(minutes)
    }

    function episodeCode(seasonNumber, episodeNumber) {
        if (!(episodeNumber > 0)) {
            return qsTr("Special")
        }
        return qsTr("S%1 E%2").arg(seasonNumber || 0).arg(episodeNumber)
    }

    function episodeCardSubtitle(seasonNumber, episodeNumber, durationMs, positionMs) {
        const parts = [episodeCode(seasonNumber, episodeNumber)]
        if (positionMs > 0 && durationMs > positionMs) {
            parts.push(remainingLabel(durationMs - positionMs))
        } else if (durationMs > 0) {
            parts.push(formatRuntime(durationMs))
        }
        return parts.join("  ·  ")
    }

    readonly property string heroEyebrow: {
        if (!selectedEpisodeData) {
            return ""
        }
        if (selectedSeasonNumber <= 0 || selectedEpisodeNumber <= 0) {
            return qsTr("Special")
        }
        return qsTr("Season %1  ·  Episode %2").arg(selectedSeasonNumber).arg(selectedEpisodeNumber)
    }

    // ---- Stage ----
    // The series backdrop stays put while browsing episodes. A poster fallback
    // is low resolution, so it is shown softened instead.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        z: -1
        mode: SeriesDetailsViewModel.backdropUrl ? "hero" : "ambient"
        source: SeriesDetailsViewModel.backdropUrl || SeriesDetailsViewModel.posterUrl
        // Quiet the artwork as the page scrolls into the shelves.
        dim: Math.min(0.72, mainContentFlickable.contentY / Math.max(1, heroArea.height) * 0.72)
    }

    NumberAnimation {
        id: scrollAnimation
        target: mainContentFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
    }

    // The hero and the shelves scroll together, like Home. The episode shelf
    // sits fully in view under the hero, which follows the focused episode.
    Flickable {
        id: mainContentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainContentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: mainContentColumn
            width: mainContentFlickable.width
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
                    eyebrow: root.heroEyebrow
                    logoSource: displayLogoUrl
                    title: seriesName || SeriesDetailsViewModel.title
                    subtitle: selectedEpisodeName
                    metadataItems: [selectedEpisodeDurationMs > 0 ? formatRuntime(selectedEpisodeDurationMs) : "",
                                    selectedEpisodeDurationMs > 0 ? calculateEndTime(selectedEpisodeDurationMs) : "",
                                    selectedEpisodeCommunityRating > 0 ? formatRating(selectedEpisodeCommunityRating) : "",
                                    selectedEpisodePremiereDate !== "" ? formatPremiereDate(selectedEpisodePremiereDate) : "",
                                    selectedEpisodeIsPlayed ? qsTr("Watched")
                                                            : (selectedEpisodePlaybackPosition > 0 ? qsTr("In Progress") : "")]
                    qualityBadges: [root.videoResolutionBadge(), root.videoHdrBadge()]
                    // Playback info loads per episode, after the episode data.
                    reserveChipRow: true
                    synopsis: selectedEpisodeOverview
                    synopsisPlaceholder: selectedEpisodeData ? qsTr("No description available.") : ""
                    // Browsing episodes must not move the action row.
                    stableSynopsisHeight: true
                    synopsisExpandedLines: Math.max(4, Math.floor(root.height * 0.3 / (Theme.fontSizeBody * 1.45)))
                    primaryAction: playResumeButton
                    progress: !selectedEpisodeIsPlayed && selectedEpisodePlaybackPosition > 0 && selectedEpisodeDurationMs > 0
                              ? Math.min(1, selectedEpisodePlaybackPosition / selectedEpisodeDurationMs) : 0
                    footnote: progress > 0 ? root.remainingLabel(selectedEpisodeDurationMs - selectedEpisodePlaybackPosition) : ""

                    ActionButton {
                        id: playResumeButton
                        variant: "primary"
                        readonly property bool loadingPlayback: selectedEpisodeId !== "" && playbackInfoLoadingItemId === selectedEpisodeId
                        text: selectedEpisodePlaybackPosition > 0 ? qsTr("Resume Episode") : qsTr("Play Episode")
                        iconGlyph: Icons.playArrow
                        busy: loadingPlayback
                        enabled: selectedEpisodeId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(40 * Theme.layoutScale)

                        Accessible.name: text

                        KeyNavigation.right: goToSeriesButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)

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
                        id: goToSeriesButton
                        text: qsTr("Go to Series")
                        iconGlyph: Icons.tvShows
                        enabled: seriesId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(36 * Theme.layoutScale)

                        KeyNavigation.left: playResumeButton
                        KeyNavigation.right: playFromBeginningButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)

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
                        id: playFromBeginningButton
                        text: qsTr("From Start")
                        iconGlyph: Icons.replay
                        showLabel: false
                        accessibleLabel: qsTr("Play from start")
                        enabled: selectedEpisodeId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: goToSeriesButton
                        KeyNavigation.right: watchedToggleButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)

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
                        accessibleLabel: text
                        enabled: selectedEpisodeId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: playFromBeginningButton
                        KeyNavigation.right: favoriteButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)

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
                        accessibleLabel: text
                        enabled: selectedEpisodeId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: watchedToggleButton
                        KeyNavigation.right: contextMenuButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)

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
                        id: contextMenuButton
                        text: ""
                        iconGlyph: Icons.moreHoriz
                        accessibleLabel: qsTr("Audio and subtitle options")
                        enabled: selectedEpisodeId !== ""
                        implicitHeight: Theme.buttonHeightHero
                        iconPixelSize: Theme.iconSizeLarge

                        KeyNavigation.left: favoriteButton

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        Keys.onUpPressed: (event) => root.heroUp(event)
                        Keys.onDownPressed: (event) => root.heroDown(event)
                        Keys.onRightPressed: (event) => { event.accepted = true }

                        Keys.onReturnPressed: if (enabled) openTrackSelector()
                        Keys.onEnterPressed: if (enabled) openTrackSelector()
                        onClicked: openTrackSelector()
                    }
                }
            }

            // Episodes, headed by the season tabs.
            ColumnLayout {
                id: episodesSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing

                ListView {
                    id: seasonsTabList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.buttonHeightSmall
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingSmall
                    // Tab labels line up with the gutter.
                    leftMargin: Theme.pageMarginX - Theme.spacingMedium
                    rightMargin: Theme.pageMarginX
                    clip: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: SeriesDetailsViewModel.seasonsModel
                    currentIndex: Math.max(0, SeriesDetailsViewModel.selectedSeasonIndex)
                    highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: qsTr("Seasons")

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(episodesSection)
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                    delegate: ItemDelegate {
                        id: seasonTab
                        required property int index
                        required property var name
                        required property var indexNumber
                        required property var itemId
                        readonly property bool isSelected: SeriesDetailsViewModel.selectedSeasonId === itemId

                        width: seasonTabText.implicitWidth + Theme.spacingMedium * 2
                        height: Theme.buttonHeightSmall
                        padding: 0
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                        focusPolicy: seasonsTabList.count > 1 ? Qt.StrongFocus : Qt.NoFocus
                        Accessible.role: Accessible.PageTab
                        Accessible.name: seasonTabText.text
                        Accessible.selected: isSelected

                        // Selected = selection fill and indicator; focus = ring.
                        background: Rectangle {
                            id: seasonTabBackground
                            radius: Theme.radiusSmall
                            color: seasonTab.down ? Theme.surfaceInteractivePressed
                                 : (seasonTab.activeFocus || seasonTab.hovered ? Theme.surfaceInteractiveHover
                                    : (seasonTab.isSelected && seasonsTabList.count > 1 ? Theme.selectionBackground : "transparent"))

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                width: parent.width - Theme.spacingMedium * 2
                                height: Theme.selectionIndicatorWidth
                                radius: height / 2
                                color: Theme.selectionIndicator
                                visible: seasonTab.isSelected && seasonsTabList.count > 1
                            }

                            FocusRing {
                                active: seasonTab.activeFocus
                                radius: seasonTabBackground.radius
                            }
                        }

                        contentItem: Text {
                            id: seasonTabText
                            text: seasonTab.name || (seasonTab.indexNumber > 0 ? qsTr("Season %1").arg(seasonTab.indexNumber)
                                                                               : qsTr("Specials"))
                            font.pixelSize: seasonsTabList.count > 1 ? Theme.fontSizeMedium : Theme.fontSizeHeader
                            font.family: Theme.fontPrimary
                            font.weight: seasonTab.isSelected ? Theme.fontWeightHeavy : Theme.fontWeightMedium
                            color: seasonTab.isSelected ? Theme.textPrimary : Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: SeriesDetailsViewModel.selectSeason(index)

                        Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(seasonsTabList, -1) }
                        Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(seasonsTabList, 1) }

                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                            }
                            event.accepted = true
                        }
                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                clicked()
                            }
                            event.accepted = true
                        }
                    }
                }

                ShelfList {
                    id: episodesList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    model: SeriesDetailsViewModel.episodesModel

                    StateMessage {
                        parent: episodesList
                        x: Theme.pageMarginX
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.pageMarginX * 2
                        compact: true
                        visible: episodesList.count === 0
                        kind: SeriesDetailsViewModel.isLoading ? "loading" : "empty"
                        iconGlyph: SeriesDetailsViewModel.isLoading ? "" : Icons.tvShows
                        title: SeriesDetailsViewModel.isLoading ? qsTr("Loading episodes") : qsTr("No episodes in this season")

                        // Focus can still land on the empty list; keep it visible.
                        FocusRing {
                            active: episodesList.activeFocus
                            radius: Theme.radiusLarge
                        }
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(episodesSection)

                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && currentIndex < count) {
                            if (!suppressInteractionTracking && activeFocus) {
                                userHasInteracted = true
                            }
                            updateSelectedEpisode(currentIndex)
                            schedulePlaybackInfoPreload()
                        }
                    }

                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(episodesList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(episodesList, 1) }

                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count) {
                            userHasInteracted = true
                            startPlayback(false, episodesList)
                        }
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count) {
                            userHasInteracted = true
                            startPlayback(false, episodesList)
                        }
                        event.accepted = true
                    }

                    delegate: MediaCard {
                        id: episodeCard
                        required property int index
                        required property var name
                        required property var imageUrl
                        required property var itemId
                        required property var indexNumber
                        required property var parentIndexNumber
                        required property var isPlayed
                        required property var isFavorite
                        // Runtime and resume point come through `model`:
                        // MediaCard already has positionMs/durationMs.
                        required property var model
                        // Read by the hero through episodesList.currentItem.
                        readonly property real playbackPosition: model.positionMs || 0
                        readonly property real runtimeMs: model.durationMs || 0

                        variant: "landscape"
                        artworkSources: [imageUrl || ""]
                        title: name || ""
                        subtitle: root.episodeCardSubtitle(parentIndexNumber, indexNumber, runtimeMs,
                                                           isPlayed ? 0 : playbackPosition)
                        placeholderIcon: Icons.tvShows
                        positionMs: isPlayed ? 0 : playbackPosition
                        durationMs: runtimeMs
                        showWatchState: !!isPlayed
                        watched: !!isPlayed
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        Accessible.description: qsTr("Play %1").arg(name || "")

                        onClicked: {
                            userHasInteracted = true
                            episodesList.currentIndex = index
                            episodesList.forceActiveFocus()
                        }

                        // The episode the hero describes keeps a selection
                        // outline while focus is elsewhere.
                        Rectangle {
                            width: episodeCard.artworkWidth
                            height: episodeCard.artworkHeight
                            radius: Theme.radiusArtwork
                            color: "transparent"
                            border.width: Theme.selectionIndicatorWidth
                            border.color: Theme.selectionIndicator
                            visible: episodeCard.ListView.isCurrentItem && !episodeCard.focused
                        }
                    }
                }
            }

            ColumnLayout {
                id: chapterSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                // Chapters without images use the compact timeline further
                // down. While the next episode's chapters load, keep the shelf
                // if the last episode had cards so the page does not jump.
                visible: selectedEpisodeId !== ""
                         && ((focusedEpisodeChapters.length > 0 && focusedEpisodeChaptersHaveImages)
                             || ((focusedEpisodeChaptersLoading || chapterPreloadTimer.running)
                                 && focusedEpisodeChapters.length === 0 && lastEpisodeHadChapterCards))

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
                    model: focusedEpisodeChapters

                    delegate: MediaCard {
                        required property var modelData
                        required property int index
                        variant: "landscape"
                        artworkSources: [modelData.thumbnailUrl || ""]
                        title: modelData.name || qsTr("Chapter %1").arg(index + 1)
                        subtitle: formatChapterTime((modelData.startMs || 0) / 1000)
                        placeholderIcon: Icons.movie
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            chapterList.currentIndex = index
                            chapterList.forceActiveFocus()
                            startPlayback(false, chapterList, modelData.startMs || 0)
                        }
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(chapterSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(chapterList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(chapterList, 1) }
                    Keys.onReturnPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < focusedEpisodeChapters.length)
                            startPlayback(false, chapterList, focusedEpisodeChapters[currentIndex].startMs || 0)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < focusedEpisodeChapters.length)
                            startPlayback(false, chapterList, focusedEpisodeChapters[currentIndex].startMs || 0)
                        event.accepted = true
                    }
                }
            }

            ColumnLayout {
                id: castSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: selectedEpisodeId !== "" && focusedEpisodePeople.length > 0

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
                    model: focusedEpisodePeople

                    delegate: PersonCard {
                        required property var modelData
                        required property int index
                        width: root.peopleCardWidth
                        height: root.peopleCardHeight
                        itemData: modelData
                        isFocused: ListView.isCurrentItem && ListView.view.activeFocus
                        onActiveFocusChanged: if (activeFocus) castList.currentIndex = index
                    }

                    onActiveFocusChanged: if (activeFocus) root.scrollToSection(castSection)
                    Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(castList, -1) }
                    Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(castList, 1) }
                    Keys.onReturnPressed: (event) => { event.accepted = true }
                    Keys.onEnterPressed: (event) => { event.accepted = true }
                }
            }

            ChapterTimeline {
                id: chapterTimeline
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.preferredHeight: implicitHeight
                visible: selectedEpisodeId !== "" && focusedEpisodeChapters.length > 0 && !focusedEpisodeChaptersHaveImages
                chapters: focusedEpisodeChapters
                durationMs: selectedEpisodeDurationMs
                positionMs: selectedEpisodePlaybackPosition

                onActiveFocusChanged: if (activeFocus) root.scrollToSection(chapterTimeline)
                onChapterActivated: (startMs) => startPlayback(false, chapterTimeline, startMs)

                Keys.onUpPressed: (event) => { event.accepted = root.moveShelfFocus(chapterTimeline, -1) }
                Keys.onDownPressed: (event) => { event.accepted = root.moveShelfFocus(chapterTimeline, 1) }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
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
