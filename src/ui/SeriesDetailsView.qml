import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Qt5Compat.GraphicalEffects
import BloomUI

FocusScope {
    id: root
    
    // Input property - the series to display
    property string seriesId: ""
    property int pendingSeasonsGridIndex: -1  // Index to restore when returning from season view
    
    // Watch for pendingSeasonsGridIndex changes - this handles the case where the property
    // is set after Component.onCompleted runs
    onPendingSeasonsGridIndexChanged: {
        console.log("[SeriesDetailsView] pendingSeasonsGridIndex changed to:", pendingSeasonsGridIndex)
        if (pendingSeasonsGridIndex >= 0 && seasonsGrid.count > 0) {
            Qt.callLater(function() {
                if (root.pendingSeasonsGridIndex >= 0) {
                    var targetIndex = Math.min(root.pendingSeasonsGridIndex, seasonsGrid.count - 1)
                    console.log("[SeriesDetailsView] Restoring seasons grid index from property change:", targetIndex)
                    seasonsGrid.currentIndex = targetIndex
                    seasonsGrid.positionViewAtIndex(targetIndex, GridView.Center)
                    seasonsGrid.forceActiveFocus()
                    root.pendingSeasonsGridIndex = -1
                }
            })
        }
    }
    
    // Signals for navigation and actions
    signal navigateToSeasons(int seasonIndex)
    signal playNextEpisode(string episodeId, var startPositionTicks)
    signal backRequested()
    
    // Key handling for back navigation
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            console.log("[SeriesDetailsView] Back key pressed")
            root.backRequested()
            event.accepted = true
        }
    }
    
    // Ensure root can receive key events
    focus: true
    
    // Focus when this view receives active focus
    onActiveFocusChanged: {
        if (activeFocus) {
            // Delegate focus to appropriate element
            Qt.callLater(function() {
                if (playButton && playButton.enabled) {
                    playButton.forceActiveFocus()
                } else if (seasonsGrid) {
                    seasonsGrid.forceActiveFocus()
                }
            })
        }
    }
    
    // Load series details when seriesId changes
    onSeriesIdChanged: {
        if (seriesId !== "") {
            console.log("[SeriesDetailsView] Loading series details for:", seriesId)
            SeriesDetailsViewModel.loadSeriesDetails(seriesId)
        }
    }
    
    // Bind to ViewModel properties for convenience
    readonly property string seriesName: SeriesDetailsViewModel.title
    readonly property string seriesOverview: SeriesDetailsViewModel.overview
    readonly property bool hasNextEpisode: SeriesDetailsViewModel.hasNextEpisode
    readonly property bool isWatched: SeriesDetailsViewModel.isWatched
    readonly property string logoUrl: SeriesDetailsViewModel.logoUrl
    readonly property string posterUrl: SeriesDetailsViewModel.posterUrl
    readonly property string backdropUrl: SeriesDetailsViewModel.backdropUrl
    readonly property bool isLoading: SeriesDetailsViewModel.isLoading

    // Backdrop with fade to avoid abrupt changes between series/season
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        color: "transparent"
        z: -1
        clip: true

        Image {
            id: backdropImage
            anchors.fill: parent
            source: backdropUrl !== "" ? backdropUrl : posterUrl
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

    // Loading overlay (non-occluding to avoid backdrop flash)
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
            text: "Loading series details..."
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
        }
    }
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: 48
        spacing: Theme.spacingLarge
        // keep visible to preserve previous artwork while loading new data
        
        // Left Content Area (70%) - wrapped in Flickable for scrolling on large displays
        Flickable {
            id: leftContentFlickable
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.7
            contentWidth: width
            contentHeight: leftContentColumn.height + bottomMargin
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            
            // Bottom margin to ensure season titles at the bottom are fully visible (DPI-scaled)
            readonly property int bottomMargin: Math.round(150 * Theme.dpiScale)
            
            // Prevent scrolling above the top (negative contentY)
            onContentYChanged: {
                if (contentY < 0) {
                    contentY = 0
                }
            }
            
            // Function to ensure the seasons grid is visible when it has focus
            function ensureSeasonsVisible() {
                // Use mapToItem for reliable coordinate calculation
                var seasonsPos = seasonsHeaderText.mapToItem(leftContentColumn, 0, 0)
                var seasonsY = seasonsPos.y
                var viewportBottom = contentY + height
                
                console.log("ensureSeasonsVisible: seasonsY=" + seasonsY + " viewportBottom=" + viewportBottom + 
                            " height=" + height + " contentHeight=" + contentHeight)
                
                // If seasons header is below or near bottom of viewport, scroll down to show it
                // We want at least 200px of the seasons visible
                if (seasonsY > contentY + height - 200) {
                    // Scroll so seasons header is about 100px from top of viewport
                    var targetY = Math.max(0, seasonsY - 100)
                    contentY = Math.min(targetY, contentHeight - height)
                    console.log("Scrolled to contentY=" + contentY)
                }
            }
            
            // Function to scroll to seasons and give them focus
            function scrollToSeasonsAndFocus() {
                // Use mapToItem for reliable coordinate calculation
                var seasonsPos = seasonsHeaderText.mapToItem(leftContentColumn, 0, 0)
                var viewportTop = contentY
                var viewportBottom = viewportTop + height
                var maxScroll = Math.max(0, contentHeight - height)
                // If everything fits (or seasons header is already visible), skip scrolling to avoid flicker
                if (contentHeight <= height ||
                    (seasonsPos.y >= viewportTop && seasonsPos.y <= viewportBottom - 80)) {
                    console.log("scrollToSeasonsAndFocus: skip scroll, seasons already visible or content fits")
                    seasonsGrid.forceActiveFocus()
                    return
                }

                // Scroll to show the seasons header near the top
                var targetY = Math.max(0, seasonsPos.y - 50)
                var clampedTargetY = Math.max(0, Math.min(targetY, maxScroll))
                contentY = clampedTargetY
                console.log("scrollToSeasonsAndFocus: targetY=" + targetY + " clampedContentY=" + contentY + " maxScroll=" + maxScroll)
                // Give focus to the seasons grid
                seasonsGrid.forceActiveFocus()
            }
            
            // Function to ensure seasons are partially visible on initial load
            // This scrolls to show at least the first row of seasons
            function ensureInitialSeasonsVisibility() {
                // Use mapToItem for reliable coordinate calculation
                var seasonsPos = seasonsHeaderText.mapToItem(leftContentColumn, 0, 0)
                var seasonsY = seasonsPos.y
                
                console.log("ensureInitialSeasonsVisibility: seasonsY=" + seasonsY + " height=" + height + 
                            " contentHeight=" + contentHeight)
                
                // Only scroll if seasons are completely out of view
                if (contentHeight > height && seasonsY > height - 50) {
                    // Calculate ideal position: show top content but also show seasons header
                    // Show enough of the top (logo + buttons) but make sure seasons are visible
                    var idealY = Math.max(0, seasonsY - height + 300)  // Show ~300px of seasons area
                    var maxScroll = Math.max(0, contentHeight - height)
                    contentY = Math.min(idealY, maxScroll)
                    console.log("Initial scroll to contentY=" + contentY + " maxScroll=" + maxScroll)
                }
            }
            
            // Function to scroll back to top (for buttons/logo)
            function ensureTopVisible() {
                if (contentY > 0) {
                    contentY = 0
                }
            }
            
            Behavior on contentY {
                NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
            }
            
            ColumnLayout {
                id: leftContentColumn
                width: leftContentFlickable.width
                spacing: Theme.spacingMedium
            
                // Series Logo
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: logoUrl ? Theme.seriesLogoHeight : 0
                    visible: logoUrl !== ""
                    
                    Image {
                        id: seriesLogo
                        anchors.left: parent.left
                        width: Math.min(Theme.seriesLogoMaxWidth, parent.width)
                        height: parent.height
                        source: logoUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                }
                
                // Fallback: Series Name if no logo
                Text {
                    visible: logoUrl === ""
                    text: seriesName
                    font.pixelSize: Theme.fontSizeDisplay
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                }
            
            // Action Buttons Row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.fontSizeSmall
                
                // Play Button
                Button {
                    id: playButton
                    text: hasNextEpisode ? "▶ Play" : "No Episodes Available"
                    enabled: hasNextEpisode
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: Theme.buttonHeightMedium
                    
                    KeyNavigation.right: markWatchedButton
                    // Don't use KeyNavigation.down - handle manually to ensure scrolling works
                    
                    // Scroll to top when button receives focus (e.g., navigating up from seasons)
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            leftContentFlickable.ensureTopVisible()
                        }
                    }
                    
                    Keys.onDownPressed: (event) => {
                        // Scroll to seasons and transfer focus
                        leftContentFlickable.scrollToSeasonsAndFocus()
                        event.accepted = true
                    }
                    
                    Keys.onReturnPressed: if (enabled) clicked()
                    Keys.onEnterPressed: if (enabled) clicked()
                    
                    onClicked: {
                        if (hasNextEpisode) {
                            root.playNextEpisode(SeriesDetailsViewModel.nextEpisodeId, SeriesDetailsViewModel.nextEpisodePlaybackPositionTicks)
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
                    
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? Theme.textPrimary : Theme.textDisabled
                        font.pixelSize: Theme.fontSizeMedium
                        font.family: Theme.fontPrimary
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // Mark as Watched Button
                Button {
                    id: markWatchedButton
                    text: ""
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: playButton
                    KeyNavigation.right: contextMenuButton
                    // Don't use KeyNavigation.down - handle manually to ensure scrolling works
                    
                    Keys.onDownPressed: (event) => {
                        leftContentFlickable.scrollToSeasonsAndFocus()
                        event.accepted = true
                    }
                    
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    
                    onClicked: {
                        if (isWatched) {
                            SeriesDetailsViewModel.markAsUnwatched()
                        } else {
                            SeriesDetailsViewModel.markAsWatched()
                        }
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: isWatched ? "Mark as Unwatched" : "Mark as Watched"
                    ToolTip.delay: 500
                    
                    background: Rectangle {
                        radius: Theme.radiusLarge
                        color: {
                            if (parent.down) return Theme.buttonIconBackgroundPressed
                            if (parent.hovered) return Theme.buttonIconBackgroundHover
                            if (isWatched) return Qt.rgba(0.67, 0.36, 0.76, 0.3)  // Subtle purple for watched
                            return Theme.buttonIconBackground
                        }
                        border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                        border.color: {
                            if (parent.activeFocus) return Theme.buttonIconBorderFocused
                            if (parent.hovered) return Theme.buttonIconBorderHover
                            if (isWatched) return Theme.accentSecondary
                            return Theme.buttonIconBorder
                        }
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: Item {
                        // Custom Icon
                        width: 32
                        height: 32
                        
                        Canvas {
                            id: checkmarkCanvas
                            anchors.centerIn: parent
                            width: 32
                            height: 32
                            property color strokeColor: isWatched ? Theme.accentSecondary : Theme.textPrimary
                            
                            onStrokeColorChanged: requestPaint()
                            
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.reset();
                                ctx.lineWidth = 4;
                                ctx.strokeStyle = strokeColor;
                                ctx.lineCap = "round";
                                ctx.lineJoin = "round";
                                ctx.beginPath();
                                ctx.moveTo(6, 16);
                                ctx.lineTo(13, 23);
                                ctx.lineTo(26, 9);
                                ctx.stroke();
                            }
                        }
                    }
                }
                
                // Context Menu Button (Vertical Ellipsis)
                Button {
                    id: contextMenuButton
                    text: Icons.moreVert
                    Layout.preferredWidth: Theme.buttonIconSize
                    Layout.preferredHeight: Theme.buttonIconSize
                    
                    KeyNavigation.left: markWatchedButton
                    // Don't use KeyNavigation.down - handle manually to ensure scrolling works
                    
                    Keys.onDownPressed: (event) => {
                        leftContentFlickable.scrollToSeasonsAndFocus()
                        event.accepted = true
                    }
                    
                    Keys.onReturnPressed: contextMenu.open()
                    Keys.onEnterPressed: contextMenu.open()
                    
                    onClicked: contextMenu.open()
                    
                    ToolTip.visible: hovered
                    ToolTip.text: "More options"
                    ToolTip.delay: 500
                    
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
                        font.family: Theme.fontIcon
                        font.pixelSize: 24
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    // Context Menu
                    Menu {
                        id: contextMenu
                        y: parent.height
                        
                        background: Rectangle {
                            implicitWidth: 280
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
                        
                        // MPV Profile Submenu
                        Menu {
                            id: profileSubmenu
                            title: "MPV Profile"
                            
                            // Track current profile - updates when ConfigManager emits signal
                            property string currentProfile: ConfigManager.getSeriesProfile(root.seriesId)
                            
                            Connections {
                                target: ConfigManager
                                function onSeriesProfilesChanged() {
                                    profileSubmenu.currentProfile = ConfigManager.getSeriesProfile(root.seriesId)
                                }
                            }
                            
                            // Also update when menu opens
                            onAboutToShow: {
                                currentProfile = ConfigManager.getSeriesProfile(root.seriesId)
                            }
                            
                            background: Rectangle {
                                implicitWidth: 220
                                color: Theme.cardBackground
                                radius: Theme.radiusMedium
                                border.color: Theme.cardBorder
                                border.width: 1
                            }
                            
                            // Current profile indicator
                            MenuItem {
                                text: {
                                    if (profileSubmenu.currentProfile === "") {
                                        return "Current: Use Default"
                                    }
                                    return "Current: " + profileSubmenu.currentProfile
                                }
                                enabled: false
                                
                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    font.italic: true
                                }
                                
                                background: Rectangle {
                                    color: "transparent"
                                }
                            }
                            
                            MenuSeparator {
                                contentItem: Rectangle {
                                    implicitHeight: 1
                                    color: Theme.borderLight
                                }
                            }
                            
                            // Use Default option
                            MenuItem {
                                id: defaultProfileItem
                                text: "Use Default"
                                
                                contentItem: RowLayout {
                                    spacing: Theme.spacingSmall
                                    
                                    Text {
                                        text: profileSubmenu.currentProfile === "" ? "✓" : "  "
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.fontPrimary
                                        color: Theme.accentPrimary
                                        Layout.preferredWidth: 20
                                    }
                                    
                                    Text {
                                        text: "Use Default"
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
                                    ConfigManager.setSeriesProfile(root.seriesId, "")
                                    profileSubmenu.close()
                                    contextMenu.close()
                                }
                            }
                            
                            MenuSeparator {
                                contentItem: Rectangle {
                                    implicitHeight: 1
                                    color: Theme.borderLight
                                }
                            }
                            
                            // Profile options
                            Repeater {
                                model: ConfigManager.mpvProfileNames
                                
                                MenuItem {
                                    required property string modelData
                                    
                                    text: modelData
                                    
                                    contentItem: RowLayout {
                                        spacing: Theme.spacingSmall
                                        
                                        Text {
                                            text: profileSubmenu.currentProfile === modelData ? "✓" : "  "
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.family: Theme.fontPrimary
                                            color: Theme.accentPrimary
                                            Layout.preferredWidth: 20
                                        }
                                        
                                        Text {
                                            text: modelData
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
                                        ConfigManager.setSeriesProfile(root.seriesId, modelData)
                                        profileSubmenu.close()
                                        contextMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Metadata Row (Year, Rating, External Ratings, Counts)
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                
                // Years
                Text {
                    id: yearText
                    text: {
                       var startYear = SeriesDetailsViewModel.productionYear
                       var end = SeriesDetailsViewModel.endDate
                       // Format year from date if valid
                       var endYear = !isNaN(end.getTime()) ? end.getFullYear() : 0
                       
                       if (startYear > 0) {
                           if (SeriesDetailsViewModel.status === "Ended" && endYear > 0) {
                               return startYear + " - " + endYear
                           }
                           return startYear + " -"
                       }
                       return ""
                    }
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }

                // Content Rating Badge (e.g. TV-PG)
                Rectangle {
                    visible: SeriesDetailsViewModel.officialRating !== ""
                    color: "transparent"
                    border.color: Theme.textSecondary
                    border.width: 1
                    width: ratingText.implicitWidth + 12
                    height: ratingText.implicitHeight + 4
                    radius: 2
                    
                    Text {
                        id: ratingText
                        anchors.centerIn: parent
                        text: SeriesDetailsViewModel.officialRating
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                    }
                }
                
                // MDBList Ratings
                Repeater {
                    model: {
                        var ratings = SeriesDetailsViewModel.mdbListRatings["ratings"] || []
                        var list = []
                        for (var i = 0; i < ratings.length; i++) {
                            var r = ratings[i]
                            var val = r.value
                            var sc = r.score
                            
                            // Skip if no score or if score/value is effectively "0"
                            if (val === undefined || val === null || val === "" || val == 0 || val === "0") continue
                            if (sc === undefined && val === undefined) continue
                            if (sc == 0 || sc === "0") continue
                            
                            list.push(r)
                        }
                        return list
                    }
                    
                    delegate: RowLayout {
                        spacing: 4
                        property var rating: modelData
                        property string originalSource: rating.source || ""
                        property var score: rating.score || rating.value
                        
                        // Normalize source for matching
                        readonly property string normalizedSource: {
                            var s = originalSource.toLowerCase().replace(/\s+/g, '_')
                            if (s.indexOf("tomatoes") !== -1) return s.indexOf("audience") !== -1 ? "audience" : "tomatoes" 
                            if (s.indexOf("imdb") !== -1) return "imdb"
                            if (s.indexOf("metacritic") !== -1) return "metacritic"
                            if (s.indexOf("tmdb") !== -1) return "tmdb"
                            if (s.indexOf("trakt") !== -1) return "trakt"
                            if (s.indexOf("letterboxd") !== -1) return "letterboxd"
                            if (s.indexOf("myanimelist") !== -1 || s === "mal") return "mal"
                            if (s.indexOf("anilist") !== -1) return "anilist"
                            return s
                        }
                        
                        // Brand color mapping
                        readonly property color brandColor: {
                            var s = normalizedSource
                            if (s === "imdb") return "#F5C518"
                            if (s === "tomatoes") return "#FA320A" // Rotten Tomatoes
                            if (s === "audience") return "#FA320A" // Popcorn
                            if (s === "metacritic") return "#66CC33"
                            if (s === "tmdb") return "#01B4E4"
                            if (s === "trakt") return "#ED1C24"
                            if (s === "letterboxd") return "#00E054"
                            if (s === "mal") return "#2E51A2" // MyAnimeList
                            if (s === "anilist") return "#02A9FF"
                            return Theme.accentPrimary
                        }
                        
                        // Source Label/Icon placeholder
                        Text {
                            text: {
                                var s = normalizedSource
                                if (s === "imdb") return "IMDb"
                                if (s === "tomatoes") return "RT"
                                if (s === "audience") return "Popcorn"
                                if (s === "metacritic") return "Meta"
                                if (s === "tmdb") return "TMDb"
                                if (s === "mal") return "MAL"
                                if (s === "anilist") return "AniList"
                                // Capitalize first letter of fallback
                                if (originalSource.length > 0) return originalSource
                                return s
                            }
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            font.bold: true
                            color: brandColor
                        }
                        
                        // Score
                        Text {
                            text: score
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }
                    }
                }
                
                // Season/Episode Counts
                Text {
                    text: {
                        var seasons = SeriesDetailsViewModel.seasonCount
                        var episodes = SeriesDetailsViewModel.recursiveItemCount
                        var parts = []
                        if (seasons > 0) parts.push(seasons + (seasons === 1 ? " Season" : " Seasons"))
                        if (episodes > 0) parts.push(episodes + (episodes === 1 ? " Episode" : " Episodes"))
                        return parts.join("  ")
                    }
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    visible: text !== ""
                }
            }
            Item {
                id: overviewContainer
                Layout.fillWidth: true
                Layout.preferredHeight: expanded ? Math.min(overviewText.implicitHeight + 8, 400) : Theme.seriesOverviewMaxHeight
                
                // Track if expanded (via mouse click on "Read More")
                property bool expanded: false
                
                // Check if text overflows
                property bool hasOverflow: overviewText.implicitHeight > (Theme.seriesOverviewMaxHeight - 8)
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                }
                
                // Text content (clipped when not expanded)
                Item {
                    id: textClipArea
                    anchors.fill: parent
                    clip: true
                    
                    Text {
                        id: overviewText
                        width: parent.width
                        text: seriesOverview
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.bold: true
                        color: Theme.textPrimary
                        style: Text.Outline
                        styleColor: "#000000"
                        wrapMode: Text.WordWrap
                    }
                    
                    // Gradient fade at bottom when not expanded and has overflow
                    Rectangle {
                        visible: !overviewContainer.expanded && overviewContainer.hasOverflow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 50
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.9) }
                        }
                    }
                }
                
                // "Read More" / "Show Less" button - MOUSE ONLY, not keyboard focusable
                Rectangle {
                    id: readMoreButton
                    visible: overviewContainer.hasOverflow
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 4
                    anchors.bottomMargin: 4
                    width: readMoreText.width + 16
                    height: readMoreText.height + 8
                    radius: Theme.radiusSmall
                    color: readMoreMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(0, 0, 0, 0.6)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.3)
                    
                    // Explicitly NOT focusable - this is mouse-only by design
                    activeFocusOnTab: false
                    
                    Behavior on color { ColorAnimation { duration: 150 } }
                    
                    Text {
                        id: readMoreText
                        anchors.centerIn: parent
                        text: overviewContainer.expanded ? "Show Less" : "Read More"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                    }
                    
                    MouseArea {
                        id: readMoreMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            overviewContainer.expanded = !overviewContainer.expanded
                        }
                    }
                }
            }
            
            // Seasons Section Header
            Text {
                id: seasonsHeaderText
                text: "Seasons"
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
                Layout.topMargin: 16
            }

            // Skeleton placeholders while seasons load
            ColumnLayout {
                visible: isLoading && seasonsGrid.count === 0
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: 3
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.seasonPosterHeight * 0.4
                        radius: Theme.radiusMedium
                        color: Qt.rgba(1,1,1,0.06)
                    }
                }
            }
            
            // Seasons Grid
            GridView {
                id: seasonsGrid
                Layout.fillWidth: true
                // Calculate height to show all seasons (no internal scrolling, parent Flickable handles scrolling)
                Layout.preferredHeight: {
                    if (count === 0) return Theme.seasonPosterHeight
                    var columns = Math.max(1, Math.floor(width / cellWidth))
                    var rows = Math.ceil(count / columns)
                    // Extra padding: account for scale animation (1.05x) and ensure titles are visible (DPI-scaled)
                    return rows * cellHeight + Math.round(140 * Theme.dpiScale)
                }
                cellWidth: Theme.seasonPosterWidth
                cellHeight: Theme.seasonPosterHeight
                clip: true
                topMargin: 24
                bottomMargin: 24
                leftMargin: 16
                rightMargin: 16
                focus: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: false  // Disable internal scrolling, let parent Flickable handle it
                
                KeyNavigation.up: playButton
                
                // Custom down key handling - navigate within grid or to next up when at bottom
                Keys.onDownPressed: (event) => {
                    var columns = Math.max(1, Math.floor(width / cellWidth))
                    var currentRow = Math.floor(currentIndex / columns)
                    var totalRows = Math.ceil(count / columns)
                    var nextRowIndex = currentIndex + columns
                    
                    if (nextRowIndex < count) {
                        // Can move to next row
                        currentIndex = nextRowIndex
                        event.accepted = true
                    } else if (currentRow < totalRows - 1) {
                        // On partial last row, move to last item
                        currentIndex = count - 1
                        event.accepted = true
                    } else {
                        // At bottom row - navigate to next up if available
                        if (hasNextEpisode && nextUpContainer) {
                            nextUpContainer.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                }
                
                // Custom up key handling - navigate within grid or to play button
                Keys.onUpPressed: (event) => {
                    var columns = Math.max(1, Math.floor(width / cellWidth))
                    var prevRowIndex = currentIndex - columns
                    
                    if (prevRowIndex >= 0) {
                        // Can move to previous row
                        currentIndex = prevRowIndex
                        event.accepted = true
                    } else {
                        // At top row - navigate to play button
                        playButton.forceActiveFocus()
                        event.accepted = true
                    }
                }
                
                // Custom right key handling to allow internal grid navigation
                // Only navigate to nextUpContainer when at the rightmost column
                Keys.onRightPressed: (event) => {
                    var columns = Math.max(1, Math.floor(width / cellWidth))
                    var currentColumn = currentIndex % columns
                    var isLastColumn = currentColumn === columns - 1
                    var isLastItem = currentIndex >= count - 1
                    
                    if (isLastColumn || isLastItem) {
                        // At rightmost column or last item - navigate to next up if available
                        if (hasNextEpisode && nextUpContainer) {
                            nextUpContainer.forceActiveFocus()
                            event.accepted = true
                        }
                    } else {
                        // Not at rightmost column - move to next item in grid
                        currentIndex = Math.min(currentIndex + 1, count - 1)
                        event.accepted = true
                    }
                }
                
                // Custom left key handling to navigate within grid
                Keys.onLeftPressed: (event) => {
                    var columns = Math.max(1, Math.floor(width / cellWidth))
                    var currentColumn = currentIndex % columns
                    
                    if (currentColumn === 0) {
                        // At leftmost column - don't navigate, stay on current item
                        event.accepted = true
                    } else {
                        // Move to previous item in grid
                        currentIndex = Math.max(currentIndex - 1, 0)
                        event.accepted = true
                    }
                }
                
                model: SeriesDetailsViewModel.seasonsModel
                
                // Scroll the parent Flickable when focus enters the grid
                onActiveFocusChanged: {
                    if (activeFocus) {
                        leftContentFlickable.ensureSeasonsVisible()
                    }
                }
                
                // Ensure focused item is visible when navigating
                onCurrentIndexChanged: {
                    if (activeFocus && currentIndex >= 0) {
                        // Calculate the position of the current item relative to the Flickable's content
                        var columns = Math.max(1, Math.floor(width / cellWidth))
                        var row = Math.floor(currentIndex / columns)
                        
                        // Use mapToItem for reliable coordinate calculation
                        var gridPosInContent = seasonsGrid.mapToItem(leftContentColumn, 0, 0)
                        var itemY = gridPosInContent.y + (row * cellHeight)
                        var itemBottom = itemY + cellHeight
                        
                        // Ensure the item is visible in the parent Flickable
                        var viewportTop = leftContentFlickable.contentY
                        var viewportBottom = viewportTop + leftContentFlickable.height
                        
                        console.log("Season scroll: itemY=" + itemY + " itemBottom=" + itemBottom + 
                                    " viewportTop=" + viewportTop + " viewportBottom=" + viewportBottom +
                                    " contentHeight=" + leftContentFlickable.contentHeight)
                        
                        if (itemBottom > viewportBottom - 40) {
                            // Scroll down to show the item with padding
                            var newContentY = Math.min(
                                itemBottom - leftContentFlickable.height + 80,
                                leftContentFlickable.contentHeight - leftContentFlickable.height
                            )
                            leftContentFlickable.contentY = Math.max(0, newContentY)
                            console.log("Scrolling down to contentY=" + newContentY)
                        } else if (itemY < viewportTop + 40) {
                            // Scroll up to show the item
                            var newContentY = Math.max(itemY - 80, 0)
                            leftContentFlickable.contentY = newContentY
                            console.log("Scrolling up to contentY=" + newContentY)
                        }

                        // Prefetch adjacent seasons to reduce episode load latency
                        SeriesDetailsViewModel.prefetchSeasonsAround(currentIndex, 2)
                    }
                }
                
                delegate: Item {
                    id: seasonDelegate
                    
                    // Required properties from model roles
                    required property int index
                    required property string name
                    required property string imageUrl
                    required property string itemId
                    required property int indexNumber
                    required property int episodeCount
                    required property int unplayedItemCount
                    required property bool isPlayed
                    
                    width: seasonsGrid.cellWidth
                    height: seasonsGrid.cellHeight
                    property bool isFocused: seasonsGrid.currentIndex === index && seasonsGrid.activeFocus
                    property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
                    scale: isFocused ? 1.05 : (isHovered ? 1.02 : 1.0)
                    z: isFocused ? 2 : 0
                    transformOrigin: Item.Center
                    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
                    
                    // Frosted glass card container
                    Rectangle {
                        id: seasonCard
                        anchors.centerIn: parent
                        width: seasonsGrid.cellWidth - 32
                        height: seasonsGrid.cellHeight - 56
                        radius: Theme.radiusMedium
                            antialiasing: true
                        color: seasonDelegate.isFocused ? Theme.cardBackgroundFocused : (seasonDelegate.isHovered ? Theme.cardBackgroundHover : Theme.cardBackground)
                        border.width: seasonDelegate.isFocused ? 2 : 1
                        border.color: seasonDelegate.isFocused ? Theme.cardBorderFocused : (seasonDelegate.isHovered ? Theme.cardBorderHover : Theme.cardBorder)
                        clip: true
                        
                        Behavior on color { ColorAnimation { duration: 150 } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                        
                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowHorizontalOffset: 0
                            shadowVerticalOffset: seasonDelegate.isFocused ? 22 : 14
                            shadowBlur: seasonDelegate.isFocused ? 1.0 : 0.65
                            shadowColor: seasonDelegate.isFocused ? "#66000000" : "#44000000"
                        }
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: Theme.spacingSmall
                            
                            // Image container with rounded corners
                            Rectangle {
                                id: seasonImageContainer
                                Layout.fillWidth: true
                                // Cap height so scaled cards never clip on 4K while keeping aspect
                                Layout.preferredHeight: Math.min(width * 1.5, seasonsGrid.cellHeight - 120)
                                radius: Theme.imageRadius
                                antialiasing: true
                                color: "transparent"
                                clip: false

                                // Rounded image
                                Image {
                                    id: seasonCoverArt
                                    anchors.fill: parent
                                    source: seasonDelegate.imageUrl !== "" ? seasonDelegate.imageUrl : posterUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    visible: false
                                }

                                Rectangle {
                                    id: seasonMask
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    color: "white"
                                    visible: false
                                    layer.enabled: true
                                }

                                OpacityMask {
                                    anchors.fill: parent
                                    source: seasonCoverArt
                                    maskSource: seasonMask
                                }

                                // Highlight overlay on focus/hover
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#00ffffff" }
                                        GradientStop { position: 0.35; color: "#30ffffff" }
                                        GradientStop { position: 0.6; color: "#10ffffff" }
                                        GradientStop { position: 1.0; color: "transparent" }
                                    }
                                    opacity: seasonDelegate.isFocused ? 0.25 : (seasonDelegate.isHovered ? 0.12 : 0.0)
                                    Behavior on opacity { NumberAnimation { duration: 200 } }
                                }

                                // Loading placeholder
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    color: Qt.rgba(0.2, 0.2, 0.2, 0.5)
                                    visible: seasonCoverArt.status !== Image.Ready
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "..."
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeMedium
                                        font.family: Theme.fontPrimary
                                    }
                                }

                                // Unwatched episode count badge
                                UnwatchedBadge {
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    parentWidth: parent.width
                                    count: seasonDelegate.unplayedItemCount
                                    isFullyWatched: seasonDelegate.isPlayed
                                }
                            }
                            
                            Text {
                                text: seasonDelegate.name
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: seasonDelegate.episodeCount > 0 ? seasonDelegate.episodeCount + " Episodes" : ""
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                seasonsGrid.currentIndex = seasonDelegate.index
                                seasonsGrid.forceActiveFocus()
                                root.navigateToSeasons(seasonsGrid.currentIndex)
                            }
                        }
                    }
                }
                
                Keys.onReturnPressed: {
                    if (currentIndex >= 0 && currentIndex < SeriesDetailsViewModel.seasonCount) {
                        root.navigateToSeasons(seasonsGrid.currentIndex)
                    }
                }
                
                Keys.onEnterPressed: {
                    if (currentIndex >= 0 && currentIndex < SeriesDetailsViewModel.seasonCount) {
                        root.navigateToSeasons(seasonsGrid.currentIndex)
                    }
                }
            }
            }  // End of leftContentColumn ColumnLayout
        }  // End of leftContentFlickable Flickable
        
        // Right Sidebar (30%)
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: {
                // If window is wide (full screen > 1200), shrink to ~15% (half of 30%)
                // Otherwise keep at 30%
                if (root.width > 1200) return parent.width * 0.15
                return parent.width * 0.3
            }
            spacing: Theme.spacingMedium
            
            // Series Poster - Frosted glass container
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

                    // Rounded image
                    Image {
                        id: posterImage
                        anchors.fill: parent
                        source: posterUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                }
            }
            
            // Next Up Box - Frosted glass container with keyboard navigation
            FocusScope {
                id: nextUpContainer
                visible: hasNextEpisode
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.nextUpHeight
                focus: false
                
                property bool isFocused: activeFocus
                property bool isHovered: InputModeManager.pointerActive && nextUpMouseArea.containsMouse
                
                KeyNavigation.up: seasonsGrid
                KeyNavigation.left: seasonsGrid
                
                Keys.onReturnPressed: playNextUp()
                Keys.onEnterPressed: playNextUp()
                
                function playNextUp() {
                    if (hasNextEpisode) {
                        root.playNextEpisode(SeriesDetailsViewModel.nextEpisodeId, SeriesDetailsViewModel.nextEpisodePlaybackPositionTicks)
                    }
                }
                
                Rectangle {
                    id: nextUpCard
                    anchors.fill: parent
                    color: nextUpContainer.isFocused ? Theme.cardBackgroundFocused : (nextUpContainer.isHovered ? Theme.cardBackgroundHover : Theme.cardBackground)
                    radius: Theme.radiusLarge
                    border.width: nextUpContainer.isFocused ? 2 : 1
                    border.color: nextUpContainer.isFocused ? Theme.cardBorderFocused : (nextUpContainer.isHovered ? Theme.cardBorderHover : Theme.cardBorder)
                    
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Behavior on border.color { ColorAnimation { duration: 150 } }
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: Theme.spacingMedium
                        
                        Text {
                            text: "Next Up"
                            font.pixelSize: Theme.fontSizeTitle
                            font.family: Theme.fontPrimary
                            font.bold: true
                            color: Theme.textPrimary
                        }
                        
                        // Next episode thumbnail with rounded corners
                        Rectangle {
                            id: nextEpImageContainer
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.nextUpImageHeight
                            radius: Theme.imageRadius
                            clip: true
                            color: "transparent"

                            // Rounded image
                            Image {
                                id: nextEpImage
                                anchors.fill: parent
                                source: SeriesDetailsViewModel.nextEpisodeImageUrl !== "" ? SeriesDetailsViewModel.nextEpisodeImageUrl : posterUrl
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                opacity: status === Image.Ready ? 1.0 : 0.0
                                Behavior on opacity { NumberAnimation { duration: 300 } }
                            }
                            
                            // Play icon overlay
                            Rectangle {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                radius: 24
                                color: Qt.rgba(0, 0, 0, 0.7)
                                visible: nextUpContainer.isFocused || nextUpContainer.isHovered
                                opacity: nextUpContainer.isFocused ? 1.0 : 0.8
                                
                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                
                                Text {
                                    anchors.centerIn: parent
                                    anchors.horizontalCenterOffset: 2  // Optical centering for play icon
                                    text: Icons.playArrow
                                    font.family: Theme.fontIcon
                                    font.pixelSize: 28
                                    color: Theme.textPrimary
                                }
                            }

                            // Progress bar for resume position
                            MediaProgressBar {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                positionTicks: SeriesDetailsViewModel.nextEpisodePlaybackPositionTicks
                                runtimeTicks: {
                                    var data = SeriesDetailsViewModel.getNextEpisodeData()
                                    return data && data.RunTimeTicks ? data.RunTimeTicks : 0
                                }
                            }

                            // Placeholder background
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.imageRadius
                                color: Qt.rgba(0.1, 0.1, 0.1, 0.6)
                                visible: nextEpImage.status !== Image.Ready
                            }
                        }
                        
                        Text {
                            visible: hasNextEpisode
                            text: {
                                if (!hasNextEpisode) return ""
                                var s = SeriesDetailsViewModel.nextSeasonNumber || "?"
                                var e = SeriesDetailsViewModel.nextEpisodeNumber || "?"
                                return "S" + s + "E" + e + " - " + (SeriesDetailsViewModel.nextEpisodeName || "")
                            }
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            font.bold: true
                            color: Theme.textPrimary
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                    
                    MouseArea {
                        id: nextUpMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            nextUpContainer.forceActiveFocus()
                            nextUpContainer.playNextUp()
                        }
                    }
                }
            }
            
            Item {
                Layout.fillHeight: true
            }
        }
    }
    
    // Connect to seriesLoaded signal to set focus when data is ready
    Connections {
        target: SeriesDetailsViewModel
        function onSeriesLoaded() {
            // Delay focus slightly to ensure UI is fully rendered
            focusTimer.start()
        }
    }
    
    Timer {
        id: focusTimer
        interval: 50
        repeat: false
        onTriggered: {
            if (playButton.enabled) {
                playButton.forceActiveFocus()
            } else {
                seasonsGrid.forceActiveFocus()
            }
            // Ensure seasons are at least partially visible on large screens
            Qt.callLater(function() {
                leftContentFlickable.ensureInitialSeasonsVisibility()
            })
        }
    }
    
    Component.onCompleted: {
        // If data is already loaded (e.g., returning from season view), set focus immediately
        if (SeriesDetailsViewModel.seriesId !== "" && SeriesDetailsViewModel.title !== "") {
            focusTimer.start()
            // Check if we need to restore seasons grid position
            if (pendingSeasonsGridIndex >= 0 && seasonsGrid.count > 0) {
                var targetIndex = Math.min(pendingSeasonsGridIndex, seasonsGrid.count - 1)
                console.log("[SeriesDetailsView] Restoring seasons grid index:", targetIndex)
                seasonsGrid.currentIndex = targetIndex
                seasonsGrid.positionViewAtIndex(targetIndex, GridView.Center)
                seasonsGrid.forceActiveFocus()
                root.pendingSeasonsGridIndex = -1
            }
        }
    }
    
    // Watch for seasons model reset to restore pending index
    Connections {
        target: SeriesDetailsViewModel.seasonsModel
        function onModelReset() {
            console.log("[SeriesDetailsView] onModelReset - pendingSeasonsGridIndex:", pendingSeasonsGridIndex, "count:", seasonsGrid.count)
            // Use Qt.callLater to ensure count is updated after model reset
            Qt.callLater(function() {
                if (root.pendingSeasonsGridIndex >= 0 && seasonsGrid.count > 0) {
                    var targetIndex = Math.min(root.pendingSeasonsGridIndex, seasonsGrid.count - 1)
                    console.log("[SeriesDetailsView] Restoring seasons grid index on model reset:", targetIndex)
                    seasonsGrid.currentIndex = targetIndex
                    seasonsGrid.positionViewAtIndex(targetIndex, GridView.Center)
                    seasonsGrid.forceActiveFocus()
                    // Clear the pending index
                    root.pendingSeasonsGridIndex = -1
                    // Scroll to show the selected season
                    leftContentFlickable.ensureSeasonsVisible()
                } else if (seasonsGrid.count > 0 && seasonsGrid.currentIndex < 0) {
                    // Ensure currentIndex is valid so navigation works
                    seasonsGrid.currentIndex = 0
                }
                // Always ensure seasons are at least partially visible on large screens
                leftContentFlickable.ensureInitialSeasonsVisibility()
            })
        }
    }
}
