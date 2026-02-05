import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Qt5Compat.GraphicalEffects
import BloomUI

FocusScope {
    id: root
    
    // Input properties
    property string seasonId: ""
    property string seasonName: ""
    property int seasonNumber: 0
    property string seriesId: ""
    property string seriesName: ""
    property string seriesLogoUrl: ""
    property string seasonPosterUrl: ""
    property string seriesPosterUrl: ""
    property int pendingEpisodeIndex: -1  // Index to restore when returning from episode view
    
    // Watch for pendingEpisodeIndex changes - this handles the case where the property
    // is set after Component.onCompleted runs
    onPendingEpisodeIndexChanged: {
        console.log("[SeasonView] pendingEpisodeIndex changed to:", pendingEpisodeIndex)
        if (pendingEpisodeIndex >= 0 && episodesList.count > 0) {
            Qt.callLater(function() {
                if (root.pendingEpisodeIndex >= 0) {
                    var targetIndex = Math.min(root.pendingEpisodeIndex, episodesList.count - 1)
                    console.log("[SeasonView] Restoring episode index from property change:", targetIndex)
                    episodesList.currentIndex = targetIndex
                    episodesList.positionViewAtIndex(targetIndex, ListView.Center)
                    episodesList.forceActiveFocus()
                    root.pendingEpisodeIndex = -1
                }
            })
        }
    }
    
    // Signals for navigation and actions
    signal episodeSelected(var episodeData)
    signal playEpisode(string episodeId, var startPositionTicks)
    signal backRequested()
    
    // Key handling for back navigation - use onReleased like EpisodeView
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            console.log("[SeasonView] Back key released - triggering backRequested")
            root.backRequested()
            event.accepted = true
        }
    }
    
    // FocusScope needs focus to propagate to children
    focus: true
    
    // Focus when this view receives active focus
    onActiveFocusChanged: {
        if (activeFocus) {
            // Delegate focus to appropriate element
            Qt.callLater(function() {
                if (pendingEpisodeIndex >= 0 && episodesList.count > 0) {
                    episodesList.forceActiveFocus()
                } else if (playButton && playButton.visible) {
                    playButton.forceActiveFocus()
                } else if (episodesList && episodesList.count > 0) {
                    episodesList.forceActiveFocus()
                }
            })
        }
    }
    
    // Load season episodes when seasonId changes
    onSeasonIdChanged: {
        if (seasonId !== "") {
            console.log("[SeasonView] Loading episodes for season:", seasonId)
            SeriesDetailsViewModel.loadSeasonEpisodes(seasonId)
        }
    }
    
    // Bind to ViewModel properties
    readonly property bool isLoading: SeriesDetailsViewModel.isLoading
    // Use ListView's count which tracks the model's row count automatically
    readonly property int episodeCount: episodesList.count
    
    // Get the best available poster
    readonly property string displayPosterUrl: {
        if (seasonPosterUrl && seasonPosterUrl !== "") return seasonPosterUrl
        if (seriesPosterUrl && seriesPosterUrl !== "") return seriesPosterUrl
        return ""
    }
    
    // Helper function to format runtime from ticks to minutes
    function formatRuntime(ticks) {
        if (!ticks || ticks === 0) return ""
        var minutes = Math.round(ticks / 600000000) // ticks to minutes
        return minutes + "m"
    }
    
    // Helper function to calculate end time
    function calculateEndTime(runtimeTicks) {
        if (!runtimeTicks || runtimeTicks === 0) return ""
        var now = new Date()
        var runtimeMs = runtimeTicks / 10000 // ticks to milliseconds
        var endTime = new Date(now.getTime() + runtimeMs)
        return "Ends at " + endTime.toLocaleTimeString(Qt.locale(), "h:mm AP")
    }
    
    // Helper function to format rating
    function formatRating(rating) {
        if (!rating || rating === 0) return ""
        return "★ " + rating.toFixed(1)
    }

    // Loading overlay (non-occluding) to avoid backdrop/poster flash when entering season
    Item {
        anchors.fill: parent
        visible: isLoading
        z: 100
        
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.35)
        }
        
        BusyIndicator {
            anchors.centerIn: parent
            running: isLoading
            width: 64
            height: 64
        }
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.topMargin: 48
            text: "Loading episodes..."
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
        }
    }
    
    // Backdrop / Poster background with fade
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        color: "transparent"
        z: 0
        clip: true
        
        Image {
            id: backdropImage
            anchors.fill: parent
            source: seriesPosterUrl || seasonPosterUrl
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
    
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 48
        anchors.rightMargin: 48
        anchors.topMargin: 24
        anchors.bottomMargin: 32
        spacing: Theme.spacingLarge
        visible: !isLoading
        
        // Left Content Area (75%)
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.75
            spacing: 4
            
            // Series Logo (if available)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: seriesLogoUrl ? Math.round(Theme.seriesLogoHeight * 0.5) : 0
                visible: seriesLogoUrl !== ""
                
                Image {
                    id: logoImage
                    anchors.left: parent.left
                    width: Math.min(Math.round(Theme.seriesLogoMaxWidth * 0.5), parent.width)
                    height: parent.height
                    source: seriesLogoUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 300 } }
                }
            }
            
            // Season Title and Metadata
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                
                // Series name (subtitle when logo not present)
                Text {
                    visible: seriesLogoUrl === "" && seriesName !== ""
                    text: seriesName
                    font.pixelSize: Theme.fontSizeDisplay
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                }
                
                // Season name
                Text {
                    text: seasonName || ("Season " + seasonNumber)
                    font.pixelSize: Theme.fontSizeHeader
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                }
                
                // Metadata row (episode count, etc.)
                RowLayout {
                    spacing: 16
                    
                    Text {
                        visible: episodeCount > 0
                        text: episodeCount + " Episodes"
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }
            }
            
            // Action Buttons Row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 12
                
                // Play Button
                Button {
                    id: playButton
                    enabled: episodeCount > 0
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: Theme.buttonHeightMedium
                    
                    contentItem: Row {
                        spacing: 8
                        anchors.centerIn: parent
                        Text {
                            text: Icons.playArrow
                            font.family: Theme.fontIcon
                            font.pixelSize: Theme.fontSizeIcon
                            color: Theme.textPrimary
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Play"
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            color: Theme.textPrimary
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    
                    KeyNavigation.right: bookmarkButton
                    KeyNavigation.down: episodesList
                    
                    Keys.onReturnPressed: if (enabled) clicked()
                    Keys.onEnterPressed: if (enabled) clicked()
                    
                    onClicked: {
                        // Play first unplayed episode, or first episode with resume position
                        for (var i = 0; i < episodeCount; i++) {
                            var ep = SeriesDetailsViewModel.episodesModel.getItem(i)
                            if (ep && !ep.isPlayed) {
                                // Get playback position for resume
                                var startPos = ep.UserData ? ep.UserData.PlaybackPositionTicks : 0
                                root.playEpisode(ep.Id, startPos)
                                return
                            }
                        }
                        // If all watched, play first episode from beginning
                        if (episodeCount > 0) {
                            var firstEp = SeriesDetailsViewModel.episodesModel.getItem(0)
                            if (firstEp) {
                                root.playEpisode(firstEp.Id, 0)
                            }
                        }
                    }
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (!parent.enabled) return Theme.buttonSecondaryBackground
                            if (parent.down) return Theme.buttonPrimaryBackgroundPressed
                            if (parent.hovered) return Theme.buttonPrimaryBackgroundHover
                            return Theme.buttonPrimaryBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: parent.activeFocus ? Theme.buttonPrimaryBorderFocused : Theme.buttonPrimaryBorder
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                }
                
                // Bookmark Button
                Button {
                    id: bookmarkButton
                    text: ""
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: playButton
                    KeyNavigation.right: markWatchedButton
                    KeyNavigation.down: episodesList
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonIconBackgroundPressed
                            if (parent.hovered) return Theme.buttonIconBackgroundHover
                            return Theme.buttonIconBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonIconBorderFocused
                            if (parent.hovered) return Theme.buttonIconBorderHover
                            return Theme.buttonIconBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Text {
                        text: Icons.bookmarkBorder
                        font.pixelSize: Theme.fontSizeIcon
                        font.family: Theme.fontIcon
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // Mark Watched Button
                Button {
                    id: markWatchedButton
                    text: ""
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: bookmarkButton
                    KeyNavigation.right: favoriteButton
                    KeyNavigation.down: episodesList
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonIconBackgroundPressed
                            if (parent.hovered) return Theme.buttonIconBackgroundHover
                            return Theme.buttonIconBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonIconBorderFocused
                            if (parent.hovered) return Theme.buttonIconBorderHover
                            return Theme.buttonIconBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Item {
                        Canvas {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.reset();
                                ctx.lineWidth = 3;
                                ctx.strokeStyle = Theme.textPrimary;
                                ctx.lineCap = "round";
                                ctx.lineJoin = "round";
                                ctx.beginPath();
                                ctx.moveTo(5, 14);
                                ctx.lineTo(11, 20);
                                ctx.lineTo(23, 8);
                                ctx.stroke();
                            }
                        }
                    }
                }
                
                // Favorite Button
                Button {
                    id: favoriteButton
                    text: ""
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: markWatchedButton
                    KeyNavigation.right: moreButton
                    KeyNavigation.down: episodesList
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonIconBackgroundPressed
                            if (parent.hovered) return Theme.buttonIconBackgroundHover
                            return Theme.buttonIconBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonIconBorderFocused
                            if (parent.hovered) return Theme.buttonIconBorderHover
                            return Theme.buttonIconBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Text {
                        text: Icons.favoriteBorder
                        font.pixelSize: Theme.fontSizeIcon
                        font.family: Theme.fontIcon
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // More Options Button
                Button {
                    id: moreButton
                    text: Icons.moreVert
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.fontSizeIcon
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: favoriteButton
                    KeyNavigation.down: episodesList
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonIconBackgroundPressed
                            if (parent.hovered) return Theme.buttonIconBackgroundHover
                            return Theme.buttonIconBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonIconBorderFocused
                            if (parent.hovered) return Theme.buttonIconBorderHover
                            return Theme.buttonIconBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 24
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            
            // Episodes List
            ListView {
                id: episodesList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Math.round(400 * Theme.dpiScale)
                Layout.topMargin: 8
                clip: true
                spacing: 12
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 150
                keyNavigationEnabled: true
                keyNavigationWraps: false
                currentIndex: 0
                
                KeyNavigation.up: playButton
                
                // Handle up arrow at top of list to go back to buttons
                Keys.onUpPressed: (event) => {
                    if (currentIndex === 0) {
                        playButton.forceActiveFocus()
                        event.accepted = true
                    } else {
                        currentIndex--
                        event.accepted = true
                    }
                }
                
                Keys.onDownPressed: (event) => {
                    if (currentIndex < count - 1) {
                        currentIndex++
                        event.accepted = true
                    }
                }
                
                model: SeriesDetailsViewModel.episodesModel
                
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
                
                delegate: Item {
                    id: episodeDelegate
                    
                    // Required properties from model roles
                    required property int index
                    required property string name
                    required property string imageUrl
                    required property string itemId
                    required property int indexNumber
                    required property int parentIndexNumber
                    required property string overview
                    required property var runtimeTicks
                    required property bool isPlayed
                    required property var playbackPositionTicks
                    required property var communityRating
                    required property string premiereDate
                    required property bool isSpecial
                    required property var modelData
                    
                    width: episodesList.width
                    height: Math.max(Theme.episodeCardHeight, Theme.episodeCardMinHeight)
                    
                    property bool isFocused: episodesList.currentIndex === index && episodesList.activeFocus
                    property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
                    
                    // Episode card with glassmorphic styling
                    Rectangle {
                        id: episodeCard
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: Theme.radiusMedium
                        color: episodeDelegate.isFocused ? Theme.cardBackgroundFocused : (episodeDelegate.isHovered ? Theme.cardBackgroundHover : Theme.cardBackground)
                        border.width: episodeDelegate.isFocused ? 2 : 1
                        border.color: episodeDelegate.isFocused ? Theme.cardBorderFocused : (episodeDelegate.isHovered ? Theme.cardBorderHover : Theme.cardBorder)
                        
                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 16
                            
                            // Episode Thumbnail
                            Rectangle {
                                id: thumbContainer
                                Layout.preferredWidth: Theme.episodeThumbWidth
                                Layout.fillHeight: true
                                radius: Theme.imageRadius
                                clip: false
                                color: "transparent"
                                
                                Image {
                                    id: thumbImage
                                    anchors.fill: parent
                                    source: episodeDelegate.imageUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    visible: false
                                }

                                Rectangle {
                                    id: thumbMask
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    color: "white"
                                    visible: false
                                    layer.enabled: true
                                }

                                OpacityMask {
                                    anchors.fill: parent
                                    source: thumbImage
                                    maskSource: thumbMask
                                }
                                
                                // Placeholder
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    color: Qt.rgba(0.2, 0.2, 0.2, 0.5)
                                    visible: thumbImage.status !== Image.Ready
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: episodeDelegate.isSpecial ? ("S" + episodeDelegate.indexNumber) : ("E" + episodeDelegate.indexNumber)
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeTitle
                                        font.family: Theme.fontPrimary
                                    }
                                }
                                
                                // Progress bar overlay (if partially watched)
                                Rectangle {
                                    visible: episodeDelegate.playbackPositionTicks > 0 && !episodeDelegate.isPlayed
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 4
                                    height: 4
                                    radius: 2
                                    color: Qt.rgba(0, 0, 0, 0.6)
                                    
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: parent.width * Math.min(1, episodeDelegate.playbackPositionTicks / episodeDelegate.runtimeTicks)
                                        radius: 2
                                        color: Theme.accentPrimary
                                    }
                                }
                                
                                // Watched indicator
                                Rectangle {
                                    visible: episodeDelegate.isPlayed
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    width: 24
                                    height: 24
                                    radius: 12
                                    color: Theme.accentSecondary
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: Icons.check
                                        font.family: Theme.fontIcon
                                        font.pixelSize: 16
                                        color: Theme.textPrimary
                                    }
                                }
                            }
                            
                            // Episode Info
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 4
                                
                                // Title row with episode number/name
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    
                                    // Special episode badge
                                    Rectangle {
                                        visible: episodeDelegate.isSpecial
                                        Layout.preferredWidth: specialLabel.implicitWidth + 12
                                        Layout.preferredHeight: 20
                                        radius: 4
                                        color: Theme.accentSecondary
                                        
                                        Text {
                                            id: specialLabel
                                            anchors.centerIn: parent
                                            text: "SPECIAL"
                                            font.pixelSize: 10
                                            font.family: Theme.fontPrimary
                                            font.bold: true
                                            color: Theme.textPrimary
                                        }
                                    }
                                    
                                    Text {
                                        text: {
                                            // For specials, show "S00E{num}. Name"
                                            // For regular episodes, show "{num}. Name"
                                            var prefix = ""
                                            if (episodeDelegate.isSpecial) {
                                                prefix = "S00E" + episodeDelegate.indexNumber
                                            } else {
                                                prefix = episodeDelegate.indexNumber.toString()
                                            }
                                            return prefix + ". " + episodeDelegate.name
                                        }
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                        font.bold: true
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                
                                // Metadata row (runtime, rating, end time)
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    
                                    Text {
                                        visible: episodeDelegate.runtimeTicks > 0
                                        text: root.formatRuntime(episodeDelegate.runtimeTicks)
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.fontPrimary
                                        color: Theme.textSecondary
                                    }
                                    
                                    Text {
                                        visible: episodeDelegate.communityRating > 0
                                        text: root.formatRating(episodeDelegate.communityRating)
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.fontPrimary
                                        color: "#FFD700" // Gold color for star
                                    }
                                    
                                    Text {
                                        visible: episodeDelegate.runtimeTicks > 0
                                        text: root.calculateEndTime(episodeDelegate.runtimeTicks)
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.fontPrimary
                                        color: Theme.textSecondary
                                    }
                                }
                                
                                // Overview/description
                                Text {
                                    text: episodeDelegate.overview || ""
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    opacity: 0.9
                                }
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                episodesList.currentIndex = episodeDelegate.index
                                episodesList.forceActiveFocus()
                                root.episodeSelected(episodeDelegate.modelData)
                            }
                            onDoubleClicked: {
                                root.playEpisode(episodeDelegate.itemId, episodeDelegate.playbackPositionTicks)
                            }
                        }
                    }
                }
                
                Keys.onReturnPressed: {
                    if (currentIndex >= 0) {
                        var item = SeriesDetailsViewModel.episodesModel.getItem(currentIndex)
                        if (item) {
                            root.episodeSelected(item)
                        }
                    }
                }
                
                Keys.onEnterPressed: {
                    if (currentIndex >= 0) {
                        var item = SeriesDetailsViewModel.episodesModel.getItem(currentIndex)
                        if (item) {
                            root.episodeSelected(item)
                        }
                    }
                }
            }
        }
        
        // Right Sidebar (25%) - Season Poster
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.2
            spacing: Theme.spacingMedium
            
            // Season Poster - Frosted glass container
            Rectangle {
                id: posterCard
                Layout.fillWidth: true
                Layout.preferredHeight: width * 1.5
                radius: Theme.radiusMedium
                color: Theme.cardBackground
                border.width: 1
                border.color: Theme.cardBorder
                clip: true
                
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 10
                    shadowBlur: 0.5
                    shadowColor: Theme.overlayDark
                }
                
                // Poster image container with rounded corners
                Rectangle {
                    id: posterImageContainer
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: Theme.imageRadius
                    clip: true
                    color: "transparent"
                    
                    Image {
                        id: posterImage
                        anchors.fill: parent
                        source: displayPosterUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                    
                    // Placeholder
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.imageRadius
                        color: Qt.rgba(0.15, 0.15, 0.15, 0.8)
                        visible: posterImage.status !== Image.Ready
                        
                        Text {
                            anchors.centerIn: parent
                            text: seasonName || "Season " + seasonNumber
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width - 20
                        }
                    }
                }
            }
            
            Item {
                Layout.fillHeight: true
            }
        }
    }
    
    Component.onCompleted: {
        // Set initial focus - if we have a pending episode index, restore it
        Qt.callLater(function() {
            if (pendingEpisodeIndex >= 0) {
                console.log("[SeasonView] Component completed with pending index:", pendingEpisodeIndex, "episodeCount:", episodesList.count)
                // If episodes are already loaded, restore immediately
                if (episodesList.count > 0) {
                    var targetIndex = Math.min(pendingEpisodeIndex, episodesList.count - 1)
                    console.log("[SeasonView] Restoring episode index immediately:", targetIndex)
                    episodesList.currentIndex = targetIndex
                    episodesList.positionViewAtIndex(targetIndex, ListView.Center)
                    episodesList.forceActiveFocus()
                    root.pendingEpisodeIndex = -1
                } else {
                    // Episodes not loaded yet, will be restored via onModelReset
                    episodesList.forceActiveFocus()
                    console.log("[SeasonView] Waiting for episodes to load before restoring index")
                }
            } else {
                playButton.forceActiveFocus()
                console.log("[SeasonView] Initial focus set to playButton, activeFocus:", playButton.activeFocus)
            }
        })
    }
    
    // Watch for episodes model reset to restore pending index
    Connections {
        target: SeriesDetailsViewModel.episodesModel
        function onModelReset() {
            if (pendingEpisodeIndex >= 0 && episodesList.count > 0) {
                var targetIndex = Math.min(pendingEpisodeIndex, episodesList.count - 1)
                console.log("[SeasonView] Restoring episode index:", targetIndex)
                episodesList.currentIndex = targetIndex
                episodesList.positionViewAtIndex(targetIndex, ListView.Center)
                episodesList.forceActiveFocus()
                // Clear the pending index
                root.pendingEpisodeIndex = -1
            }
        }
    }
}
