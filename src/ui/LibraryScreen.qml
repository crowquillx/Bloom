import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI

FocusScope {
    id: root

    property string navigationId: currentLibraryId !== "" ? "library:" + currentLibraryId : "media"
    property string currentParentId: ""
    property string currentLibraryId: ""   // The root library ID (for profile resolution)
    property string currentLibraryName: ""  // Name of the current library for display
    property string currentLibraryType: ""
    /// Libraries shown together on a combined page ("All Movies"), whose
    /// parent ID is `combined:movies` or `combined:series`.
    property var combinedLibraryIds: []
    property string currentBackdropUrl: ""
    property string currentSeriesId: ""
    property var navigationStack: []
    property bool componentReady: false
    property var letterBuckets: [] // [{ letter: "A", index: 0 }]
    
    // Filter properties
    property var selectedGenres: []
    property var selectedNetworks: []
    property var selectedTags: []
    property var selectedStudios: []
    property bool showFilterPanel: false
    property string activeFilterCategory: "genres"
    property string librarySearchText: ""
    property bool queryToolbarPinned: true
    property bool queryToolbarScrollArmed: false
    property string pendingQueryFocusTarget: ""
    property bool pendingQueryFilterPanelOpen: false
    property bool ratingSliderEditing: false
    
    // Position restoration properties (used when navigating back)
    property int _pendingGridIndex: -1
    property real _pendingGridContentY: -1
    property int _pendingEpisodeIndex: -1
    property int _pendingSeasonsGridIndex: -1
    property int _lastSelectedSeasonIndex: -1  // Temporarily stores season index during navigation
    property var _seriesDetailsReturnState: null
    property bool _restoreSeriesDetailsReturnState: false
    property var _movieDetailsReturnState: null
    property bool _restoreMovieDetailsReturnState: false
    
    // Theme song context - true while viewing a series, season, or episode
    property bool inSeriesContext: currentSeriesId !== "" && (showSeriesDetails || showSeasonView)
    
    // Series Details properties
    property bool showSeriesDetails: false
    property var currentSeriesData: null
    property var currentSeriesSeasons: []
    property var currentNextEpisode: null
    
    // Season View properties
    property bool showSeasonView: false
    property string currentSeasonId: ""
    property string currentSeasonName: ""
    property int currentSeasonNumber: 0
    property string currentSeasonPosterUrl: ""
    
    // Episode View properties
    property var currentEpisodeData: null
    property string initialEpisodeId: ""  // Episode ID to highlight when navigating from Next Up
    
    // Pending track indices from post-playback navigation
    // When navigating to next episode after playback, these are set to preserve track selection
    property int pendingAudioTrackIndex: -2
    property int pendingSubtitleTrackIndex: -2  // -2 means "not set", -1 means "none/disabled"
    
    // Movie Details View properties
    property bool showMovieDetails: false
    property var currentMovieData: null
    
    property var pendingEpisodeData: null
    property bool restoringFocusFromNavigation: false
    property bool restoringFocusFromSeriesDetailsReturn: false
    property bool restoringFocusFromMovieDetailsReturn: false
    property var _seerrRecommendationCache: ({})
    /// The navigation bar recedes while the grid (or a detail view) is
    /// scrolled into its content.
    readonly property bool navigationRecessed: contentLoader.item
                                               ? contentLoader.item["navigationRecessed"] === true : false
    readonly property bool canRestoreUpNextEpisodeContext: currentSeriesId !== "" && (showSeasonView || showSeriesDetails)

    component LibraryComboBox: ComboBox {
        id: combo
        // Sized to the widest option so the selection never truncates or
        // shifts neighbouring controls when it changes.
        implicitWidth: widestLabelWidth + Theme.spacingMedium * 2 + Theme.fontSizeIcon + Theme.spacingSmall
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        property real widestLabelWidth: 0
        function measureLabels() {
            var widest = 0
            var options = model || []
            for (var i = 0; i < options.length; ++i) {
                labelMetrics.text = String(options[i])
                widest = Math.max(widest, labelMetrics.advanceWidth)
            }
            widestLabelWidth = Math.ceil(widest)
        }
        onModelChanged: measureLabels()
        onLabelFontSizeChanged: measureLabels()
        Component.onCompleted: measureLabels()
        TextMetrics {
            id: labelMetrics
            font.pixelSize: combo.labelFontSize
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
        }

        property var values: []
        property Item leftTarget: null
        property Item rightTarget: null
        property Item upTarget: null
        property Item downTarget: null
        property int labelFontSize: Theme.fontSizeBody
        property string previousNavigationMode: "pointer"
        signal valueAccepted(var value)

        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.ComboBox

        function selectedValue() {
            if (currentIndex >= 0 && currentIndex < values.length)
                return values[currentIndex]
            return ""
        }

        function commitIndex(index) {
            if (index < 0 || index >= count)
                return
            currentIndex = index
            valueAccepted(selectedValue())
            popup.close()
            Qt.callLater(function() { combo.forceActiveFocus() })
        }

        Keys.priority: Keys.BeforeItem
        Keys.onReturnPressed: (event) => {
            if (!event.isAutoRepeat)
                popup.open()
            event.accepted = true
        }
        Keys.onEnterPressed: (event) => {
            if (!event.isAutoRepeat)
                popup.open()
            event.accepted = true
        }
        Keys.onLeftPressed: (event) => {
            if (!popup.visible && leftTarget) {
                leftTarget.forceActiveFocus()
                event.accepted = true
            }
        }
        Keys.onRightPressed: (event) => {
            if (!popup.visible && rightTarget) {
                rightTarget.forceActiveFocus()
                event.accepted = true
            }
        }
        Keys.onUpPressed: (event) => {
            if (!popup.visible) {
                if (upTarget)
                    upTarget.forceActiveFocus()
                event.accepted = true
            }
        }
        Keys.onDownPressed: (event) => {
            if (!popup.visible) {
                if (downTarget)
                    downTarget.forceActiveFocus()
                event.accepted = true
            }
        }

        onActivated: valueAccepted(selectedValue())

        background: Rectangle {
            id: comboBackground
            implicitHeight: Theme.buttonHeightMedium
            radius: Theme.radiusControl
            color: combo.hovered || combo.activeFocus ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
            border.width: Theme.borderWidth
            border.color: Theme.surfaceBorder

            FocusRing {
                active: combo.activeFocus || combo.popup.visible
                radius: comboBackground.radius
            }
        }

        indicator: Text {
            x: combo.width - width - Theme.spacingMedium
            y: (combo.height - height) / 2
            text: Icons.expandMore
            font.family: Theme.fontIcon
            font.pixelSize: Theme.fontSizeIcon
            color: Theme.textSecondary
        }

        contentItem: Text {
            text: combo.displayText
            font.pixelSize: combo.labelFontSize
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.spacingMedium
            rightPadding: Theme.spacingMedium + Theme.fontSizeIcon + Theme.spacingSmall
            elide: Text.ElideRight
        }

        delegate: ItemDelegate {
            id: comboDelegate
            width: combo.width - Theme.spacingSmall * 2
            implicitHeight: Theme.buttonHeightMedium
            highlighted: ListView.isCurrentItem || combo.highlightedIndex === index
            onClicked: combo.commitIndex(index)

            contentItem: RowLayout {
                spacing: Theme.spacingSmall
                Text {
                    text: modelData
                    color: Theme.textPrimary
                    font.pixelSize: combo.labelFontSize
                    font.family: Theme.fontPrimary
                    font.weight: index === combo.currentIndex ? Theme.fontWeightStrong : Theme.fontWeightRegular
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    visible: index === combo.currentIndex
                    text: Icons.check
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.fontSizeIcon
                    color: Theme.selectionIndicator
                }
            }

            background: Rectangle {
                color: comboDelegate.highlighted ? Theme.surfaceInteractiveHover : "transparent"
                radius: Theme.radiusSmall
                FocusRing {
                    inset: true
                    active: comboDelegate.highlighted && combo.popup.visible
                    radius: parent.radius
                }
            }
        }

        popup: Popup {
            y: combo.height + Theme.spacingSmall
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, Math.round(420 * Theme.layoutScale))
            padding: Theme.spacingSmall
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            onOpened: {
                previousNavigationMode = InputModeManager.pointerActive ? "pointer" : "keyboard"
                InputModeManager.setNavigationMode("keyboard")
                InputModeManager.hideCursor(true)
                comboList.currentIndex = combo.highlightedIndex >= 0 ? combo.highlightedIndex : combo.currentIndex
                comboList.forceActiveFocus()
            }
            onClosed: {
                Qt.callLater(function() { combo.forceActiveFocus() })
                InputModeManager.setNavigationMode(previousNavigationMode)
                InputModeManager.hideCursor(previousNavigationMode !== "pointer")
            }

            contentItem: ListView {
                id: comboList
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex >= 0 ? combo.highlightedIndex : combo.currentIndex
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator { }

                Keys.onReturnPressed: (event) => {
                    combo.commitIndex(currentIndex)
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    combo.commitIndex(currentIndex)
                    event.accepted = true
                }
                Keys.onEscapePressed: (event) => {
                    combo.popup.close()
                    event.accepted = true
                }
                Keys.onBackPressed: (event) => {
                    combo.popup.close()
                    event.accepted = true
                }
            }

            background: DialogSurface { floating: true; radius: Theme.radiusMedium }
        }
    }

    function saveFocusForNavigation() {
        if (contentLoader.item && typeof contentLoader.item.saveFocusForNavigation === "function") {
            contentLoader.item.saveFocusForNavigation()
        }
    }

    function restoreFocusFromNavigation() {
        restoringFocusFromNavigation = true
        if (contentLoader.item && typeof contentLoader.item.restoreFocusFromNavigation === "function") {
            contentLoader.item.restoreFocusFromNavigation()
        } else if (contentLoader.item) {
            contentLoader.item.forceActiveFocus()
        } else {
            forceActiveFocus()
        }
        Qt.callLater(function() {
            restoringFocusFromNavigation = false
        })
    }

    function resetQueryToolbarVisibility() {
        queryToolbarPinned = true
        queryToolbarScrollArmed = false
        queryToolbarScrollArmTimer.restart()
    }

    function syncComboIndex(combo, value) {
        if (!combo || !combo.values)
            return
        for (var i = 0; i < combo.values.length; ++i) {
            if (combo.values[i] === value) {
                combo.currentIndex = i
                return
            }
        }
        combo.currentIndex = 0
    }

    function normalizeActiveFilterCategory() {
        if (activeFilterCategory !== "tags" && activeFilterCategory !== "studios")
            activeFilterCategory = "genres"
    }

    function toggleActiveFacet(name) {
        if (!name)
            return
        if (activeFilterCategory === "tags") {
            LibraryViewModel.toggleTag(name)
        } else if (activeFilterCategory === "studios") {
            LibraryViewModel.toggleStudio(name)
        } else {
            LibraryViewModel.toggleGenre(name)
        }
        applyFilters("facetGrid")
    }

    function restoreUpNextEpisodeContext(episodeData, fallbackSeriesId, audioIndex, subtitleIndex) {
        var itemData = Object.assign({}, episodeData || {})
        var targetSeriesId = fallbackSeriesId || itemData.seriesId || currentSeriesId || ""
        var targetSeasonId = itemData.seasonId || itemData.parentId || currentSeasonId || ""
        var targetEpisodeId = itemData.itemId || ""

        console.log("[Library] Restoring Up Next context",
                    "seriesId:", targetSeriesId,
                    "seasonId:", targetSeasonId,
                    "episodeId:", targetEpisodeId)

        pendingAudioTrackIndex = audioIndex
        pendingSubtitleTrackIndex = subtitleIndex

        if (targetSeriesId) {
            currentSeriesId = targetSeriesId
        }
        if (targetSeasonId) {
            currentSeasonId = targetSeasonId
        }
        initialEpisodeId = targetEpisodeId

        showMovieDetails = false
        if (targetSeasonId) {
            showSeasonView = true
            showSeriesDetails = false
        } else {
            showSeasonView = false
            showSeriesDetails = true
        }

        if (targetSeriesId && SeriesDetailsViewModel.seriesId !== targetSeriesId) {
            SeriesDetailsViewModel.loadSeriesDetails(targetSeriesId)
        } else if (targetSeriesId) {
            LibraryService.getSeriesDetails(targetSeriesId)
        }

        if (targetSeasonId) {
            SeriesDetailsViewModel.refreshSeasonEpisodes(targetSeasonId)
        } else if (targetSeriesId) {
            LibraryService.getSeasons(targetSeriesId)
        }

        Qt.callLater(function() {
            if (!contentLoader.item) {
                root.forceActiveFocus()
                return
            }
            if (targetEpisodeId && typeof contentLoader.item.restoreEpisodeSelection === "function") {
                contentLoader.item.restoreEpisodeSelection(targetEpisodeId)
            }
            contentLoader.item.forceActiveFocus()
        })
    }

    function saveSeriesDetailsReturnState() {
        if (contentLoader.item && typeof contentLoader.item.saveReturnState === "function") {
            _seriesDetailsReturnState = contentLoader.item.saveReturnState()
            _restoreSeriesDetailsReturnState = false
        }
    }

    function consumeSeriesDetailsReturnState() {
        var state = _seriesDetailsReturnState
        _seriesDetailsReturnState = null
        _restoreSeriesDetailsReturnState = false
        restoringFocusFromSeriesDetailsReturn = false
        return state
    }

    function clearSeriesDetailsReturnState() {
        _seriesDetailsReturnState = null
        _restoreSeriesDetailsReturnState = false
        restoringFocusFromSeriesDetailsReturn = false
    }

    function saveMovieDetailsReturnState() {
        if (contentLoader.item && typeof contentLoader.item.saveReturnState === "function") {
            _movieDetailsReturnState = contentLoader.item.saveReturnState()
            _restoreMovieDetailsReturnState = false
        }
    }

    function clearMovieDetailsReturnState() {
        _movieDetailsReturnState = null
        _restoreMovieDetailsReturnState = false
        restoringFocusFromMovieDetailsReturn = false
    }
    
    // Only show pagination/filter controls at the library level (not in series/seasons/episodes/movies)
    property bool showPaginationControls: {
        if (currentParentId === "") return false  // Top level (library list)
        if (showSeriesDetails) return false  // Hide when showing series details
        if (showSeasonView) return false  // Hide when showing season view
        if (showMovieDetails) return false  // Hide when showing movie details
        // Show controls when we're in a library (no series selected) - this means we're viewing Series or Movies
        // When currentSeriesId is set, we're inside a series viewing seasons/episodes
        return currentSeriesId === ""
    }
    
    // Alias for loading state from ViewModel
    property bool isLoading: LibraryViewModel.isLoading

    Timer {
        id: searchDebounceTimer
        interval: 300
        repeat: false
        onTriggered: {
            LibraryViewModel.setSearchTerm(librarySearchText)
            applyFilters("searchField")
        }
    }

    onCurrentParentIdChanged: {
        if (currentParentId !== "" && currentSeriesId === "" && !showSeriesDetails && !showSeasonView && !showMovieDetails) {
            resetQueryToolbarVisibility()
        }
        if (componentReady) {
            loadItemsForCurrentParent()
        }
    }
    
    // Ensure focus is transferred to content when screen becomes active
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            console.log("[LibraryScreen] Screen activated, transferring focus to content")
            if (pendingEpisodeData) {
                var episodeData = pendingEpisodeData
                pendingEpisodeData = null
                Qt.callLater(function() {
                    if (root.showEpisodeDetails) {
                        root.showEpisodeDetails(episodeData)
                    }
                })
            }
            if (!restoringFocusFromNavigation
                    && !restoringFocusFromSeriesDetailsReturn
                    && !restoringFocusFromMovieDetailsReturn) {
                Qt.callLater(function() {
                    if (contentLoader.item) {
                        contentLoader.item.forceActiveFocus()
                    }
                })
            }
        }
        updateThemeSongPlayback()
    }
    
    function updateThemeSongPlayback() {
        if (!ThemeSongManager || typeof ThemeSongManager.play !== "function") {
            return
        }
        
        var isActive = StackView.status === StackView.Active
        var playbackActive = PlayerController && PlayerController.isPlaybackActive

        if (isActive && inSeriesContext && !playbackActive && ConfigManager.themeSongVolume > 0) {
            ThemeSongManager.play(currentSeriesId)
        } else {
            ThemeSongManager.fadeOutAndStop()
        }
        
        // Keep loop setting in sync with config
        if (typeof ThemeSongManager.setLoopEnabled === "function") {
            ThemeSongManager.setLoopEnabled(ConfigManager.themeSongLoop)
        }
    }
    
    onInSeriesContextChanged: updateThemeSongPlayback()
    onCurrentSeriesIdChanged: updateThemeSongPlayback()
    
    Connections {
        target: ConfigManager
        function onThemeSongVolumeChanged() { updateThemeSongPlayback() }
        function onThemeSongLoopChanged() {
            if (ThemeSongManager && typeof ThemeSongManager.setLoopEnabled === "function") {
                ThemeSongManager.setLoopEnabled(ConfigManager.themeSongLoop)
            }
        }
    }
    
    Connections {
        target: PlayerController
        function onIsPlaybackActiveChanged() { updateThemeSongPlayback() }
    }

    // Back shortcut only when browsing library grid/folders; detail views handle their own back key
    Shortcut {
        sequences: ["Esc", "Back"]
        enabled: currentParentId !== "" && !showSeriesDetails && !showSeasonView && !showMovieDetails
        onActivated: navigateBack()
    }
    
    // Rectangle {
    //     anchors.fill: parent
    //     color: "#06060f"
    //     z: -3
    // }

    property var currentBackdropCandidates: []
    property int currentBackdropCandidateIndex: 0
    property int backdropRotationInterval: ConfigManager.backdropRotationInterval // user-configurable, default 30 seconds
    // Rounded image mode: "auto" tries pre-rounded assets first, then shader; "shader" forces shader; "prerender" prefers pre-rounded only.
    property string roundedImageMode: ConfigManager.roundedImageMode || "auto"
    property bool roundedPreprocessEnabled: ConfigManager.roundedImagePreprocessEnabled

    // Stage: the focused item's artwork, softened behind the grid; sharp
    // behind detail views, which draw their own hero over it.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        z: 0
        mode: (root.showMovieDetails || root.showSeriesDetails || root.showSeasonView) ? "hero" : "ambient"
        source: root.currentBackdropUrl
        onArtworkFailed: root.loadNextBackdropCandidate()
    }

    // Timer to cycle backdrop candidates when viewing a Series/Season/Episode/Movie detail
    Timer {
        id: backdropRotationTimer
        interval: root.backdropRotationInterval
        repeat: true
        // Only run when we have multiple candidates and we're in a details view (including movies)
        running: root.currentBackdropCandidates.length > 1
                 && (root.showSeriesDetails || root.showSeasonView || root.showMovieDetails)
                 && !PlayerController.isPlaybackActive
        onTriggered: {
            cycleBackdropCandidate()
        }
    }

    // Main Content - conditionally show SeriesDetailsView, SeasonView, EpisodeView, MovieDetailsView or regular grid
    Loader {
        id: contentLoader
        anchors.fill: parent
        anchors.margins: 0
        z: 1
        
        sourceComponent: {
            if (showMovieDetails) return movieDetailsComponent
            if (showSeasonView) return seasonViewComponent
            if (showSeriesDetails) return seriesDetailsComponent
            return libraryGridComponent
        }
        
        // Transfer focus to loaded content when it changes
        onLoaded: {
            if (item
                    && !root.restoringFocusFromNavigation
                    && !root.restoringFocusFromSeriesDetailsReturn
                    && !root.restoringFocusFromMovieDetailsReturn) {
                item.forceActiveFocus()
            }
        }
        
        // Also handle status changes for initial load
        onStatusChanged: {
            if (status === Loader.Ready
                    && item
                    && !root.restoringFocusFromNavigation
                    && !root.restoringFocusFromSeriesDetailsReturn
                    && !root.restoringFocusFromMovieDetailsReturn) {
                item.forceActiveFocus()
            }
        }
    }
    
    // Series Details Component
    Component {
        id: seriesDetailsComponent
        
        SeriesDetailsView {
            seriesId: root.currentSeriesId
            pendingSeasonsGridIndex: root._pendingSeasonsGridIndex
            pendingReturnState: root._seriesDetailsReturnState
            restorePendingReturnState: root._restoreSeriesDetailsReturnState
            seerrRecommendationCacheStore: root._seerrRecommendationCache

            onReturnStateConsumed: {
                Qt.callLater(function() {
                    root.clearSeriesDetailsReturnState()
                })
            }
            
            onNavigateToSeasons: function(seasonIndex) {
                // Navigate into selected season
                if (SeriesDetailsViewModel.seasonCount > 0 && seasonIndex >= 0) {
                    root.saveSeriesDetailsReturnState()
                    // Store the season index for navigation context
                    root._lastSelectedSeasonIndex = seasonIndex
                    var season = SeriesDetailsViewModel.seasonsModel.getItem(seasonIndex)
                    if (season && season.itemId) {
                        handleSelection(season)
                    }
                }
            }
            
            onPlayNextEpisode: function(episodeId, startPositionMs) {
                var overlayLogoUrl = ""
                if (SeriesDetailsViewModel.seriesId === root.currentSeriesId
                        && SeriesDetailsViewModel.logoUrl !== "") {
                    overlayLogoUrl = SeriesDetailsViewModel.logoUrl
                } else if (root.currentSeriesData && root.currentSeriesData.logoArtwork) {
                    const artwork = root.currentSeriesData.logoArtwork
                    overlayLogoUrl = LibraryService.getCachedArtworkUrlFromRef(artwork, 600)
                }
                root.requestPlaybackWithResolvedLibrary({
                    itemId: episodeId,
                    startPositionMs: startPositionMs || 0,
                    seriesId: root.currentSeriesId,
                    seasonId: "",
                    overlayTitle: root.currentSeriesData
                                  ? (root.currentSeriesData.name || qsTr("Now Playing"))
                                  : qsTr("Now Playing"),
                    overlaySubtitle: qsTr("Episode"),
                    overlayBackdropUrl: root.currentBackdropUrl,
                    overlayLogoUrl: overlayLogoUrl,
                    preferredAudioIndex: -2,
                    preferredSubtitleIndex: -2,
                    isMovie: false,
                    allowVersionPrompt: true,
                    restoreFocusTarget: null
                })
            }

            onNavigateToEpisode: function(episodeData) {
                root.saveSeriesDetailsReturnState()
                showEpisodeDetails(episodeData)
            }

            onItemSelected: function(itemData) {
                root.saveSeriesDetailsReturnState()
                handleSelection(itemData)
            }
            
            onBackRequested: {
                exitSeriesDetails()
            }
        }
    }
    
    // Merged Season + Episode Browse View Component
    Component {
        id: seasonViewComponent
        
        SeriesSeasonEpisodeView {
            seriesId: root.currentSeriesId
            seriesName: root.currentSeriesData ? (root.currentSeriesData.name || "") : ""
            initialSeasonId: root.currentSeasonId
            initialSeasonIndex: root._lastSelectedSeasonIndex
            initialEpisodeId: root.initialEpisodeId
            pendingAudioTrackIndex: root.pendingAudioTrackIndex
            pendingSubtitleTrackIndex: root.pendingSubtitleTrackIndex
            screenStackActive: root.StackView.status === StackView.Active
            onAutoplayOverridesConsumed: {
                root.pendingAudioTrackIndex = -2
                root.pendingSubtitleTrackIndex = -2
            }
            
            onPlayRequested: function(request) {
                requestPlaybackWithResolvedLibrary(Object.assign({}, request || {}, {
                    seriesId: request && request.seriesId ? request.seriesId : root.currentSeriesId,
                    seasonId: request && request.seasonId ? request.seasonId : SeriesDetailsViewModel.selectedSeasonId
                }))
            }
            
            onSeriesDetailsRequested: function(episodeId) {
                showSeriesDetailsFromSeasonView(episodeId)
            }

            onBackRequested: {
                exitSeasonView()
            }
        }
    }
    
    // Movie Details View Component
    Component {
        id: movieDetailsComponent
        
        MovieDetailsView {
            movieId: root.currentMovieData ? (root.currentMovieData.itemId || "") : ""
            pendingReturnState: root._movieDetailsReturnState
            restorePendingReturnState: root._restoreMovieDetailsReturnState
             
            onPlayRequested: function(request) {
                requestPlaybackWithResolvedLibrary(Object.assign({}, request || {}, {
                    seriesId: "",
                    seasonId: ""
                }))
            }

            onReturnStateConsumed: {
                Qt.callLater(function() {
                    root.clearMovieDetailsReturnState()
                })
            }

            onItemSelected: function(itemData) {
                root.saveMovieDetailsReturnState()
                handleSelection(itemData)
            }
             
            onBackRequested: {
                exitMovieDetails()
            }
        }
    }
    
    // Regular Library Grid Component
    Component {
        id: libraryGridComponent
        
    FocusScope {
        property alias grid: grid

	        function toggleFilterDrawer() {
	            showFilterPanel = !showFilterPanel
	            if (showFilterPanel) {
	                LibraryViewModel.loadFilterOptions(currentParentId, currentLibraryType)
	                Qt.callLater(function() {
	                    favoriteFilterCombo.forceActiveFocus()
	                })
	            } else {
	                filterDrawerButton.forceActiveFocus()
	            }
        }

        function syncFilterControls() {
            syncComboIndex(sortCombo, LibraryViewModel.sortBy)
            syncComboIndex(orderCombo, LibraryViewModel.sortOrder)
            syncComboIndex(favoriteFilterCombo, LibraryViewModel.favoriteFilter)
            syncComboIndex(addedSinceCombo, LibraryViewModel.addedSinceFilter)
            ratingSlider.value = LibraryViewModel.minCommunityRating
        }

        function currentQueryFocusTarget() {
            if (searchField.activeFocus) return "searchField"
            if (sortCombo.activeFocus || sortCombo.popup.visible) return "sortCombo"
            if (orderCombo.activeFocus || orderCombo.popup.visible) return "orderCombo"
            if (filterDrawerButton.activeFocus) return "filterDrawerButton"
            if (favoriteFilterCombo.activeFocus || favoriteFilterCombo.popup.visible) return "favoriteFilterCombo"
            if (addedSinceCombo.activeFocus || addedSinceCombo.popup.visible) return "addedSinceCombo"
            if (ratingSlider.activeFocus) return "ratingSlider"
            if (clearFiltersButton.activeFocus) return "clearFiltersButton"
            if (facetTabs.activeFocus) return "facetTabs"
            if (facetGrid.activeFocus) return "facetGrid"
            return ""
        }

        function queryFocusItem(target) {
            if (target === "searchField") return searchField
            if (target === "sortCombo") return sortCombo
            if (target === "orderCombo") return orderCombo
            if (target === "filterDrawerButton") return filterDrawerButton
            if (target === "favoriteFilterCombo") return favoriteFilterCombo
            if (target === "addedSinceCombo") return addedSinceCombo
            if (target === "ratingSlider") return ratingSlider
            if (target === "clearFiltersButton") return clearFiltersButton
            if (target === "facetTabs") return facetTabs
            if (target === "facetGrid") return facetGrid
            return null
        }

        function rememberQueryFocus(target) {
            pendingQueryFocusTarget = target || currentQueryFocusTarget()
            pendingQueryFilterPanelOpen = showFilterPanel || pendingQueryFocusTarget.indexOf("Filter") >= 0
                    || pendingQueryFocusTarget === "ratingSlider"
                    || pendingQueryFocusTarget === "clearFiltersButton"
                    || pendingQueryFocusTarget === "facetTabs"
                    || pendingQueryFocusTarget === "facetGrid"
            if (pendingQueryFilterPanelOpen) {
                showFilterPanel = true
                normalizeActiveFilterCategory()
            }
            resetQueryToolbarVisibility()
        }

        function restorePendingQueryFocus() {
            var target = pendingQueryFocusTarget
            var shouldKeepFilterPanelOpen = pendingQueryFilterPanelOpen
            pendingQueryFocusTarget = ""
            pendingQueryFilterPanelOpen = false
            if (!target)
                return false
            if (shouldKeepFilterPanelOpen) {
                showFilterPanel = true
                normalizeActiveFilterCategory()
            }
            var item = queryFocusItem(target)
            if (item) {
                item.forceActiveFocus()
                return true
            }
            return false
        }

        function restoreFilterPanelFocus() {
            if (facetGrid.activeFocus) {
                facetGrid.forceActiveFocus()
            } else if (clearFiltersButton.activeFocus) {
                clearFiltersButton.forceActiveFocus()
            } else if (ratingSlider.activeFocus) {
                ratingSlider.forceActiveFocus()
            } else if (addedSinceCombo.activeFocus) {
                addedSinceCombo.forceActiveFocus()
            } else if (favoriteFilterCombo.activeFocus) {
                favoriteFilterCombo.forceActiveFocus()
            } else {
                filterDrawerButton.forceActiveFocus()
            }
        }
        
        // Empty / error state for the grid area.
        StateMessage {
            id: gridStateMessage
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.pageMarginX * 2, Math.round(760 * Theme.layoutScale))
            z: 5
            visible: !isLoading && grid.count === 0 && (LibraryViewModel.hasError || currentParentId !== "")
            kind: LibraryViewModel.hasError ? "error" : "empty"
            title: LibraryViewModel.hasError ? qsTr("Couldn't load this library")
                   : (LibraryViewModel.activeFilterCount > 0 || librarySearchText !== ""
                      ? qsTr("Nothing matches") : qsTr("Nothing here yet"))
            message: LibraryViewModel.hasError ? LibraryViewModel.errorMessage
                     : (LibraryViewModel.activeFilterCount > 0 || librarySearchText !== ""
                        ? qsTr("Try a different search or clear the filters.")
                        : qsTr("This library has no titles."))
            actionText: LibraryViewModel.hasError ? qsTr("Try Again")
                        : (LibraryViewModel.activeFilterCount > 0 ? qsTr("Clear Filters") : "")
            onActionTriggered: {
                if (LibraryViewModel.hasError) {
                    LibraryViewModel.reload()
                } else {
                    LibraryViewModel.clearFilters()
                    syncFilterControls()
                    applyFilters("")
                }
            }
            Keys.onUpPressed: (event) => { searchField.forceActiveFocus(); event.accepted = true }
        }

        // When this component receives focus, delegate to the grid
        onActiveFocusChanged: {
            if (activeFocus && grid) {
                grid.forceActiveFocus()
            }
        }
        
    // The header collapses (and the navigation bar recedes) once the grid is
    // scrolled, so browsing uses the full height. It returns at the top of
    // the grid or whenever a header control has focus.
    readonly property bool headerControlFocused: searchField.activeFocus
                                                 || sortCombo.activeFocus || sortCombo.popup.visible
                                                 || orderCombo.activeFocus || orderCombo.popup.visible
                                                 || filterDrawerButton.activeFocus || backButton.activeFocus
    // Keyboard: expanded while the first row has focus. Pointer: at the top.
    readonly property bool headerExpanded: queryToolbarPinned || headerControlFocused || showFilterPanel
                                           || !grid.activeFocus || grid.count === 0
                                           || (InputModeManager.pointerActive ? grid.atYBeginning
                                                                              : grid.currentIndex < grid.columns)
    readonly property bool navigationRecessed: !headerExpanded

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX - Theme.spacingMedium
        anchors.topMargin: headerExpanded ? Theme.pageMarginTop : Theme.navBarInsetTop
        anchors.bottomMargin: 0
        spacing: Theme.spacingLarge

        Behavior on anchors.topMargin {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: headerExpanded && !showSeriesDetails ? headerRow.implicitHeight : 0
                visible: Layout.preferredHeight > 0
                opacity: headerExpanded ? 1.0 : 0.0
                clip: !headerExpanded

                Behavior on Layout.preferredHeight {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard }
                }
                Behavior on opacity {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationNormal }
                }

            RowLayout {
                id: headerRow
                width: parent.width
                spacing: Theme.controlSpacing

                PageHeader {
                    Layout.fillWidth: true
                    eyebrow: currentParentId === "" ? "" : qsTr("Library")
                    title: currentParentId === "" ? qsTr("Libraries") : (currentLibraryName || qsTr("Library"))
                    detail: {
                        if (currentParentId === "")
                            return ""
                        var loaded = LibraryViewModel.rowCount()
                        var total = LibraryViewModel.totalRecordCount
                        if (total > 0 && loaded > 0)
                            return loaded < total ? qsTr("%1 of %2 titles").arg(loaded).arg(total)
                                                  : qsTr("%1 titles").arg(total)
                        return ""
                    }
                }

	                RowLayout {
	                    id: queryToolbar
	                    Layout.alignment: Qt.AlignBottom
	                    visible: showPaginationControls
	                    spacing: Theme.controlSpacing

                    TextField {
                        id: searchField
                        Layout.preferredWidth: Math.round(380 * Theme.layoutScale)
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        placeholderText: "Search"
                        text: librarySearchText
                        selectByMouse: true
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        leftPadding: Theme.spacingMedium
                        rightPadding: Theme.spacingMedium
                        verticalAlignment: TextInput.AlignVCenter
                        cursorVisible: activeFocus
                        Accessible.name: "Search library"
                        KeyNavigation.right: sortCombo
                        KeyNavigation.down: grid
                        background: Rectangle {
                            id: searchFieldBackground
                            implicitHeight: Theme.buttonHeightMedium
                            radius: height / 2
                            color: searchField.activeFocus ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
                            border.width: Theme.borderWidth
                            border.color: Theme.surfaceBorder
                            FocusRing {
                                active: searchField.activeFocus
                                radius: searchFieldBackground.radius
                            }
                        }
                        onTextEdited: {
                            librarySearchText = text
                            searchDebounceTimer.restart()
                        }
                        Keys.onEscapePressed: (event) => {
                            if (text.length > 0) {
                                text = ""
                                librarySearchText = ""
                                searchDebounceTimer.restart()
                                event.accepted = true
                            }
                        }
                    }

                    LibraryComboBox {
                        id: sortCombo
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        model: ["Library order", "Title", "Release date", "Date added", "Rating", "Year", "Random"]
                        values: ["", "title", "releaseDate", "dateAdded", "rating", "year", "random"]
                        Accessible.name: "Sort library"
                        leftTarget: searchField
                        rightTarget: orderCombo
                        downTarget: grid
                        Component.onCompleted: syncComboIndex(sortCombo, LibraryViewModel.sortBy)
                        onValueAccepted: function(value) {
                            LibraryViewModel.setSortBy(value)
                            applyFilters("sortCombo")
                        }
                    }

                    LibraryComboBox {
                        id: orderCombo
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        model: ["Ascending", "Descending"]
                        values: ["ascending", "descending"]
                        Accessible.name: "Sort order"
                        leftTarget: sortCombo
                        rightTarget: filterDrawerButton
                        downTarget: grid
                        Component.onCompleted: syncComboIndex(orderCombo, LibraryViewModel.sortOrder)
                        onValueAccepted: function(value) {
                            LibraryViewModel.setSortOrder(value)
                            applyFilters("orderCombo")
                        }
                    }

                    ActionButton {
                        id: filterDrawerButton
                        variant: "secondary"
                        text: LibraryViewModel.activeFilterCount > 0 ? ("Filters " + LibraryViewModel.activeFilterCount) : "Filters"
                        iconGlyph: Icons.filter
                        selected: showFilterPanel
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        Accessible.name: text
                        KeyNavigation.left: orderCombo
                        KeyNavigation.right: backButton
                        KeyNavigation.down: showFilterPanel ? favoriteFilterCombo : grid
                        Keys.priority: Keys.BeforeItem
                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                toggleFilterDrawer()
                            }
                            event.accepted = true
                        }
                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat) {
                                toggleFilterDrawer()
                            }
                            event.accepted = true
                        }
                        Keys.onPressed: (event) => {
                            if (event.isAutoRepeat)
                                return
                            if (event.key === Qt.Key_Left) {
                                orderCombo.forceActiveFocus()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Right) {
                                if (backButton.visible) {
                                    backButton.forceActiveFocus()
                                }
                                event.accepted = true
	                            } else if (event.key === Qt.Key_Down) {
	                                if (showFilterPanel) {
	                                    favoriteFilterCombo.forceActiveFocus()
	                                } else {
	                                    grid.forceActiveFocus()
	                                }
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up) {
                                event.accepted = true
                            }
                        }
                        onClicked: toggleFilterDrawer()
                    }
                }
                
                ActionButton {
                    id: backButton
                    variant: "secondary"
                    iconGlyph: Icons.arrowBack
                    visible: currentParentId !== ""
                    text: "Back"
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: "Return to previous library view"
                    Layout.alignment: Qt.AlignBottom
                    Layout.preferredHeight: Theme.buttonHeightMedium
                    
                    KeyNavigation.left: filterDrawerButton
                    KeyNavigation.down: grid
                    
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
                    
                    onClicked: navigateBack()
                }
            }
            }
            
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: showFilterPanel ? Math.round(380 * Theme.layoutScale) : 0
                visible: showFilterPanel
                color: Theme.surfaceGlassStrong
                radius: Theme.radiusPanel
                border.width: Theme.borderWidth
                border.color: Theme.surfaceBorder
                clip: true
                Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.durationNormal } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacingMedium

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

		                        LibraryComboBox {
		                            id: favoriteFilterCombo
		                            	                            Layout.preferredHeight: Theme.buttonHeightMedium
                            model: ["Any favorite", "Favorites", "Not favorite"]
	                            values: ["any", "favorite", "notFavorite"]
		                            Component.onCompleted: syncComboIndex(favoriteFilterCombo, LibraryViewModel.favoriteFilter)
	                                rightTarget: addedSinceCombo
	                                upTarget: filterDrawerButton
	                                downTarget: facetTabs
	                            onValueAccepted: function(value) {
	                                LibraryViewModel.setFavoriteFilter(value)
	                                applyFilters("favoriteFilterCombo")
                            }
                        }
	
	                        LibraryComboBox {
	                            id: addedSinceCombo
	                            	                            Layout.preferredHeight: Theme.buttonHeightMedium
                            model: ["Any added date", "Last 7 days", "Last 30 days", "Last 90 days", "Last year"]
	                            values: ["any", "7d", "30d", "90d", "1y"]
		                            Component.onCompleted: syncComboIndex(addedSinceCombo, LibraryViewModel.addedSinceFilter)
	                                leftTarget: favoriteFilterCombo
	                                rightTarget: ratingSlider
	                                upTarget: filterDrawerButton
	                                downTarget: facetTabs
	                            onValueAccepted: function(value) {
	                                LibraryViewModel.setAddedSinceFilter(value)
	                                applyFilters("addedSinceCombo")
                            }
                        }

	                        Slider {
	                            id: ratingSlider
	                            Layout.preferredWidth: Math.round(220 * Theme.layoutScale)
                                Layout.preferredHeight: Theme.buttonHeightMedium
                            from: 0
                            to: 10
                            stepSize: 0.5
	                            value: LibraryViewModel.minCommunityRating
                            Accessible.name: "Minimum rating"
	                            KeyNavigation.left: addedSinceCombo
                            KeyNavigation.right: clearFiltersButton
                            KeyNavigation.down: facetTabs
                            Keys.priority: Keys.BeforeItem
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    ratingSliderEditing = false
                            }
                            function adjustRating(delta) {
                                value = Math.max(from, Math.min(to, value + delta))
                                LibraryViewModel.setMinCommunityRating(value)
                                applyFilters("ratingSlider")
                            }
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    ratingSliderEditing = !ratingSliderEditing
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                                    ratingSliderEditing = false
                                    event.accepted = true
	                                } else if (event.key === Qt.Key_Left) {
	                                    if (ratingSliderEditing) {
	                                        adjustRating(-stepSize)
	                                    } else {
	                                        addedSinceCombo.forceActiveFocus()
	                                    }
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Right) {
                                    if (ratingSliderEditing) {
                                        adjustRating(stepSize)
                                    } else {
                                        clearFiltersButton.forceActiveFocus()
                                    }
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Up) {
                                    filterDrawerButton.forceActiveFocus()
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Down) {
                                    facetTabs.forceActiveFocus()
                                    event.accepted = true
                                }
                            }
                            onMoved: {
                                LibraryViewModel.setMinCommunityRating(value)
                                applyFilters("ratingSlider")
                            }
                            background: Rectangle {
                                x: ratingSlider.leftPadding
                                y: ratingSlider.topPadding + ratingSlider.availableHeight / 2 - height / 2
                                width: ratingSlider.availableWidth
                                height: Theme.progressHeight
                                radius: height / 2
                                color: Theme.progressTrack

                                Rectangle {
                                    width: ratingSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: Theme.accentPrimary
                                }
                            }
                            handle: Rectangle {
                                x: ratingSlider.leftPadding + ratingSlider.visualPosition * (ratingSlider.availableWidth - width)
                                y: ratingSlider.topPadding + ratingSlider.availableHeight / 2 - height / 2
                                width: Math.round(26 * Theme.layoutScale)
                                height: width
                                radius: width / 2
                                color: ratingSliderEditing || ratingSlider.pressed ? Theme.accentPrimary : Theme.textPrimary
                                FocusRing {
                                    active: ratingSlider.activeFocus
                                    radius: parent.radius
                                    color: ratingSliderEditing ? Theme.selectionIndicator : Theme.focusRing
                                }
                            }
                        }
	
	                        ActionButton {
	                            id: clearFiltersButton
	                            variant: "ghost"
	                            text: "Clear"
	                            Layout.preferredHeight: Theme.buttonHeightMedium
	                            KeyNavigation.left: ratingSlider
	                            KeyNavigation.down: facetTabs
                            onClicked: {
                                LibraryViewModel.clearFilters()
                                syncFilterControls()
                                applyFilters("clearFiltersButton")
                            }
                        }
                    }

	                    TabBar {
	                        id: facetTabs
	                        Layout.fillWidth: false
                            background: Rectangle {
                                color: "transparent"
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: Theme.borderWidth
                                    color: Theme.surfaceBorder
                                }
                            }
	                        KeyNavigation.up: favoriteFilterCombo
	                        KeyNavigation.down: facetGrid
	                        currentIndex: activeFilterCategory === "tags" ? 1 : (activeFilterCategory === "studios" ? 2 : 0)
                            Keys.priority: Keys.BeforeItem
                            Keys.onLeftPressed: (event) => {
                                currentIndex = currentIndex > 0 ? currentIndex - 1 : count - 1
                                forceActiveFocus()
                                event.accepted = true
                            }
                            Keys.onRightPressed: (event) => {
                                currentIndex = currentIndex < count - 1 ? currentIndex + 1 : 0
                                forceActiveFocus()
                                event.accepted = true
                            }
	                            Keys.onUpPressed: (event) => {
	                                favoriteFilterCombo.forceActiveFocus()
	                                event.accepted = true
	                            }
                            Keys.onDownPressed: (event) => {
                                facetGrid.forceActiveFocus()
                                event.accepted = true
                            }
	                        onCurrentIndexChanged: {
	                            activeFilterCategory = currentIndex === 1 ? "tags" : (currentIndex === 2 ? "studios" : "genres")
	                            facetGrid.currentIndex = 0
	                        }
	                        TabButton {
                                width: implicitContentWidth + Theme.spacingXLarge * 2
                                implicitHeight: Theme.buttonHeightMedium
                                text: "Genres"
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.checked ? Theme.textPrimary : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: parent.checked ? Theme.fontWeightStrong : Theme.fontWeightRegular
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.hovered ? Theme.surfaceInteractive : "transparent"
                                    radius: Theme.radiusSmall
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.bottom
                                        width: parent.width * 0.6
                                        height: Theme.selectionIndicatorWidth
                                        radius: height / 2
                                        color: Theme.selectionIndicator
                                        visible: parent.parent.checked
                                    }
                                    FocusRing {
                                        inset: true
                                        active: facetTabs.activeFocus && parent.parent.checked
                                        radius: parent.radius
                                    }
                                }
                            }
	                        TabButton {
                                width: implicitContentWidth + Theme.spacingXLarge * 2
                                implicitHeight: Theme.buttonHeightMedium
                                text: "Tags"
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.checked ? Theme.textPrimary : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: parent.checked ? Theme.fontWeightStrong : Theme.fontWeightRegular
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.hovered ? Theme.surfaceInteractive : "transparent"
                                    radius: Theme.radiusSmall
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.bottom
                                        width: parent.width * 0.6
                                        height: Theme.selectionIndicatorWidth
                                        radius: height / 2
                                        color: Theme.selectionIndicator
                                        visible: parent.parent.checked
                                    }
                                    FocusRing {
                                        inset: true
                                        active: facetTabs.activeFocus && parent.parent.checked
                                        radius: parent.radius
                                    }
                                }
                            }
	                        TabButton {
                                width: implicitContentWidth + Theme.spacingXLarge * 2
                                implicitHeight: Theme.buttonHeightMedium
                                text: currentLibraryType === "tvshows" || currentLibraryType === "series" ? "Networks" : "Studios"
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.checked ? Theme.textPrimary : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: parent.checked ? Theme.fontWeightStrong : Theme.fontWeightRegular
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.hovered ? Theme.surfaceInteractive : "transparent"
                                    radius: Theme.radiusSmall
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.bottom
                                        width: parent.width * 0.6
                                        height: Theme.selectionIndicatorWidth
                                        radius: height / 2
                                        color: Theme.selectionIndicator
                                        visible: parent.parent.checked
                                    }
                                    FocusRing {
                                        inset: true
                                        active: facetTabs.activeFocus && parent.parent.checked
                                        radius: parent.radius
                                    }
                                }
                            }
	                    }
	
	                    GridView {
	                        id: facetGrid
	                        Layout.fillWidth: true
	                        Layout.fillHeight: true
	                        Accessible.role: Accessible.List
	                        Accessible.name: "Filter choices"
	                        clip: true
	                        cellWidth: Math.round(260 * Theme.layoutScale)
	                        cellHeight: Theme.buttonHeightMedium + Theme.spacingMedium
	                        boundsBehavior: Flickable.StopAtBounds
	                        KeyNavigation.up: facetTabs
	                        KeyNavigation.down: grid
	                        model: {
	                            if (activeFilterCategory === "tags") return LibraryViewModel.availableTags
	                            if (activeFilterCategory === "studios") return LibraryViewModel.availableStudios
	                            return LibraryViewModel.availableGenres
	                        }
	                        onModelChanged: currentIndex = count > 0 ? 0 : -1
	                        Keys.onReturnPressed: (event) => {
	                            if (currentIndex >= 0 && currentIndex < count) {
	                                toggleActiveFacet(model[currentIndex])
	                                event.accepted = true
	                            }
	                        }
	                        Keys.onEnterPressed: (event) => {
	                            if (currentIndex >= 0 && currentIndex < count) {
	                                toggleActiveFacet(model[currentIndex])
	                                event.accepted = true
	                            }
	                        }
	                        delegate: Item {
                                id: facetDelegate
	                            width: facetGrid.cellWidth
	                            height: facetGrid.cellHeight
	                            required property string modelData
	                            required property int index
	                            property bool selected: {
	                                if (activeFilterCategory === "tags") return LibraryViewModel.selectedTags.indexOf(modelData) >= 0
	                                if (activeFilterCategory === "studios") return LibraryViewModel.selectedStudios.indexOf(modelData) >= 0
	                                return LibraryViewModel.selectedGenres.indexOf(modelData) >= 0
	                            }
	                            property bool focused: facetGrid.activeFocus && facetGrid.currentIndex === index
	
	                            Rectangle {
                                    id: facetButton
	                                anchors.left: parent.left
	                                anchors.right: parent.right
                                anchors.leftMargin: Theme.spacingXSmall
                                anchors.rightMargin: Theme.spacingXSmall
	                                anchors.verticalCenter: parent.verticalCenter
	                                height: Theme.buttonHeightMedium
                                radius: Theme.radiusControl
                                color: parent.selected ? Theme.selectionBackground : Theme.surfaceInteractive
                                border.width: Theme.borderWidth
                                border.color: parent.selected ? Theme.selectionIndicator : "transparent"
                                FocusRing {
                                    inset: true
                                    active: facetDelegate.focused
                                    radius: parent.radius
                                }
	
	                                Text {
	                                anchors.fill: parent
	                                anchors.leftMargin: Theme.spacingMedium
	                                anchors.rightMargin: Theme.spacingMedium
	                                text: (facetDelegate.selected ? "\u2713  " : "") + facetDelegate.modelData
	                                color: Theme.textPrimary
	                                font.pixelSize: Theme.fontSizeBody
	                                font.family: Theme.fontPrimary
	                                horizontalAlignment: Text.AlignHCenter
	                                verticalAlignment: Text.AlignVCenter
	                                elide: Text.ElideRight
	                            }
	                            }

	                            MouseArea {
	                                anchors.fill: parent
	                                hoverEnabled: true
	                                onClicked: {
	                                    facetGrid.forceActiveFocus()
	                                    facetGrid.currentIndex = index
	                                    toggleActiveFacet(modelData)
	                                }
	                            }
	
	                            Accessible.role: Accessible.CheckBox
	                            Accessible.name: modelData
	                            Accessible.checked: selected
	                        }
	                    }
                }
            }


            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: isLoading
                visible: isLoading
                width: Math.round(72 * Theme.layoutScale)
                height: Math.round(72 * Theme.layoutScale)
                palette.dark: Theme.textPrimary
                palette.light: Theme.accentSecondary
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                Accessible.role: Accessible.List
                Accessible.name: currentParentId === "" ? "Media Libraries" : (currentLibraryName + " Library Items")
                property int columns: Math.max(Theme.gridColumns, 1)

                cellWidth: width / Math.max(columns, 1)
                // Poster cards fill each column, leaving the card gap between them.
                readonly property int cardWidth: Math.round(cellWidth - Theme.cardGap)
                cellHeight: Math.round(cardWidth * 3 / 2) + Theme.cardCaptionHeight + Theme.cardGap

                Behavior on opacity {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: 200 }
                }

                property int savedFocusIndex: -1

                Connections {
                    target: Theme
                    function onGridColumnsChanged() {
                        grid.savedFocusIndex = grid.currentIndex
                        grid.opacity = 0
                        Qt.callLater(function() {
                            grid.columns = Theme.gridColumns
                            grid.opacity = 1
                            grid.restoreFocusAfterColumnChange()
                        })
                    }
                }

                Component.onCompleted: columns = Theme.gridColumns

                function restoreFocusAfterColumnChange() {
                    if (savedFocusIndex >= 0 && savedFocusIndex < count) {
                        currentIndex = savedFocusIndex
                        positionViewAtIndex(savedFocusIndex, GridView.Contain)
                        savedFocusIndex = -1
                    }
                }
                focus: true
                onActiveFocusChanged: {
                    if (activeFocus && showFilterPanel) {
                        showFilterPanel = false
                    }
                    if (activeFocus && count === 0 && gridStateMessage.visible && gridStateMessage.actionText !== "")
                        Qt.callLater(gridStateMessage.focusAction)
                }
                // Allow focused-scale/border overflow at viewport edges.
                clip: false
                boundsBehavior: Flickable.StopAtBounds
                // Fractional wheel/touchpad deltas can be pulled back by row snapping.
                // Keep free scrolling so the first gesture always sticks.
                snapMode: GridView.NoSnap
                flow: GridView.FlowLeftToRight
                // Outer RowLayout margins handle horizontal padding.
                leftMargin: 0
                rightMargin: 0
                // Extra headroom so focused first-row posters/badges do not clip at the top.
                topMargin: Theme.shelfFocusMargin
                preferredHighlightBegin: height * 0.05
                preferredHighlightEnd: height * 0.95
                // ApplyRange can clamp pointer-driven wheel scrolling to the current item.
                // Keep it for keyboard/remote navigation, disable for pointer scrolling.
                highlightRangeMode: (typeof InputModeManager !== "undefined" && InputModeManager.pointerActive)
                    ? GridView.NoHighlightRange
                    : GridView.ApplyRange
                
                // Performance optimizations
                // Cache 3 rows worth of items above and below viewport
                cacheBuffer: Theme.posterHeightLarge * 3
                // Enable item pooling/reuse for better performance
                reuseItems: true
                
                // Infinite scroll detection
                property real loadMoreThreshold: Theme.posterHeightLarge * 2
                
                onContentYChanged: {
                    if (showPaginationControls && queryToolbarScrollArmed) {
                        queryToolbarPinned = contentY <= 1
                    }
                    // Check if we're near the bottom and should load more
                    // Only trigger when we have content and are not already loading
                    if (!isLoading && !LibraryViewModel.isLoadingMore && LibraryViewModel.canLoadMore && contentHeight > height) {
                        var distanceFromBottom = contentHeight - contentY - height
                        if (distanceFromBottom < loadMoreThreshold && distanceFromBottom >= 0) {
                            LibraryViewModel.loadMore(50)
                        }
                    }
                }
                
	                KeyNavigation.up: (showPaginationControls && queryToolbar.visible) ? searchField : (backButton.visible ? backButton : null)

                model: LibraryViewModel

                onCurrentIndexChanged: {
                    // Only update backdrop if we have a valid index and items
                    // Don't clear the backdrop when index is invalid (e.g., during model reload)
                    // This prevents flickering when navigating between views
                    if (currentIndex >= 0 && currentIndex < LibraryViewModel.rowCount()) {
                        var item = LibraryViewModel.getItem(currentIndex)
                        updateBackdropForItem(item)
                    }
                    // Note: We intentionally don't clear backdrop here to prevent flicker
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                WheelStepScroller {
                    anchors.fill: parent
                    target: grid
                    // Match HomeScreen scroll feel.
                    stepPx: grid.cellHeight * 0.45
                    pixelDeltaMultiplier: 0.7
                    z: 2
                }
                
                // Loading indicator footer for infinite scroll
                footer: Item {
                    width: grid.width
                    height: LibraryViewModel.isLoadingMore ? Math.round(80 * Theme.layoutScale) : 0
                    visible: LibraryViewModel.isLoadingMore
                    
                    Behavior on height { NumberAnimation { duration: 200 } }
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.spacingMedium
                        visible: LibraryViewModel.isLoadingMore
                        
                        BusyIndicator {
                            width: Math.round(32 * Theme.layoutScale)
                            height: Math.round(32 * Theme.layoutScale)
                            running: LibraryViewModel.isLoadingMore
                            palette.dark: Theme.textPrimary
                            palette.light: Theme.accentSecondary
                        }
                        
                        Text {
                            text: "Loading more..."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                delegate: Item {
                    id: delegateItem
                    width: grid.cellWidth
                    height: grid.cellHeight
                    z: card.focused ? 2 : 0

                    // Access the full item data from the model
                    required property var modelData
                    required property string imageUrl
                    required property int index

                    MediaCard {
                        id: card
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(Theme.cardGap / 2)
                        anchors.horizontalCenter: parent.horizontalCenter
                        variant: "poster"
                        artworkWidth: grid.cardWidth
                        artworkSources: [delegateItem.imageUrl]
                        title: formatTitle(delegateItem.modelData)
                        subtitle: formatCardSubtitle(delegateItem.modelData)
                        placeholderIcon: delegateItem.modelData.mediaType === "Series" ? Icons.tvShows
                                       : (delegateItem.modelData.mediaType === "Movie" ? Icons.movie : Icons.folder)
                        item: delegateItem.modelData
                        showWatchState: delegateItem.modelData.mediaType === "Series"
                                        || (delegateItem.modelData.mediaType === "Movie" && !!delegateItem.modelData.watched)
                        unplayedCount: delegateItem.modelData.mediaType === "Series"
                                       ? (delegateItem.modelData.unplayedItemCount || 0) : 0
                        watched: !!delegateItem.modelData.watched
                        positionMs: delegateItem.modelData.positionMs || 0
                        durationMs: delegateItem.modelData.durationMs || 0
                        focused: grid.currentIndex === delegateItem.index && grid.activeFocus
                        onClicked: {
                            grid.forceActiveFocus()
                            grid.currentIndex = delegateItem.index
                            handleSelection(delegateItem.modelData)
                        }
                    }

                    Accessible.role: Accessible.ListItem
                    Accessible.name: card.Accessible.name
                    Accessible.description: qsTr("Press to select %1").arg(card.title)
                }
                }



                Keys.onPressed: (event) => {
                    if (event.isAutoRepeat) {
                        return
                    }
                    if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
                        if (currentParentId !== "") {
                            navigateBack()
                            event.accepted = true
                        }
                    }
                }

                // Right from the last column (or last item) moves to the A-Z rail.
                Keys.onRightPressed: (event) => {
                    var lastColumn = (grid.currentIndex % Math.max(1, grid.columns)) === grid.columns - 1
                    if (letterRail.visible && (lastColumn || grid.currentIndex === grid.count - 1)) {
                        letterRail.enter()
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }

                Keys.onReturnPressed: (event) => {
                    if (grid.currentIndex >= 0 && grid.currentIndex < LibraryViewModel.rowCount()) {
                        handleSelection(LibraryViewModel.getItem(grid.currentIndex))
                        event.accepted = true
                    }
                }

                Keys.onEnterPressed: (event) => {
                    if (grid.currentIndex >= 0 && grid.currentIndex < LibraryViewModel.rowCount()) {
                        handleSelection(LibraryViewModel.getItem(grid.currentIndex))
                        event.accepted = true
                    }
                }
            }

        }

        FocusScope {
            id: letterRail
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Math.round(52 * Theme.layoutScale)
            Layout.fillHeight: true
            Layout.maximumHeight: letterBuckets.length * (idealEntryHeight + entrySpacing) - entrySpacing
            visible: letterBuckets.length > 2
            activeFocusOnTab: false
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Jump to letter")

            // Bucket that contains the grid's current item.
            readonly property int activeBucket: {
                var result = -1
                for (var i = 0; i < letterBuckets.length; ++i) {
                    if (grid.currentIndex >= letterBuckets[i].index)
                        result = i
                }
                return result
            }
            property int cursor: 0

            // Shrink entries so every bucket fits the available height.
            readonly property int idealEntryHeight: Math.round(40 * Theme.layoutScale)
            readonly property int entrySpacing: Math.round(2 * Theme.layoutScale)
            readonly property int entryHeight: letterBuckets.length > 0
                ? Math.max(1, Math.min(idealEntryHeight,
                                       Math.floor((height + entrySpacing) / letterBuckets.length) - entrySpacing))
                : idealEntryHeight

            function enter() {
                cursor = Math.max(0, activeBucket)
                forceActiveFocus()
            }

            function commit() {
                if (cursor >= 0 && cursor < letterBuckets.length)
                    jumpToLetter(letterBuckets[cursor].letter)
            }

            Keys.onUpPressed: (event) => { cursor = Math.max(0, cursor - 1); event.accepted = true }
            Keys.onDownPressed: (event) => { cursor = Math.min(letterBuckets.length - 1, cursor + 1); event.accepted = true }
            Keys.onLeftPressed: (event) => { grid.forceActiveFocus(); event.accepted = true }
            Keys.onRightPressed: (event) => { event.accepted = true }
            Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) commit(); event.accepted = true }
            Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) commit(); event.accepted = true }
            Keys.onEscapePressed: (event) => { grid.forceActiveFocus(); event.accepted = true }

            Column {
                id: letterColumn
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                spacing: letterRail.entrySpacing

                Repeater {
                    model: letterBuckets
                    delegate: Item {
                        width: letterColumn.width
                        height: letterRail.entryHeight
                        readonly property bool isActive: index === letterRail.activeBucket
                        readonly property bool isCursor: letterRail.activeFocus && index === letterRail.cursor

                        Rectangle {
                            id: letterChip
                            anchors.centerIn: parent
                            width: letterRail.entryHeight
                            height: width
                            radius: width / 2
                            color: parent.isActive ? Theme.selectionBackground
                                 : (letterMouse.containsMouse ? Theme.surfaceInteractive : "transparent")
                            FocusRing {
                                inset: true
                                active: parent.parent.isCursor
                                radius: letterChip.radius
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.letter
                            color: parent.isActive || parent.isCursor ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Math.min(Theme.fontSizeCaption, Math.round(letterRail.entryHeight * 0.6))
                            font.family: Theme.fontPrimary
                            font.weight: parent.isActive ? Theme.fontWeightHeavy : Theme.fontWeightMedium
                        }

                        MouseArea {
                            id: letterMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: jumpToLetter(modelData.letter)
                        }
                    }
                }
            }
        }
    } // End RowLayout
    } // End FocusScope - libraryGridComponent

    // Clear any pending position restoration values (called when navigating forward)
    function clearPendingRestoreState() {
        _pendingGridIndex = -1
        _pendingGridContentY = -1
        _pendingEpisodeIndex = -1
        _pendingSeasonsGridIndex = -1
        // Note: _lastSelectedSeasonIndex is set before handleSelection and used within it,
        // so we don't clear it here
    }

    function isInitialDirectNavigationEntry() {
        return directNavigationMode
                && navigationStack.length === 0
                && !showSeriesDetails
                && !showSeasonView
                && !showMovieDetails
    }

    function handleSelection(item) {
        const mediaType = item ? (item.mediaType || "") : ""
        const remoteId = item ? (item.itemId || "") : ""
        console.log("[Library] handleSelection",
                    "itemType:", mediaType,
                    "itemId:", remoteId,
                    "currentParentId:", currentParentId,
                    "currentSeriesId:", currentSeriesId)
        
        // Clear pending restore state when navigating forward
        clearPendingRestoreState()
        
        if (mediaType === "Series") {
            // Show series details view
            // Capture current grid position for restoration
            var gridRef = contentLoader.item ? contentLoader.item.grid : null
            var previousContext = {
                parentId: currentParentId,
                seriesId: currentSeriesId,
                showSeriesDetails: showSeriesDetails,
                showSeasonView: showSeasonView,
                showMovieDetails: showMovieDetails,
                seriesDetailsReturnState: _seriesDetailsReturnState,
                movieDetailsReturnState: _movieDetailsReturnState,
                movieData: currentMovieData,
                selectedGenres: selectedGenres.slice(),
                selectedNetworks: selectedNetworks.slice(),
                showFilterPanel: showFilterPanel,
                activeFilterCategory: activeFilterCategory,
                gridIndex: gridRef ? gridRef.currentIndex : -1,
                gridContentY: gridRef ? gridRef.contentY : -1
            }
            navigationStack.push(previousContext)
            console.log("[Library] push navigation context",
                        JSON.stringify(previousContext),
                        "stackSize:", navigationStack.length)
            
            currentSeriesId = remoteId
            currentParentId = remoteId
            showSeriesDetails = true
            showSeasonView = false
            
            // loadItemsForCurrentParent() now preserves the current backdrop while loading;
            // updateBackdropForItem(item) refreshes or restores the series backdrop after navigation changes.
            updateBackdropForItem(item)
            
        } else if (mediaType === "Season") {
            // Show season view with episodes list
            var previousContext = {
                parentId: currentParentId,
                seriesId: currentSeriesId,
                showSeriesDetails: showSeriesDetails,
                showSeasonView: showSeasonView,
                showMovieDetails: showMovieDetails,
                seriesDetailsReturnState: _seriesDetailsReturnState,
                movieDetailsReturnState: _movieDetailsReturnState,
                movieData: currentMovieData,
                seasonId: currentSeasonId,
                seasonName: currentSeasonName,
                seasonNumber: currentSeasonNumber,
                seasonPosterUrl: currentSeasonPosterUrl,
                selectedGenres: selectedGenres.slice(),
                selectedNetworks: selectedNetworks.slice(),
                showFilterPanel: showFilterPanel,
                activeFilterCategory: activeFilterCategory,
                seasonsGridIndex: _lastSelectedSeasonIndex
            }
            navigationStack.push(previousContext)
            console.log("[Library] push navigation context for Season",
                        JSON.stringify(previousContext),
                        "stackSize:", navigationStack.length)
            
            // Keep series context
            const seriesId = resolveSeriesId(item)
            currentSeriesId = seriesId
            
            // Set season properties
            currentSeasonId = remoteId
            currentSeasonNumber = item.indexNumber !== undefined && item.indexNumber >= 0
                    ? item.indexNumber : 0
            currentSeasonName = item.name || ("Season " + currentSeasonNumber)

            currentSeasonPosterUrl = item.imageUrl || ""
            
            showSeriesDetails = false
            showSeasonView = true
            
            // Update backdrop for the season
            updateBackdropForItem(item)
            
        } else if (mediaType === "CollectionFolder" || mediaType === "UserView") {
            // Capture current grid position for restoration
            var gridRef = contentLoader.item ? contentLoader.item.grid : null
            var previousContext = {
                parentId: currentParentId,
                seriesId: currentSeriesId,
                showSeriesDetails: showSeriesDetails,
                showSeasonView: showSeasonView,
                showMovieDetails: showMovieDetails,
                seriesDetailsReturnState: _seriesDetailsReturnState,
                movieDetailsReturnState: _movieDetailsReturnState,
                movieData: currentMovieData,
                selectedGenres: selectedGenres.slice(),
                selectedNetworks: selectedNetworks.slice(),
                showFilterPanel: showFilterPanel,
                activeFilterCategory: activeFilterCategory,
                gridIndex: gridRef ? gridRef.currentIndex : -1,
                gridContentY: gridRef ? gridRef.contentY : -1
            }
            navigationStack.push(previousContext)
            console.log("[Library] push navigation context",
                        JSON.stringify(previousContext),
                        "stackSize:", navigationStack.length)
            if (currentParentId === "") {
                currentLibraryId = remoteId
                currentLibraryName = item.name || ""
                currentLibraryType = item.collectionType || ""
                librarySearchText = ""
                resetQueryToolbarVisibility()
                LibraryViewModel.clearQuery()
                Qt.callLater(function() {
                    if (contentLoader.item && typeof contentLoader.item.syncFilterControls === "function") {
                        contentLoader.item.syncFilterControls()
                    }
                })
                LibraryViewModel.loadFilterOptions(currentLibraryId, currentLibraryType)
            }
            currentSeriesId = ""
            currentParentId = remoteId
            showSeriesDetails = false
            showSeasonView = false
        } else if (mediaType === "Movie") {
            // Show movie details view
            showMovieDetailsView(item)
        } else if (mediaType === "Episode") {
            // Show episode view
            showEpisodeDetails(item)
        }
    }

    function navigateBack() {
        console.log("[Library] navigateBack invoked",
                    "stackSize:", navigationStack.length,
                    "directNavigationMode:", directNavigationMode,
                    "returnToHomeOnDirectBack:", returnToHomeOnDirectBack,
                    "currentParentId:", currentParentId,
                    "currentSeriesId:", currentSeriesId,
                    "showSeriesDetails:", showSeriesDetails,
                    "showSeasonView:", showSeasonView,
                    "showMovieDetails:", showMovieDetails)
        
        // In direct navigation mode with empty stack, prefer returning to the library grid
        // when we have a known library context; otherwise fall back to Home.
        if (directNavigationMode && navigationStack.length === 0) {
            if (returnToHomeOnDirectBack) {
                console.log("[Library] Direct navigation mode with empty stack, returning to Home")
                if (StackView.view) {
                    StackView.view.pop()
                }
            } else if (preferStackPopOnDirectBack) {
                console.log("[Library] Direct navigation mode with empty stack, returning to underlying screen")
                if (StackView.view) {
                    StackView.view.pop()
                }
            } else if (currentLibraryId !== "") {
                console.log("[Library] Direct navigation mode with empty stack, returning to library:", currentLibraryId)
                directNavigationMode = false
                currentSeriesId = ""
                showSeriesDetails = false
                showSeasonView = false
                showMovieDetails = false
                currentSeriesData = null
                currentSeriesSeasons = []
                currentNextEpisode = null
                currentSeasonId = ""
                currentSeasonName = ""
                currentSeasonNumber = 0
                currentSeasonPosterUrl = ""
                initialEpisodeId = ""
                currentMovieData = null
                clearSeriesDetailsReturnState()
                clearMovieDetailsReturnState()

                if (currentParentId !== currentLibraryId) {
                    currentParentId = currentLibraryId
                } else {
                    loadItemsForCurrentParent()
                }
            } else {
                console.log("[Library] Direct navigation mode with empty stack, popping to Home")
                if (StackView.view) {
                    StackView.view.pop()
                }
            }
            return
        }
        
        if (navigationStack.length > 0) {
            var prevContext = navigationStack.pop()
            console.log("[Library] pop navigation context",
                        JSON.stringify(prevContext),
                        "newStackSize:", navigationStack.length)
            
            var targetParentId = prevContext.parentId || ""
            var targetSeriesId = prevContext.seriesId || ""
            var targetShowSeriesDetails = prevContext.showSeriesDetails === true
            var targetShowSeasonView = prevContext.showSeasonView === true
            var targetShowMovieDetails = prevContext.showMovieDetails === true
            
            // Restore filter state
            selectedGenres = prevContext.selectedGenres || []
            selectedNetworks = prevContext.selectedNetworks || []
            showFilterPanel = prevContext.showFilterPanel || false
            activeFilterCategory = prevContext.activeFilterCategory || ""
            
            // Set pending grid position restoration for library grid
            // Only restore if we're going back to the library grid view (not series/season/episode views)
            if (!targetShowSeriesDetails && !targetShowSeasonView && !targetShowMovieDetails) {
                _pendingGridIndex = prevContext.gridIndex !== undefined ? prevContext.gridIndex : -1
                _pendingGridContentY = prevContext.gridContentY !== undefined ? prevContext.gridContentY : -1
                console.log("[Library] Set pending grid restore - index:", _pendingGridIndex, "contentY:", _pendingGridContentY)
            }
            
            // Restore season state if we're going back to season view
            if (targetShowSeasonView) {
                currentSeasonId = prevContext.seasonId || ""
                currentSeasonName = prevContext.seasonName || ""
                currentSeasonNumber = prevContext.seasonNumber || 0
                currentSeasonPosterUrl = prevContext.seasonPosterUrl || ""
                // Set episode index to restore in SeasonView
                if (prevContext.episodeIndex !== undefined && prevContext.episodeIndex >= 0) {
                    _pendingEpisodeIndex = prevContext.episodeIndex
                    console.log("[Library] Set pending episode index:", _pendingEpisodeIndex)
                }
            }
            
            // Restore series details state if we're going back to series details view
            if (targetShowSeriesDetails) {
                // Set seasons grid index to restore in SeriesDetailsView
                if (prevContext.seasonsGridIndex !== undefined && prevContext.seasonsGridIndex >= 0) {
                    _pendingSeasonsGridIndex = prevContext.seasonsGridIndex
                    console.log("[Library] Set pending seasons grid index:", _pendingSeasonsGridIndex)
                }
                _seriesDetailsReturnState = prevContext.seriesDetailsReturnState || null
                _restoreSeriesDetailsReturnState = _seriesDetailsReturnState !== null
                restoringFocusFromSeriesDetailsReturn = _restoreSeriesDetailsReturnState
            } else {
                clearSeriesDetailsReturnState()
            }

            if (targetShowMovieDetails) {
                currentMovieData = prevContext.movieData || null
                _movieDetailsReturnState = prevContext.movieDetailsReturnState || null
                _restoreMovieDetailsReturnState = _movieDetailsReturnState !== null
                restoringFocusFromMovieDetailsReturn = _restoreMovieDetailsReturnState
                if (currentMovieData) {
                    updateBackdropForItem(currentMovieData)
                }
            } else {
                clearMovieDetailsReturnState()
            }
            
            console.log("[Library] Restoring state:",
                        "targetParentId:", targetParentId,
                        "targetSeriesId:", targetSeriesId,
                        "targetShowSeriesDetails:", targetShowSeriesDetails,
                        "targetShowSeasonView:", targetShowSeasonView,
                        "targetShowMovieDetails:", targetShowMovieDetails,
                        "activeFilterCategory:", activeFilterCategory)

            // Clear data when exiting views
            if (showMovieDetails && !targetShowMovieDetails) {
                console.log("[Library] Exiting movie details - clearing data")
                currentMovieData = null
                showMovieDetails = false
            }
            
            if (showSeasonView && !targetShowSeasonView) {
                console.log("[Library] Exiting season view - clearing data")
                currentSeasonId = ""
                currentSeasonName = ""
                currentSeasonNumber = 0
                currentSeasonPosterUrl = ""
                showSeasonView = false
            }
            
            if (showSeriesDetails && !targetShowSeriesDetails) {
                console.log("[Library] Exiting series details - clearing data")
                currentSeriesData = null
                currentSeriesSeasons = []
                currentNextEpisode = null
                showSeriesDetails = false
            }

            // Set IDs before toggling view state so recreated views bind to the correct item immediately.
            currentSeriesId = targetSeriesId

            // Now set the target states
            showSeriesDetails = targetShowSeriesDetails
            showSeasonView = targetShowSeasonView
            showMovieDetails = targetShowMovieDetails
            
            if (currentParentId !== targetParentId) {
                console.log("[Library] Parent ID changed, updating and reloading")
                currentParentId = targetParentId
                // loadItemsForCurrentParent will be called by onCurrentParentIdChanged
            } else if (!showSeriesDetails && !showSeasonView && !showMovieDetails) {
                console.log("[Library] Parent ID same, forcing reload")
                loadItemsForCurrentParent()
            }
        } else if (StackView.view) {
            console.log("[Library] Stack empty, popping StackView")
            StackView.view.pop()
        } else {
            console.log("[Library] Stack empty, resetting to root")
            currentParentId = ""
            currentSeriesId = ""
            showSeriesDetails = false
            showSeasonView = false
            showMovieDetails = false
            loadItemsForCurrentParent()
        }
    }
    
    function exitSeriesDetails() {
        // Helper to exit series details view
        showSeriesDetails = false
        navigateBack()
    }
    
    function exitSeasonView() {
        // Helper to exit season view
        showSeasonView = false
        currentSeasonId = ""
        currentSeasonName = ""
        currentSeasonNumber = 0
        currentSeasonPosterUrl = ""
        initialEpisodeId = ""  // Clear the initial episode ID when exiting
        navigateBack()
    }
    
    function exitMovieDetails() {
        // Helper to exit movie details view
        showMovieDetails = false
        currentMovieData = null
        navigateBack()
    }
    
    function showMovieDetailsView(movieData) {
        // Show movie details view
        // Capture current grid position for restoration unless we came directly from Home
        var shouldPushContext = !isInitialDirectNavigationEntry()
        if (shouldPushContext) {
            var gridRef = contentLoader.item ? contentLoader.item.grid : null
            var previousContext = {
                parentId: currentParentId,
                seriesId: currentSeriesId,
                showSeriesDetails: showSeriesDetails,
                showSeasonView: showSeasonView,
                showMovieDetails: showMovieDetails,
                seriesDetailsReturnState: _seriesDetailsReturnState,
                movieDetailsReturnState: _movieDetailsReturnState,
                selectedGenres: selectedGenres.slice(),
                selectedNetworks: selectedNetworks.slice(),
                showFilterPanel: showFilterPanel,
                activeFilterCategory: activeFilterCategory,
                movieData: currentMovieData,
                gridIndex: gridRef ? gridRef.currentIndex : -1,
                gridContentY: gridRef ? gridRef.contentY : -1
            }
            navigationStack.push(previousContext)
            console.log("[Library] push navigation context for Movie",
                        "stackSize:", navigationStack.length)
        } else {
            console.log("[Library] direct navigation - skipping initial movie context push")
        }
        
        currentMovieData = movieData
        showMovieDetails = true
        showSeasonView = false
        showSeriesDetails = false
        
        // Update backdrop for the movie
        updateBackdropForItem(movieData)
    }
    
    function showEpisodeDetails(episodeData, forcePushContext) {
        // Show episode using the SeriesSeasonEpisodeView to allow full season/episode navigation
        // Capture episode index for restoration when coming back to season view
        const shouldPushContext = forcePushContext === true
                                || !isInitialDirectNavigationEntry()
        const targetEpisodeId = episodeData ? (episodeData.itemId || "") : ""
        var episodeIndex = -1
        if (SeriesDetailsViewModel.episodesModel) {
            for (let i = 0; i < SeriesDetailsViewModel.episodesModel.rowCount(); i++) {
                const ep = SeriesDetailsViewModel.episodesModel.getItem(i)
                if (ep && ep.itemId === targetEpisodeId) {
                    episodeIndex = i
                    break
                }
            }
        }
        if (shouldPushContext) {
            var previousContext = {
                parentId: currentParentId,
                seriesId: currentSeriesId,
                showSeriesDetails: showSeriesDetails,
                showSeasonView: showSeasonView,
                showMovieDetails: showMovieDetails,
                seriesDetailsReturnState: _seriesDetailsReturnState,
                movieDetailsReturnState: _movieDetailsReturnState,
                movieData: currentMovieData,
                seasonId: currentSeasonId,
                seasonName: currentSeasonName,
                seasonNumber: currentSeasonNumber,
                seasonPosterUrl: currentSeasonPosterUrl,
                selectedGenres: selectedGenres.slice(),
                selectedNetworks: selectedNetworks.slice(),
                showFilterPanel: showFilterPanel,
                activeFilterCategory: activeFilterCategory,
                episodeIndex: episodeIndex
            }
            navigationStack.push(previousContext)
            console.log("[Library] push navigation context for Episode",
                        "stackSize:", navigationStack.length)
        } else {
            console.log("[Library] direct navigation - skipping initial episode context push")
        }
        
        currentEpisodeData = episodeData
        // Store the episode ID so SeriesSeasonEpisodeView can highlight it
        initialEpisodeId = episodeData.itemId || ""

        // Extract seasonId from episode data so the view initializes with the correct season.
        if (episodeData) {
            const extractedSeasonId = episodeData.seasonId || episodeData.parentId || ""
            if (extractedSeasonId && currentSeasonId !== extractedSeasonId) {
                console.log("[Library] Using seasonId from episode data:", extractedSeasonId)
                currentSeasonId = extractedSeasonId
            }
        }

        showSeasonView = true
        showSeriesDetails = false
        
        // Ensure series details (including logo) are loaded when navigating directly to an episode
        if (currentSeriesId && SeriesDetailsViewModel.seriesId !== currentSeriesId) {
            console.log("[Library] Loading series details for episode context:", currentSeriesId)
            SeriesDetailsViewModel.loadSeriesDetails(currentSeriesId)
        }
        
        // Update backdrop for the episode
        updateBackdropForItem(episodeData)
        
        // Ensure focus is transferred to the episode view after it loads
        Qt.callLater(function() {
            if (contentLoader.item) {
                contentLoader.item.forceActiveFocus()
            }
        })
    }

    function showSeriesDetailsFromSeasonView(episodeId) {
        if (!currentSeriesId) {
            return
        }
        var episodeIndex = -1
        var lookupEpisodeId = episodeId || initialEpisodeId
        if (SeriesDetailsViewModel.episodesModel && lookupEpisodeId !== "") {
            for (let i = 0; i < SeriesDetailsViewModel.episodesModel.rowCount(); i++) {
                const ep = SeriesDetailsViewModel.episodesModel.getItem(i)
                if (ep && ep.itemId === lookupEpisodeId) {
                    episodeIndex = i
                    break
                }
            }
        }
        var previousContext = {
            parentId: currentParentId,
            seriesId: currentSeriesId,
            showSeriesDetails: showSeriesDetails,
            showSeasonView: showSeasonView,
            showMovieDetails: showMovieDetails,
            seriesDetailsReturnState: _seriesDetailsReturnState,
            movieDetailsReturnState: _movieDetailsReturnState,
            movieData: currentMovieData,
            seasonId: currentSeasonId || SeriesDetailsViewModel.selectedSeasonId,
            seasonName: currentSeasonName,
            seasonNumber: currentSeasonNumber,
            seasonPosterUrl: currentSeasonPosterUrl,
            seasonsGridIndex: SeriesDetailsViewModel.selectedSeasonIndex >= 0 ? SeriesDetailsViewModel.selectedSeasonIndex : -1,
            selectedGenres: selectedGenres.slice(),
            selectedNetworks: selectedNetworks.slice(),
            showFilterPanel: showFilterPanel,
            activeFilterCategory: activeFilterCategory,
            episodeIndex: episodeIndex
        }
        navigationStack.push(previousContext)
        console.log("[Library] push navigation context for Series from season view",
                    "stackSize:", navigationStack.length)
        if (SeriesDetailsViewModel.selectedSeasonIndex >= 0) {
            _pendingSeasonsGridIndex = SeriesDetailsViewModel.selectedSeasonIndex
        }
        if (episodeId) {
            initialEpisodeId = episodeId
        }
        showSeriesDetails = true
        showSeasonView = false
        currentParentId = currentSeriesId
        if (SeriesDetailsViewModel.backdropUrl !== "") {
            currentBackdropUrl = SeriesDetailsViewModel.backdropUrl
        } else if (currentSeriesData) {
            updateBackdropForItem(currentSeriesData)
        }
        Qt.callLater(function() {
            if (contentLoader.item) {
                contentLoader.item.forceActiveFocus()
            }
        })
    }

    function clearBackdropState() {
        root.currentBackdropCandidates = []
        root.currentBackdropCandidateIndex = 0
        root.currentBackdropUrl = ""
    }

    function updateBackdropForItem(item) {
        root.currentBackdropCandidates = buildBackdropCandidates(item)
        root.currentBackdropCandidateIndex = 0
        if (root.currentBackdropCandidates.length === 0) {
            root.currentBackdropUrl = ""
            return
        }
        loadNextBackdropCandidate()
    }

    function loadNextBackdropCandidate() {
        if (root.currentBackdropCandidateIndex >= root.currentBackdropCandidates.length) {
            root.currentBackdropUrl = ""
            return
        }
        root.currentBackdropUrl = root.currentBackdropCandidates[root.currentBackdropCandidateIndex]
        root.currentBackdropCandidateIndex += 1
    }

    // Cycle (wrap) through available backdrop candidates. Used by the rotation timer.
    function cycleBackdropCandidate() {
        var len = root.currentBackdropCandidates.length
        if (!len) {
            root.currentBackdropUrl = ""
            return
        }
        // Ensure index is within bounds
        var idx = root.currentBackdropCandidateIndex % len
        root.currentBackdropUrl = root.currentBackdropCandidates[idx]
        // Advance for next cycle
        root.currentBackdropCandidateIndex = (idx + 1) % len
    }

    function buildBackdropCandidates(item) {
        if (!item)
            return []

        const candidates = []
        function appendArtwork(artwork, fallbackKind, width) {
            if (!artwork || !artwork.itemId)
                return
            const kind = artwork.kind || fallbackKind
            if (!kind)
                return
            const resolvedArtwork = Object.assign({}, artwork, { kind: kind })
            const url = LibraryService.getCachedArtworkUrlFromRef(resolvedArtwork, width)
            if (url && candidates.indexOf(url) === -1)
                candidates.push(url)
        }

        const artwork = item.artwork || []
        for (const ref of artwork) {
            if (ref && ref.kind === "backdrop")
                appendArtwork(ref, "backdrop", 1920)
        }
        if (candidates.length === 0)
            appendArtwork(item.backdropArtwork, "backdrop", 1920)
        if (candidates.length === 0 && (item.seriesId || currentSeriesId)) {
            appendArtwork({
                connectionId: item.connectionId || LibraryService.getActiveConnectionId(),
                itemId: item.seriesId || currentSeriesId,
                kind: "backdrop",
                index: 0,
                tag: ""
            }, "backdrop", 1920)
        }
        if (candidates.length === 0)
            appendArtwork(item.primaryArtwork, "primary", 1920)
        return candidates
    }

    function resolveSeriesId(item) {
        if (!item)
            return ""
        return item.seriesId || item.parentId || currentSeriesId || ""
    }

    function resolveLibraryIdForPlayback() {
        if (currentLibraryId && currentLibraryId !== "") {
            return currentLibraryId
        }
        return ""
    }

    function dispatchPlaybackRequest(request, libraryId) {
        var resolvedLibraryId = libraryId || ""
        var normalizedRequest = Object.assign({}, request || {})
        if (resolvedLibraryId !== "" || !normalizedRequest.hasOwnProperty("libraryId")) {
            normalizedRequest.libraryId = resolvedLibraryId
        }
        if (!normalizedRequest.hasOwnProperty("seriesId")) {
            normalizedRequest.seriesId = root.currentSeriesId || ""
        }
        if (normalizedRequest.seriesId && !normalizedRequest.hasOwnProperty("seasonId")) {
            normalizedRequest.seasonId = root.currentSeasonId || ""
        }

        PlayerController.requestPlayback(normalizedRequest)
    }

    function requestPlaybackWithResolvedLibrary(request) {
        var playbackLibraryId = resolveLibraryIdForPlayback()
        dispatchPlaybackRequest(request, playbackLibraryId)
    }

    function loadItemsForCurrentParent() {
        console.log("[Library] loadItemsForCurrentParent start",
                    "parentId:", currentParentId,
                    "seriesId:", currentSeriesId,
                    "selectedGenres:", selectedGenres,
                    "selectedNetworks:", selectedNetworks,
                    "libraryType:", currentLibraryType,
                    "_pendingGridIndex:", _pendingGridIndex)
        letterBuckets = []
        // NOTE: Don't clear backdrop here - keep it visible while loading
        // The backdrop will be updated in applyItems() once new data arrives
        
        if (!currentParentId) {
            currentLibraryId = ""
            currentLibraryName = ""
            currentLibraryType = ""
            librarySearchText = ""
            LibraryViewModel.loadViews()
        } else {
            // Determine if we should apply pagination based on navigation level
            // - If currentSeriesId is empty: we're viewing a library (Series/Movies) → use incremental loading (50 items at a time)
            // - If currentSeriesId is set: we're viewing seasons/episodes → fetch all items (no pagination)
            var shouldPaginate = (currentSeriesId === "")
            console.log("[Library] pagination decision",
                        "shouldPaginate:", shouldPaginate,
                        "navigationStackDepth:", navigationStack.length)
            
            // For libraries, use incremental loading starting with 50 items
            // For seasons/episodes, fetch all (no limit)
            var startIndex = 0  // Always start from beginning for initial load
            var baseLimit = 200  // Larger first batch to cover small libraries without extra paging
            var limit = shouldPaginate ? baseLimit : 0 // 50 items for incremental loading, 0 means no limit
            
            // If we have a pending position to restore, load enough items to include that index
            // This ensures when navigating back to a library at (e.g.) index 75, we load at least 76 items
            if (shouldPaginate && _pendingGridIndex >= 0) {
                // Load enough items to include the pending index, plus a buffer for scrolling context
                // Round up to the next multiple of baseLimit to avoid odd partial loads
                var neededItems = _pendingGridIndex + 1 + baseLimit  // +1 for zero-indexing, +baseLimit for buffer
                var roundedLimit = Math.ceil(neededItems / baseLimit) * baseLimit
                limit = Math.max(limit, roundedLimit)
                console.log("[Library] Adjusted limit for pending restore - pendingIndex:", _pendingGridIndex, 
                            "neededItems:", neededItems, "adjustedLimit:", limit)
            }
            
            console.log("[Library] computed pagination",
                        "startIndex:", startIndex,
                        "limit:", limit)
            
            // Use LibraryViewModel instead of direct service call
            if (currentParentId.indexOf("combined:") === 0)
                LibraryViewModel.loadCombinedLibraries(combinedLibraryIds, currentLibraryType, limit)
            else
                LibraryViewModel.loadLibrary(currentParentId, currentLibraryType, startIndex, limit)
            if (shouldPaginate) {
                LibraryViewModel.loadFilterOptions(currentParentId, currentLibraryType)
            }
        }
    }

    Timer {
        id: queryToolbarScrollArmTimer
        interval: 400
        repeat: false
        onTriggered: queryToolbarScrollArmed = true
    }
    
    function applyFilters(focusTarget) {
        if (contentLoader.item && typeof contentLoader.item.rememberQueryFocus === "function") {
            contentLoader.item.rememberQueryFocus(focusTarget || "")
        }
        // Reload from beginning when filters change
        loadItemsForCurrentParent()
    }

    // Connect to LibraryViewModel for load completion
    Connections {
        target: LibraryViewModel
        function onLoadComplete() {
            // If showing series details and we loaded seasons
            if (showSeriesDetails && LibraryViewModel.currentParentId === currentSeriesId) {
                // Convert model items to array for currentSeriesSeasons
                var seasons = []
                for (var i = 0; i < LibraryViewModel.rowCount(); i++) {
                    seasons.push(LibraryViewModel.getItem(i))
                }
                currentSeriesSeasons = seasons
                // Ensure focus is set to the content for series details view
                Qt.callLater(function() {
                    if (contentLoader.item
                            && !root.restoringFocusFromNavigation
                            && !root.restoringFocusFromSeriesDetailsReturn
                            && !root.restoringFocusFromMovieDetailsReturn
                            && !contentLoader.item.isRestoringReturnFocus) {
                        contentLoader.item.forceActiveFocus()
                    }
                })
            } else {
                applyItems()
            }
        }
        function onLoadMoreComplete() {
            // Update letter buckets after loading more items
            var items = []
            for (var i = 0; i < LibraryViewModel.rowCount(); i++) {
                items.push(LibraryViewModel.getItem(i))
            }
            updateLetterBuckets(items)
        }
        function onLoadError(error) {
            console.log("[Library] Load error:", error)
        }
    }

    Connections {
        target: SeriesDetailsViewModel

        function onSeriesDataChanged() {
            const responseSeriesId = SeriesDetailsViewModel.seriesId
            if (responseSeriesId !== "" && responseSeriesId !== currentSeriesId) {
                return
            }
            currentSeriesData = SeriesDetailsViewModel.getSeriesData()
            if (responseSeriesId === "") {
                return
            }
            console.log("[Library] Series details loaded:", currentSeriesData.name)
            if (directNavigationMode && showSeriesDetails) {
                updateBackdropForItem(currentSeriesData)
                console.log("[Library] Direct navigation - updated backdrop from series data")
            }
        }

        function onNextEpisodeChanged() {
            const responseSeriesId = SeriesDetailsViewModel.seriesId
            if (responseSeriesId !== "" && responseSeriesId !== currentSeriesId) {
                return
            }
            currentNextEpisode = SeriesDetailsViewModel.getNextEpisodeData()
            console.log("[Library] Next unplayed episode loaded:", currentNextEpisode.name || "None")
        }

        function onSeasonsLoaded() {
            if (SeriesDetailsViewModel.seriesId !== currentSeriesId) {
                return
            }
            const seasons = []
            for (let index = 0; index < SeriesDetailsViewModel.seasonsModel.rowCount(); ++index) {
                seasons.push(SeriesDetailsViewModel.seasonsModel.getItem(index))
            }
            currentSeriesSeasons = seasons
        }
    }

    // Connection to SeriesDetailsViewModel for direct navigation backdrop updates
    Connections {
        target: SeriesDetailsViewModel
        enabled: directNavigationMode && showSeriesDetails
        
        function onBackdropUrlChanged() {
            // If we already have backdrop candidates (rotation), prefer them.
            if (root.currentBackdropCandidates.length > 0) {
                console.log("[Library] Direct navigation - ignoring single backdrop since rotation candidates exist")
                return
            }
            if (SeriesDetailsViewModel.backdropUrl !== "") {
                currentBackdropUrl = SeriesDetailsViewModel.backdropUrl
                console.log("[Library] Direct navigation - backdrop updated from SeriesDetailsViewModel:", currentBackdropUrl)
            }
        }
    }

    // Track whether we started with direct navigation (showSeriesDetails true on load)
    property bool directNavigationMode: false
    // When true, back from direct-navigation details should always pop to Home.
    property bool returnToHomeOnDirectBack: false
    // When true, direct-navigation back with an empty internal stack should pop to the previous StackView item.
    property bool preferStackPopOnDirectBack: false
    // Signal to parent StackView that this screen manages back navigation internally
    property bool handlesOwnBackNavigation: true

    Component.onCompleted: {
        componentReady = true
        
        // If we're starting with showSeriesDetails, showSeasonView, or showMovieDetails,
        // we came from Home screen via direct navigation.
        if (showSeriesDetails || showSeasonView || showMovieDetails) {
            directNavigationMode = true
            console.log("[Library] Direct navigation mode activated")
        }
        
        loadItemsForCurrentParent()
        updateThemeSongPlayback()
    }

    function applyItems() {
        // Update letter buckets from ViewModel data
        var items = []
        for (var i = 0; i < LibraryViewModel.rowCount(); i++) {
            items.push(LibraryViewModel.getItem(i))
        }
        updateLetterBuckets(items)
        
        // Only force focus to grid if we're at top level or just navigated into a folder
        var grid = contentLoader.item ? contentLoader.item.grid : null
        
        // Use Qt.callLater to defer focus - ensures grid delegates are instantiated
        // This is critical for cache hits where data loads synchronously
        if (pendingQueryFocusTarget !== "") {
            Qt.callLater(function() {
                if (contentLoader.item && typeof contentLoader.item.restorePendingQueryFocus === "function") {
                    contentLoader.item.restorePendingQueryFocus()
                }
            })
        } else if ((currentParentId === "" || !LibraryViewModel.isLoadingMore) && !showFilterPanel) {
            Qt.callLater(function() {
                var g = contentLoader.item ? contentLoader.item.grid : null
                if (g) {
                    g.forceActiveFocus()
                }
            })
        } else if (showFilterPanel) {
            Qt.callLater(function() {
                if (contentLoader.item && typeof contentLoader.item.restoreFilterPanelFocus === "function") {
                    contentLoader.item.restoreFilterPanelFocus()
                }
            })
        }
        
        if (grid && grid.count > 0) {
            // Check if we have a pending position to restore
            if (_pendingGridIndex >= 0 && _pendingGridIndex < grid.count) {
                console.log("[Library] Restoring grid position - index:", _pendingGridIndex, "contentY:", _pendingGridContentY)
                grid.currentIndex = _pendingGridIndex
                
                // Restore scroll position if available, otherwise position at the index
                if (_pendingGridContentY >= 0) {
                    grid.contentY = _pendingGridContentY
                } else {
                    grid.positionViewAtIndex(_pendingGridIndex, GridView.Center)
                }
                
                // Update backdrop for the restored item
                var restoredItem = LibraryViewModel.getItem(_pendingGridIndex)
                if (restoredItem) {
                    updateBackdropForItem(restoredItem)
                }
                
                // Clear pending restore values
                _pendingGridIndex = -1
                _pendingGridContentY = -1
            } else {
                // Default behavior: go to first item
                grid.currentIndex = 0
                grid.positionViewAtIndex(0, GridView.Beginning)
                
                // Explicitly update backdrop for the first item since onCurrentIndexChanged
                // won't fire if currentIndex was already 0 (the default value)
                var firstItem = LibraryViewModel.getItem(0)
                if (firstItem) {
                    updateBackdropForItem(firstItem)
                }
            }
        } else {
            clearBackdropState()
        }
    }

    function formatTitle(item) {
        if (!item)
            return ""
        const mediaType = item.mediaType || ""
        const indexNumber = item.indexNumber !== undefined && item.indexNumber >= 0
                ? item.indexNumber : undefined
        const name = item.name || ""
        if (mediaType === "Episode" && indexNumber !== undefined)
            return indexNumber + ". " + name
        return name
    }

    function formatMetaLine(item) {
        if (!item)
            return ""
        const mediaType = item.mediaType || ""
        const childCount = item.childCount || 0
        const durationMs = item.durationMs || 0
        var parts = []
        var year = item.productionYear || extractYear(item.premiereDate)
        if (year)
            parts.push(year)
        if (mediaType === "Series" && childCount)
            parts.push(childCount + " items")
        if (mediaType === "Movie" && durationMs)
            parts.push(Math.round(durationMs / 60000) + " min")
        return parts.join(" • ")
    }

    // Poster subtitle per the style guide: year for movies, a year range for
    // series, "S1 E4 · Title" for episodes.
    function formatCardSubtitle(item) {
        if (!item)
            return ""
        const mediaType = item.mediaType || ""
        if (mediaType === "Episode") {
            const season = item.parentIndexNumber
            const episode = item.indexNumber
            if (season !== undefined && season >= 0 && episode !== undefined && episode >= 0)
                return "S" + season + " E" + episode
            return ""
        }
        if (mediaType === "Season")
            return item.childCount ? qsTr("%n episode(s)", "", item.childCount) : ""
        const start = item.productionYear || extractYear(item.premiereDate)
        if (!start)
            return ""
        if (mediaType === "Series") {
            if (item.status === "Continuing")
                return qsTr("%1 – Present").arg(start)
            const end = extractYear(item.endDate)
            return end && end !== start ? start + " – " + end : String(start)
        }
        return String(start)
    }

    function formatTypeChip(item) {
        if (!item)
            return ""
        switch (item.mediaType || "") {
        case "Series": return "Series"
        case "Season": return "Season"
        case "Movie": return "Movie"
        case "Episode": return "Episode"
        case "CollectionFolder": return "Collection"
        case "UserView": return "Library"
        default: return ""
        }
    }

    function formatCountBadge(item) {
        if (!item)
            return ""
        const mediaType = item.mediaType || ""
        const childCount = item.childCount || 0
        const indexNumber = item.indexNumber !== undefined && item.indexNumber >= 0
                ? item.indexNumber : undefined
        const productionYear = item.productionYear || 0
        if (mediaType === "Series" && childCount)
            return childCount.toString()
        if (mediaType === "Season" && indexNumber !== undefined)
            return "S" + indexNumber
        if (mediaType === "Movie" && productionYear)
            return productionYear.toString()
        return ""
    }

    function extractYear(dateString) {
        if (!dateString)
            return ""
        var d = new Date(dateString)
        if (isNaN(d.getTime()))
            return ""
        return d.getFullYear()
    }

    function updateLetterBuckets(items) {
        var buckets = []
        var seen = {}
        for (var i = 0; i < items.length; ++i) {
            var letter = getSortLetter(items[i])
            if (!seen[letter]) {
                seen[letter] = true
                buckets.push({ letter: letter, index: i })
            }
        }
        letterBuckets = buckets
    }

    function getSortLetter(item) {
        if (!item)
            return "#"
        var base = (item.sortName || item.name || "").trim()
        if (!base.length)
            return "#"
        var ch = base.charAt(0).toUpperCase()
        if (ch < "A" || ch > "Z")
            return "#"
        return ch
    }

    function jumpToLetter(letter) {
        var grid = contentLoader.item ? contentLoader.item.grid : null
        if (!grid) return

        for (var i = 0; i < letterBuckets.length; ++i) {
            if (letterBuckets[i].letter === letter) {
                grid.currentIndex = letterBuckets[i].index
                grid.positionViewAtIndex(letterBuckets[i].index, GridView.Beginning)
                grid.forceActiveFocus()
                break
            }
        }
    }
    
}
