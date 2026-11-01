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
    // Continue Watching and Up Next, split and filtered in C++.
    readonly property var nextUpModel: shelves.items
    readonly property var continueWatchingModel: shelves.continueWatching
    readonly property var displayedNextUpModel: shelves.nextUp
    property var recentlyAddedMap: ({}) // Map of libraryKey(connectionId, libraryId) -> items array
    // Merged spaces read Home rows through MergedCatalog across every
    // enabled source; single spaces keep using LibraryService.
    readonly property bool mergedMode: typeof MergedCatalog !== "undefined" && MergedCatalog.active
    property bool mergedViewsRequested: false

    // Library IDs are only unique per connection (two users on one server
    // share them), so per-library rows key on both.
    function libraryKey(connectionId, libraryId) {
        return (connectionId || "") + "\n" + (libraryId || "")
    }

    function loadViews() {
        librariesLoading = true
        librariesError = ""
        // Card menus offer "Not interested" once the profile allows it.
        LibraryService.loadNotInterestedCapability()
        if (mergedMode) {
            mergedViewsRequested = true
            MergedCatalog.refreshViews()
        } else {
            LibraryService.getViews()
        }
    }

    function resetSpaceContent() {
        librariesModel = []
        HomeSectionsViewModel.clear()
        recentlyAddedMap = ({})
        shelves.clear()
        loadViews()
    }

    ContinueWatchingViewModel {
        id: shelves
        mergeRows: ConfigManager.mergeContinueWatchingWithNextUp
        onItemsChanged: heroProvider.rebuild()
        onRefreshRequested: root.refreshNextUp()
        onUndoOffered: function(message) { cardToast.showWithAction(message, qsTr("Undo"), root.undoHint()) }
        onActionFailed: function(message) { cardToast.show(message) }
    }

    SourceRouter {
        id: sourceRouter
    }
    // Shelf card sizes come from the Theme tokens (fixed design units), so
    // cards cover the same fraction of the screen at any resolution.
    readonly property int homeCardWidth: Theme.landscapeCardWidth
    readonly property int homeCardHeight: Theme.landscapeCardHeight
    // Request higher-resolution images for the scaled cards; never below the
    // physical card width at high OS display scaling.
    readonly property int homeCardImageRequestWidth: Math.round(homeCardWidth * Math.max(2, Screen.devicePixelRatio))

    /// The navigation bar recedes once the page scrolls past the hero.
    readonly property bool navigationRecessed: mainFlickable.contentY > Theme.navBarReservedHeight

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
    // Not heroBanner.visible: that turns false while another page covers Home,
    // and the page backdrop must keep following the hero meanwhile.
    readonly property bool heroBackdropActive: ConfigManager.heroBannerEnabled
                                                && ConfigManager.heroBannerBackdropSyncEnabled
                                                && heroProvider.hasContent && heroBanner.currentBackdropUrl !== ""
    // Derived, not copied on change, so the page backdrop always matches the
    // hero text no matter what cycled or rotated while Home was covered.
    readonly property string displayedBackdropUrl: heroBackdropActive ? heroBanner.currentBackdropUrl
                                                                      : currentBackdropUrl

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
    
    // Signals for specific item navigation (from Recently Added)
    signal navigateToMovie(var movieData, var libraryId, var libraryName)
    signal navigateToEpisode(var episodeData, var seriesId, var libraryId, var libraryName)
    signal navigateToSeason(var seasonId, var seasonNumber, var seriesId, var libraryId, var libraryName)
    signal navigateToSeries(var seriesId, var libraryId, var libraryName)
    
    /// Refresh dynamic content (Next Up, Recently Added) without reloading libraries
    /// Call this after playback ends to update watch progress indicators
    function refreshDynamicContent(reloadRecommendations) {
        console.log("HomeScreen: Refreshing dynamic content")
        // Badge choices made in the Silo web app apply on the next refresh.
        PosterBadges.refresh()
        if (mergedMode) {
            // Merged spaces keep Bloom's rows only; curated rows belong to
            // one Silo profile.
            HomeSectionsViewModel.clear()
            MergedCatalog.getNextUp()
            for (var j = 0; j < librariesModel.length; j++) {
                MergedCatalog.getLatestMedia(librariesModel[j].connectionId || "",
                                             librariesModel[j].itemId)
            }
            return
        }
        LibraryService.getNextUp()
        for (var i = 0; i < librariesModel.length; i++) {
            LibraryService.getLatestMedia(librariesModel[i].itemId)
        }
        // The profile's own Silo rows, or Jellyfin's Favorites and
        // recommendation rows.
        HomeSectionsViewModel.refresh(reloadRecommendations === true)
    }

    /// Reload Continue Watching and Next Up only (a Next Up option changed).
    function refreshNextUp() {
        if (mergedMode)
            MergedCatalog.getNextUp()
        else if (AuthenticationService.authenticated)
            LibraryService.getNextUp()
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
        var fallbackItems = Array.from(continueWatchingModel).concat(Array.from(displayedNextUpModel))
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
        // Merged views arrive in member-priority order; the saved order holds
        // one server's library IDs only.
        if (mergedMode)
            return views
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
    property string lastFocusedSection: ""
    property int lastFocusedIndex: 0
    property real lastContentY: 0  // Remember scroll position
    // Position of the focused server row, for when a refresh removes it.
    property int lastFocusedServerRow: 0
    // Recommendation rows are read once per Home (then after playback and
    // when their setting is turned on); other refreshes reuse them.
    property bool recommendationsRequested: false

    // First activation lands on the hero. Hero content arrives after the
    // shelves load, so the first shelf holds provisional focus until then; any
    // user navigation away from it cancels the hand-off so late hero data never
    // steals focus.
    property bool initialFocusPending: true
    property bool initialFocusProvisional: false
    // Set while applyInitialFocus() itself moves the provisional focus, so
    // that move is not mistaken for the user leaving the shelf.
    property bool applyingInitialFocus: false

    function applyInitialFocus() {
        if (!initialFocusPending || root.StackView.status !== StackView.Active)
            return
        if (heroBanner.visible) {
            initialFocusPending = false
            initialFocusProvisional = false
            heroBanner.resetToCarousel()
            heroBanner.forceActiveFocus()
            return
        }
        var first = firstShelfList()
        if (first && !first.activeFocus) {
            applyingInitialFocus = true
            first.forceActiveFocus()
            applyingInitialFocus = false
            initialFocusProvisional = true
        }
        if (!ConfigManager.heroBannerEnabled && first && first.activeFocus) {
            initialFocusPending = false
            initialFocusProvisional = false
        }
    }

    function cancelInitialFocus() {
        if (!initialFocusProvisional || applyingInitialFocus)
            return
        initialFocusPending = false
        initialFocusProvisional = false
    }

    // A curated row delegate (Repeater.itemAt answers a plain Item).
    function serverRowMeta(row) {
        return {
            item: row || null,
            list: row ? (row.serverRowListRef || null) : null,
            sectionId: row ? (row.sectionId || "") : ""
        }
    }

    function getRecentlyAddedDelegateMeta(index) {
        var item = recentlyAddedRepeater.itemAt(index)
        return {
            item: item || null,
            recentlyAddedListRef: item ? (item["recentlyAddedListRef"] || null) : null,
            libraryId: item ? (item["libraryKey"] || "") : ""
        }
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
            for (var j = 0; section === "" && j < serverRowsRepeater.count; j++) {
                var focusRow = serverRowMeta(serverRowsRepeater.itemAt(j))
                if (focusRow.list && focusRow.list.activeFocus) {
                    section = "serverRow:" + focusRow.sectionId
                    index = focusRow.list.currentIndex
                    lastFocusedServerRow = j
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
            mainFlickable.contentY = 0
            Qt.callLater(function() {
                heroBanner.resetToCarousel()
                heroBanner.forceActiveFocus()
            })
            return
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
        } else if (lastFocusedSection.indexOf("serverRow:") === 0) {
            // The same row, or when a refresh removed it the row now in its
            // place (or the last one above), else the last shelf above.
            var sectionId = lastFocusedSection.substring("serverRow:".length)
            var rowIndex = HomeSectionsViewModel.restoreRowIndex(sectionId, lastFocusedServerRow)
            var restoreRow = serverRowMeta(rowIndex >= 0 ? serverRowsRepeater.itemAt(rowIndex) : null)
            if (restoreRow.list && restoreRow.item.visible) {
                targetList = restoreRow.list
                targetSection = restoreRow.item
            } else {
                var shelves = shelfLists()
                if (shelves.length > 0) {
                    targetList = shelves[shelves.length - 1]
                    targetSection = targetList.parent
                }
            }
        }
        
        if (targetList && targetSection) {
            // First scroll the section into view
            mainFlickable.contentY = lastContentY
            
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
            // Fall back to the hero, then the first shelf.
            console.log("[FocusDebug] Could not find target list, defaulting to the hero or first shelf")
            Qt.callLater(function() {
                if (heroBanner.visible) {
                    mainFlickable.contentY = 0
                    heroBanner.resetToCarousel()
                    heroBanner.forceActiveFocus()
                    return
                }
                var first = root.firstShelfList()
                if (first)
                    first.forceActiveFocus()
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
            if (hasBeenActivated)
                restoreFocusState()
            else
                applyInitialFocus()
            
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
    
    // Stage: the hero's artwork, sharp behind the hero copy, or the rotating
    // backdrop, softened, when the hero does not drive it. Full-bleed under
    // the navigation bar.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        z: -1
        mode: root.heroBackdropActive ? "hero" : "ambient"
        source: root.displayedBackdropUrl
        // As the page scrolls past the hero, quiet the artwork so the shelves
        // keep their contrast.
        dim: mode === "hero"
             ? Math.min(0.72, mainFlickable.contentY / Math.max(1, heroBanner.height) * 0.72)
             : 0
        onArtworkShown: function(url) { homeBackdropSettings.lastBackdropUrl = url }
    }

    // Subtitle for landscape cards: episode code plus time left when resuming,
    // otherwise the episode title.
    function landscapeSubtitle(item) {
        if (!item)
            return ""
        var parts = []
        if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
            parts.push("S" + item.parentIndexNumber + " E" + item.indexNumber)
        if ((item.positionMs || 0) > 0 && (item.durationMs || 0) > 0) {
            var minutes = Math.max(1, Math.round((item.durationMs - item.positionMs) / 60000))
            parts.push(minutes >= 60 ? qsTr("%1h %2m left").arg(Math.floor(minutes / 60)).arg(minutes % 60)
                                     : qsTr("%1 min left").arg(minutes))
        } else if (item.name && item.seriesName) {
            parts.push(item.name)
        }
        return parts.join("  ·  ")
    }

    function posterSubtitle(item) {
        if (!item)
            return ""
        if (item.mediaType === "Episode") {
            if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
                return "S" + item.parentIndexNumber + " E" + item.indexNumber + (item.name ? "  ·  " + item.name : "")
            return item.name || ""
        }
        if (item.mediaType === "Series") {
            var startYear = item.productionYear || extractYear(item.premiereDate)
            if (!startYear)
                return ""
            if (item.status === "Continuing" || !item.endDate)
                return qsTr("%1 – Present").arg(startYear)
            var endYear = extractYear(item.endDate)
            return endYear && endYear !== startYear ? startYear + " – " + endYear : String(startYear)
        }
        var year = item.productionYear || extractYear(item.premiereDate)
        return year ? String(year) : ""
    }

    // Landscape card for Continue Watching / Up Next. Landscape art first:
    // thumbs, then the backdrop (Silo has no thumbs on Home cards, and its
    // episode primary is the portrait season poster), then the primary.
    // Thumbs usually carry the title art already; a card without one shows
    // the logo in place of the title text.
    Component {
        id: landscapeCardDelegate

        MediaCard {
            id: landscapeCard
            required property var modelData
            required property int index
            readonly property var thumbSources: [
                root.artworkUrlFromRef(modelData.seriesThumbArtwork, homeCardImageRequestWidth),
                root.artworkUrlFromRef(modelData.parentThumbArtwork, homeCardImageRequestWidth),
                root.artworkUrlFromRef(modelData.thumbArtwork, homeCardImageRequestWidth)
            ]
            readonly property bool hasThumb: thumbSources.some(function(url) { return url !== "" })
            variant: "landscape"
            artworkSources: thumbSources.concat([
                root.artworkUrlFromRef(modelData.backdropArtwork, homeCardImageRequestWidth),
                root.artworkUrlFromRef(modelData.primaryArtwork, homeCardImageRequestWidth)
            ])
            logoSource: hasThumb ? "" : root.artworkUrlFromRef(modelData.logoArtwork, homeCardImageRequestWidth)
            title: modelData.seriesName || modelData.name || ""
            subtitle: root.landscapeSubtitle(modelData)
            placeholderIcon: modelData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
            positionMs: modelData.positionMs || 0
            durationMs: modelData.durationMs || 0
            sourceLabel: root.mergedMode ? (modelData.sourceLabel || "") : ""
            focused: ListView.isCurrentItem && ListView.view.activeFocus
            menuEnabled: true
            onClicked: {
                landscapeCard.ListView.view.currentIndex = index
                landscapeCard.ListView.view.forceActiveFocus()
                handleNextUpSelection(modelData)
            }
            onMenuRequested: {
                var shelf = landscapeCard.ListView.view
                shelf.currentIndex = index
                shelf.forceActiveFocus()
                cardActionsMenu.openFor(shelf, index)
            }
            Accessible.description: qsTr("Continue watching %1").arg(modelData.seriesName || modelData.name || "")
        }
    }

    // The actions key (Menu, Shift+F10, controller Start) opens a card's
    // actions; InputBindingManager posts Key_Menu for the controller.
    // Right-click or press-and-hold on a card opens the same menu.
    function isActionsKey(event) {
        return event.key === Qt.Key_Menu
                || InputBindingManager.actionForKeyboardEvent(event.key, event.modifiers, "navigation") === "nav.menu"
    }

    // The key the undo toast names: the controller's actions button when a
    // controller is connected, else the keyboard's first nav.menu key.
    function actionsKeyText() {
        var devices = InputBindingManager.gamepadAvailable ? ["gamepad", "keyboard"] : ["keyboard"]
        for (var i = 0; i < devices.length; i++) {
            var bindings = InputBindingManager.bindingsForAction(devices[i], "nav.menu")
            if (bindings && bindings.length > 0)
                return InputBindingManager.displayTextForBinding(bindings[0])
        }
        return ""
    }

    function undoHint() {
        var key = actionsKeyText()
        return key !== "" ? qsTr("To undo, press %1 and choose Undo removal.").arg(key) : ""
    }

    // Elsewhere on Home the actions key only reaches the undo.
    Keys.onPressed: function(event) {
        if (isActionsKey(event) && shelves.undoAvailable) {
            event.accepted = true
            cardActionsMenu.openFor(null, -1)
        }
    }

    // Actions for a Continue Watching or Up Next card, from C++. While a
    // removal can be undone, "Undo removal" ends the menu (the toast's button
    // takes no keyboard focus), so focus starts on the card's first action.
    // Back closes the menu and focus returns to the card.
    BloomMenu {
        id: cardActionsMenu
        property var item: null
        property var list: null
        property var actions: []
        // The entry picked, run once the menu has closed.
        property var pendingAction: null
        // Where focus goes back when the menu opened off a shelf.
        property Item returnFocusItem: null

        function openFor(shelf, index) {
            var model = shelf ? shelf.model : null
            var at = index >= 0 ? index : (shelf ? shelf.currentIndex : -1)
            var card = model && at >= 0 && at < model.count ? model.get(at) : null
            var entries = card ? Array.from(shelves.actionsFor(card)) : []
            if (shelves.undoAvailable)
                entries.push({ id: "undo", text: qsTr("Undo removal"), enabled: true, navigation: false })
            if (entries.length === 0)
                return
            if (card && shelf.currentIndex !== at)
                shelf.currentIndex = at
            list = card ? shelf : null
            returnFocusItem = card ? shelf : Window.activeFocusItem
            item = card
            actions = entries
            pendingAction = null
            var delegate = card ? shelf.itemAtIndex(at) : null
            if (delegate)
                popup(delegate, 0, delegate.height)
            else
                open()
        }

        onOpened: {
            for (var i = 0; i < actions.length; i++) {
                if (actions[i].enabled) {
                    currentIndex = i
                    break
                }
            }
            forceActiveFocus()
        }
        // Run the action after the menu has closed, so the page it opens or
        // the card focus it moves keeps focus.
        onClosed: {
            var action = pendingAction
            var target = item
            var shelf = list
            var previousFocus = returnFocusItem
            pendingAction = null
            returnFocusItem = null
            Qt.callLater(function() {
                root.returnCardFocus(shelf, previousFocus)
                if (action)
                    root.pickCardAction(action, target, shelf)
            })
        }

        Repeater {
            model: cardActionsMenu.actions

            BloomMenuItem {
                required property var modelData
                text: modelData.text
                enabled: modelData.enabled
                // Esc closes the menu instead of reaching Main's Home Esc
                // shortcut (the power menu).
                Keys.onShortcutOverride: function(event) {
                    event.accepted = event.key === Qt.Key_Escape
                }
                onTriggered: {
                    cardActionsMenu.pendingAction = modelData
                    cardActionsMenu.close()
                }
            }
        }
    }

    // A Next Up refresh restoring shelf focus never strands the open menu.
    ModalFocusGuard {
        popup: cardActionsMenu
    }

    function returnCardFocus(shelf, previousFocus) {
        if (root.StackView.status !== StackView.Active)
            return
        if (shelf && shelf.visible && shelf.count > 0)
            shelf.forceActiveFocus()
        else if (previousFocus && previousFocus.visible)
            previousFocus.forceActiveFocus()
    }

    function pickCardAction(action, item, shelf) {
        if (action.id === "undo") {
            requestUndo()
        } else if (action.confirmTitle) {
            stopWatchingDialog.ask(action, item, shelf)
        } else {
            runCardAction(action.id, item, shelf)
        }
    }

    // The undo waiting for its route, so its toast closing does not forget
    // it and a failed route can say so.
    property var undoRouteAction: null

    function requestUndo() {
        // The toast may have closed, or a newer removal replaced it, while
        // the menu was open.
        if (!shelves.undoAvailable) {
            cardToast.show(qsTr("That removal can no longer be undone."))
            return
        }
        var action = function() {
            if (root.undoRouteAction !== action)
                return
            root.undoRouteAction = null
            shelves.undo()
        }
        undoRouteAction = action
        cardToast.hide()
        sourceRouter.run(shelves.undoConnectionId, action)
    }

    // A newer route replaced the undo before it ran.
    function settleUndoRoute() {
        if (!undoRouteAction)
            return
        undoRouteAction = null
        if (!cardToast.showing)
            shelves.dismissUndo()
    }

    Connections {
        target: sourceRouter
        function onPendingActionChanged() {
            if (root.undoRouteAction && sourceRouter.pendingAction !== root.undoRouteAction)
                Qt.callLater(root.settleUndoRoute)
        }
        function onFailed(message) {
            var detail = message || qsTr("The server isn't reachable.")
            if (root.undoRouteAction) {
                root.undoRouteAction = null
                shelves.dismissUndo()
                cardToast.show(qsTr("Couldn't undo the removal. %1").arg(detail))
            } else {
                cardToast.show(qsTr("Couldn't switch to that title's server. %1").arg(detail))
            }
        }
    }

    function runCardAction(actionId, item, shelf) {
        if (!item)
            return
        if (actionId === "openDetails") {
            handleNextUpSelection(item)
            return
        }
        if (actionId === "openSeries") {
            saveFocusState()
            var seriesId = item.seriesId || ""
            sourceRouter.run(item.connectionId || "", function() {
                navigateToSeries(seriesId, "", "")
            })
            return
        }
        var shelfOrder = shelfLists().indexOf(shelf)
        var index = shelf ? shelf.currentIndex : 0
        // Merged cards write through their own server.
        sourceRouter.run(item.connectionId || "", function() {
            shelves.trigger(actionId, item)
            root.focusShelfAfterChange(shelf, index, shelfOrder)
        })
    }

    // After a card leaves its shelf, focus the card that took its place (or
    // the one before it); an emptied shelf hands focus to the shelf that
    // took its place, then the one above, then the hero.
    function focusShelfAfterChange(shelf, index, shelfOrder) {
        Qt.callLater(function() {
            if (root.StackView.status !== StackView.Active || root.cardModalOpen)
                return
            // Focus moved on while a merged card waited for its route.
            if (!(shelf && shelf.activeFocus) && !root.homeFocusIdle())
                return
            if (shelf && shelf.visible && shelf.count > 0) {
                shelf.currentIndex = Math.max(0, Math.min(index, shelf.count - 1))
                if (!shelf.activeFocus)
                    shelf.forceActiveFocus()
                ensureListItemVisible(shelf)
            } else {
                var lists = shelfLists()
                if (lists.length > 0) {
                    lists[Math.max(0, Math.min(shelfOrder, lists.length - 1))].forceActiveFocus()
                } else if (heroBanner.visible) {
                    heroBanner.resetToCarousel()
                    heroBanner.forceActiveFocus()
                }
            }
            // A Next Up refresh restores focus from the saved state.
            saveFocusState()
        })
    }

    // The card menu or its confirmation holds focus.
    readonly property bool cardModalOpen: cardActionsMenu.opened || stopWatchingDialog.opened

    // Nothing on Home holds focus: the focused card's shelf went away, or
    // focus fell back to the page itself.
    function homeFocusIdle() {
        var focused = Window.activeFocusItem
        return !focused || focused === root || focused === mainFlickable
                || focused === mainFlickable.contentItem
    }

    // Asks before a Silo episode's dismissal drops its whole show, which
    // the server also sends on to the profile's watch services.
    Dialog {
        id: stopWatchingDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.round(680 * Theme.layoutScale),
                        Math.max(0, root.width - Theme.spacingLarge * 2))
        padding: Theme.dialogPadding
        topPadding: Theme.spacingLarge

        property var action: null
        property var item: null
        property var shelf: null
        property bool confirmed: false

        function ask(cardAction, card, cardShelf) {
            action = cardAction
            item = card
            shelf = cardShelf
            open()
        }

        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        ModalFocusGuard {
            popup: stopWatchingDialog
            fallbackItem: stopWatchingCancelButton
        }

        header: Item {
            implicitHeight: stopWatchingTitle.implicitHeight + Theme.dialogPadding + Theme.spacingSmall

            Text {
                id: stopWatchingTitle
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.dialogPadding
                anchors.rightMargin: Theme.dialogPadding
                text: stopWatchingDialog.action ? stopWatchingDialog.action.confirmTitle : ""
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
        }

        contentItem: Text {
            text: stopWatchingDialog.action ? stopWatchingDialog.action.confirmMessage : ""
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        footer: Item {
            implicitHeight: Theme.buttonHeightMedium + Theme.dialogPadding + Theme.spacingMedium

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogPadding
                anchors.topMargin: Theme.spacingMedium
                spacing: Theme.controlSpacing

                Keys.onShortcutOverride: function(event) {
                    event.accepted = event.key === Qt.Key_Escape
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back
                            || event.key === Qt.Key_Backspace) {
                        event.accepted = true
                        stopWatchingDialog.close()
                    }
                }

                // Cancel takes focus first, so a stray Select changes nothing.
                ActionButton {
                    id: stopWatchingCancelButton
                    variant: "secondary"
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.right: stopWatchingConfirmButton
                    Keys.onReturnPressed: stopWatchingDialog.close()
                    Keys.onEnterPressed: stopWatchingDialog.close()
                    onClicked: stopWatchingDialog.close()
                }

                ActionButton {
                    id: stopWatchingConfirmButton
                    variant: "destructive"
                    Layout.fillWidth: true
                    text: stopWatchingDialog.action ? stopWatchingDialog.action.confirmText : ""
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.left: stopWatchingCancelButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    onClicked: {
                        stopWatchingDialog.confirmed = true
                        stopWatchingDialog.close()
                    }
                }
            }
        }

        onOpened: {
            confirmed = false
            Qt.callLater(function() { stopWatchingCancelButton.forceActiveFocus() })
        }
        onClosed: {
            var cardAction = action
            var card = item
            var cardShelf = shelf
            var run = confirmed
            action = null
            item = null
            shelf = null
            confirmed = false
            Qt.callLater(function() {
                root.returnCardFocus(cardShelf, null)
                if (run && cardAction)
                    root.runCardAction(cardAction.id, card, cardShelf)
            })
        }
    }

    ToastNotification {
        id: cardToast
        z: 10
        displayDuration: 8000
        // The menu offers the same undo; its timeout waits for the menu.
        paused: root.cardModalOpen
        onActionTriggered: root.requestUndo()
        // An undo waiting for its route still needs the record.
        onDismissed: if (!root.undoRouteAction) shelves.dismissUndo()
    }

    // Poster card for Recently Added.
    Component {
        id: posterCardDelegate

        MediaCard {
            id: posterCard
            required property var modelData
            required property int index
            variant: "poster"
            artworkSources: [
                modelData.mediaType === "Episode" ? root.artworkUrlFromRef(modelData.seriesPrimaryArtwork, 640) : "",
                root.artworkUrlFromRef(modelData.primaryArtwork, 640)
            ]
            title: modelData.mediaType === "Episode" ? (modelData.seriesName || modelData.name || "")
                                                     : (modelData.name || "")
            subtitle: root.posterSubtitle(modelData)
            placeholderIcon: modelData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
            item: modelData
            showWatchState: modelData.mediaType === "Series" || (modelData.mediaType === "Movie" && !!modelData.watched)
            unplayedCount: modelData.mediaType === "Series" ? (modelData.unplayedItemCount || 0) : 0
            watched: !!modelData.watched
            focused: ListView.isCurrentItem && ListView.view.activeFocus
            onClicked: {
                var list = posterCard.ListView.view
                list.currentIndex = index
                list.forceActiveFocus()
                root.openPosterCard(list, modelData)
            }
            Accessible.description: qsTr("Details for %1").arg(modelData.name || "")
        }
    }

    // Poster cards serve the Recently Added rows and the profile's curated
    // rows; each list says which it is.
    function openPosterCard(list, item) {
        if (list.serverRow)
            handleServerRowSelection(item)
        else
            handleRecentlyAddedSelection(item, list.parentLibraryId)
    }

    // Shelves in focus order (visible ones only).
    function shelfLists() {
        var lists = []
        if (continueWatchingSection.visible && continueWatchingList.count > 0)
            lists.push(continueWatchingList)
        if (nextUpSection.visible && nextUpList.count > 0)
            lists.push(nextUpList)
        for (var i = 0; i < recentlyAddedRepeater.count; i++) {
            var meta = getRecentlyAddedDelegateMeta(i)
            if (meta.item && meta.item.visible && meta.recentlyAddedListRef)
                lists.push(meta.recentlyAddedListRef)
        }
        for (var j = 0; j < serverRowsRepeater.count; j++) {
            var row = serverRowMeta(serverRowsRepeater.itemAt(j))
            if (row.list && row.item.visible)
                lists.push(row.list)
        }
        return lists
    }

    function firstShelfList() {
        var lists = shelfLists()
        return lists.length > 0 ? lists[0] : null
    }

    // Moves focus to the shelf above/below. Above the first shelf is the hero;
    // returns false when there is nowhere to go so Up reaches the navigation bar.
    function moveShelfFocus(fromList, delta) {
        var lists = shelfLists()
        var index = lists.indexOf(fromList)
        var target = index + delta
        if (index >= 0 && target >= 0 && target < lists.length) {
            lists[target].forceActiveFocus()
            return true
        }
        if (target < 0 && heroBanner.visible) {
            heroBanner.resetToCarousel()
            heroBanner.forceActiveFocus()
            return true
        }
        return delta > 0
    }

    readonly property real shelfScrollOffset: Math.round(56 * Theme.layoutScale)

    // Page to a shelf: the first shelf keeps the hero in view; later shelves
    // scroll to the top of the screen so the focused row always sits in the
    // same place.
    function scrollToSection(section) {
        if (!section)
            return
        var first = firstShelfList()
        var y
        if (heroBanner.visible && first && first.parent === section)
            y = 0
        else
            y = section.mapToItem(mainFlickable.contentItem, 0, 0).y - root.shelfScrollOffset
        var maxY = Math.max(0, mainFlickable.contentHeight - mainFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    // While the hero's Play/Details actions hold focus the shelves recede, so
    // the hero reads as the focused surface; Up or Esc returns to the
    // carousel and brings them back.
    property real shelfOpacity: heroBanner.actionsFocused ? Theme.recededOpacity : 1.0
    Behavior on shelfOpacity {
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard }
    }

    NumberAnimation {
        id: scrollAnimation
        target: mainFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
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

            // Without a cinematic hero, clear the navigation bar.
            Item { Layout.preferredHeight: heroBanner.cinematic && heroBanner.visible ? 0 : Theme.pageMarginTop }

            HeroBanner {
                id: heroBanner
                Layout.fillWidth: true
                // Cinema mode gives the hero most of the first screen and lets
                // the first shelf sit fully in view below it.
                Layout.preferredHeight: cinematic ? Math.round(root.height * 0.60) : implicitHeight
                Layout.bottomMargin: cinematic ? -Theme.spacingLarge : 0
                cinematic: ConfigManager.heroBannerBackdropSyncEnabled
                visible: ConfigManager.heroBannerEnabled && heroProvider.hasContent
                heroModel: heroProvider.heroModel
                scrolling: mainFlickable.moving || scrollAnimation.running
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.lastFocusedSection = "hero"
                        scrollAnimation.to = 0
                        scrollAnimation.restart()
                    } else {
                        heroBanner.resetToCarousel()
                    }
                }
                onVisibleChanged: if (visible) Qt.callLater(root.applyInitialFocus)
                onMoveDownRequested: {
                    var first = root.firstShelfList()
                    if (first)
                        first.forceActiveFocus()
                }
                onDetailsRequested: function(item) { root.handleHeroSelection(item) }
                onPlayRequested: function(item) { root.playHeroItem(item) }
            }

            ColumnLayout {
                id: continueWatchingSection
                opacity: root.shelfOpacity
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

                ShelfList {
                    id: continueWatchingList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    // A list model, so the focused card keeps its place
                    // while other cards come and go.
                    model: shelves.continueWatchingModel
                    delegate: landscapeCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(continueWatchingList, -1) }
                    Keys.onPressed: function(event) {
                        if (root.isActionsKey(event)) {
                            event.accepted = true
                            cardActionsMenu.openFor(continueWatchingList, -1)
                        }
                    }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(continueWatchingList, 1) }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < count)
                            handleNextUpSelection(model.get(currentIndex))
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < count)
                            handleNextUpSelection(model.get(currentIndex))
                    }
                    onCurrentIndexChanged: if (activeFocus) root.cancelInitialFocus()
                    onActiveFocusChanged: {
                        if (activeFocus)
                            root.scrollToSection(continueWatchingSection)
                        else
                            root.cancelInitialFocus()
                    }
                }
            }

            ColumnLayout {
                id: nextUpSection
                opacity: root.shelfOpacity
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

                ShelfList {
                    id: nextUpList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    // A list model, so the focused card keeps its place
                    // while other cards come and go.
                    model: shelves.nextUpModel
                    delegate: landscapeCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(nextUpList, -1) }
                    Keys.onPressed: function(event) {
                        if (root.isActionsKey(event)) {
                            event.accepted = true
                            cardActionsMenu.openFor(nextUpList, -1)
                        }
                    }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(nextUpList, 1) }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0 && currentIndex < count)
                            handleNextUpSelection(model.get(currentIndex))
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0 && currentIndex < count)
                            handleNextUpSelection(model.get(currentIndex))
                    }
                    onCurrentIndexChanged: if (activeFocus) root.cancelInitialFocus()
                    onActiveFocusChanged: {
                        if (activeFocus)
                            root.scrollToSection(nextUpSection)
                        else
                            root.cancelInitialFocus()
                    }
                }
            }

            ColumnLayout {
                id: recentlyAddedSection
                opacity: root.shelfOpacity
                Layout.fillWidth: true
                spacing: Theme.sectionSpacing

                Repeater {
                    id: recentlyAddedRepeater
                    model: librariesModel

                    delegate: ColumnLayout {
                        id: recentlyAddedShelf
                        Layout.fillWidth: true
                        spacing: Theme.sectionTitleSpacing

                        property string libraryKey: root.libraryKey(modelData.connectionId, modelData.itemId)
                        property var items: recentlyAddedMap[libraryKey] || []
                        property string libraryId: modelData.itemId
                        property var recentlyAddedListRef: recentlyAddedList
                        visible: items.length > 0

                        SectionHeader {
                            text: qsTr("Recently Added")
                            detail: root.mergedMode && modelData.sourceLabel
                                    ? (modelData.name || "") + "  ·  " + modelData.sourceLabel
                                    : (modelData.name || "")
                            Layout.leftMargin: Theme.pageMarginX
                            Layout.rightMargin: Theme.pageMarginX
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Heading
                            Accessible.name: text + " - " + detail
                        }

                        ShelfList {
                            id: recentlyAddedList
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            model: recentlyAddedShelf.items
                            delegate: posterCardDelegate
                            property string parentLibraryId: recentlyAddedShelf.libraryId

                            Keys.onReturnPressed: {
                                if (currentIndex >= 0 && currentIndex < count)
                                    handleRecentlyAddedSelection(model[currentIndex], parentLibraryId)
                            }
                            Keys.onEnterPressed: {
                                if (currentIndex >= 0 && currentIndex < count)
                                    handleRecentlyAddedSelection(model[currentIndex], parentLibraryId)
                            }
                            Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(recentlyAddedList, -1) }
                            Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(recentlyAddedList, 1) }
                            onCurrentIndexChanged: if (activeFocus) root.cancelInitialFocus()
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.scrollToSection(recentlyAddedShelf)
                                else
                                    root.cancelInitialFocus()
                            }
                        }
                    }
                }
            }

            // Rows the Silo profile curates (recommendations, collections,
            // its watchlist...) that Bloom does not already show, in the
            // server's layout order, or the Favorites row for providers
            // without a watchlist and the recommendation rows (Jellyfin).
            // HomeSectionsViewModel picks them.
            ColumnLayout {
                id: serverRowsSection
                opacity: root.shelfOpacity
                Layout.fillWidth: true
                spacing: Theme.sectionSpacing
                visible: !root.mergedMode && serverRowsRepeater.count > 0

                Repeater {
                    id: serverRowsRepeater
                    model: HomeSectionsViewModel

                    delegate: ColumnLayout {
                        id: serverRowShelf
                        required property string sectionId
                        required property string title
                        required property var items
                        property var serverRowListRef: serverRowList
                        Layout.fillWidth: true
                        spacing: Theme.sectionTitleSpacing
                        visible: (items || []).length > 0

                        SectionHeader {
                            text: serverRowShelf.title
                            Layout.leftMargin: Theme.pageMarginX
                            Layout.rightMargin: Theme.pageMarginX
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }

                        ShelfList {
                            id: serverRowList
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            model: serverRowShelf.items
                            delegate: posterCardDelegate
                            readonly property bool serverRow: true

                            Keys.onReturnPressed: {
                                if (currentIndex >= 0 && currentIndex < count)
                                    root.handleServerRowSelection(serverRowShelf.items[currentIndex])
                            }
                            Keys.onEnterPressed: {
                                if (currentIndex >= 0 && currentIndex < count)
                                    root.handleServerRowSelection(serverRowShelf.items[currentIndex])
                            }
                            Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(serverRowList, -1) }
                            Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(serverRowList, 1) }
                            onCurrentIndexChanged: if (activeFocus) root.cancelInitialFocus()
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.scrollToSection(serverRowShelf)
                                else
                                    root.cancelInitialFocus()
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

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
            root.loadViews()
        }
        onVisibleChanged: if (visible && actionText !== "") Qt.callLater(focusAction)
        // Stays visible from loading to error, so visibility alone misses the first failure.
        onActionTextChanged: if (visible && actionText !== "") Qt.callLater(focusAction)
    }
    
    // Functions
    function ensureListItemVisible(listView) {
        if (!listView || listView.currentIndex < 0)
            return
        listView.positionViewAtIndex(listView.currentIndex, ListView.Visible)
    }

    function handleNextUpSelection(item) {
        if (root.StackView.status !== StackView.Active) {
            console.log("[Home] Ignoring Next Up selection while screen is not active")
            return
        }

        console.log("[Home] Next Up selected: " + item.name)
        
        // Save focus state before navigating
        saveFocusState()

        // In-progress movies share Continue Watching with episodes; they
        // open their own details page.
        if ((item.mediaType || "") === "Movie") {
            sourceRouter.run(item.connectionId || "", function() {
                navigateToMovie(item, item.parentId || "", "")
            })
            return
        }
        
        // Navigate to episode details view instead of playing directly
        var seriesId = item.seriesId || ""
        
        // Find the library for this series (needed for navigation context)
        // For Next Up items, we may not have the library ID directly, so we use an empty string
        // The LibraryScreen will handle this appropriately
        var libraryId = ""
        var libraryName = ""
        
        sourceRouter.run(item.connectionId || "", function() {
            navigateToEpisode(item, seriesId, libraryId, libraryName)
        })
    }

    function handleHeroSelection(item) {
        if (!item) return
        saveFocusState()
        const mediaType = item.mediaType || ""
        const itemId = item.itemId || ""
        const parentId = item.parentId || ""
        const seriesId = item.seriesId || ""
        sourceRouter.run(item.connectionId || "", function() {
            if (mediaType === "Movie") navigateToMovie(item, parentId, "")
            else if (mediaType === "Series") navigateToSeries(itemId, parentId, "")
            else if (mediaType === "Episode") navigateToEpisode(item, seriesId, parentId, "")
        })
    }

    // A curated row's card opens its own page on its connection, as the hero
    // does; its server row carries no library context.
    function handleServerRowSelection(item) {
        if (root.StackView.status !== StackView.Active)
            return
        handleHeroSelection(item)
    }

    function playHeroItem(item) {
        if (!item) return
        // Playback and its progress reports run on the owning connection.
        sourceRouter.run(item.connectionId || "", function() { root.startHeroPlayback(item) })
    }

    function startHeroPlayback(item) {
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
        sourceRouter.run(item.connectionId || "", function() {
            root.openRecentlyAdded(item, libraryId)
        })
    }

    function openRecentlyAdded(item, libraryId) {
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
            var items = recentlyAddedMap[root.libraryKey(item.connectionId, libraryId)] || []
            
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
    
    function applyViews(views) {
        librariesLoading = false
        librariesError = ""
        var orderedViews = orderLibraries(views)
        librariesModel = orderedViews
        // Recommendation rows come from the movie libraries shown here.
        HomeSectionsViewModel.setLibraries(orderedViews)
        heroProvider.rebuild()
        if (orderedViews.length > 0)
            Qt.callLater(root.applyInitialFocus)

        // Next Up and Recently Added for each library. A new Home reads
        // recommendations; a later views load (libraries shown or hidden)
        // reuses them for the libraries it keeps.
        refreshDynamicContent(!recommendationsRequested)
        recommendationsRequested = true

        // Fetch a larger random pool from all available media for backdrop rotation.
        // Merged spaces draw it from the live session's source.
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

    function applyNextUp(items) {
        shelves.setItems(items)

        // Use section fallback only if global pool is unavailable.
        if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
            root.addBackdropCandidates(items)
        }

        // The shelves are list models, so a focused card keeps its place.
        // Focus is restored only when nothing on Home holds it (its shelf
        // emptied or Home just came back), never from another shelf, the
        // navigation bar, or an open card menu.
        Qt.callLater(function() {
            if (root.StackView.status !== StackView.Active || root.cardModalOpen)
                return
            if (lastFocusedSection !== "nextUp" && lastFocusedSection !== "continueWatching")
                return
            if (continueWatchingList.activeFocus || nextUpList.activeFocus) {
                saveFocusState()
                return
            }
            if (root.homeFocusIdle())
                restoreFocusState()
        })
    }

    function applyLatestMedia(connectionId, parentId, items) {
        // Update map safely to trigger bindings without destroying delegates
        var newMap = {}
        for (var key in recentlyAddedMap) {
            newMap[key] = recentlyAddedMap[key]
        }
        newMap[root.libraryKey(connectionId, parentId)] = items
        recentlyAddedMap = newMap
        if (ConfigManager.heroBannerSource !== "library") {
            heroProvider.rebuild()
        }

        // Use section fallback only if global pool is unavailable.
        if (useSectionBackdropFallback && !globalBackdropPoolLoaded) {
            root.addBackdropCandidates(items)
        }
    }

    Connections {
        target: typeof MergedCatalog !== "undefined" ? MergedCatalog : null
        ignoreUnknownSignals: true

        function onViewsChanged() {
            if (!root.mergedMode || !root.mergedViewsRequested || MergedCatalog.viewsLoading)
                return
            root.mergedViewsRequested = false
            root.applyViews(MergedCatalog.views)
        }
        function onViewsFailed(error) {
            if (!root.mergedMode)
                return
            root.librariesLoading = false
            root.librariesError = error
        }
        function onNextUpLoaded(items) {
            if (root.mergedMode)
                root.applyNextUp(items)
        }
        function onLatestMediaLoaded(connectionId, parentId, items) {
            if (root.mergedMode)
                root.applyLatestMedia(connectionId, parentId, items)
        }
        // Sources were added, removed, or toggled: rows from the previous
        // membership are stale.
        function onActiveChanged() {
            if (AuthenticationService.authenticated)
                root.resetSpaceContent()
        }
    }

    // New, removed or reordered curated rows rebuild their shelves; keep
    // focus on the same row (or the nearest shelf) when it was on one.
    property bool serverRowFocusLost: false

    function serverRowHasFocus() {
        for (var i = 0; i < serverRowsRepeater.count; i++) {
            var row = serverRowMeta(serverRowsRepeater.itemAt(i))
            if (row.list && row.list.activeFocus)
                return true
        }
        return false
    }

    Connections {
        target: HomeSectionsViewModel
        function onModelAboutToBeReset() {
            root.serverRowFocusLost = root.StackView.status === StackView.Active
                    && root.serverRowHasFocus()
            if (root.serverRowFocusLost)
                root.saveFocusState()
        }
        function onModelReset() {
            if (root.serverRowFocusLost)
                Qt.callLater(root.restoreFocusState)
            root.serverRowFocusLost = false
        }
    }

    // A single space changed which libraries it shows. Merged spaces reload
    // through MergedCatalog.activeChanged instead.
    Connections {
        target: typeof Workspaces !== "undefined" ? Workspaces : null
        ignoreUnknownSignals: true
        function onLibraryVisibilityChanged() {
            if (!root.mergedMode && AuthenticationService.authenticated)
                root.resetSpaceContent()
        }
    }

    // Connections
    Connections {
        target: LibraryService
        
        function onCanonicalViewsLoadedForConnection(connectionId, views) {
            if (root.mergedMode || !root.acceptsConnectionResponse(connectionId)) return
            // Drop libraries the space hides (MergedCatalog filters merged views).
            root.applyViews(typeof Workspaces !== "undefined" ? Workspaces.visibleLibraries(views) : views)
        }

        function onCanonicalNextUpLoaded(connectionId, items) {
            if (root.mergedMode || !root.acceptsConnectionResponse(connectionId)) return
            root.applyNextUp(items)
        }

        function onCanonicalLatestMediaLoaded(connectionId, parentId, items) {
            if (root.mergedMode || !root.acceptsConnectionResponse(connectionId)) return
            root.applyLatestMedia(connectionId, parentId, items)
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
            if (endpoint === "getViews" && !root.mergedMode) {
                librariesLoading = false
                librariesError = error
            }
        }
    }

    Connections {
        target: ConfigManager
        function onNextUpRewatchingChanged() { root.refreshNextUp() }
        function onNextUpCutoffDaysChanged() { root.refreshNextUp() }
        function onHomeFavoritesRowEnabledChanged() {
            if (!root.mergedMode && AuthenticationService.authenticated)
                HomeSectionsViewModel.refresh()
        }
        function onHomeRecommendationsEnabledChanged() {
            if (root.mergedMode || !AuthenticationService.authenticated)
                return
            // Off: the rows go at once, whatever the refresh answers. On:
            // read them from the server.
            if (!ConfigManager.homeRecommendationsEnabled)
                HomeSectionsViewModel.removeRecommendationRows()
            HomeSectionsViewModel.refresh(ConfigManager.homeRecommendationsEnabled)
        }
    }

    Connections {
        target: SidebarSettings
        function onLibraryOrderChanged() {
            librariesModel = orderLibraries(librariesModel)
            // Recommendation rows follow the library order.
            if (root.mergedMode)
                return
            HomeSectionsViewModel.setLibraries(root.librariesModel)
            if (AuthenticationService.authenticated)
                HomeSectionsViewModel.refresh()
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
        // Playback changes what the server recommends.
        onTriggered: root.refreshDynamicContent(true)
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
        // A new Home (sign-in, profile or space switch) never shows the
        // previous session's rows.
        HomeSectionsViewModel.clear()
        loadViews()
        heroProvider.rebuild()
    }
}
