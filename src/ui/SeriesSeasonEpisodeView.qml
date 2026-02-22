import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQml

import BloomUI
import "TrackUtils.js" as TrackUtils

FocusScope {
    id: root
    
    // Input properties
    property string seriesId: ""
    property string seriesName: ""
    property string initialSeasonId: ""  // Season to load on entry
    property int initialSeasonIndex: -1  // Optional: season index to highlight
    property string initialEpisodeId: ""  // Optional: specific episode ID to highlight on load
    
    // Signals for navigation and actions
    signal backRequested()
    signal playRequested(string itemId, var startPositionTicks, double framerate, bool isHDR)
    signal playRequestedWithTracks(string itemId, var startPositionTicks, string mediaSourceId, 
                                    string playSessionId, int audioIndex, int subtitleIndex,
                                    int mpvAudioTrack, int mpvSubtitleTrack,
                                    var audioTrackMap, var subtitleTrackMap,
                                    var availableAudioTracks, var availableSubtitleTracks,
                                    double framerate, bool isHDR)
    
    Connections {
        target: SeriesDetailsViewModel
        function onSeriesLoaded() {
            // [FIX] When series details are loaded, ensure we enforce the initialSeasonId
            // This prevents the view from defaulting to the first season/specials if the series
            // load completes after the view is shown.
            Qt.callLater(function() {
                if (initialSeasonId !== "") {
                    console.log("[SeriesSeasonEpisodeView] Series loaded signal (delayed), checking enforcement. Target:", initialSeasonId, "Current:", SeriesDetailsViewModel.selectedSeasonId)
                    if (SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                        console.log("[SeriesSeasonEpisodeView] Enforcing initial season:", initialSeasonId)
                        SeriesDetailsViewModel.loadSeasonEpisodes(initialSeasonId)
                    } else {
                        console.log("[SeriesSeasonEpisodeView] Correct season already selected:", initialSeasonId)
                    }
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
                    if (SeriesDetailsViewModel.selectedSeasonId !== initialSeasonId) {
                        console.log("[SeriesSeasonEpisodeView] Enforcing initial season (post-reset):", initialSeasonId)
                        SeriesDetailsViewModel.loadSeasonEpisodes(initialSeasonId)
                    }
                }
            })
        }
    }
    
    // Currently selected episode (from ListView currentIndex)
    property var selectedEpisodeData: null
    property string selectedEpisodeId: episodesList.currentItem ? episodesList.currentItem.itemId : ""
    property string selectedEpisodeName: selectedEpisodeData ? (selectedEpisodeData.name || selectedEpisodeData.Name || "") : ""
    property int selectedEpisodeNumber: selectedEpisodeData ? (selectedEpisodeData.indexNumber || selectedEpisodeData.IndexNumber || 0) : 0
    property int selectedSeasonNumber: selectedEpisodeData ? (selectedEpisodeData.parentIndexNumber || selectedEpisodeData.ParentIndexNumber || 0) : 0
    property string selectedEpisodeOverview: selectedEpisodeData ? (selectedEpisodeData.overview || selectedEpisodeData.Overview || "") : ""
    property var selectedEpisodeRuntimeTicks: selectedEpisodeData ? (selectedEpisodeData.runtimeTicks || selectedEpisodeData.RunTimeTicks || 0) : 0
    property var selectedEpisodeCommunityRating: selectedEpisodeData ? (selectedEpisodeData.communityRating || selectedEpisodeData.CommunityRating || 0) : 0
    property string selectedEpisodePremiereDate: selectedEpisodeData ? (selectedEpisodeData.premiereDate || selectedEpisodeData.PremiereDate || "") : ""
    property bool selectedEpisodeIsPlayed: episodesList.currentItem ? episodesList.currentItem.isPlayed : false
    property var selectedEpisodePlaybackPosition: episodesList.currentItem ? episodesList.currentItem.playbackPosition : 0
    property bool selectedEpisodeIsFavorite: episodesList.currentItem ? episodesList.currentItem.isFavorite : false
    
    property bool overviewExpanded: false
    onSelectedEpisodeIdChanged: overviewExpanded = false
    
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
    property var currentMediaSource: {
        var info = playbackInfo || pendingPlaybackInfo
        return info && info.mediaSources && info.mediaSources.length > 0 ? info.mediaSources[0] : null
    }
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1
    property bool playbackInfoLoading: false
    
    // Logo priority: Season logo -> Series logo -> Text fallback
    readonly property string displayLogoUrl: {
        // Try season-specific logo
        if (SeriesDetailsViewModel.selectedSeasonIndex >= 0) {
            var season = SeriesDetailsViewModel.seasonsModel.getItem(SeriesDetailsViewModel.selectedSeasonIndex)
            if (season && season.ImageTags && season.ImageTags.Logo) {
                return LibraryService.getCachedImageUrlWithWidth(season.Id, "Logo", 600)
            }
        }
        // Fallback to series logo
        return SeriesDetailsViewModel.logoUrl
    }
    
    readonly property string displayName: {
        if (displayLogoUrl) return ""  // Hide text when logo available
        return seriesName
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
            console.log("[SeriesSeasonEpisodeView] Loading initial season:", initialSeasonId)
            SeriesDetailsViewModel.loadSeasonEpisodes(initialSeasonId)
        }
    }
    
    // When episodes are loaded, find next-up/partially-watched episode
    Connections {
        target: SeriesDetailsViewModel
        function onEpisodesLoaded() {
            Qt.callLater(selectInitialEpisode)
        }
    }
    
    function selectInitialEpisode() {
        if (episodesList.count === 0) {
            console.log("[SeriesSeasonEpisodeView] selectInitialEpisode: No episodes, skipping")
            return
        }
        
        var targetIndex = 0
        var foundInitialEpisode = false
        
        // First priority: if a specific episode ID was requested, find and select it
        if (initialEpisodeId !== "") {
            console.log("[SeriesSeasonEpisodeView] Looking for initial episode ID:", initialEpisodeId, "in count:", episodesList.count, "for season:", SeriesDetailsViewModel.selectedSeasonId)
            for (var i = 0; i < episodesList.count; i++) {
                var ep = SeriesDetailsViewModel.episodesModel.getItem(i)
                if (ep && (ep.itemId === initialEpisodeId || ep.Id === initialEpisodeId)) {
                    console.log("[SeriesSeasonEpisodeView] Found initial episode at index:", i)
                    targetIndex = i
                    foundInitialEpisode = true
                    break
                }
            }
            if (!foundInitialEpisode) {
                console.log("[SeriesSeasonEpisodeView] Initial episode ID not found in current season, using first unplayed")
            }
        }
        
        // Fallback: find first unplayed or partially-watched episode
        if (!foundInitialEpisode) {
            for (var j = 0; j < episodesList.count; j++) {
                var ep2 = SeriesDetailsViewModel.episodesModel.getItem(j)
                if (!ep2.isPlayed) {
                    targetIndex = j
                    break
                }
                if (ep2.playbackPositionTicks > 0) {
                    targetIndex = j
                }
            }
        }
        
        console.log("[SeriesSeasonEpisodeView] Setting currentIndex to:", targetIndex)
        episodesList.currentIndex = targetIndex
        episodesList.positionViewAtIndex(targetIndex, ListView.Center)
        updateSelectedEpisode(targetIndex)
        
        // Don't request playback info on initial load - only request when user presses play
        
        // Focus the episode list with a slight delay to ensure UI is ready
        Qt.callLater(function() {
            console.log("[SeriesSeasonEpisodeView] Restoring focus to episodesList")
            episodesList.forceActiveFocus()
        })
    }
    
    function updateSelectedEpisode(index) {
        if (index >= 0 && index < SeriesDetailsViewModel.episodesModel.rowCount()) {
            selectedEpisodeData = SeriesDetailsViewModel.episodesModel.getItem(index)
            console.log("[SeriesSeasonEpisodeView] Selected episode:", selectedEpisodeName, "ID:", selectedEpisodeId,
                        "Played:", selectedEpisodeIsPlayed)
        } else {
            selectedEpisodeData = null
        }
    }
    
    // Refresh episode progress when playback stops
    Connections {
        target: PlayerController
        function onPlaybackStopped() {
            if (PlayerController.awaitingNextEpisodeResolution) {
                return
            }
            // Reload episodes to update watch progress
            if (SeriesDetailsViewModel.selectedSeasonId) {
                refreshEpisodesTimer.start()
            }
        }
    }
    
    Timer {
        id: refreshEpisodesTimer
        interval: 200  // Minimal delay to ensure playback stop event is processed before refresh
        repeat: false
        onTriggered: {
            console.log("[SeriesSeasonEpisodeView] Refreshing episodes immediately after playback stop")
            if (SeriesDetailsViewModel.selectedSeasonId) {
                SeriesDetailsViewModel.loadSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
            }
        }
    }
    
    // Update selected episode data when episodes are reloaded
    Connections {
        target: SeriesDetailsViewModel
        function onEpisodesLoaded() {
            updateSelectedEpisode(episodesList.currentIndex)
        }
    }

    // Update selected episode data when played status changes
    Connections {
        target: LibraryService
        function onItemPlayedStatusChanged(itemId, isPlayed) {
            if (itemId === selectedEpisodeId) {
                // Refresh the episodes model to get updated UserData
                if (SeriesDetailsViewModel.selectedSeasonId) {
                    SeriesDetailsViewModel.loadSeasonEpisodes(SeriesDetailsViewModel.selectedSeasonId)
                }
            }
        }
    }
    
    // Helper functions for episode info display
    function formatRuntime(ticks) {
        if (!ticks || ticks === 0) return ""
        var totalMinutes = Math.round(ticks / 600000000)
        var hours = Math.floor(totalMinutes / 60)
        var minutes = totalMinutes % 60
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
    
    function calculateEndTime(runtimeTicks) {
        if (!runtimeTicks || runtimeTicks === 0) return ""
        var now = new Date()
        var runtimeMs = runtimeTicks / 10000
        var endTime = new Date(now.getTime() + runtimeMs)
        return "Ends at " + endTime.toLocaleTimeString(Qt.locale(), "h:mm AP")
    }
    
    // Debounce timer for preloading playback info on episode selection
    // Prevents excessive API calls when user rapidly navigates episodes
    Timer {
        id: playbackInfoPreloadTimer
        interval: 300
        repeat: false
        onTriggered: {
            if (selectedEpisodeId && !playbackInfoLoading) {
                playbackInfoLoading = true
                console.log("[SeriesSeasonEpisodeView] Preloading playback info for episode:", selectedEpisodeId)
                PlaybackService.getPlaybackInfo(selectedEpisodeId)
            }
        }
    }
    
    // Playback info request
    function requestPlaybackInfo() {
        if (!selectedEpisodeId || playbackInfoLoading) return
        
        playbackInfoLoading = true
        console.log("[SeriesSeasonEpisodeView] Requesting playback info for episode:", selectedEpisodeId)
        PlaybackService.getPlaybackInfo(selectedEpisodeId)
    }
    
    // Trigger debounced playback info preload when episode is selected
    function schedulePlaybackInfoPreload() {
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
        if (playbackInfo && playbackInfo.mediaSources && playbackInfo.mediaSources.length > 0) {
            console.log("[SeriesSeasonEpisodeView] Opening track selector with cached playback info")
            contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
            return
        }
        
        // Otherwise, request playback info and open when ready
        waitingForContextInfo = true
        playbackInfoLoading = true
        PlaybackService.getPlaybackInfo(selectedEpisodeId)
        // The response will come through the Connections handler below
    }
    
    // Connection to receive playback info
    Connections {
        target: PlaybackService
        
        function onPlaybackInfoLoaded(itemId, info) {
            if (itemId === selectedEpisodeId) {
                playbackInfo = info
                lastLoadedPlaybackInfo = info  // Persist for track selector access
                pendingPlaybackInfo = info  // Store for deferred playback
                
                // Apply track preferences
                applyTrackPreferences()
                
                // If there's a pending playback request, execute it now
                if (hasPendingPlayback) {
                    hasPendingPlayback = false
                    var fromBeginning = pendingPlaybackFromBeginning
                    playbackInfoLoading = false
                    console.log("[SeriesSeasonEpisodeView] Executing pending playback, fromBeginning:", fromBeginning, "playbackInfo available:", playbackInfo !== null)
                    // Use callLater to ensure property bindings have updated
                    Qt.callLater(function() { performPlayback(fromBeginning) })
                } else {
                    // Only clear loading flag if this wasn't a pending playback request
                    playbackInfoLoading = false
                }
                
                // Open the popup if we were waiting for this info
                if (waitingForContextInfo) {
                    waitingForContextInfo = false
                    contextMenu.popup(contextMenuButton, 0, contextMenuButton.height)
                }
            }
        }
    }
    
    function applyTrackPreferences() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return
        
        // Get saved preferences for this season
        var seasonId = SeriesDetailsViewModel.selectedSeasonId
        var lastAudio = PlayerController.getLastAudioTrackForSeason(seasonId)
        var lastSubtitle = PlayerController.getLastSubtitleTrackForSeason(seasonId)
        
        console.log("[SeriesSeasonEpisodeView] applyTrackPreferences - seasonId:", seasonId, "lastAudio:", lastAudio, "lastSubtitle:", lastSubtitle)
        
        // Find matching streams
        selectedAudioIndex = -1
        selectedSubtitleIndex = -1
        
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Audio") {
                if (selectedAudioIndex < 0 || stream.index === lastAudio) {
                    selectedAudioIndex = stream.index
                }
            }
            if (stream.type === "Subtitle") {
                if (stream.index === lastSubtitle) {
                    selectedSubtitleIndex = stream.index
                }
            }
        }
        
        console.log("[SeriesSeasonEpisodeView] applyTrackPreferences - selectedAudioIndex:", selectedAudioIndex, "selectedSubtitleIndex:", selectedSubtitleIndex)
    }
    
    // Canonical contract for Milestone D:
    // - UI + PlayerController selected* use Jellyfin stream index.
    // - mpv aid/sid use runtime track IDs, mapped by media-type order.
    // - subtitle none stays Jellyfin -1 and maps to sid "no".
    function buildTrackMap(streamType, mediaSource) {
        var map = []
        if (!mediaSource || !mediaSource.mediaStreams) return map
        var mpvTrackId = 1
        for (var i = 0; i < mediaSource.mediaStreams.length; i++) {
            var stream = mediaSource.mediaStreams[i]
            if (stream.type === streamType) {
                map.push({ jellyfinIndex: stream.index, mpvTrackId: mpvTrackId })
                mpvTrackId++
            }
        }
        return map
    }
    
    function resolveMpvTrackId(jellyfinStreamIndex, trackMap) {
        if (jellyfinStreamIndex < 0) return -1
        for (var i = 0; i < trackMap.length; i++) {
            if (trackMap[i].jellyfinIndex === jellyfinStreamIndex) {
                return trackMap[i].mpvTrackId
            }
        }
        return -1
    }

    function buildTrackOptions(streams) {
        var tracks = []
        for (var i = 0; i < streams.length; i++) {
            var stream = streams[i]
            tracks.push({
                index: stream.index,
                displayTitle: TrackUtils.formatTrackName(stream),
                language: stream.language,
                codec: stream.codec,
                channels: stream.channels,
                channelLayout: stream.channelLayout,
                isDefault: stream.isDefault,
                isForced: stream.isForced,
                isHearingImpaired: stream.isHearingImpaired
            })
        }
        return tracks
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
            if (stream.type === "Video" && stream.videoRange) {
                var range = stream.videoRange.toUpperCase()
                if (range !== "SDR" && range !== "") {
                    return true
                }
            }
        }
        return false
    }
    
    // Track pending playback request when waiting for playback info
    property bool pendingPlaybackFromBeginning: false
    property bool hasPendingPlayback: false
    property var pendingPlaybackInfo: null  // Store playback info for deferred playback
    
    // Playback actions
    function startPlayback(fromBeginning) {
        if (!selectedEpisodeId) return
        
        console.log("[SeriesSeasonEpisodeView] startPlayback - Episode:", selectedEpisodeName,
                    "ID:", selectedEpisodeId,
                    "fromBeginning:", fromBeginning,
                    "hasPlaybackInfo:", playbackInfo !== null,
                    "playbackInfoLoading:", playbackInfoLoading)
        
        // If playback info isn't loaded, request it and defer playback
        if (!playbackInfo && !playbackInfoLoading) {
            console.log("[SeriesSeasonEpisodeView] Playback info not loaded, requesting it...")
            hasPendingPlayback = true
            pendingPlaybackFromBeginning = fromBeginning
            playbackInfoLoading = true
            PlaybackService.getPlaybackInfo(selectedEpisodeId)
            return
        }
        
        // If currently loading, just update pending state
        if (playbackInfoLoading) {
            hasPendingPlayback = true
            pendingPlaybackFromBeginning = fromBeginning
            return
        }
        
        // Playback info is ready, proceed with playback
        // Save track preferences before calling performPlayback to ensure they're set
        var seasonId = SeriesDetailsViewModel.selectedSeasonId;
        if (selectedAudioIndex >= 0) {
            PlayerController.saveAudioTrackPreference(seasonId, selectedAudioIndex);
        }
        // Allow saving "None" (-1) if a season is selected, or any valid index
        if (selectedSubtitleIndex >= -1 && seasonId) {
            PlayerController.saveSubtitleTrackPreference(seasonId, selectedSubtitleIndex);
        }
        performPlayback(fromBeginning)
    }
    
    function performPlayback(fromBeginning) {
        if (!selectedEpisodeId) return
        
        var startPos = fromBeginning ? 0 : selectedEpisodePlaybackPosition
        var framerate = getVideoFramerate()
        var isHDR = isVideoHDR()
        
        // Use pendingPlaybackInfo if playbackInfo is not set
        var info = playbackInfo || pendingPlaybackInfo
        var mediaSource = currentMediaSource  // Use the same source as applyTrackPreferences
        
        console.log("[SeriesSeasonEpisodeView] performPlayback - Episode:", selectedEpisodeName,
                    "ID:", selectedEpisodeId,
                    "fromBeginning:", fromBeginning,
                    "startPos:", startPos,
                    "playbackInfo:", playbackInfo !== null,
                    "pendingPlaybackInfo:", pendingPlaybackInfo !== null,
                    "currentMediaSource:", currentMediaSource !== null,
                    "mediaSource:", mediaSource !== null)
        console.log("[SeriesSeasonEpisodeView] performPlayback - selectedAudioIndex:", selectedAudioIndex, "selectedSubtitleIndex:", selectedSubtitleIndex)

        var overlayTitle = seriesName || qsTr("Now Playing")
        var episodePrefix = qsTr("S%1 E%2").arg(selectedSeasonNumber).arg(selectedEpisodeNumber)
        var overlaySubtitle = selectedEpisodeName ? (episodePrefix + " - " + selectedEpisodeName) : episodePrefix
        PlayerController.setOverlayMetadata(overlayTitle, overlaySubtitle, SeriesDetailsViewModel.backdropUrl || SeriesDetailsViewModel.posterUrl)
        
        if (info && mediaSource) {
            var mediaSourceId = mediaSource.id
            var playSessionId = info.playSessionId || ""
            var audioTrackMap = buildTrackMap("Audio", mediaSource)
            var subtitleTrackMap = buildTrackMap("Subtitle", mediaSource)
            var mpvAudio = resolveMpvTrackId(selectedAudioIndex, audioTrackMap)
            var mpvSubtitle = resolveMpvTrackId(selectedSubtitleIndex, subtitleTrackMap)
            var availableAudioTracks = buildTrackOptions(getAudioStreams())
            var availableSubtitleTracks = buildTrackOptions(getSubtitleStreams())
            
            console.log("[SeriesSeasonEpisodeView] Playback with tracks - mediaSourceId:", mediaSourceId,
                        "playSessionId:", playSessionId, "startPos:", startPos,
                        "audioIndex:", selectedAudioIndex, "subtitleIndex:", selectedSubtitleIndex,
                        "mpvAudio:", mpvAudio, "mpvSubtitle:", mpvSubtitle)
            
            root.playRequestedWithTracks(selectedEpisodeId, startPos, mediaSourceId, playSessionId,
                                         selectedAudioIndex, selectedSubtitleIndex,
                                         mpvAudio, mpvSubtitle,
                                         audioTrackMap, subtitleTrackMap,
                                         availableAudioTracks, availableSubtitleTracks,
                                         framerate, isHDR)
            
            // Save track preferences
            var seasonId = SeriesDetailsViewModel.selectedSeasonId
            PlayerController.saveAudioTrackPreference(seasonId, selectedAudioIndex)
            PlayerController.saveSubtitleTrackPreference(seasonId, selectedSubtitleIndex)
            
            // Clear the pending info after use
            pendingPlaybackInfo = null
        } else {
            console.log("[SeriesSeasonEpisodeView] Playback with basic info - no playbackInfo or mediaSource, startPos:", startPos)
            root.playRequested(selectedEpisodeId, startPos, framerate, isHDR)
            
            // Clear the pending info after use
            pendingPlaybackInfo = null
        }
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
    
    // Backdrop with fade
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        color: "transparent"
        z: 0
        clip: true
        
        Image {
            id: backdropImage
            anchors.fill: parent
            source: SeriesDetailsViewModel.backdropUrl || SeriesDetailsViewModel.posterUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
            
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.6
                blurMax: 48
            }
        }
        
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.3) }
                GradientStop { position: 0.5; color: Qt.rgba(0, 0, 0, 0.6) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.9) }
            }
        }
    }
    
    // Main content wrapped in Flickable for scrollability on small viewports
    Flickable {
        id: mainContentFlickable
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.paddingLarge
        anchors.rightMargin: Theme.paddingLarge
        anchors.topMargin: root.height < Math.round(1200 * Theme.layoutScale) ? Math.round(20 * Theme.layoutScale) : Math.round(60 * Theme.layoutScale)
        anchors.bottomMargin: 0
        contentWidth: width
        contentHeight: mainContentColumn.implicitHeight + bottomMargin
        
        // Bottom margin to ensure last item is fully visible
        readonly property int bottomMargin: Math.round(150 * Theme.layoutScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        z: 1
        
        // Scroll to ensure focused element is visible
        function ensureVisible(item) {
            if (!item) return
            var itemPos = item.mapToItem(mainContentColumn, 0, 0)
            var itemBottom = itemPos.y + item.height
            var viewportBottom = contentY + height
            
            if (itemBottom > viewportBottom - 80) {
                contentY = Math.max(0, Math.min(itemBottom - height + 80, contentHeight - height))
            } else if (itemPos.y < contentY + 50) {
                contentY = Math.max(0, itemPos.y - 50)
            }
        }
        
        Behavior on contentY {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
        
        ColumnLayout {
            id: mainContentColumn
            width: mainContentFlickable.width
            spacing: root.height < 1200 ? Theme.spacingMedium : Theme.spacingLarge
        
        // Season tabs row
        ListView {
            id: seasonsTabList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(50 * Theme.layoutScale)
            orientation: ListView.Horizontal
            spacing: Theme.spacingSmall
            clip: false
            
            model: SeriesDetailsViewModel.seasonsModel
            
            KeyNavigation.down: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : episodesList
            
            delegate: ItemDelegate {
                width: Math.max(Math.round(100 * Theme.layoutScale), seasonTabText.implicitWidth + Math.round(32 * Theme.layoutScale))
                height: Math.round(44 * Theme.layoutScale)
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                
                property bool isSelected: SeriesDetailsViewModel.selectedSeasonId === model.itemId
                
                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: {
                        if (parent.down) return Theme.buttonSecondaryBackgroundPressed
                        if (isSelected) return Theme.accentPrimary
                        if (parent.hovered) return Theme.buttonSecondaryBackgroundHover
                        return Theme.buttonSecondaryBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : 0
                    border.color: Theme.buttonSecondaryBorderFocused
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Text {
                    id: seasonTabText
                    text: model.name || ("Season " + model.indexNumber)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.bold: isSelected
                    color: isSelected ? Theme.textOnAccent : Theme.textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    SeriesDetailsViewModel.selectSeason(index)
                    SeriesDetailsViewModel.loadSeasonEpisodes(model.itemId)
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
        
        // Show logo or name
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: displayLogoUrl 
                ? Math.min(root.height * 0.15, Math.round(180 * Theme.layoutScale)) 
                : Math.round(60 * Theme.layoutScale)
            Layout.minimumHeight: Math.round(60 * Theme.layoutScale)
            visible: displayLogoUrl || displayName

            Image {
                visible: displayLogoUrl !== ""
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                // Height drives the logo size; width follows aspect ratio
                height: parent.height
                width: parent.width * 0.5
                source: displayLogoUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }
            }

            Text {
                visible: !displayLogoUrl && displayName
                text: displayName
                font.pixelSize: Theme.fontSizeDisplay
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
            }
        }
        
        // Episode info and metadata
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: selectedEpisodeData !== null
            
            Text {
                text: {
                    if (!selectedEpisodeData) return ""
                    var prefix = "S" + selectedSeasonNumber + " E" + selectedEpisodeNumber
                    if (selectedEpisodeNumber === 0) prefix = "Special"
                    return prefix + " • " + selectedEpisodeName
                }
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
            }
            
            RowLayout {
                spacing: Theme.spacingMedium
                
                Text {
                    visible: selectedEpisodeRuntimeTicks > 0
                    text: formatRuntime(selectedEpisodeRuntimeTicks)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
                
                Text {
                    visible: selectedEpisodeCommunityRating > 0
                    text: formatRating(selectedEpisodeCommunityRating)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: "#FFD700"
                }
                
                Text {
                    visible: selectedEpisodePremiereDate !== ""
                    text: formatPremiereDate(selectedEpisodePremiereDate)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
                
                Text {
                    visible: selectedEpisodeRuntimeTicks > 0
                    text: calculateEndTime(selectedEpisodeRuntimeTicks)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
            }
            
            ColumnLayout {
                id: overviewContainer
                Layout.fillWidth: true
                Layout.maximumWidth: root.width * 0.7
                Layout.preferredHeight: root.overviewExpanded 
                    ? Math.min(overviewText.implicitHeight + (readMoreButton.visible ? 48 : 0), root.height * 0.35) 
                    : Math.min(Theme.seriesOverviewMaxHeight, root.height * 0.12)
                Layout.topMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall
                visible: selectedEpisodeOverview !== ""
                
                property bool hasOverflow: overviewText.truncated || overviewText.lineCount > 3
                
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Text {
                        id: overviewText
                        width: parent.width
                        text: selectedEpisodeOverview
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.bold: true
                        color: Theme.textPrimary
                        style: Text.Outline
                        styleColor: "#000000"
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                        maximumLineCount: root.overviewExpanded ? 20 : 3
                        lineHeight: 1.1
                        
                        // maximumLineCount is an int; Behavior removed (not smoothly animatable)
                    }
                }
                
                Button {
                    id: readMoreButton
                    visible: overviewContainer.hasOverflow
                    text: root.overviewExpanded ? "Show Less" : "Read More"
                    Layout.preferredHeight: Math.round(32 * Theme.layoutScale)
                    Layout.preferredWidth: Math.round(120 * Theme.layoutScale)
                    
                    KeyNavigation.up: seasonsTabList
                    KeyNavigation.down: episodesList
                    
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
                    
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: parent.activeFocus ? Theme.accentPrimary : (parent.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent")
                        border.width: 1
                        border.color: parent.activeFocus ? Theme.accentPrimary : Theme.buttonSecondaryBorder
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: parent.activeFocus ? Theme.textOnAccent : Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
        
        // Horizontal episode list
        ListView {
            id: episodesList
            Layout.fillWidth: true
            
            // Responsive height calculation
            readonly property int responsiveHeight: Math.min(Theme.episodeListHeight, root.height * 0.4)
            Layout.preferredHeight: responsiveHeight
            
            Layout.topMargin: Theme.spacingMedium
            orientation: ListView.Horizontal
            spacing: Theme.spacingLarge
            clip: true
            highlightMoveDuration: Theme.durationNormal
            highlightMoveVelocity: -1
            
            model: SeriesDetailsViewModel.episodesModel
            
            KeyNavigation.up: (readMoreButton.visible && readMoreButton.enabled) ? readMoreButton : seasonsTabList
            KeyNavigation.down: playResumeButton
            
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < count) {
                    updateSelectedEpisode(currentIndex)
                    // Preload playback info for track selector and immediate playback
                    // Using debounce timer to avoid excessive API calls when rapidly navigating
                    schedulePlaybackInfoPreload()
                }
            }
            
            delegate: ItemDelegate {
                id: episodeDelegate
                
                // Calculate dimensions to fit within the responsive list height
                // Reserve space for text/padding (approx 50px scaled)
                readonly property int textAllowance: Math.round(50 * Theme.layoutScale)
                readonly property int availableThumbHeight: Math.max(1, episodesList.responsiveHeight - textAllowance)
                
                width: Math.round(availableThumbHeight * 16 / 9)
                height: availableThumbHeight  // This sets the ItemDelegate height, typically content is anchored
                
                // Explicitly set implicitHeight for the delegate to ensure ListView handles it correctly
                implicitHeight: availableThumbHeight + textAllowance
                implicitWidth: width
                
                // Expose model data for external access
                readonly property bool isPlayed: model.isPlayed || false
                readonly property bool isFavorite: model.isFavorite || false
                readonly property string itemId: model.itemId || ""
                readonly property var playbackPosition: model.playbackPositionTicks || 0

                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: Theme.cardBackground
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : 0
                    border.color: Theme.accentPrimary
                    
                    // Thumbnail image with rounded corners
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
                    
                    // Hidden mask rectangle for rounded corners
                    Rectangle {
                        id: episodeThumbnailMask
                        anchors.fill: parent
                        anchors.margins: Math.round(2 * Theme.layoutScale)
                        radius: Theme.radiusMedium
                        visible: false
                        layer.enabled: true
                        layer.smooth: true
                    }
                    
                    // Gradient overlay for text readability
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Math.round(2 * Theme.layoutScale)
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
                            GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.3) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.8) }
                        }
                    }
                    
                    // Progress bar for resume position
                    MediaProgressBar {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Math.round(2 * Theme.layoutScale)
                        anchors.rightMargin: Math.round(2 * Theme.layoutScale)
                        anchors.bottomMargin: Math.round(2 * Theme.layoutScale)
                        positionTicks: model.playbackPositionTicks || 0
                        runtimeTicks: model.runtimeTicks || 0
                    }

                    // Played indicator
                    Text {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Theme.spacingMedium
                        text: Icons.checkCircle
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: Theme.accentPrimary
                        visible: model.isPlayed || false
                        
                        // Add a small shadow for better visibility on light backgrounds
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowHorizontalOffset: 0
                            shadowVerticalOffset: 1
                            shadowBlur: 0.2  // equivalent to radius 4 / 20 roughly, tweaked for visual similarity
                            shadowColor: "black"
                        }
                    }
                }
                
                contentItem: ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    spacing: Math.round(2 * Theme.layoutScale)
                    
                    Item { Layout.fillHeight: true }
                    
                    Text {
                        text: "E" + model.indexNumber
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
                        visible: model.runtimeTicks > 0
                        text: formatRuntime(model.runtimeTicks)
                        font.pixelSize: Theme.fontSizeCaption
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }
                
                onClicked: {
                    episodesList.currentIndex = index
                    episodesList.forceActiveFocus()
                }
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    episodesList.currentIndex = index
                    startPlayback(false)
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    episodesList.currentIndex = index
                    startPlayback(false)
                    event.accepted = true
                }
            }
        }
        
        // Action buttons row
        RowLayout {
            id: actionsRow
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            // Scroll action buttons into view when any child receives focus
            onActiveFocusChanged: {
                if (activeFocus) {
                    mainContentFlickable.ensureVisible(actionsRow)
                }
            }
            
            Button {
                id: playResumeButton
                Layout.preferredWidth: Theme.buttonIconSize
                Layout.preferredHeight: Theme.buttonIconSize
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                enabled: selectedEpisodeId !== ""
                
                KeyNavigation.up: episodesList
                KeyNavigation.right: playFromBeginningButton
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    if (enabled) {
                        startPlayback(false)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    if (enabled) {
                        startPlayback(false)
                        event.accepted = true
                    }
                }
                onClicked: startPlayback(false)
                
                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: {
                        if (!parent.enabled) return Theme.buttonIconBackground
                        if (parent.down) return Theme.buttonPrimaryBackgroundPressed
                        if (parent.hovered) return Theme.buttonPrimaryBackgroundHover
                        return Theme.buttonPrimaryBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: parent.activeFocus ? Theme.buttonPrimaryBorderFocused : Theme.buttonPrimaryBorder
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Item {
                    BusyIndicator {
                        id: busyIndicator
                        visible: playbackInfoLoading
                        running: visible
                        anchors.centerIn: parent
                        width: Theme.fontSizeIcon
                        height: Theme.fontSizeIcon
                    }
                    Text {
                        visible: !playbackInfoLoading
                        text: selectedEpisodePlaybackPosition > 0 ? Icons.fastForward : Icons.playArrow
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeIcon
                        font.family: Theme.fontIcon
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        anchors.fill: parent
                    }
                }
            }
            
            Button {
                id: playFromBeginningButton
                Layout.preferredWidth: Theme.buttonIconSize
                Layout.preferredHeight: Theme.buttonIconSize
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                enabled: selectedEpisodeId !== ""
                
                KeyNavigation.left: playResumeButton
                KeyNavigation.right: watchedToggleButton
                KeyNavigation.up: episodesList
                
                Keys.onReturnPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    if (enabled) {
                        startPlayback(true)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (event.isAutoRepeat) {
                        event.accepted = true
                        return
                    }
                    if (enabled) {
                        startPlayback(true)
                        event.accepted = true
                    }
                }
                onClicked: startPlayback(true)
                
                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: {
                        if (!parent.enabled) return Theme.buttonIconBackground
                        if (parent.down) return Theme.buttonSecondaryBackgroundPressed
                        if (parent.hovered) return Theme.buttonSecondaryBackgroundHover
                        return Theme.buttonSecondaryBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: parent.activeFocus ? Theme.buttonSecondaryBorderFocused : Theme.buttonSecondaryBorder
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Item {
                    BusyIndicator {
                        id: busyIndicator2
                        visible: playbackInfoLoading
                        running: visible
                        anchors.centerIn: parent
                        width: Theme.fontSizeIcon
                        height: Theme.fontSizeIcon
                    }
                    Text {
                        visible: !playbackInfoLoading
                        text: Icons.replay
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeIcon
                        font.family: Theme.fontIcon
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        anchors.fill: parent
                    }
                }
            }
            
            Button {
                id: watchedToggleButton
                Layout.preferredWidth: Theme.buttonIconSize
                Layout.preferredHeight: Theme.buttonIconSize
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                enabled: selectedEpisodeId !== ""
                
                KeyNavigation.left: playFromBeginningButton
                KeyNavigation.right: favoriteButton
                KeyNavigation.up: episodesList
                
                Keys.onReturnPressed: if (enabled) toggleWatchedStatus()
                Keys.onEnterPressed: if (enabled) toggleWatchedStatus()
                onClicked: toggleWatchedStatus()
                
                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: {
                        if (!parent.enabled) return Theme.buttonIconBackground
                        if (parent.down) return Theme.buttonSecondaryBackgroundPressed
                        if (parent.hovered) return Theme.buttonSecondaryBackgroundHover
                        return Theme.buttonSecondaryBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: parent.activeFocus ? Theme.buttonSecondaryBorderFocused : Theme.buttonSecondaryBorder
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Text {
                    text: selectedEpisodeIsPlayed ? Icons.visibilityOff : Icons.visibility
                    color: selectedEpisodeIsPlayed ? Theme.accentPrimary : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeIcon
                    font.family: Theme.fontIcon
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
            
            Button {
                id: favoriteButton
                Layout.preferredWidth: Theme.buttonIconSize
                Layout.preferredHeight: Theme.buttonIconSize
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                enabled: selectedEpisodeId !== ""
                
                KeyNavigation.left: watchedToggleButton
                KeyNavigation.right: contextMenuButton
                KeyNavigation.up: episodesList
                
                Keys.onReturnPressed: if (enabled) toggleFavorite()
                Keys.onEnterPressed: if (enabled) toggleFavorite()
                onClicked: toggleFavorite()
                
                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: {
                        if (!parent.enabled) return Theme.buttonIconBackground
                        if (parent.down) return Theme.buttonIconBackgroundPressed
                        if (parent.hovered) return Theme.buttonIconBackgroundHover
                        return Theme.buttonIconBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: parent.activeFocus ? Theme.buttonIconBorderFocused : Theme.buttonIconBorder
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Text {
                    text: selectedEpisodeIsFavorite ? Icons.favorite : Icons.favoriteBorder
                    color: selectedEpisodeIsFavorite ? "#e74c3c" : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeIcon
                    font.family: Theme.fontIcon
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
            
            Button {
                id: contextMenuButton
                Layout.preferredWidth: Theme.buttonIconSize
                Layout.preferredHeight: Theme.buttonIconSize
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                enabled: selectedEpisodeId !== ""
                
                KeyNavigation.left: favoriteButton
                KeyNavigation.up: episodesList
                
                Keys.onReturnPressed: if (enabled) openTrackSelector()
                Keys.onEnterPressed: if (enabled) openTrackSelector()
                onClicked: openTrackSelector()
                
                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: {
                        if (!parent.enabled) return Theme.buttonIconBackground
                        if (parent.down) return Theme.buttonIconBackgroundPressed
                        if (parent.hovered) return Theme.buttonIconBackgroundHover
                        return Theme.buttonIconBackground
                    }
                    border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                    border.color: parent.activeFocus ? Theme.buttonIconBorderFocused : Theme.buttonIconBorder
                    
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }
                
                contentItem: Text {
                    text: Icons.moreVert
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSizeIcon
                    font.family: Theme.fontIcon
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }  // End of mainContentColumn ColumnLayout
    }  // End of mainContentFlickable
    
    // Context Menu for Audio/Subtitle Selection
    Menu {
        id: contextMenu
        
        background: Rectangle {
            implicitWidth: Math.round(280 * Theme.layoutScale)
            color: Theme.cardBackground
            radius: Theme.radiusMedium
            border.color: Theme.cardBorder
            border.width: 1
            
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 4
                shadowBlur: 0.5
                shadowColor: "#44000000"
            }
        }
        
        delegate: MenuItem {
            id: menuItem
            implicitWidth: Math.round(240 * Theme.layoutScale)
            implicitHeight: Math.round(40 * Theme.layoutScale)
            
            arrow: Canvas {
                x: parent.width - width - Math.round(12 * Theme.layoutScale)
                y: parent.height / 2 - height / 2
                width: Math.round(12 * Theme.layoutScale)
                height: Math.round(12 * Theme.layoutScale)
                visible: menuItem.subMenu
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.fillStyle = menuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                    ctx.moveTo(0, 0)
                    ctx.lineTo(0, height)
                    ctx.lineTo(width, height / 2)
                    ctx.closePath()
                    ctx.fill()
                }
            }
            
            contentItem: Text {
                text: menuItem.text
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: menuItem.highlighted ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spacingSmall
                rightPadding: menuItem.arrow.width + Theme.spacingSmall
            }
            
            background: Rectangle {
                implicitWidth: Math.round(240 * Theme.layoutScale)
                implicitHeight: Math.round(40 * Theme.layoutScale)
                opacity: enabled ? 1 : 0.3
                color: menuItem.highlighted ? Theme.hoverOverlay : "transparent"
                radius: Theme.radiusSmall
            }
        }
        
        onOpened: {
            // Re-fetch tracks when menu opens
            console.log("[ContextMenu] Menu opened")
            
            // Focus the first item
            currentIndex = 0
            forceActiveFocus()
            
             // Apply saved track preferences for this season
            var seasonId = SeriesDetailsViewModel.selectedSeasonId
            if (seasonId && seasonId !== "") {
                var lastAudio = PlayerController.getLastAudioTrackForSeason(seasonId)
                var lastSubtitle = PlayerController.getLastSubtitleTrackForSeason(seasonId)
                
                if (lastAudio >= 0) selectedAudioIndex = lastAudio
                if (lastSubtitle >= 0) selectedSubtitleIndex = lastSubtitle
            }
        }
        
        onClosed: {
            console.log("[ContextMenu] Menu closed, restoring focus to contextMenuButton")
            Qt.callLater(function() {
                contextMenuButton.forceActiveFocus()
            })
        }
        
        Menu {
            id: audioMenu
            title: "Audio Tracks"
            enabled: getAudioStreams().length > 0
            
            background: Rectangle {
                implicitWidth: Math.round(280 * Theme.layoutScale)
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }
            
            Repeater {
                model: getAudioStreams()
                
                MenuItem {
                    id: audioMenuItem
                    required property var modelData
                    
                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedAudioIndex
                    
                    indicator: Item {}
                    
                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall
                        
                        Text {
                            text: audioMenuItem.checked ? "✓" : "  "
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                        }
                        
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: scrollingAudioText.height
                            clip: true
                            
                            Text {
                                id: scrollingAudioText
                                text: audioMenuItem.text
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                                
                                SequentialAnimation on x {
                                    running: scrollingAudioText.width > parent.width && audioMenuItem.highlighted
                                    loops: Animation.Infinite
                                    
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: -scrollingAudioText.width + parent.width
                                        duration: Math.max(1000, (scrollingAudioText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: 0
                                        duration: Math.max(1000, (scrollingAudioText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                }
                                
                                // Reset position when not running
                                onXChanged: {
                                    if (!running && x !== 0) x = 0
                                }
                                property bool running: scrollingAudioText.width > parent.width && audioMenuItem.highlighted
                            }
                        }
                    }
                    
                    background: Rectangle {
                        color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                        radius: Theme.radiusSmall
                    }
                    
                    onTriggered: {
                        selectedAudioIndex = modelData.index
                        console.log("[ContextMenu] Selected audio:", selectedAudioIndex)
                        var seasonId = SeriesDetailsViewModel.selectedSeasonId
                        if (seasonId) {
                             PlayerController.saveAudioTrackPreference(seasonId, selectedAudioIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
        
        Menu {
            id: subtitleMenu
            title: "Subtitle Tracks"
            
            background: Rectangle {
                implicitWidth: Math.round(280 * Theme.layoutScale)
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.color: Theme.cardBorder
                border.width: 1
            }
            
            MenuItem {
                text: "None"
                checkable: true
                checked: selectedSubtitleIndex === -1
                indicator: Item {}
                
                contentItem: RowLayout {
                    spacing: Theme.spacingSmall
                    
                    Text {
                        text: parent.checked ? "✓" : "  "
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.accentPrimary
                        Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                    }
                    
                    Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                }
                
                background: Rectangle {
                    color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                    radius: Theme.radiusSmall
                }
                
                onTriggered: {
                    selectedSubtitleIndex = -1
                    console.log("[ContextMenu] Selected subtitle: None")
                    var seasonId = SeriesDetailsViewModel.selectedSeasonId
                    if (seasonId) {
                         PlayerController.saveSubtitleTrackPreference(seasonId, selectedSubtitleIndex)
                    }
                }
            }
            
            Repeater {
                model: getSubtitleStreams()
                
                MenuItem {
                    id: subtitleMenuItem
                    required property var modelData
                    
                    text: TrackUtils.formatTrackName(modelData)
                    checkable: true
                    checked: modelData.index === selectedSubtitleIndex
                    
                    indicator: Item {}
                    
                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall
                        
                        Text {
                            text: subtitleMenuItem.checked ? "✓" : "  "
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.accentPrimary
                            Layout.preferredWidth: Math.round(20 * Theme.layoutScale)
                        }
                        
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: scrollingSubtitleText.height
                            clip: true
                            
                            Text {
                                id: scrollingSubtitleText
                                text: subtitleMenuItem.text
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                                
                                SequentialAnimation on x {
                                    running: scrollingSubtitleText.width > parent.width && subtitleMenuItem.highlighted
                                    loops: Animation.Infinite
                                    
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: -scrollingSubtitleText.width + parent.width
                                        duration: Math.max(1000, (scrollingSubtitleText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                    PauseAnimation { duration: 1000 }
                                    NumberAnimation {
                                        to: 0
                                        duration: Math.max(1000, (scrollingSubtitleText.width - parent.width) * 20)
                                        easing.type: Easing.Linear
                                    }
                                }
                                
                                // Reset position when not running
                                onXChanged: {
                                    if (!running && x !== 0) x = 0
                                }
                                property bool running: scrollingSubtitleText.width > parent.width && subtitleMenuItem.highlighted
                            }
                        }
                    }
                    
                    background: Rectangle {
                        color: parent.highlighted ? Theme.hoverOverlay : "transparent"
                        radius: Theme.radiusSmall
                    }
                    
                    onTriggered: {
                        selectedSubtitleIndex = modelData.index
                        console.log("[ContextMenu] Selected subtitle:", selectedSubtitleIndex)
                        var seasonId = SeriesDetailsViewModel.selectedSeasonId
                        if (seasonId) {
                             PlayerController.saveSubtitleTrackPreference(seasonId, selectedSubtitleIndex)
                        }
                        contextMenu.close()
                    }
                }
            }
        }
    }

    // Focus restoration on breakpoint changes
    property var savedFocusItem: null
    property int savedEpisodeIndex: -1

    Connections {
        target: ResponsiveLayoutManager
        function onBreakpointChanged() {
            root.savedEpisodeIndex = episodesList.currentIndex
            var active = root.Window.activeFocusItem
            if (active && active.parent && typeof active.forceActiveFocus === 'function') {
                root.savedFocusItem = active
            } else {
                root.savedFocusItem = null
            }
            Qt.callLater(root.restoreFocusAfterBreakpoint)
        }
    }

    function restoreFocusAfterBreakpoint() {
        if (savedEpisodeIndex >= 0 && episodesList.count > 0) {
            episodesList.currentIndex = Math.min(savedEpisodeIndex, episodesList.count - 1)
            episodesList.positionViewAtIndex(episodesList.currentIndex, ListView.Contain)
        }
        if (savedFocusItem && savedFocusItem.parent && typeof savedFocusItem.forceActiveFocus === 'function') {
            savedFocusItem.forceActiveFocus()
        } else {
            if (episodesList.currentItem) {
                episodesList.currentItem.forceActiveFocus()
            } else {
                root.forceActiveFocus()
            }
        }
        savedFocusItem = null
    }
}
