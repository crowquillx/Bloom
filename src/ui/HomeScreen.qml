import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtCore

import BloomUI

FocusScope {
    id: root
    focus: true
    property string navigationId: "home"

    property var librariesModel: []
    property var nextUpModel: []
    property var recentlyAddedMap: ({}) // Map of libraryId -> items array
    // Home card sizing — dynamically fills row based on visible-items breakpoint
    property int homeCardWidth: Math.round((parent.width - Theme.paddingLarge * 2 - Theme.spacingMedium * (Theme.homeRowVisibleItems - 1)) / Theme.homeRowVisibleItems)
    // Force 16:9 aspect to reduce image crop vs. server thumbs
    property int homeCardHeight: Math.round(homeCardWidth * 9 / 16)
    // Request higher-resolution images for the scaled cards
    property int homeCardImageRequestWidth: Math.round(homeCardWidth * 2)
    // Recently Added poster sizing — same column count as home cards
    property int recentlyAddedPosterWidth: Math.round((parent.width - Theme.paddingLarge * 2 - Theme.spacingMedium * (Theme.homeRowVisibleItems - 1)) / Theme.homeRowVisibleItems)
    property int recentlyAddedPosterHeight: Math.round(recentlyAddedPosterWidth * 1.5)

    // Smooth transitions when breakpoint changes resize cards
    Behavior on homeCardWidth {
        NumberAnimation { duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.InOutQuad }
    }
    Behavior on homeCardHeight {
        NumberAnimation { duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.InOutQuad }
    }
    Behavior on recentlyAddedPosterWidth {
        NumberAnimation { duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.InOutQuad }
    }
    Behavior on recentlyAddedPosterHeight {
        NumberAnimation { duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.InOutQuad }
    }

    // Rotating backdrop properties
    property var backdropCandidates: []  // Array of {itemId, backdropTag} objects
    property string currentBackdropUrl: ""
    property var backdropShuffleOrder: []
    property int backdropShuffleCursor: 0
    property int backdropRngState: 1
    property bool globalBackdropPoolLoaded: false
    property bool loadingGlobalBackdropPool: false
    property bool useSectionBackdropFallback: false
    property bool fullBackdropIndexRequested: false

    Settings {
        id: homeBackdropSettings
        category: "HomeBackdrop"
        property string lastBackdropUrl: ""
    }
    
    // Signal to request navigation to library screen
    signal navigateToLibrary(var libraryId, var libraryName)
    
    // Signals for specific item navigation (from Recently Added)
    signal navigateToMovie(var movieData, var libraryId, var libraryName)
    signal navigateToEpisode(var episodeData, var seriesId, var libraryId, var libraryName)
    signal navigateToSeason(var seasonId, var seasonNumber, var seriesId, var libraryId, var libraryName)
    signal navigateToSeries(var seriesId, var libraryId, var libraryName)
    
    /// Refresh dynamic content (Next Up, Recently Added) without reloading libraries
    /// Call this after playback ends to update watch progress indicators
    function refreshDynamicContent() {
        console.log("HomeScreen: Refreshing dynamic content")
        LibraryService.getNextUp()
        for (var i = 0; i < librariesModel.length; i++) {
            LibraryService.getLatestMedia(librariesModel[i].Id)
        }
    }

    function hydrateBackdropFallbackFromSections() {
        for (var i = 0; i < nextUpModel.length; i++) {
            addBackdropCandidate(nextUpModel[i])
        }
        for (var key in recentlyAddedMap) {
            var items = recentlyAddedMap[key] || []
            for (var j = 0; j < items.length; j++) {
                addBackdropCandidate(items[j])
            }
        }
        if (backdropCandidates.length > 0) {
            invalidateBackdropShuffle()
            selectRandomBackdrop()
        }
    }

    function reseedBackdropRng() {
        var seed = Date.now() & 0x7fffffff
        if (seed === 0) {
            seed = 1
        }
        backdropRngState = seed
    }

    function nextBackdropRandomInt(maxExclusive) {
        if (maxExclusive <= 0) {
            return 0
        }
        backdropRngState = (1103515245 * backdropRngState + 12345) & 0x7fffffff
        return backdropRngState % maxExclusive
    }

    function invalidateBackdropShuffle() {
        backdropShuffleOrder = []
        backdropShuffleCursor = 0
    }

    function rebuildBackdropShuffle() {
        var order = []
        for (var i = 0; i < backdropCandidates.length; i++) {
            order.push(i)
        }
        // Fisher-Yates shuffle for full-cycle non-repeating rotation.
        for (var j = order.length - 1; j > 0; j--) {
            var k = nextBackdropRandomInt(j + 1)
            var tmp = order[j]
            order[j] = order[k]
            order[k] = tmp
        }
        backdropShuffleOrder = order
        backdropShuffleCursor = 0
    }

    function orderLibraries(views) {
        var order = SidebarSettings.libraryOrder || []
        var byId = {}
        var ordered = []
        for (var i = 0; i < views.length; i++) {
            byId[views[i].Id] = views[i]
        }

        for (var j = 0; j < order.length; j++) {
            var id = order[j]
            if (byId[id]) {
                ordered.push(byId[id])
                delete byId[id]
            }
        }

        var remaining = Object.keys(byId)
        for (var k = 0; k < remaining.length; k++) {
            ordered.push(byId[remaining[k]])
        }
        return ordered
    }
    
    // Track if this is the first activation (don't refresh on initial load)
    property bool hasBeenActivated: false
    
    // Focus state tracking for restoration
    property string lastFocusedSection: "myMedia"
    property int lastFocusedIndex: 0
    property real lastContentY: 0  // Remember scroll position

    // Breakpoint focus restoration state
    property int savedMyMediaIndex: -1
    property int savedNextUpIndex: -1
    property int savedRecentlyAddedIndex: -1
    property string savedRecentlyAddedLibraryId: ""

    Connections {
        target: ResponsiveLayoutManager
        function onBreakpointChanged() {
            if (myMediaList.activeFocus) savedMyMediaIndex = myMediaList.currentIndex
            if (nextUpList.activeFocus) savedNextUpIndex = nextUpList.currentIndex
            // Check if a recentlyAddedList has focus and save its state
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var item = recentlyAddedRepeater.itemAt(i)
                if (item && item.recentlyAddedListRef && item.recentlyAddedListRef.activeFocus) {
                    savedRecentlyAddedLibraryId = item.libraryId
                    savedRecentlyAddedIndex = item.recentlyAddedListRef.currentIndex
                    break
                }
            }
            Qt.callLater(restoreBreakpointFocus)
        }
    }

    function restoreBreakpointFocus() {
        if (savedMyMediaIndex >= 0 && savedMyMediaIndex < myMediaList.count) {
            myMediaList.currentIndex = savedMyMediaIndex
            myMediaList.positionViewAtIndex(savedMyMediaIndex, ListView.Contain)
            myMediaList.forceActiveFocus()
        } else if (savedNextUpIndex >= 0 && savedNextUpIndex < nextUpList.count) {
            nextUpList.currentIndex = savedNextUpIndex
            nextUpList.positionViewAtIndex(savedNextUpIndex, ListView.Contain)
            nextUpList.forceActiveFocus()
        } else if (savedRecentlyAddedLibraryId !== "" && savedRecentlyAddedIndex >= 0) {
            // Find the matching recentlyAddedList for the saved library
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var item = recentlyAddedRepeater.itemAt(i)
                if (item && item.libraryId === savedRecentlyAddedLibraryId && item.recentlyAddedListRef) {
                    var list = item.recentlyAddedListRef
                    if (savedRecentlyAddedIndex < list.count) {
                        list.currentIndex = savedRecentlyAddedIndex
                        list.positionViewAtIndex(savedRecentlyAddedIndex, ListView.Contain)
                        list.forceActiveFocus()
                    }
                    break
                }
            }
        }
        savedMyMediaIndex = -1
        savedNextUpIndex = -1
        savedRecentlyAddedIndex = -1
        savedRecentlyAddedLibraryId = ""
    }
    
    // Function to save current focus state before navigating away
    function saveFocusState() {
        var section = ""
        var index = 0
        
        // Check which list has active focus
        if (myMediaList.activeFocus) {
            section = "myMedia"
            index = myMediaList.currentIndex
        } else if (nextUpList.activeFocus) {
            section = "nextUp"
            index = nextUpList.currentIndex
        } else {
            // Check recentlyAdded lists
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var list = recentlyAddedRepeater.itemAt(i)
                if (list && list.recentlyAddedListRef && list.recentlyAddedListRef.activeFocus) {
                    section = "recentlyAdded:" + list.libraryId
                    index = list.recentlyAddedListRef.currentIndex
                    break
                }
            }
        }
        
        // Only update if we found a focused section
        if (section !== "") {
            lastFocusedSection = section
            lastFocusedIndex = Math.max(0, index)
            lastContentY = mainFlickable.contentY
            console.log("[FocusDebug] Saved focus state:", section, "index:", index, "scroll:", lastContentY)
        }
    }
    
    // Function to restore focus state when returning to HomeScreen
    function restoreFocusState() {
        console.log("[FocusDebug] Restoring focus state:", lastFocusedSection, "index:", lastFocusedIndex)
        
        var targetList = null
        var targetSection = null
        
        if (lastFocusedSection === "myMedia") {
            targetList = myMediaList
            targetSection = myMediaSection
        } else if (lastFocusedSection === "nextUp") {
            targetList = nextUpList
            targetSection = nextUpSection
        } else if (lastFocusedSection.indexOf("recentlyAdded:") === 0) {
            var libraryId = lastFocusedSection.substring("recentlyAdded:".length)
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var list = recentlyAddedRepeater.itemAt(i)
                if (list && list.libraryId === libraryId && list.recentlyAddedListRef) {
                    targetList = list.recentlyAddedListRef
                    targetSection = list
                    break
                }
            }
        }
        
        if (targetList && targetSection) {
            // First scroll the section into view
            ensureSectionVisible(targetSection)
            
            // Then set the current index and focus
            targetList.currentIndex = Math.min(lastFocusedIndex, Math.max(0, targetList.count - 1))
            
            Qt.callLater(function() {
                if (targetList && targetList.count > 0) {
                    targetList.forceActiveFocus()
                    // Make sure the horizontal list item is visible too
                    ensureListItemVisible(targetList)
                    console.log("[FocusDebug] Restored focus to:", lastFocusedSection, "currentIndex:", targetList.currentIndex)
                }
            })
        } else {
            // Fallback to myMediaList
            console.log("[FocusDebug] Could not find target list, defaulting to myMediaList")
            myMediaList.currentIndex = 0
            Qt.callLater(function() {
                myMediaList.forceActiveFocus()
            })
        }
    }
    
    // Track when we left the home screen to determine if refresh is needed
    property double lastDeactivatedTime: 0
    property int refreshThresholdMs: 30000  // Only refresh if gone for more than 30 seconds
    
    StackView.onStatusChanged: {
        console.log("[FocusDebug] HomeScreen StackView.statusChanged:", StackView.status)
        if (StackView.status === StackView.Active) {
            console.log("[FocusDebug] HomeScreen now active")
            
            // Restore focus IMMEDIATELY when returning to home screen
            restoreFocusState()
            
            // Only refresh if we've been away for a while (30+ seconds)
            if (hasBeenActivated) {
                var timeSinceDeactivation = Date.now() - lastDeactivatedTime
                if (timeSinceDeactivation > refreshThresholdMs) {
                    console.log("[FocusDebug] Refreshing dynamic content (been away", timeSinceDeactivation, "ms)")
                    refreshDynamicContent()
                } else {
                    console.log("[FocusDebug] Skipping refresh (returned quickly)")
                }
            } else {
                hasBeenActivated = true
            }
        } else if (StackView.status === StackView.Deactivating) {
            lastDeactivatedTime = Date.now()
        }
    }
    
    // Timer for rotating backdrops
    Timer {
        id: backdropRotationTimer
        interval: ConfigManager.backdropRotationInterval
        repeat: true
        running: backdropCandidates.length > 1 && !PlayerController.isPlaybackActive
        onTriggered: selectRandomBackdrop()
    }
    
    // Function to collect backdrop from an item
    function addBackdropCandidate(item) {
        var itemId = item.Id
        var backdropTag = null
        var backdropItemId = itemId
        
        // Check for direct backdrop
        if (item.BackdropImageTags && item.BackdropImageTags.length > 0) {
            backdropTag = item.BackdropImageTags[0]
        } else if (item.ImageTags && item.ImageTags.Backdrop) {
            // Some Jellyfin item queries expose backdrop as ImageTags.Backdrop.
            backdropTag = item.ImageTags.Backdrop
        }
        // Check for parent backdrop (for episodes/seasons)
        else if (item.ParentBackdropImageTags && item.ParentBackdropImageTags.length > 0) {
            backdropTag = item.ParentBackdropImageTags[0]
            backdropItemId = item.ParentBackdropItemId || item.SeriesId || itemId
        }
        
        if (backdropTag && backdropItemId) {
            // Check if we already have this backdrop
            for (var i = 0; i < backdropCandidates.length; i++) {
                if (backdropCandidates[i].itemId === backdropItemId) {
                    return  // Already have it
                }
            }
            
            var newCandidates = backdropCandidates.slice()
            newCandidates.push({
                itemId: backdropItemId,
                backdropTag: backdropTag
            })
            backdropCandidates = newCandidates
            invalidateBackdropShuffle()
            
            // If this is the first backdrop, show it immediately unless we are still
            // populating the global pool (avoid deterministic "first item" startup).
            if (backdropCandidates.length === 1 && !loadingGlobalBackdropPool) {
                selectRandomBackdrop()
            }
        }
    }
    
    // Function to select a random backdrop
    function selectRandomBackdrop() {
        if (backdropCandidates.length === 0) {
            currentBackdropUrl = ""
            return
        }

        if (backdropShuffleOrder.length !== backdropCandidates.length || backdropShuffleCursor >= backdropShuffleOrder.length) {
            rebuildBackdropShuffle()
        }

        var candidateIndex = backdropShuffleOrder[backdropShuffleCursor]
        backdropShuffleCursor++
        var candidate = backdropCandidates[candidateIndex]
        
        // Construct backdrop URL
        var url = LibraryService.getCachedImageUrlWithWidth(candidate.itemId, "Backdrop", 1920)
        if (url && candidate.backdropTag) {
            url += "?tag=" + candidate.backdropTag
        }
        
        currentBackdropUrl = url
    }
    
    // Backdrop Container with Cross-fade
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        z: -1
        radius: Theme.radiusLarge
        color: "transparent"
        clip: true

        property bool showBackdrop1: true

        Image {
            id: backdrop1
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: parent.showBackdrop1 ? 1.0 : 0.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }

            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: Theme.blurRadius
            }

            onStatusChanged: backdropContainer.checkStatus(this)
        }

        Image {
            id: backdrop2
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: parent.showBackdrop1 ? 0.0 : 1.0
            visible: true
            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }

            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: Theme.blurRadius
            }

            onStatusChanged: backdropContainer.checkStatus(this)
        }

        function checkStatus(img) {
            // Only act if this image is loading the CURRENT requested url
            if (img.source.toString() !== root.currentBackdropUrl) return

            if (img.status === Image.Ready || (img.status === Image.Null && root.currentBackdropUrl === "")) {
                if (img.status === Image.Ready && root.currentBackdropUrl !== "") {
                    homeBackdropSettings.lastBackdropUrl = root.currentBackdropUrl
                }
                // New image loaded (or cleared), switch to it
                if (img === backdrop1) showBackdrop1 = true
                else showBackdrop1 = false
            } else if (img.status === Image.Error) {
                console.log("Home backdrop failed to load: " + img.source)
            }
        }

        // Gradient Overlay for readability
        Rectangle {
            anchors.fill: parent
            z: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.gradientOverlayStart }
                GradientStop { position: 0.4; color: Theme.gradientOverlayMiddle }
                GradientStop { position: 1.0; color: Theme.gradientOverlayEnd }
            }
        }
    }

    onCurrentBackdropUrlChanged: {
        // Load the new URL into the INACTIVE image
        var target = backdropContainer.showBackdrop1 ? backdrop2 : backdrop1
        
        // If the target is already displaying this URL, just switch immediately
        if (target.source.toString() === currentBackdropUrl && target.status === Image.Ready) {
             if (target === backdrop1) backdropContainer.showBackdrop1 = true
             else backdropContainer.showBackdrop1 = false
             return
        }

        target.source = currentBackdropUrl
    }
    
    // Fallback solid background (visible when no backdrop loaded)
    Rectangle {
        anchors.fill: parent
        z: -2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundPrimary }
            GradientStop { position: 1.0; color: Theme.backgroundSecondary }
        }
    }

    Flickable {
        id: mainFlickable
        anchors.fill: parent
        contentHeight: mainColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        
        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: Theme.spacingXLarge
            
            // Top spacing
            Item { height: Theme.paddingLarge }
            
            // My Media Section
            ColumnLayout {
                id: myMediaSection
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                
                Text {
                    text: "My Media"
                    font.pixelSize: Theme.fontSizeDisplay
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    leftPadding: Theme.paddingLarge
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: "#B0000000"
                        shadowVerticalOffset: 2
                        shadowBlur: 0.35
                    }
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                
                ListView {
                    id: myMediaList
                    Layout.fillWidth: true
                    Layout.preferredHeight: homeCardHeight + Theme.paddingLarge
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingMedium
                    leftMargin: Theme.paddingLarge
                    rightMargin: Theme.paddingLarge
                    clip: false
                    focus: true
                    preferredHighlightBegin: Theme.paddingLarge
                    preferredHighlightEnd: width - Theme.paddingLarge
                    highlightRangeMode: ListView.StrictlyEnforceRange
                    highlightMoveDuration: 0
                move: Transition {
                    NumberAnimation { properties: "x"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                }
                displaced: Transition {
                    NumberAnimation { properties: "x"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                }
                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                }
                populate: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                }
                remove: Transition {
                    NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                }
                    
                    model: librariesModel
                    
                    delegate: Item {
                        width: homeCardWidth
                        height: homeCardHeight
                        property bool isFocused: myMediaList.currentIndex === index && myMediaList.activeFocus
                        property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
                        scale: isFocused ? 1.04 : (isHovered ? 1.01 : 1.0)
                        z: isFocused ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                        // Edge-to-edge poster without outer card chrome
                        Rectangle {
                            id: imageContainer
                            anchors.fill: parent
                            radius: Theme.imageRadius
                            antialiasing: true
                            color: "transparent"
                            clip: false

                            Image {
                                id: myMediaImageSource
                                anchors.fill: parent
                                source: LibraryService.getCachedImageUrlWithWidth(modelData.Id, "Primary", homeCardImageRequestWidth)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                visible: true

                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    maskEnabled: true
                                    maskSource: myMediaMask
                                }
                            }

                            Rectangle {
                                id: myMediaMask
                                anchors.fill: parent
                                radius: Theme.imageRadius
                                visible: false
                                layer.enabled: true
                                layer.smooth: true
                            }

                            // Loading placeholder
                            Text {
                                anchors.centerIn: parent
                                text: "..."
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.family: Theme.fontPrimary
                                visible: myMediaImageSource.status !== Image.Ready
                            }

                            // Bottom gradient overlay for text readability
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.imageRadius
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 0.6; color: "transparent" }
                                    GradientStop { position: 1.0; color: "#80000000" }
                                }
                            }

                            // Highlight overlay on focus/hover
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.imageRadius
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#00ffffff" }
                                    GradientStop { position: 0.45; color: "#30ffffff" }
                                    GradientStop { position: 0.65; color: "#10ffffff" }
                                    GradientStop { position: 1.0; color: "transparent" }
                                }
                                opacity: isFocused ? 0.3 : (isHovered ? 0.15 : 0.0)
                                Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } enabled: Theme.uiAnimationsEnabled }
                            }

                            // Outline on focus/hover
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + border.width * 2
                                height: parent.height + border.width * 2
                                radius: Theme.imageRadius + border.width
                                color: "transparent"
                                border.width: isFocused ? Theme.buttonFocusBorderWidth : (isHovered ? Theme.buttonFocusBorderWidth - 1 : 0)
                                border.color: Theme.accentPrimary
                                antialiasing: true
                                visible: border.width > 0
                                Behavior on border.width { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                myMediaList.currentIndex = index
                                myMediaList.forceActiveFocus()
                                handleLibrarySelection(modelData)
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.Name
                        Accessible.description: "Browse " + modelData.Name + " library"
                    }
                    
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < librariesModel.length) {
                            handleLibrarySelection(librariesModel[currentIndex])
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < librariesModel.length) {
                            handleLibrarySelection(librariesModel[currentIndex])
                        }
                    }
                    Keys.onDownPressed: {
                        if (nextUpModel.length > 0) {
                            console.log("[FocusDebug] myMediaList Down -> nextUpList")
                            nextUpList.forceActiveFocus()
                        } else if (recentlyAddedRepeater.count > 0) {
                            // Find first visible recently added list
                            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                                var item = recentlyAddedRepeater.itemAt(i)
                                if (item && item.visible) {
                                    item.recentlyAddedListRef.forceActiveFocus()
                                    break
                                }
                            }
                        }
                    }

                    onCurrentIndexChanged: root.ensureListItemVisible(myMediaList)
                    onActiveFocusChanged: if (activeFocus) root.ensureSectionVisible(myMediaSection)
                }
            }
            
            // Next Up Section
            ColumnLayout {
                id: nextUpSection
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: nextUpModel.length > 0
                
                Text {
                    text: "Next Up"
                    font.pixelSize: Theme.fontSizeHeader
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    leftPadding: Theme.paddingLarge
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: "#B0000000"
                        shadowVerticalOffset: 2
                        shadowBlur: 0.35
                    }
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                
                ListView {
                    id: nextUpList
                    Layout.fillWidth: true
                    Layout.preferredHeight: homeCardHeight + Theme.spacingLarge * 2
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingMedium
                    leftMargin: Theme.paddingLarge
                    rightMargin: Theme.paddingLarge
                    clip: false
                    preferredHighlightBegin: Theme.paddingLarge
                    preferredHighlightEnd: width - Theme.paddingLarge
                    highlightRangeMode: ListView.StrictlyEnforceRange
                    highlightMoveDuration: 0

                    add: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                        NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    }
                    populate: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                        NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    }
                    addDisplaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    move: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    remove: Transition {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                        NumberAnimation { property: "scale"; to: 0.9; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                    }
                    removeDisplaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    displaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    
                    model: nextUpModel
                    
                    delegate: Item {
                        id: nextUpDelegate
                        width: homeCardWidth
                        height: homeCardHeight + 60
                        property bool isFocused: nextUpList.currentIndex === index && nextUpList.activeFocus
                        property bool isHovered: InputModeManager.pointerActive && nextUpMouseArea.containsMouse
                        scale: isFocused ? 1.02 : (isHovered ? 1.01 : 1.0)
                        z: isFocused ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                        Column {
                            anchors.fill: parent
                            spacing: 8

                            Rectangle {
                                id: nextUpImageContainer
                                width: parent.width
                                height: homeCardHeight
                                radius: Theme.imageRadius
                                color: "transparent"
                                    
                                Image {
                                    id: nextUpImage
                                    property string seriesThumbUrl: (modelData.SeriesId && (modelData.SeriesThumbImageTag || modelData.ParentThumbImageTag)) ? LibraryService.getCachedImageUrlWithWidth(modelData.SeriesId, "Thumb", homeCardImageRequestWidth) : ""
                                    property string parentThumbUrl: (modelData.ParentId && modelData.ParentThumbImageTag) ? LibraryService.getCachedImageUrlWithWidth(modelData.ParentId, "Thumb", homeCardImageRequestWidth) : ""
                                    property string episodeThumbUrl: (modelData.ImageTags && modelData.ImageTags.Thumb) ? LibraryService.getCachedImageUrlWithWidth(modelData.Id, "Thumb", homeCardImageRequestWidth) : ""
                                    property string episodePrimaryUrl: LibraryService.getCachedImageUrlWithWidth(modelData.Id, "Primary", homeCardImageRequestWidth)
                                    anchors.fill: parent
                                    source: {
                                        if (seriesThumbUrl && seriesThumbUrl !== "") return seriesThumbUrl
                                        if (parentThumbUrl && parentThumbUrl !== "") return parentThumbUrl
                                        if (episodeThumbUrl && episodeThumbUrl !== "") return episodeThumbUrl
                                        return episodePrimaryUrl
                                    }
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    visible: true

                                    layer.enabled: true
                                    layer.effect: MultiEffect {
                                        maskEnabled: true
                                        maskSource: nextUpMask
                                    }

                                    onStatusChanged: {
                                        if (status === Image.Error) {
                                            if (source === seriesThumbUrl && parentThumbUrl && parentThumbUrl !== "") {
                                                source = parentThumbUrl
                                                return
                                            }
                                            if ((source === seriesThumbUrl || source === parentThumbUrl) && episodeThumbUrl && episodeThumbUrl !== "") {
                                                source = episodeThumbUrl
                                                return
                                            }
                                            if (source !== episodePrimaryUrl) source = episodePrimaryUrl
                                        }
                                    }
                                }

                                Rectangle {
                                    id: nextUpMask
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    visible: false
                                    layer.enabled: true
                                    layer.smooth: true
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.imageRadius
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#00ffffff" }
                                        GradientStop { position: 0.4; color: "#40ffffff" }
                                        GradientStop { position: 0.65; color: "#10ffffff" }
                                        GradientStop { position: 1.0; color: "transparent" }
                                    }
                                    opacity: isFocused ? 0.25 : (isHovered ? 0.15 : 0.0)
                                    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } enabled: Theme.uiAnimationsEnabled }
                                }

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width + border.width * 2
                                    height: parent.height + border.width * 2
                                    radius: Theme.imageRadius + border.width
                                    color: "transparent"
                                    border.width: isFocused ? Theme.buttonFocusBorderWidth : (isHovered ? Theme.buttonFocusBorderWidth - 1 : 0)
                                    border.color: Theme.accentPrimary
                                    antialiasing: true
                                    visible: border.width > 0
                                    Behavior on border.width { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }
                                }

                                MediaProgressBar {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    positionTicks: modelData.UserData ? modelData.UserData.PlaybackPositionTicks : 0
                                    runtimeTicks: modelData.RunTimeTicks || 0
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: "..."
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    visible: nextUpImage.status !== Image.Ready
                                }
                            }

                            Column {
                                width: parent.width
                                spacing: Theme.spacingSmall / 4

                                Text {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: modelData.SeriesName || modelData.Name || ""
                                    font.pixelSize: Theme.fontSizeMedium
                                    font.family: Theme.fontPrimary
                                    font.bold: true
                                    color: Theme.textPrimary
                                    style: Text.Outline
                                    styleColor: "#000000"
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }

                                Text {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: {
                                        var txt = ""
                                        if (modelData.ParentIndexNumber !== undefined && modelData.IndexNumber !== undefined) {
                                            txt = "S" + modelData.ParentIndexNumber + ":E" + modelData.IndexNumber
                                            if (modelData.Name) txt += " - " + modelData.Name
                                        } else if (modelData.Name && modelData.SeriesName) {
                                            txt = modelData.Name
                                        }
                                        return txt
                                    }
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textSecondary
                                    style: Text.Outline
                                    styleColor: "#000000"
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                        }

                        MouseArea {
                            id: nextUpMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                nextUpList.currentIndex = index
                                nextUpList.forceActiveFocus()
                                handleNextUpSelection(modelData)
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: (modelData.SeriesName ? modelData.SeriesName + ", " : "") + (modelData.Name || "")
                        Accessible.description: "Continue watching " + (modelData.SeriesName || modelData.Name || "")
                    }
                    
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < nextUpModel.length) {
                            handleNextUpSelection(nextUpModel[currentIndex])
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < nextUpModel.length) {
                            handleNextUpSelection(nextUpModel[currentIndex])
                        }
                    }
                    Keys.onUpPressed: {
                        myMediaList.forceActiveFocus()
                    }
                    Keys.onDownPressed: {
                        if (recentlyAddedRepeater.count > 0) {
                            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                                var item = recentlyAddedRepeater.itemAt(i)
                                if (item && item.visible) {
                                    item.recentlyAddedListRef.forceActiveFocus()
                                    break
                                }
                            }
                        }
                    }

                    onCurrentIndexChanged: root.ensureListItemVisible(nextUpList)
                    onActiveFocusChanged: if (activeFocus) root.ensureSectionVisible(nextUpSection)
                }
            }
            
            // Recently Added Section
            ColumnLayout {
                id: recentlyAddedSection
                Layout.fillWidth: true
                spacing: Theme.spacingXLarge
                
                Repeater {
                    id: recentlyAddedRepeater
                    model: librariesModel
                    
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium
                        
                        property var items: recentlyAddedMap[modelData.Id] || []
                        property string libraryId: modelData.Id
                        property var recentlyAddedListRef: recentlyAddedList
                        visible: items.length > 0
                        
                        Text {
                            text: "Recently Added - " + modelData.Name
                            font.pixelSize: Theme.fontSizeHeader
                            font.family: Theme.fontPrimary
                            font.bold: true
                            color: Theme.textPrimary
                            leftPadding: Theme.paddingLarge
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: "#B0000000"
                                shadowVerticalOffset: 2
                                shadowBlur: 0.35
                            }
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        
                        ListView {
                            id: recentlyAddedList
                            Layout.fillWidth: true
                            Layout.preferredHeight: recentlyAddedPosterHeight + Theme.spacingLarge * 4
                            orientation: ListView.Horizontal
                            spacing: Theme.spacingMedium
                            leftMargin: Theme.paddingLarge
                            rightMargin: Theme.paddingLarge
                            clip: false
                            preferredHighlightBegin: Theme.paddingLarge
                            preferredHighlightEnd: width - Theme.paddingLarge
                            highlightRangeMode: ListView.StrictlyEnforceRange
                            highlightMoveDuration: 0
                            
                            add: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                                NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                            }
                            populate: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                                NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                            }
                            addDisplaced: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            move: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            remove: Transition {
                                NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                                NumberAnimation { property: "scale"; to: 0.9; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                            }
                            removeDisplaced: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            displaced: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            
                            model: parent.items
                            
                            property int libraryIndex: index === undefined ? -1 : index
                            property string parentLibraryId: parent.libraryId
                            
                            delegate: Item {
                                id: recentlyAddedDelegate
                                width: recentlyAddedPosterWidth
                                height: recentlyAddedPosterHeight + 110
                                property bool isFocused: recentlyAddedList.currentIndex === index && recentlyAddedList.activeFocus
                                property bool isHovered: InputModeManager.pointerActive && recentMouseArea.containsMouse
                                scale: isFocused ? 1.05 : (isHovered ? 1.02 : 1.0)
                                z: isFocused ? 1 : 0
                                transformOrigin: Item.Bottom  // Scale from bottom to prevent top clipping
                                Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                                Column {
                                    anchors.fill: parent
                                    anchors.topMargin: Theme.spacingLarge
                                    spacing: Theme.spacingSmall

                                    // Poster image (2:3 aspect ratio) with rounded corners
                                    Rectangle {
                                        id: recentImageContainer
                                        width: recentlyAddedPosterWidth
                                        height: recentlyAddedPosterHeight  // 2:3 poster aspect ratio
                                        radius: Theme.imageRadius
                                        clip: false
                                        color: "transparent"
                                        
                                        Image {
                                            id: recentCoverArt
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                            visible: true
                                            source: {
                                                if (modelData.Type === "Episode" && modelData.SeriesId) {
                                                    return LibraryService.getCachedImageUrlWithWidth(modelData.SeriesId, "Primary", 640)
                                                }
                                                return LibraryService.getCachedImageUrlWithWidth(modelData.Id, "Primary", 640)
                                            }

                                            layer.enabled: true
                                            layer.effect: MultiEffect {
                                                maskEnabled: true
                                                maskSource: recentMask
                                            }

                                            onStatusChanged: {
                                                if (status === Image.Error && modelData.Type === "Episode") {
                                                    source = LibraryService.getCachedImageUrl(modelData.Id, "Primary")
                                                }
                                            }
                                        }

                                        Rectangle {
                                            id: recentMask
                                            anchors.fill: parent
                                            radius: Theme.imageRadius
                                            visible: false
                                            layer.enabled: true
                                            layer.smooth: true
                                        }

                                        // Focus border overlay
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width + border.width * 2
                                            height: parent.height + border.width * 2
                                            radius: Theme.imageRadius + border.width
                                            color: "transparent"
                                            border.width: isFocused ? Theme.buttonFocusBorderWidth : 0
                                            border.color: Theme.accentPrimary
                                            Behavior on border.width { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }
                                        }

                                        // Loading placeholder
                                        Text {
                                            anchors.centerIn: parent
                                            text: "..."
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontSizeBody
                                            font.family: Theme.fontPrimary
                                            visible: recentCoverArt.status !== Image.Ready
                                        }

                                        // Unwatched episode count badge (for Series only, not individual Episodes)
                                        UnwatchedBadge {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            parentWidth: parent.width
                                            count: (modelData.Type === "Series" && modelData.UserData) 
                                                   ? (modelData.UserData.UnplayedItemCount || 0) : 0
                                            isFullyWatched: (modelData.Type === "Series" && modelData.UserData) 
                                                            ? (modelData.UserData.Played || false) : false
                                            visible: modelData.Type === "Series"
                                        }

                                        // Watched checkmark (for Movies only)
                                        UnwatchedBadge {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            parentWidth: parent.width
                                            count: 0
                                            isFullyWatched: (modelData.UserData) 
                                                            ? (modelData.UserData.Played || false) : false
                                            visible: modelData.Type === "Movie" && 
                                                     modelData.UserData && 
                                                     modelData.UserData.Played
                                        }
                                    }

                                    // Text container below the image
                                    Column {
                                        width: parent.width
                                        spacing: Theme.spacingSmall / 4

                                        // Title (bold, white with black outline)
                                        // For episodes: SeriesName, For series/movies: Name
                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: {
                                                if (modelData.Type === "Episode") {
                                                    return modelData.SeriesName || modelData.Name || ""
                                                }
                                                return modelData.Name || ""
                                            }
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.family: Theme.fontPrimary
                                            font.bold: true
                                            color: Theme.textPrimary
                                            style: Text.Outline
                                            styleColor: "#000000"
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }

                                        // Subtitle (smaller, grey with black outline)
                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: {
                                                if (modelData.Type === "Episode") {
                                                    var txt = ""
                                                    if (modelData.ParentIndexNumber !== undefined && modelData.IndexNumber !== undefined) {
                                                        txt = "S" + modelData.ParentIndexNumber + ":E" + modelData.IndexNumber
                                                        if (modelData.Name) txt += " - " + modelData.Name
                                                    } else if (modelData.Name) {
                                                        txt = modelData.Name
                                                    }
                                                    return txt
                                                } else if (modelData.Type === "Series") {
                                                    var startYear = modelData.ProductionYear || extractYear(modelData.PremiereDate)
                                                    if (!startYear) return ""
                                                    if (modelData.Status === "Continuing" || !modelData.EndDate) {
                                                        return startYear + " - Present"
                                                    } else {
                                                        var endYear = extractYear(modelData.EndDate)
                                                        if (endYear && endYear !== startYear) {
                                                            return startYear + " - " + endYear
                                                        }
                                                        return startYear.toString()
                                                    }
                                                } else if (modelData.Type === "Movie") {
                                                    var year = modelData.ProductionYear || extractYear(modelData.PremiereDate)
                                                    return year ? year.toString() : ""
                                                }
                                                return ""
                                            }
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.family: Theme.fontPrimary
                                            color: Theme.textSecondary
                                            style: Text.Outline
                                            styleColor: "#000000"
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }
                                    }
                                }

                                MouseArea {
                                    id: recentMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        recentlyAddedList.currentIndex = index
                                        recentlyAddedList.forceActiveFocus()
                                        handleRecentlyAddedSelection(modelData, recentlyAddedList.parentLibraryId)
                                    }
                                }

                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.Name
                                Accessible.description: "Details for " + modelData.Name
                            }

                            Keys.onReturnPressed: {
                                if (currentIndex >= 0 && currentIndex < count) {
                                    handleRecentlyAddedSelection(model[currentIndex], parentLibraryId)
                                }
                            }
                            Keys.onEnterPressed: {
                                if (currentIndex >= 0 && currentIndex < count) {
                                    handleRecentlyAddedSelection(model[currentIndex], parentLibraryId)
                                }
                            }
                            Keys.onUpPressed: {
                                if (libraryIndex === 0) {
                                    if (nextUpModel.length > 0) nextUpList.forceActiveFocus()
                                    else myMediaList.forceActiveFocus()
                                } else {
                                    for (var i = libraryIndex - 1; i >= 0; i--) {
                                        var item = recentlyAddedRepeater.itemAt(i)
                                        if (item && item.visible) {
                                            item.recentlyAddedListRef.forceActiveFocus()
                                            break
                                        }
                                    }
                                }
                            }
                            Keys.onDownPressed: {
                                for (var i = libraryIndex + 1; i < recentlyAddedRepeater.count; i++) {
                                    var item = recentlyAddedRepeater.itemAt(i)
                                    if (item && item.visible) {
                                        item.recentlyAddedListRef.forceActiveFocus()
                                        break
                                    }
                                }
                            }

                            onCurrentIndexChanged: root.ensureListItemVisible(recentlyAddedList)
                            onActiveFocusChanged: if (activeFocus) root.ensureSectionVisible(recentlyAddedList.parent)
                        }
                    }
                }
            }
            
            // Bottom spacing
            Item { height: Theme.paddingLarge }
        } // end mainColumn
    } // end Flickable
    
    // Scrollbar for mainFlickable
    ScrollBar {
        id: scrollBar
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        policy: ScrollBar.AsNeeded
        size: mainFlickable.height / mainFlickable.contentHeight
        position: mainFlickable.contentY / mainFlickable.contentHeight
        
        onPositionChanged: {
            if (pressed) {
                mainFlickable.contentY = position * mainFlickable.contentHeight
            }
        }
    }
    
    // Loading indicator
    BusyIndicator {
        anchors.centerIn: parent
        running: librariesModel.length === 0
        visible: running
        palette.dark: "white"
    }
    
    // Functions
    function ensureSectionVisible(targetItem, padding) {
        if (!targetItem)
            return
        var extra = padding !== undefined ? padding : Theme.paddingLarge
        var itemPos = targetItem.mapToItem(mainFlickable.contentItem, 0, 0)
        var top = itemPos.y - extra
        var bottom = itemPos.y + targetItem.height + extra
        var viewTop = mainFlickable.contentY
        var viewBottom = viewTop + mainFlickable.height
        if (top < viewTop) {
            mainFlickable.contentY = Math.max(0, top)
        } else if (bottom > viewBottom) {
            var maxY = Math.max(0, mainFlickable.contentHeight - mainFlickable.height)
            mainFlickable.contentY = Math.min(maxY, bottom - mainFlickable.height)
        }
    }

    function ensureListItemVisible(listView) {
        if (!listView || listView.currentIndex < 0)
            return
        listView.positionViewAtIndex(listView.currentIndex, ListView.Visible)
    }

    function handleLibrarySelection(library) {
        console.log("Selected library: " + library.Name)
        // Save focus state before navigating
        saveFocusState()
        // Emit signal to navigate to library screen
        navigateToLibrary(library.Id, library.Name)
    }
    
    function handleNextUpSelection(item) {
        console.log("[Home] Next Up selected: " + item.Name + " - navigating to episode details")
        
        // Save focus state before navigating
        saveFocusState()
        
        // Navigate to episode details view instead of playing directly
        var seriesId = item.SeriesId || ""
        
        // Find the library for this series (needed for navigation context)
        // For Next Up items, we may not have the library ID directly, so we use an empty string
        // The LibraryScreen will handle this appropriately
        var libraryId = ""
        var libraryName = ""
        
        navigateToEpisode(item, seriesId, libraryId, libraryName)
    }
    
    function handleRecentlyAddedSelection(item, libraryId) {
        console.log("[Home] Selected Recently Added:", item.Name, "Type:", item.Type, "Library:", libraryId)
        
        // Save focus state before navigating
        saveFocusState()
        
        // Find the library name for this item
        var libraryName = ""
        for (var i = 0; i < librariesModel.length; i++) {
            if (librariesModel[i].Id === libraryId) {
                libraryName = librariesModel[i].Name
                break
            }
        }
        
        if (item.Type === "Movie") {
            // Navigate to movie details view
            console.log("[Home] Navigating to movie details")
            navigateToMovie(item, libraryId, libraryName)
        } else if (item.Type === "Episode") {
            // For episodes, check if there are other episodes from the same series in recently added
            var seriesId = item.SeriesId || ""
            var items = recentlyAddedMap[libraryId] || []
            
            // Find all episodes from the same series in this recently added list
            var relatedEpisodes = []
            for (var j = 0; j < items.length; j++) {
                if (items[j].Type === "Episode" && items[j].SeriesId === seriesId) {
                    relatedEpisodes.push(items[j])
                }
            }
            
            console.log("[Home] Found", relatedEpisodes.length, "related episodes from series:", seriesId)
            
            if (relatedEpisodes.length <= 1) {
                // Single episode - go directly to episode view
                console.log("[Home] Navigating to episode view (single episode)")
                navigateToEpisode(item, seriesId, libraryId, libraryName)
            } else {
                // Multiple episodes - check if they're from the same season or different seasons
                var seasons = {}
                for (var k = 0; k < relatedEpisodes.length; k++) {
                    var seasonNumber = relatedEpisodes[k].ParentIndexNumber || 0
                    var parentId = relatedEpisodes[k].ParentId || ""
                    if (!seasons[seasonNumber]) {
                        seasons[seasonNumber] = {
                            seasonNumber: seasonNumber,
                            parentId: parentId,
                            count: 0
                        }
                    }
                    seasons[seasonNumber].count++
                }
                
                var seasonCount = Object.keys(seasons).length
                console.log("[Home] Episodes span", seasonCount, "season(s)")
                
                if (seasonCount === 1) {
                    // All episodes from the same season - go to season view
                    var seasonInfo = seasons[Object.keys(seasons)[0]]
                    console.log("[Home] Navigating to season view (season", seasonInfo.seasonNumber + ")")
                    navigateToSeason(seasonInfo.parentId, seasonInfo.seasonNumber, seriesId, libraryId, libraryName)
                } else {
                    // Episodes from multiple seasons - go to series view
                    console.log("[Home] Navigating to series view (multiple seasons)")
                    navigateToSeries(seriesId, libraryId, libraryName)
                }
            }
        } else if (item.Type === "Series") {
            // Navigate to series details
            console.log("[Home] Navigating to series details")
            navigateToSeries(item.Id, libraryId, libraryName)
        } else if (item.Type === "Season") {
            // Navigate to season view
            var seasonSeriesId = item.SeriesId || item.ParentId || ""
            console.log("[Home] Navigating to season view")
            navigateToSeason(item.Id, item.IndexNumber || 0, seasonSeriesId, libraryId, libraryName)
        } else {
            console.log("[Home] Unknown item type:", item.Type)
        }
    }

    function extractYear(dateString) {
        if (!dateString)
            return ""
        var d = new Date(dateString)
        if (isNaN(d.getTime()))
            return ""
        return d.getFullYear()
    }
    
    // Connections
    Connections {
        target: LibraryService
        
        function onViewsLoaded(views) {
            var orderedViews = orderLibraries(views)
            librariesModel = orderedViews
            if (orderedViews.length > 0) {
                if (myMediaList.currentIndex < 0) {
                    myMediaList.currentIndex = 0
                }
                // Only apply default My Media focus on initial screen activation.
                // Returning from detail screens should use restoreFocusState().
                if (!hasBeenActivated && lastFocusedSection === "myMedia" && root.StackView.status === StackView.Active) {
                    Qt.callLater(function() {
                        if (!hasBeenActivated && lastFocusedSection === "myMedia" && root.StackView.status === StackView.Active) {
                            myMediaList.forceActiveFocus()
                            root.ensureSectionVisible(myMediaSection)
                        }
                    })
                }
            }
            
            // Fetch Next Up
            LibraryService.getNextUp()
            
            // Fetch recently added for each library
            for (var i = 0; i < orderedViews.length; i++) {
                LibraryService.getLatestMedia(orderedViews[i].Id)
            }

            // Fetch a larger random pool from all available media for backdrop rotation.
            // Phase 1: fast randomized starter sample so first backdrop appears quickly.
            loadingGlobalBackdropPool = true
            globalBackdropPoolLoaded = false
            useSectionBackdropFallback = false
            fullBackdropIndexRequested = false
            backdropCandidates = []
            root.invalidateBackdropShuffle()
            globalBackdropTimeout.restart()
            LibraryService.getHomeBackdropItems(80)
            // Phase 2: build full index in background without blocking initial paint.
            fullBackdropIndexTimer.restart()
        }
        
        function onNextUpLoaded(items) {
            nextUpModel = items
            
            // Use section fallback only if global pool is unavailable.
            if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
                for (var i = 0; i < items.length; i++) {
                    root.addBackdropCandidate(items[i])
                }
            }
            
            // Restore focus if we had focus in Next Up section AND we're still the active screen
            Qt.callLater(function() {
                if (lastFocusedSection === "nextUp" && root.StackView.status === StackView.Active) {
                    restoreFocusState()
                }
            })
        }
        
        function onLatestMediaLoaded(parentId, items) {
            // Update map safely to trigger bindings without destroying delegates
            var newMap = {}
            for (var key in recentlyAddedMap) {
                newMap[key] = recentlyAddedMap[key]
            }
            newMap[parentId] = items
            recentlyAddedMap = newMap
            
            // Use section fallback only if global pool is unavailable.
            if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
                for (var i = 0; i < items.length; i++) {
                    root.addBackdropCandidate(items[i])
                }
            }
            
            // Note: We removed the aggressive recentlyAddedRepeater.model = null reset
            // because updating recentlyAddedMap (with a new object reference) is sufficient 
            // for the inner ListViews to update their 'items' binding.
        }

        function onHomeBackdropItemsLoaded(items) {
            globalBackdropPoolLoaded = true
            loadingGlobalBackdropPool = false
            globalBackdropTimeout.stop()
            console.log("[HomeBackdrop] global items received:", items ? items.length : 0)
            for (var i = 0; i < items.length; i++) {
                root.addBackdropCandidate(items[i])
            }
            console.log("[HomeBackdrop] candidates built:", backdropCandidates.length)
            if (backdropCandidates.length === 0)
                return
            useSectionBackdropFallback = false
            // Force immediate reselection from the full global pool.
            if (currentBackdropUrl === "") {
                root.invalidateBackdropShuffle()
                root.selectRandomBackdrop()
            }
        }
        
        function onErrorOccurred(endpoint, error) {
            console.error("Error in " + endpoint + ": " + error)
            if (endpoint === "getHomeBackdropItems") {
                loadingGlobalBackdropPool = false
                useSectionBackdropFallback = true
                root.hydrateBackdropFallbackFromSections()
            }
        }
    }

    Connections {
        target: SidebarSettings
        function onLibraryOrderChanged() {
            librariesModel = orderLibraries(librariesModel)
        }
    }
    
    // Refresh dynamic content when playback stops
    // This updates Next Up and Recently Added with new watch progress
    Connections {
        target: PlayerController
        function onPlaybackStopped() {
            // Small delay to allow server to process the playback report
            refreshTimer.start()
        }
    }
    
    Timer {
        id: refreshTimer
        interval: 500  // 500ms delay for server to process playback report
        repeat: false
        onTriggered: root.refreshDynamicContent()
    }

    Timer {
        id: globalBackdropTimeout
        interval: 6000
        repeat: false
        onTriggered: {
            if (!globalBackdropPoolLoaded) {
                console.log("[HomeBackdrop] global pool timeout; enabling section fallback")
                loadingGlobalBackdropPool = false
                useSectionBackdropFallback = true
                root.hydrateBackdropFallbackFromSections()
            }
        }
    }

    Timer {
        id: fullBackdropIndexTimer
        interval: 300
        repeat: false
        onTriggered: {
            if (fullBackdropIndexRequested)
                return
            fullBackdropIndexRequested = true
            LibraryService.getHomeBackdropItems()
        }
    }
    
    Component.onCompleted: {
        reseedBackdropRng()
        if (homeBackdropSettings.lastBackdropUrl !== "") {
            currentBackdropUrl = homeBackdropSettings.lastBackdropUrl
        }
        console.log("[FocusDebug] HomeScreen completed, requesting initial views")
        LibraryService.getViews()
    }
}
