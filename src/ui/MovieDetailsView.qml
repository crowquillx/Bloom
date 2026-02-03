import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Qt5Compat.GraphicalEffects
import BloomUI

FocusScope {
    id: root
    
    // Input properties - Movie data
    property var movieData: null
    property string movieId: movieData ? movieData.Id : ""
    property string movieName: movieData ? movieData.Name : ""
    property string overview: movieData ? (movieData.Overview || "") : ""
    property var runtimeTicks: movieData ? (movieData.RunTimeTicks || 0) : 0
    property var communityRating: movieData ? (movieData.CommunityRating || 0) : 0
    property string premiereDate: movieData ? (movieData.PremiereDate || "") : ""
    property int productionYear: movieData ? (movieData.ProductionYear || 0) : 0
    property bool isPlayed: movieData ? (movieData.UserData ? movieData.UserData.Played : false) : false
    property var playbackPositionTicks: movieData ? (movieData.UserData ? movieData.UserData.PlaybackPositionTicks : 0) : 0
    property string officialRating: movieData ? (movieData.OfficialRating || "") : ""
    property var genres: movieData ? (movieData.Genres || []) : []
    
    // Playback info and media source state (for framerate)
    property var playbackInfo: null
    property var currentMediaSource: playbackInfo && playbackInfo.mediaSources && playbackInfo.mediaSources.length > 0 
                                     ? playbackInfo.mediaSources[0] : null
    property bool playbackInfoLoading: false
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1
    
    // Signals for navigation and actions
    signal playRequested(string itemId, var startPositionTicks, double framerate, bool isHDR)
    signal playRequestedWithTracks(string itemId, var startPositionTicks, string mediaSourceId, 
                                    string playSessionId, int audioIndex, int subtitleIndex,
                                    int mpvAudioTrack, int mpvSubtitleTrack, double framerate, bool isHDR)
    signal backRequested()
    
    // Helper function to get framerate from media source
    function getVideoFramerate() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return 0.0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video") {
                // Prefer RealFrameRate if available, otherwise use AverageFrameRate
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
    
    // Helper function to check if content is HDR
    function isVideoHDR() {
        if (!currentMediaSource || !currentMediaSource.mediaStreams) return false
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Video" && stream.videoRange) {
                // Check for HDR variants: HDR, HDR10, HDR10+, HLG, Dolby Vision
                var range = stream.videoRange.toUpperCase()
                if (range !== "SDR" && range !== "") {
                    console.log("[MovieDetailsView] Detected HDR content, videoRange:", stream.videoRange)
                    return true
                }
            }
        }
        return false
    }
    
    // Convert Jellyfin stream index to mpv track number
    // mpv uses 1-based indices that are separate for each track type (vid, aid, sid)
    // Returns 1-based mpv track number, or -1 if not found
    function getMpvAudioTrackNumber(jellyfinStreamIndex) {
        if (!currentMediaSource || !currentMediaSource.mediaStreams || jellyfinStreamIndex < 0) return -1
        var audioTrackNum = 0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Audio") {
                audioTrackNum++
                if (stream.index === jellyfinStreamIndex) {
                    return audioTrackNum  // 1-based
                }
            }
        }
        return -1  // Not found
    }
    
    function getMpvSubtitleTrackNumber(jellyfinStreamIndex) {
        if (!currentMediaSource || !currentMediaSource.mediaStreams || jellyfinStreamIndex < 0) return -1
        var subTrackNum = 0
        for (var i = 0; i < currentMediaSource.mediaStreams.length; i++) {
            var stream = currentMediaSource.mediaStreams[i]
            if (stream.type === "Subtitle") {
                subTrackNum++
                if (stream.index === jellyfinStreamIndex) {
                    return subTrackNum  // 1-based
                }
            }
        }
        return -1  // Not found
    }
    
    // Function to start playback with current track selections
    function startPlaybackWithTracks() {
        var framerate = getVideoFramerate()
        var hdr = isVideoHDR()
        console.log("[MovieDetailsView] Starting playback with framerate:", framerate, "isHDR:", hdr)
        
        if (playbackInfo && currentMediaSource) {
            // Convert Jellyfin stream indices to mpv track numbers
            var mpvAudioTrack = getMpvAudioTrackNumber(selectedAudioIndex)
            var mpvSubTrack = getMpvSubtitleTrackNumber(selectedSubtitleIndex)
            
            console.log("[MovieDetailsView] Track mapping - Audio: Jellyfin", selectedAudioIndex, "-> mpv", mpvAudioTrack,
                        "Subtitle: Jellyfin", selectedSubtitleIndex, "-> mpv", mpvSubTrack)
            
            root.playRequestedWithTracks(
                movieId,
                playbackPositionTicks,
                currentMediaSource.id,
                playbackInfo.playSessionId,
                selectedAudioIndex,     // Jellyfin index for URL/API
                selectedSubtitleIndex,  // Jellyfin index for URL/API
                mpvAudioTrack,          // mpv track number for mpv commands
                mpvSubTrack,            // mpv track number for mpv commands
                framerate,
                hdr
            )
        } else {
            // Fallback to simple play if no playback info
            root.playRequested(movieId, playbackPositionTicks, framerate, hdr)
        }
    }
    
    // Key handling for back navigation
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            console.log("[MovieDetailsView] Back key pressed")
            root.backRequested()
            event.accepted = true
        }
    }
    
    focus: true
    
    // Focus when this view receives active focus
    onActiveFocusChanged: {
        if (activeFocus && playButton) {
            Qt.callLater(function() {
                playButton.forceActiveFocus()
            })
        }
    }
    
    // Helper functions
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
    
    function calculateEndTime(runtimeTicks) {
        if (!runtimeTicks || runtimeTicks === 0) return ""
        var now = new Date()
        var runtimeMs = runtimeTicks / 10000
        var endTime = new Date(now.getTime() + runtimeMs)
        return "Ends at " + endTime.toLocaleTimeString(Qt.locale(), "h:mm AP")
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
    
    function getMovieBackdropUrl() {
        if (!movieData) return ""
        
        var itemId = movieData.Id
        var imageTags = movieData.BackdropImageTags || []
        
        // Try Backdrop first
        if (imageTags.length > 0) {
            return LibraryService.getCachedImageUrlWithWidth(itemId, "Backdrop", 1920)
        }
        // Fallback to Primary
        if (movieData.ImageTags && movieData.ImageTags.Primary) {
            return LibraryService.getCachedImageUrlWithWidth(itemId, "Primary", 1920)
        }
        return ""
    }
    
    function getMoviePosterUrl() {
        if (!movieData) return ""
        
        var itemId = movieData.Id
        var imageTags = movieData.ImageTags || {}
        
        if (imageTags.Primary) {
            return LibraryService.getCachedImageUrlWithWidth(itemId, "Primary", 400)
        }
        return ""
    }
    
    function getMovieLogoUrl() {
        if (!movieData) return ""
        
        var itemId = movieData.Id
        var imageTags = movieData.ImageTags || {}
        
        if (imageTags.Logo) {
            return LibraryService.getCachedImageUrlWithWidth(itemId, "Logo", 2000)
        }
        return ""
    }
    
    // Movie Backdrop
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        z: 0
        radius: Theme.radiusLarge
        color: "transparent"
        clip: true
        
        Image {
            id: backdropImage
            anchors.fill: parent
            source: getMovieBackdropUrl()
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.6
                blurMax: 48
            }
        }
        
        // Gradient overlay for readability
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.3) }
                GradientStop { position: 0.5; color: Qt.rgba(0, 0, 0, 0.6) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.9) }
            }
        }
    }
    
    // Main Content
    RowLayout {
        anchors.fill: parent
        anchors.margins: 64
        spacing: 48
        z: 1
        
        // Left side - Movie details
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.6
            spacing: 16
            
            // Movie Logo (if available)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: getMovieLogoUrl() ? Math.round(Theme.seriesLogoHeight * 0.5) : 0
                visible: getMovieLogoUrl() !== ""
                
                Image {
                    id: logoImage
                    anchors.left: parent.left
                    width: Math.min(Math.round(Theme.seriesLogoMaxWidth * 0.5), parent.width)
                    height: parent.height
                    source: getMovieLogoUrl()
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 300 } }
                }
            }
            
            // Movie Title
            Text {
                visible: getMovieLogoUrl() === ""
                text: movieName
                font.pixelSize: Theme.fontSizeDisplay
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            
            // Metadata row
            RowLayout {
                Layout.fillWidth: true
                spacing: 24
                
                Text {
                    visible: productionYear > 0
                    text: productionYear.toString()
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
                
                Text {
                    visible: officialRating !== ""
                    text: officialRating
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        z: -1
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondary
                        radius: 2
                    }
                }
                
                Text {
                    visible: runtimeTicks > 0
                    text: formatRuntime(runtimeTicks)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
                
                Text {
                    visible: communityRating > 0
                    text: formatRating(communityRating)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: "#FFD700"
                }
                
                Text {
                    visible: runtimeTicks > 0
                    text: calculateEndTime(runtimeTicks)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
            }
            
            // Genres row
            RowLayout {
                visible: genres.length > 0
                Layout.fillWidth: true
                spacing: 8
                
                Repeater {
                    model: genres.slice(0, 4)  // Limit to 4 genres
                    
                    Rectangle {
                        width: genreText.width + 16
                        height: 28
                        radius: 14
                        color: Theme.backgroundSecondary
                        border.width: 1
                        border.color: Theme.borderLight
                        
                        Text {
                            id: genreText
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                        }
                    }
                }
            }
            
            // Action Buttons Row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                spacing: 16
                
                // Play Button
                Button {
                    id: playButton
                    text: playbackPositionTicks > 0 ? "▶ Resume" : "▶ Play"
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: Theme.buttonHeightLarge
                    
                    KeyNavigation.right: markWatchedButton
                    KeyNavigation.down: mediaInfoPanel.visible ? mediaInfoPanel : overviewText
                    
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    
                    onClicked: {
                        // Use startPlaybackWithTracks to include track selections
                        root.startPlaybackWithTracks()
                    }
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonPrimaryBackgroundPressed
                            if (parent.hovered) return Theme.buttonPrimaryBackgroundHover
                            return Theme.buttonPrimaryBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: parent.activeFocus ? Theme.buttonPrimaryBorderFocused : Theme.buttonPrimaryBorder
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeMedium
                        font.family: Theme.fontPrimary
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // Mark Watched/Unwatched Button
                Button {
                    id: markWatchedButton
                    text: isPlayed ? "Mark Unwatched" : "Mark Watched"
                    Layout.preferredWidth: implicitWidth + 32
                    Layout.preferredHeight: Theme.buttonHeightLarge
                    
                    KeyNavigation.left: playButton
                    KeyNavigation.down: mediaInfoPanel.visible ? mediaInfoPanel : overviewText
                    
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    
                    onClicked: {
                        // TODO: Implement mark watched/unwatched
                        console.log("Mark movie", isPlayed ? "unwatched" : "watched")
                    }
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonSecondaryBackgroundPressed
                            if (parent.hovered) return Theme.buttonSecondaryBackgroundHover
                            return Theme.buttonSecondaryBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonSecondaryBorderFocused
                            if (parent.hovered) return Theme.buttonSecondaryBorderHover
                            return Theme.buttonSecondaryBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            
            // Progress bar (if partially watched)
            ColumnLayout {
                visible: playbackPositionTicks > 0 && !isPlayed
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Text {
                        property var watchedMinutes: Math.round(playbackPositionTicks / 600000000)
                        property var totalMinutes: Math.round(runtimeTicks / 600000000)
                        text: watchedMinutes + " of " + totalMinutes + " min watched"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        property var remainingMinutes: Math.round((runtimeTicks - playbackPositionTicks) / 600000000)
                        text: remainingMinutes + " min remaining"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: Qt.rgba(1, 1, 1, 0.2)
                    
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(1, playbackPositionTicks / runtimeTicks)
                        radius: 3
                        color: Theme.accentPrimary
                    }
                }
            }
            
            // Media Info Panel - Video/Audio/Subtitle selection
            MediaInfoPanel {
                id: mediaInfoPanel
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.preferredWidth: 350
                Layout.maximumHeight: 250
                visible: currentMediaSource !== null && !playbackInfoLoading
                
                mediaSource: root.currentMediaSource
                selectedAudioIndex: root.selectedAudioIndex
                selectedSubtitleIndex: root.selectedSubtitleIndex
                
                KeyNavigation.up: playButton
                KeyNavigation.down: overviewText
                
                onAudioTrackChanged: function(index) {
                    root.selectedAudioIndex = index
                    console.log("[MovieDetailsView] Audio track changed to", index, "movieId:", root.movieId)
                    if (root.movieId) {
                        PlayerController.saveMovieAudioTrackPreference(root.movieId, index)
                    }
                }
                
                onSubtitleTrackChanged: function(index) {
                    root.selectedSubtitleIndex = index
                    console.log("[MovieDetailsView] Subtitle track changed to", index, "movieId:", root.movieId)
                    if (root.movieId) {
                        PlayerController.saveMovieSubtitleTrackPreference(root.movieId, index)
                    }
                }
            }
            
            // Overview/Description
            ScrollView {
                id: overviewScrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 24
                clip: true
                
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                
                Text {
                    id: overviewText
                    width: overviewScrollView.availableWidth
                    text: overview || "No description available."
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                    opacity: activeFocus ? 1.0 : 0.85
                    focus: true
                    
                    KeyNavigation.up: mediaInfoPanel.visible ? mediaInfoPanel : playButton
                    
                    Keys.onUpPressed: (event) => {
                        var step = 40
                        if (overviewScrollView.contentY > 0) {
                            overviewScrollView.contentY = Math.max(0, overviewScrollView.contentY - step)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onDownPressed: (event) => {
                        var step = 40
                        var maxScroll = height - overviewScrollView.height
                        if (maxScroll > 0 && overviewScrollView.contentY < maxScroll) {
                            overviewScrollView.contentY = Math.min(maxScroll, overviewScrollView.contentY + step)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                }
            }
        }
        
        // Right side - Movie Poster
        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.35
            
            // Movie poster card
            Rectangle {
                id: posterCard
                anchors.top: parent.top
                anchors.right: parent.right
                width: Math.min(parent.width, 350)
                height: width * 1.5 // 2:3 aspect ratio
                radius: Theme.radiusLarge
                antialiasing: true
                color: Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                clip: true
                
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 12
                    shadowBlur: 0.6
                    shadowColor: Qt.rgba(0, 0, 0, 0.5)
                }
                
                // Poster image container with rounded corners
                Rectangle {
                    id: posterImageContainer
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: Theme.imageRadius
                    antialiasing: true
                    color: "transparent"
                    clip: false
                    
                    Image {
                        id: posterImage
                        anchors.fill: parent
                        source: getMoviePosterUrl()
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        visible: false
                    }

                    Rectangle {
                        id: posterMask
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        color: "white"
                        visible: false
                        layer.enabled: true
                    }

                    OpacityMask {
                        anchors.fill: parent
                        source: posterImage
                        maskSource: posterMask
                    }
                    
                    // Play icon overlay
                    Rectangle {
                        anchors.centerIn: parent
                        width: 80
                        height: 80
                        radius: 40
                        color: Qt.rgba(0, 0, 0, 0.7)
                        visible: posterImage.status === Image.Ready
                        
                        Text {
                            anchors.centerIn: parent
                            text: Icons.playArrow
                            font.pixelSize: 36
                            font.family: Theme.fontIcon
                            color: Theme.textPrimary
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.startPlaybackWithTracks()
                        }
                    }
                    
                    // Placeholder
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        color: Qt.rgba(0.15, 0.15, 0.15, 0.9)
                        visible: posterImage.status !== Image.Ready
                        
                        Text {
                            anchors.centerIn: parent
                            text: movieName ? movieName.charAt(0).toUpperCase() : "?"
                            font.pixelSize: Theme.fontSizeDisplay
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                        }
                    }
                }
            }
            
            // Watched badge
            Rectangle {
                visible: isPlayed
                anchors.top: posterCard.bottom
                anchors.topMargin: 16
                anchors.horizontalCenter: posterCard.horizontalCenter
                width: watchedLabel.width + 24
                height: 32
                radius: 16
                color: Theme.accentSecondary
                
                Row {
                    id: watchedLabel
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        text: Icons.check
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "Watched"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        font.bold: true
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
    
    // Load playback info when movie changes
    onMovieIdChanged: {
        if (movieId) {
            playbackInfoLoading = true
            playbackInfo = null
            selectedAudioIndex = -1
            selectedSubtitleIndex = -1
            PlaybackService.getPlaybackInfo(movieId)
        }
    }
    
    // Watch for movieData changes to set focus when data becomes available
    onMovieDataChanged: {
        if (movieData && movieData.Id) {
            console.log("[MovieDetailsView] Movie data loaded:", movieData.Name)
            // Delay focus slightly to ensure UI is fully rendered
            focusTimer.start()
        }
    }
    
    // Connection to receive playback info
    Connections {
        target: PlaybackService
        
        function onPlaybackInfoLoaded(itemId, info) {
            if (itemId === root.movieId) {
                console.log("[MovieDetailsView] Playback info loaded for", itemId)
                root.playbackInfo = info
                root.playbackInfoLoading = false
                
                // Initialize track selections - prefer saved preferences, fall back to server defaults
                if (info.mediaSources && info.mediaSources.length > 0) {
                    var source = info.mediaSources[0]
                    
                    // Check for saved track preferences for this movie
                    var savedAudio = PlayerController.getLastAudioTrackForMovie(root.movieId)
                    var savedSubtitle = PlayerController.getLastSubtitleTrackForMovie(root.movieId)
                    
                    // Apply saved audio preference if available, otherwise use server default
                    if (savedAudio >= 0) {
                        console.log("[MovieDetailsView] Using saved audio track preference:", savedAudio)
                        root.selectedAudioIndex = savedAudio
                    } else if (source.defaultAudioStreamIndex >= 0) {
                        root.selectedAudioIndex = source.defaultAudioStreamIndex
                    }
                    
                    // Apply saved subtitle preference if available, otherwise use server default
                    // Note: -1 means "no subtitles" which is a valid saved preference
                    if (source.defaultSubtitleStreamIndex >= 0) {
                        root.selectedSubtitleIndex = source.defaultSubtitleStreamIndex
                    }
                    // Override with saved preference (savedSubtitle >= -1 means any saved value including "off")
                    if (savedSubtitle >= 0 || (savedSubtitle === -1 && savedAudio >= 0)) {
                        // If we have a valid audio preference, we likely have subtitle preference too
                        console.log("[MovieDetailsView] Using saved subtitle track preference:", savedSubtitle)
                        root.selectedSubtitleIndex = savedSubtitle
                    }
                }
            }
        }
    }
    
    Timer {
        id: focusTimer
        interval: 50
        repeat: false
        onTriggered: {
            console.log("[MovieDetailsView] Setting focus to playButton")
            playButton.forceActiveFocus()
        }
    }
    
    Component.onCompleted: {
        // If data is already loaded (e.g., direct navigation), set focus via timer
        if (movieData && movieData.Id) {
            focusTimer.start()
        }
        
        // Load playback info if we have a movie
        if (movieId) {
            playbackInfoLoading = true
            PlaybackService.getPlaybackInfo(movieId)
        }
    }
}
