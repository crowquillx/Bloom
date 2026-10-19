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
    property bool librariesLoading: true
    property string librariesError: ""
    property var nextUpModel: []
    property var continueWatchingModel: []
    property var displayedNextUpModel: []
    property var recentlyAddedMap: ({}) // Map of libraryId -> items array
    // Shelf spacing and sizing. Landscape cards (Continue Watching, Next Up,
    // My Media) show two thirds as many items as poster rows so artwork stays
    // large from across a room; both grow with ultrawide viewports.
    readonly property int cardSpacing: Math.round(24 * Theme.layoutScale)
    readonly property int landscapeVisibleItems: Math.max(3, Math.round(Theme.homeRowVisibleItems * 2 / 3))
    property int homeCardWidth: Math.round(Math.max(0, root.width - Theme.pageMarginX * 2 - cardSpacing * (landscapeVisibleItems - 1)) / landscapeVisibleItems)
    readonly property int myMediaTileHeight: Math.round(108 * Theme.layoutScale)
    // Force 16:9 aspect to reduce image crop vs. server thumbs
    property int homeCardHeight: Math.round(homeCardWidth * 9 / 16)
    // Request higher-resolution images for the scaled cards; never below the
    // physical card width at high OS display scaling.
    property int homeCardImageRequestWidth: Math.round(homeCardWidth * Math.max(2, Screen.devicePixelRatio))
    // Recently Added poster sizing â same column count as home cards
    property int recentlyAddedPosterWidth: Math.round(Math.max(0, root.width - Theme.pageMarginX * 2 - cardSpacing * (Theme.homeRowVisibleItems - 1)) / Theme.homeRowVisibleItems)
    property int recentlyAddedPosterHeight: Math.round(recentlyAddedPosterWidth * 1.5)

    // Smooth transitions when window resizes change card sizes
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
    property var backdropCandidates: []  // Array of canonical backdrop ArtworkRef maps
    property string currentBackdropUrl: ""
    property var backdropShuffleOrder: []
    property int backdropShuffleCursor: 0
    property int backdropRngState: 1
    property bool globalBackdropPoolLoaded: false
    property bool loadingGlobalBackdropPool: false
    property bool useSectionBackdropFallback: false
    property bool fullBackdropIndexRequested: false
    readonly property bool heroBackdropActive: ConfigManager.heroBannerEnabled
                                                && ConfigManager.heroBannerBackdropSyncEnabled
                                                && heroBanner.visible && heroBanner.currentBackdropUrl !== ""

    HeroBannerProvider {
        id: heroProvider
        librariesModel: root.librariesModel
        recentlyAddedMap: root.recentlyAddedMap
        nextUpModel: root.nextUpModel
        continueWatchingModel: root.continueWatchingModel
        displayedNextUpModel: root.displayedNextUpModel
    }

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
            LibraryService.getLatestMedia(librariesModel[i].itemId)
        }
    }

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId)
            return ""
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 0)
    }

    function acceptsConnectionResponse(connectionId) {
        const activeConnectionId = LibraryService.getActiveConnectionId()
        return activeConnectionId !== "" && connectionId === activeConnectionId
    }

    function hydrateBackdropFallbackFromSections() {
        var fallbackItems = continueWatchingModel.concat(displayedNextUpModel)
        for (var key in recentlyAddedMap) {
            fallbackItems = fallbackItems.concat(recentlyAddedMap[key] || [])
        }
        addBackdropCandidates(fallbackItems)
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
            byId[views[i].itemId] = views[i]
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

    function getRecentlyAddedDelegateMeta(index) {
        var item = recentlyAddedRepeater.itemAt(index)
        return {
            item: item || null,
            recentlyAddedListRef: item ? (item["recentlyAddedListRef"] || null) : null,
            libraryId: item ? (item["libraryId"] || "") : ""
        }
    }

    function focusFirstVisibleRecentlyAddedList() {
        for (var i = 0; i < recentlyAddedRepeater.count; i++) {
            var meta = getRecentlyAddedDelegateMeta(i)
            if (meta.item && meta.item.visible && meta.recentlyAddedListRef) {
                meta.recentlyAddedListRef.forceActiveFocus()
                return true
            }
        }
        return false
    }

    function updateDisplayedNextUpModel() {
        var inProgress = []
        var nextItems = []
        for (var i = 0; i < nextUpModel.length; i++) {
            var episode = nextUpModel[i]
            var pos = episode ? (episode.positionMs || 0) : 0
            if (pos > 0) inProgress.push(episode)
            else nextItems.push(episode)
        }
        continueWatchingModel = inProgress
        displayedNextUpModel = ConfigManager.mergeContinueWatchingWithNextUp ? nextUpModel : nextItems
        heroProvider.rebuild()
    }

    Connections {
        target: ConfigManager
        function onHeroBannerSourceChanged() { heroProvider.rebuild() }
        function onHeroBannerMaxItemsChanged() { heroProvider.rebuild() }
        function onHeroBannerHiddenItemTypesChanged() { heroProvider.rebuild() }
        function onHeroBannerLibraryUnwatchedOnlyChanged() { heroProvider.rebuild() }
        function onHeroBannerLibraryIdsChanged() { heroProvider.rebuild() }
    }

    
    // Function to save current focus state before navigating away
    function saveFocusState() {
        var section = ""
        var index = 0
        
        // Check which list has active focus
        if (heroBanner.activeFocus || heroBanner.buttonsFocused) {
            section = "hero"
            index = heroBanner.currentIndex
        } else if (myMediaList.activeFocus) {
            section = "myMedia"
            index = myMediaList.currentIndex
        } else if (continueWatchingList.activeFocus) {
            section = "continueWatching"
            index = continueWatchingList.currentIndex
        } else if (nextUpList.activeFocus) {
            section = "nextUp"
            index = nextUpList.currentIndex
        } else {
            // Check recentlyAdded lists
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var focusMeta = getRecentlyAddedDelegateMeta(i)
                if (focusMeta.item && focusMeta.recentlyAddedListRef && focusMeta.recentlyAddedListRef.activeFocus) {
                    section = "recentlyAdded:" + focusMeta.libraryId
                    index = focusMeta.recentlyAddedListRef.currentIndex
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
        
        if (lastFocusedSection === "hero" && heroBanner.visible) {
            heroBanner.currentIndex = Math.min(lastFocusedIndex, Math.max(0, heroProvider.heroModel.length - 1))
            ensureSectionVisible(heroBanner)
            Qt.callLater(function() {
                heroBanner.resetToCarousel()
                heroBanner.forceActiveFocus()
            })
            return
        } else if (lastFocusedSection === "myMedia") {
            targetList = myMediaList
            targetSection = myMediaSection
        } else if (lastFocusedSection === "continueWatching") {
            targetList = continueWatchingList
            targetSection = continueWatchingSection
        } else if (lastFocusedSection === "nextUp") {
            targetList = nextUpList
            targetSection = nextUpSection
        } else if (lastFocusedSection.indexOf("recentlyAdded:") === 0) {
            var libraryId = lastFocusedSection.substring("recentlyAdded:".length)
            for (var i = 0; i < recentlyAddedRepeater.count; i++) {
                var restoreMeta = getRecentlyAddedDelegateMeta(i)
                if (restoreMeta.item && restoreMeta.libraryId === libraryId && restoreMeta.recentlyAddedListRef) {
                    targetList = restoreMeta.recentlyAddedListRef
                    targetSection = restoreMeta.item
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
        running: backdropCandidates.length > 1 && !PlayerController.isPlaybackActive && !root.heroBackdropActive
        onTriggered: selectRandomBackdrop()
    }
    
    function backdropCandidateKey(candidate) {
        return (candidate.connectionId || "") + "\n"
                + (candidate.itemId || "") + "\n"
                + (candidate.index || 0) + "\n"
                + (candidate.tag || "")
    }

    // Collect canonical backdrop refs in one model update to avoid repeated array copies.
    function addBackdropCandidates(items) {
        if (!items || items.length === 0)
            return

        const candidates = backdropCandidates.slice()
        const seen = ({})
        for (let i = 0; i < candidates.length; i++) {
            seen[backdropCandidateKey(candidates[i])] = true
        }

        const wasEmpty = candidates.length === 0
        for (let j = 0; j < items.length; j++) {
            const item = items[j]
            const art = item ? item.backdropArtwork : null
            if (!art || !art.itemId)
                continue

            const candidate = Object.assign({}, art, {
                connectionId: art.connectionId || item.connectionId || "",
                kind: art.kind || "backdrop"
            })
            const key = backdropCandidateKey(candidate)
            if (!seen[key]) {
                seen[key] = true
                candidates.push(candidate)
            }
        }

        if (candidates.length === backdropCandidates.length)
            return
        backdropCandidates = candidates
        invalidateBackdropShuffle()

        if (wasEmpty && !loadingGlobalBackdropPool) {
            selectRandomBackdrop()
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
        
        // Construct backdrop URL from canonical ArtworkRef fields
        var url = LibraryService.getCachedArtworkUrlFromRef(candidate, 1920)
        
        currentBackdropUrl = url
    }
    
    // Backdrop Container with Cross-fade
    // Full-bleed: extends under the floating navigation rail to the window edge.
    Rectangle {
        id: backdropContainer
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        z: -1
        color: "transparent"
        clip: true
        // "Floating cinema": when the hero drives the backdrop it is shown sharp
        // and the scrims hug the content; otherwise the rotating backdrop is a
        // soft blurred wash.
        readonly property bool cinema: root.heroBackdropActive
        readonly property real blurAmount: cinema ? 0.0 : 1.0

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
                blurEnabled: backdropContainer.blurAmount > 0
                blur: backdropContainer.blurAmount
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
                blurEnabled: backdropContainer.blurAmount > 0
                blur: backdropContainer.blurAmount
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

        // Blurred wash: uniform page scrim for readability.
        Rectangle {
            anchors.fill: parent
            z: 1
            visible: !backdropContainer.cinema
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.gradientOverlayStart }
                GradientStop { position: 0.4; color: Theme.gradientOverlayMiddle }
                GradientStop { position: 1.0; color: Theme.gradientOverlayEnd }
            }
        }

        // Cinema: a leading-edge scrim behind the hero copy...
        Rectangle {
            anchors.fill: parent
            z: 1
            visible: backdropContainer.cinema
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.94) }
                GradientStop { position: 0.30; color: Theme.withAlpha(Theme.backgroundPrimary, 0.80) }
                GradientStop { position: 0.55; color: Theme.withAlpha(Theme.backgroundPrimary, 0.30) }
                GradientStop { position: 0.80; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
            }
        }

        // ...and a floor that settles the shelves onto the theme background.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.52
            z: 1
            visible: backdropContainer.cinema
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
                GradientStop { position: 0.5; color: Theme.withAlpha(Theme.backgroundPrimary, 0.62) }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.96) }
            }
        }

        // As the page scrolls past the hero, quiet the artwork further so
        // shelves keep their contrast.
        Rectangle {
            anchors.fill: parent
            z: 1
            visible: backdropContainer.cinema && opacity > 0
            color: Theme.backgroundPrimary
            opacity: Math.min(0.7, mainFlickable.contentY / Math.max(1, heroBanner.height) * 0.7)
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
        anchors.leftMargin: -Theme.navRailReservedWidth
        z: -2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundPrimary }
            GradientStop { position: 1.0; color: Theme.backgroundSecondary }
        }
    }

    // Landscape card for Continue Watching / Up Next: artwork with the title
    // set on a bottom scrim and progress along the lower edge.
    Component {
        id: landscapeCardDelegate

        Item {
            id: nextUpDelegate
            width: homeCardWidth
            height: homeCardHeight
            property bool isFocused: ListView.view.currentIndex === index && ListView.view.activeFocus
            property bool isHovered: InputModeManager.pointerActive && nextUpMouseArea.containsMouse
            scale: isFocused ? Theme.focusScale : 1.0
            z: isFocused ? 1 : 0
            Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

            readonly property string titleText: modelData.seriesName || modelData.name || ""
            readonly property string subtitleText: {
                var txt = ""
                if (modelData.parentIndexNumber >= 0 && modelData.indexNumber >= 0) {
                    txt = "S" + modelData.parentIndexNumber + " E" + modelData.indexNumber
                    if (modelData.name) txt += "  \u00b7  " + modelData.name
                } else if (modelData.name && modelData.seriesName) {
                    txt = modelData.name
                }
                return txt
            }

            ArtworkPlaceholder {
                anchors.fill: parent
                visible: nextUpImage.status !== Image.Ready
                loading: nextUpImage.status === Image.Loading
                iconGlyph: modelData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
            }

            Image {
                id: nextUpImage
                property string seriesThumbUrl: root.artworkUrlFromRef(modelData.seriesThumbArtwork, homeCardImageRequestWidth)
                property string parentThumbUrl: root.artworkUrlFromRef(modelData.parentThumbArtwork, homeCardImageRequestWidth)
                property string episodeThumbUrl: root.artworkUrlFromRef(modelData.thumbArtwork, homeCardImageRequestWidth)
                property string episodePrimaryUrl: root.artworkUrlFromRef(modelData.primaryArtwork, homeCardImageRequestWidth)
                property var artworkCandidates: {
                    const candidates = [seriesThumbUrl, parentThumbUrl,
                                        episodeThumbUrl, episodePrimaryUrl]
                    return candidates.filter(function(url) { return !!url })
                }
                property int artworkCandidateIndex: 0
                anchors.fill: parent
                source: artworkCandidates.length > 0
                        ? artworkCandidates[Math.min(artworkCandidateIndex,
                                                     artworkCandidates.length - 1)]
                        : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                visible: status === Image.Ready

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: nextUpMask
                }

                onArtworkCandidatesChanged: artworkCandidateIndex = 0
                onStatusChanged: {
                    if (status === Image.Error
                            && artworkCandidateIndex + 1 < artworkCandidates.length) {
                        artworkCandidateIndex++
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

            // Bottom scrim keeps the overlaid title legible on any artwork.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height * 0.62
                radius: Theme.imageRadius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0) }
                    GradientStop { position: 0.55; color: Theme.withAlpha(Theme.overlayTextBackground, 0.55) }
                    GradientStop { position: 1.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0.82) }
                }
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: progressBar.visible ? progressBar.top : parent.bottom
                anchors.leftMargin: Theme.spacingMedium + Theme.spacingXSmall
                anchors.rightMargin: Theme.spacingMedium + Theme.spacingXSmall
                anchors.bottomMargin: progressBar.visible ? -Theme.spacingXSmall : Theme.spacingMedium
                spacing: Math.round(2 * Theme.layoutScale)

                Text {
                    width: parent.width
                    text: nextUpDelegate.titleText
                    font.pixelSize: Theme.fontSizeMedium
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textOnDarkOverlay
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    width: parent.width
                    visible: text !== ""
                    text: nextUpDelegate.subtitleText
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightMedium
                    color: Theme.withAlpha(Theme.textOnDarkOverlay, 0.78)
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            MediaProgressBar {
                id: progressBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                barMargin: Theme.spacingMedium
                positionMs: modelData.positionMs || 0
                durationMs: modelData.durationMs || 0
            }

            FocusRing {
                active: isFocused
                radius: Theme.imageRadius
            }

            MouseArea {
                id: nextUpMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    nextUpDelegate.ListView.view.currentIndex = index
                    nextUpDelegate.ListView.view.forceActiveFocus()
                    handleNextUpSelection(modelData)
                }
            }

            Accessible.role: Accessible.Button
            Accessible.name: (modelData.seriesName ? modelData.seriesName + ", " : "") + (modelData.name || "")
            Accessible.description: "Continue watching " + (modelData.seriesName || modelData.name || "")
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
            spacing: Theme.sectionSpacing

            // Top spacing
            Item { Layout.preferredHeight: heroBanner.cinematic && heroBanner.visible ? 0 : Theme.pageMarginTop }

            HeroBanner {
                id: heroBanner
                Layout.fillWidth: true
                // Cinema mode gives the hero most of the first screen and lets
                // My Media and the first shelf peek in below it.
                Layout.preferredHeight: cinematic ? Math.round(root.height * 0.60) : implicitHeight
                Layout.bottomMargin: cinematic ? -Theme.spacingLarge : 0
                cinematic: ConfigManager.heroBannerBackdropSyncEnabled
                visible: ConfigManager.heroBannerEnabled && heroProvider.hasContent
                heroModel: heroProvider.heroModel
                scrolling: mainFlickable.moving
                onCurrentBackdropUrlChanged: {
                    if (root.heroBackdropActive) root.currentBackdropUrl = currentBackdropUrl
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.lastFocusedSection = "hero"
                        root.ensureSectionVisible(heroBanner)
                    } else {
                        heroBanner.resetToCarousel()
                    }
                }
                onMoveDownRequested: myMediaList.forceActiveFocus()
                onDetailsRequested: function(item) { root.handleHeroSelection(item) }
                onPlayRequested: function(item) { root.playHeroItem(item) }
            }
            
            // My Media Section
            ColumnLayout {
                id: myMediaSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                
                SectionHeader {
                    text: qsTr("My Media")
                    level: "group"
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                
                ListView {
                    id: myMediaList
                    Layout.fillWidth: true
                    Layout.preferredHeight: myMediaTileHeight + Theme.spacingLarge
                    orientation: ListView.Horizontal
                    spacing: root.cardSpacing
                    leftMargin: Theme.pageMarginX
                    rightMargin: Theme.pageMarginX
                    topMargin: Theme.spacingLarge / 2
                    clip: false
                    focus: true
                    preferredHighlightBegin: Theme.pageMarginX
                    preferredHighlightEnd: width - Theme.pageMarginX
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
                }
                populate: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                }
                remove: Transition {
                    NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                }
                    
                    model: librariesModel
                    
                    // Glass library tile: collection icon and name. Library artwork is
                    // not used here because server-generated library images usually
                    // contain the library name already.
                    delegate: Item {
                        width: homeCardWidth
                        height: myMediaTileHeight
                        property bool isFocused: myMediaList.currentIndex === index && myMediaList.activeFocus
                        property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
                        scale: isFocused ? Theme.focusScale : 1.0
                        z: isFocused ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                        Rectangle {
                            id: tileSurface
                            anchors.fill: parent
                            radius: Theme.radiusLarge
                            color: isFocused || isHovered ? Theme.surfaceGlassStrong : Theme.surfaceGlass
                            border.width: Theme.borderWidth
                            border.color: isHovered ? Theme.surfaceBorderStrong : Theme.surfaceBorder
                            antialiasing: true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingLarge
                                anchors.rightMargin: Theme.spacingLarge
                                spacing: Theme.spacingMedium

                                Text {
                                    text: {
                                        var type = String(modelData.collectionType || "").toLowerCase()
                                        if (type === "tvshows") return Icons.tvShows
                                        if (type === "movies") return Icons.movie
                                        if (type === "music") return Icons.music
                                        if (type === "boxsets") return Icons.videoLibrary
                                        if (type === "homevideos" || type === "photos") return Icons.photo
                                        return Icons.folder
                                    }
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Theme.iconSizeNav
                                    color: isFocused ? Theme.textPrimary : Theme.textSecondary
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Text {
                                    text: modelData.name || ""
                                    font.pixelSize: Theme.fontSizeMedium
                                    font.family: Theme.fontPrimary
                                    font.weight: Theme.fontWeightStrong
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }

                            FocusRing {
                                active: isFocused
                                radius: tileSurface.radius
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
                        Accessible.name: modelData.name
                        Accessible.description: "Browse " + modelData.name + " library"
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
                    Keys.onUpPressed: {
                        if (heroBanner.visible) {
                            heroBanner.resetToCarousel()
                            heroBanner.forceActiveFocus()
                        }
                    }
                    Keys.onDownPressed: {
                        if (continueWatchingSection.visible) {
                            console.log("[FocusDebug] myMediaList Down -> continueWatchingList")
                            continueWatchingList.forceActiveFocus()
                        } else if (displayedNextUpModel.length > 0) {
                            console.log("[FocusDebug] myMediaList Down -> nextUpList")
                            nextUpList.forceActiveFocus()
                        } else if (recentlyAddedRepeater.count > 0) {
                            focusFirstVisibleRecentlyAddedList()
                        }
                    }

                    onCurrentIndexChanged: root.ensureListItemVisible(myMediaList)
                    onActiveFocusChanged: if (activeFocus) root.ensureSectionVisible(myMediaSection)
                }
            }
            
            ColumnLayout {
                id: continueWatchingSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: continueWatchingModel.length > 0 && !ConfigManager.mergeContinueWatchingWithNextUp

                SectionHeader {
                    text: qsTr("Continue Watching")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ListView {
                    id: continueWatchingList
                    Layout.fillWidth: true
                    Layout.preferredHeight: homeCardHeight + Theme.spacingLarge
                    orientation: ListView.Horizontal
                    spacing: root.cardSpacing
                    leftMargin: Theme.pageMarginX
                    rightMargin: Theme.pageMarginX
                    topMargin: Theme.spacingLarge / 2
                    clip: false
                    preferredHighlightBegin: Theme.pageMarginX
                    preferredHighlightEnd: width - Theme.pageMarginX
                    highlightRangeMode: ListView.StrictlyEnforceRange
                    highlightMoveDuration: 0
                    model: continueWatchingModel
                    delegate: landscapeCardDelegate
                    Keys.onUpPressed: myMediaList.forceActiveFocus()
                    Keys.onDownPressed: {
                        if (displayedNextUpModel.length > 0) nextUpList.forceActiveFocus()
                        else focusFirstVisibleRecentlyAddedList()
                    }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < continueWatchingModel.length) {
                            handleNextUpSelection(continueWatchingModel[currentIndex])
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < continueWatchingModel.length) {
                            handleNextUpSelection(continueWatchingModel[currentIndex])
                        }
                    }
                    onCurrentIndexChanged: root.ensureListItemVisible(continueWatchingList)
                    onActiveFocusChanged: if (activeFocus) root.ensureSectionVisible(continueWatchingSection)
                }
            }

            // Next Up Section
            ColumnLayout {
                id: nextUpSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: displayedNextUpModel.length > 0
                
                SectionHeader {
                    text: qsTr("Up Next")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                
                ListView {
                    id: nextUpList
                    Layout.fillWidth: true
                    Layout.preferredHeight: homeCardHeight + Theme.spacingLarge
                    orientation: ListView.Horizontal
                    spacing: root.cardSpacing
                    leftMargin: Theme.pageMarginX
                    rightMargin: Theme.pageMarginX
                    topMargin: Theme.spacingLarge / 2
                    clip: false
                    preferredHighlightBegin: Theme.pageMarginX
                    preferredHighlightEnd: width - Theme.pageMarginX
                    highlightRangeMode: ListView.StrictlyEnforceRange
                    highlightMoveDuration: 0

                    add: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    }
                    populate: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                    }
                    addDisplaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    move: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    remove: Transition {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
                    }
                    removeDisplaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    displaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                    }
                    
                    model: displayedNextUpModel
                    delegate: landscapeCardDelegate
                    
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < displayedNextUpModel.length) {
                            handleNextUpSelection(displayedNextUpModel[currentIndex])
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < displayedNextUpModel.length) {
                            handleNextUpSelection(displayedNextUpModel[currentIndex])
                        }
                    }
                    Keys.onUpPressed: {
                        if (continueWatchingSection.visible) continueWatchingList.forceActiveFocus()
                        else myMediaList.forceActiveFocus()
                    }
                    Keys.onDownPressed: {
                        if (recentlyAddedRepeater.count > 0) {
                            focusFirstVisibleRecentlyAddedList()
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
                spacing: Theme.sectionSpacing
                
                Repeater {
                    id: recentlyAddedRepeater
                    model: librariesModel
                    
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sectionTitleSpacing
                        
                        property var items: recentlyAddedMap[modelData.itemId] || []
                        property string libraryId: modelData.itemId
                        property var recentlyAddedListRef: recentlyAddedList
                        visible: items.length > 0
                        
                        SectionHeader {
                            text: qsTr("Recently Added")
                            detail: modelData.name || ""
                            Layout.leftMargin: Theme.pageMarginX
                            Layout.rightMargin: Theme.pageMarginX
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Heading
                            Accessible.name: text + " - " + detail
                        }
                        
                        ListView {
                            id: recentlyAddedList
                            Layout.fillWidth: true
                            Layout.preferredHeight: recentlyAddedPosterHeight + Math.round(120 * Theme.layoutScale)
                            orientation: ListView.Horizontal
                            spacing: root.cardSpacing
                            leftMargin: Theme.pageMarginX
                            rightMargin: Theme.pageMarginX
                            topMargin: Theme.spacingLarge / 2
                            clip: false
                            preferredHighlightBegin: Theme.pageMarginX
                            preferredHighlightEnd: width - Theme.pageMarginX
                            highlightRangeMode: ListView.StrictlyEnforceRange
                            highlightMoveDuration: 0
                            
                            add: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                            }
                            populate: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutQuad }
                            }
                            addDisplaced: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            move: Transition {
                                NumberAnimation { properties: "x,y"; duration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0; easing.type: Easing.OutCubic }
                            }
                            remove: Transition {
                                NumberAnimation { property: "opacity"; to: 0; duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0; easing.type: Easing.OutQuad }
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
                                height: recentlyAddedPosterHeight + Math.round(100 * Theme.layoutScale)
                                property bool isFocused: recentlyAddedList.currentIndex === index && recentlyAddedList.activeFocus
                                property bool isHovered: InputModeManager.pointerActive && recentMouseArea.containsMouse
                                readonly property string titleText: modelData.mediaType === "Episode"
                                                                    ? (modelData.seriesName || modelData.name || "")
                                                                    : (modelData.name || "")

                                Column {
                                    anchors.fill: parent
                                    spacing: Theme.spacingMedium

                                    // Poster image (2:3 aspect ratio) with rounded corners
                                    Item {
                                        id: recentImageContainer
                                        width: recentlyAddedPosterWidth
                                        height: recentlyAddedPosterHeight  // 2:3 poster aspect ratio
                                        scale: isFocused ? Theme.focusScale : 1.0
                                        z: isFocused ? 1 : 0
                                        Behavior on scale { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }

                                        ArtworkPlaceholder {
                                            anchors.fill: parent
                                            visible: recentCoverArt.status !== Image.Ready
                                            loading: recentCoverArt.status === Image.Loading
                                            iconGlyph: modelData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
                                            title: recentlyAddedDelegate.titleText
                                        }
                                        
                                        Image {
                                            id: recentCoverArt
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                            visible: status === Image.Ready
                                            property string seriesPrimaryUrl:
                                                modelData.mediaType === "Episode"
                                                ? root.artworkUrlFromRef(modelData.seriesPrimaryArtwork, 640)
                                                : ""
                                            property string itemPrimaryUrl:
                                                root.artworkUrlFromRef(modelData.primaryArtwork, 640)
                                            property var artworkCandidates: {
                                                const candidates = [seriesPrimaryUrl, itemPrimaryUrl]
                                                return candidates.filter(function(url) { return !!url })
                                            }
                                            property int artworkCandidateIndex: 0
                                            source: artworkCandidates.length > 0
                                                    ? artworkCandidates[Math.min(artworkCandidateIndex,
                                                                                 artworkCandidates.length - 1)]
                                                    : ""

                                            layer.enabled: true
                                            layer.effect: MultiEffect {
                                                maskEnabled: true
                                                maskSource: recentMask
                                            }

                                            onArtworkCandidatesChanged: artworkCandidateIndex = 0
                                            onStatusChanged: {
                                                if (status === Image.Error
                                                        && artworkCandidateIndex + 1 < artworkCandidates.length) {
                                                    artworkCandidateIndex++
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

                                        FocusRing {
                                            active: isFocused
                                            radius: Theme.imageRadius
                                        }

                                        // Unwatched episode count badge (for Series only, not individual Episodes)
                                        UnwatchedBadge {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            parentWidth: parent.width
                                            count: (modelData.mediaType === "Series")
                                                   ? (modelData.unplayedItemCount || 0) : 0
                                            isFullyWatched: (modelData.mediaType === "Series")
                                                            ? (modelData.watched || false) : false
                                            visible: modelData.mediaType === "Series"
                                        }

                                        // Watched checkmark (for Movies only)
                                        UnwatchedBadge {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            parentWidth: parent.width
                                            count: 0
                                            isFullyWatched: modelData.watched || false
                                            visible: modelData.mediaType === "Movie" &&
                                                     modelData.watched
                                        }
                                    }

                                    // Text container below the image
                                    Column {
                                        width: parent.width
                                        spacing: Theme.spacingXSmall

                                        // For episodes: seriesName, For series/movies: name
                                        Text {
                                            width: parent.width
                                            text: recentlyAddedDelegate.titleText
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.family: Theme.fontPrimary
                                            font.weight: Theme.fontWeightStrong
                                            color: Theme.textPrimary
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }

                                        Text {
                                            width: parent.width
                                            text: {
                                                if (modelData.mediaType === "Episode") {
                                                    var txt = ""
                                                    if (modelData.parentIndexNumber >= 0 && modelData.indexNumber >= 0) {
                                                        txt = "S" + modelData.parentIndexNumber + ":E" + modelData.indexNumber
                                                        if (modelData.name) txt += " - " + modelData.name
                                                    } else if (modelData.name) {
                                                        txt = modelData.name
                                                    }
                                                    return txt
                                                } else if (modelData.mediaType === "Series") {
                                                    var startYear = modelData.productionYear || extractYear(modelData.premiereDate)
                                                    if (!startYear) return ""
                                                    if (modelData.status === "Continuing" || !modelData.endDate) {
                                                        return startYear + " - Present"
                                                    } else {
                                                        var endYear = extractYear(modelData.endDate)
                                                        if (endYear && endYear !== startYear) {
                                                            return startYear + " - " + endYear
                                                        }
                                                        return startYear.toString()
                                                    }
                                                } else if (modelData.mediaType === "Movie") {
                                                    var year = modelData.productionYear || extractYear(modelData.premiereDate)
                                                    return year ? year.toString() : ""
                                                }
                                                return ""
                                            }
                                            font.pixelSize: Theme.fontSizeCaption
                                            font.family: Theme.fontPrimary
                                            color: Theme.textSecondary
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
                                Accessible.name: modelData.name
                                Accessible.description: "Details for " + modelData.name
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
                                    if (displayedNextUpModel.length > 0) nextUpList.forceActiveFocus()
                                    else if (continueWatchingSection.visible) continueWatchingList.forceActiveFocus()
                                    else myMediaList.forceActiveFocus()
                                } else {
                                    for (var i = libraryIndex - 1; i >= 0; i--) {
                                        var previousMeta = getRecentlyAddedDelegateMeta(i)
                                        if (previousMeta.item && previousMeta.item.visible && previousMeta.recentlyAddedListRef) {
                                            previousMeta.recentlyAddedListRef.forceActiveFocus()
                                            break
                                        }
                                    }
                                }
                            }
                            Keys.onDownPressed: {
                                for (var i = libraryIndex + 1; i < recentlyAddedRepeater.count; i++) {
                                    var followingMeta = getRecentlyAddedDelegateMeta(i)
                                    if (followingMeta.item && followingMeta.item.visible && followingMeta.recentlyAddedListRef) {
                                        followingMeta.recentlyAddedListRef.forceActiveFocus()
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
            Item { Layout.preferredHeight: Theme.paddingLarge }
        } // end mainColumn
    } // end Flickable

    WheelStepScroller {
        anchors.fill: mainFlickable
        target: mainFlickable
        // Home was too jumpy with proportional wheel handling; keep movement tighter.
        stepPx: (homeCardHeight + Theme.spacingLarge) * 0.45
        pixelDeltaMultiplier: 0.7
    }
    
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
    
    StateMessage {
        id: homeStateMessage
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.pageMarginX * 2, Math.round(760 * Theme.layoutScale))
        visible: root.librariesLoading || root.librariesModel.length === 0
        kind: root.librariesLoading ? "loading" : (root.librariesError !== "" ? "error" : "empty")
        title: root.librariesLoading ? ""
               : (root.librariesError !== "" ? qsTr("Could not load your libraries") : qsTr("No libraries yet"))
        message: root.librariesLoading ? qsTr("Loading your libraries\u2026")
                 : (root.librariesError !== "" ? root.librariesError
                                               : qsTr("No media libraries are available for this profile."))
        actionText: !root.librariesLoading && root.librariesError !== "" ? qsTr("Try Again") : ""
        onActionTriggered: {
            root.librariesLoading = true
            root.librariesError = ""
            LibraryService.getViews()
        }
        onVisibleChanged: if (visible && actionText !== "") Qt.callLater(focusAction)
        // Stays visible from loading to error, so visibility alone misses the first failure.
        onActionTextChanged: if (visible && actionText !== "") Qt.callLater(focusAction)
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
        console.log("Selected library: " + library.name)
        // Save focus state before navigating
        saveFocusState()
        // Emit signal to navigate to library screen
        navigateToLibrary(library.itemId, library.name)
    }
    
    function handleNextUpSelection(item) {
        if (root.StackView.status !== StackView.Active) {
            console.log("[Home] Ignoring Next Up selection while screen is not active")
            return
        }

        console.log("[Home] Next Up selected: " + item.name + " - navigating to episode details")
        
        // Save focus state before navigating
        saveFocusState()
        
        // Navigate to episode details view instead of playing directly
        var seriesId = item.seriesId || ""
        
        // Find the library for this series (needed for navigation context)
        // For Next Up items, we may not have the library ID directly, so we use an empty string
        // The LibraryScreen will handle this appropriately
        var libraryId = ""
        var libraryName = ""
        
        navigateToEpisode(item, seriesId, libraryId, libraryName)
    }

    function handleHeroSelection(item) {
        if (!item) return
        saveFocusState()
        const mediaType = item.mediaType || ""
        const itemId = item.itemId || ""
        const parentId = item.parentId || ""
        const seriesId = item.seriesId || ""
        if (mediaType === "Movie") navigateToMovie(item, parentId, "")
        else if (mediaType === "Series") navigateToSeries(itemId, parentId, "")
        else if (mediaType === "Episode") navigateToEpisode(item, seriesId, parentId, "")
    }

    function playHeroItem(item) {
        if (!item) return
        const mediaType = item.mediaType || ""
        if (mediaType === "Series") return
        const itemId = item.itemId || ""
        const seriesId = item.seriesId || ""
        const seasonId = item.parentId || ""
        const startPositionMs = Math.round(item.positionMs || 0)
        var overlayLogoUrl = root.artworkUrlFromRef(item.logoArtwork, 600)
        if (!overlayLogoUrl && seriesId) {
            overlayLogoUrl = LibraryService.getCachedArtworkUrlForConnection(
                        item.connectionId || "", seriesId, "logo", 0, "", 600)
        }
        PlayerController.requestPlayback({
            itemId: itemId,
            startPositionMs: startPositionMs,
            seriesId: seriesId,
            seasonId: seasonId,
            overlayTitle: item.seriesName || item.name || qsTr("Now Playing"),
            overlaySubtitle: mediaType === "Episode" ? (item.name || "") : "",
            overlayBackdropUrl: heroBanner.currentBackdropUrl,
            overlayLogoUrl: overlayLogoUrl,
            preferredAudioIndex: -2,
            preferredSubtitleIndex: -2,
            isMovie: mediaType === "Movie",
            allowVersionPrompt: true
        })
    }
    
    function handleRecentlyAddedSelection(item, libraryId) {
        console.log("[Home] Selected Recently Added:", item.name, "Type:", item.mediaType, "Library:", libraryId)
        
        // Save focus state before navigating
        saveFocusState()
        
        // Find the library name for this item
        var libraryName = ""
        for (var i = 0; i < librariesModel.length; i++) {
            if (librariesModel[i].itemId === libraryId) {
                libraryName = librariesModel[i].name
                break
            }
        }
        
        if (item.mediaType === "Movie") {
            // Navigate to movie details view
            console.log("[Home] Navigating to movie details")
            navigateToMovie(item, libraryId, libraryName)
        } else if (item.mediaType === "Episode") {
            // For episodes, check if there are other episodes from the same series in recently added
            var seriesId = item.seriesId || ""
            var items = recentlyAddedMap[libraryId] || []
            
            // Find all episodes from the same series in this recently added list
            var relatedEpisodes = []
            for (var j = 0; j < items.length; j++) {
                if (items[j].mediaType === "Episode" && items[j].seriesId === seriesId) {
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
                    var seasonNumber = relatedEpisodes[k].parentIndexNumber >= 0
                                       ? relatedEpisodes[k].parentIndexNumber : 0
                    var parentId = relatedEpisodes[k].parentId || ""
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
        } else if (item.mediaType === "Series") {
            // Navigate to series details
            console.log("[Home] Navigating to series details")
            navigateToSeries(item.itemId, libraryId, libraryName)
        } else if (item.mediaType === "Season") {
            // Navigate to season view
            var seasonSeriesId = item.seriesId || item.parentId || ""
            console.log("[Home] Navigating to season view")
            navigateToSeason(item.itemId, item.indexNumber >= 0 ? item.indexNumber : 0, seasonSeriesId, libraryId, libraryName)
        } else {
            console.log("[Home] Unknown item type:", item.mediaType)
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
        
        function onCanonicalViewsLoadedForConnection(connectionId, views) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            librariesLoading = false
            librariesError = ""
            var orderedViews = orderLibraries(views)
            librariesModel = orderedViews
            heroProvider.rebuild()
            if (orderedViews.length > 0) {
                if (myMediaList.currentIndex < 0) {
                    myMediaList.currentIndex = 0
                }
                // Only apply default My Media focus on initial screen activation.
                // Returning from detail screens should use restoreFocusState().
                if (!hasBeenActivated && lastFocusedSection === "myMedia" && root.StackView.status === StackView.Active) {
                    Qt.callLater(function() {
                        if (!hasBeenActivated && lastFocusedSection === "myMedia" && root.StackView.status === StackView.Active) {
                            if (heroBanner.visible) {
                                heroBanner.resetToCarousel()
                                heroBanner.forceActiveFocus()
                                root.ensureSectionVisible(heroBanner)
                            } else {
                                myMediaList.forceActiveFocus()
                                root.ensureSectionVisible(myMediaSection)
                            }
                        }
                    })
                }
            }
            
            // Fetch Next Up
            LibraryService.getNextUp()
            
            // Fetch recently added for each library
            for (var i = 0; i < orderedViews.length; i++) {
                LibraryService.getLatestMedia(orderedViews[i].itemId)
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
        
        function onCanonicalNextUpLoaded(connectionId, items) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            nextUpModel = items
            updateDisplayedNextUpModel()
            
            // Use section fallback only if global pool is unavailable.
            if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
                root.addBackdropCandidates(items)
            }
            
            // Restore focus if we had focus in Next Up section AND we're still the active screen
            Qt.callLater(function() {
                if ((lastFocusedSection === "nextUp" || lastFocusedSection === "continueWatching") && root.StackView.status === StackView.Active) {
                    restoreFocusState()
                }
            })
        }
        
        function onCanonicalLatestMediaLoaded(connectionId, parentId, items) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            // Update map safely to trigger bindings without destroying delegates
            var newMap = {}
            for (var key in recentlyAddedMap) {
                newMap[key] = recentlyAddedMap[key]
            }
            newMap[parentId] = items
            recentlyAddedMap = newMap
            if (ConfigManager.heroBannerSource !== "library") {
                heroProvider.rebuild()
            }
            
            // Use section fallback only if global pool is unavailable.
            if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
                root.addBackdropCandidates(items)
            }
            
            // Note: We removed the aggressive recentlyAddedRepeater.model = null reset
            // because updating recentlyAddedMap (with a new object reference) is sufficient 
            // for the inner ListViews to update their 'items' binding.
        }

        function onCanonicalHomeBackdropItemsLoaded(connectionId, items) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            globalBackdropPoolLoaded = true
            loadingGlobalBackdropPool = false
            globalBackdropTimeout.stop()
            console.log("[HomeBackdrop] global items received:", items ? items.length : 0)
            root.addBackdropCandidates(items)
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

        function onCanonicalHeroLibraryItemsLoaded(connectionId, items) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            heroProvider.handleHeroLibraryItemsLoaded(items)
        }

        function onCanonicalHeroLibraryItemsFailed(connectionId, error) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            console.error("Error in getHeroLibraryItems: " + error)
            heroProvider.handleHeroLibraryItemsFailed()
        }

        function onCanonicalHeroSeriesOverviewsLoaded(connectionId, overviewsBySeriesId) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            heroProvider.handleHeroSeriesOverviewsLoaded(connectionId, overviewsBySeriesId)
        }

        function onCanonicalHomeBackdropItemsFailed(connectionId, error) {
            if (!root.acceptsConnectionResponse(connectionId)) return
            console.error("Error in getHomeBackdropItems: " + error)
            loadingGlobalBackdropPool = false
            if (!globalBackdropPoolLoaded) {
                useSectionBackdropFallback = true
                root.hydrateBackdropFallbackFromSections()
            }
        }

        function onErrorOccurred(endpoint, error) {
            console.error("Error in " + endpoint + ": " + error)
            if (endpoint === "getViews") {
                librariesLoading = false
                librariesError = error
            }
        }
    }

    Connections {
        target: ConfigManager
        function onMergeContinueWatchingWithNextUpChanged() {
            updateDisplayedNextUpModel()
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
        librariesLoading = true
        librariesError = ""
        console.log("[FocusDebug] HomeScreen completed, requesting initial views")
        LibraryService.getViews()
        heroProvider.rebuild()
    }
}
