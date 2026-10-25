import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * SearchScreen - Search for movies and TV shows
 * 
 * Features:
 * - Search input field with keyboard focus
 * - Random suggestions when no search term is entered
 * - Categorized results (TV Shows and Movies)
 * - Full keyboard navigation
 * - Escape returns to home screen
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "search"
    
    // Navigation signals
    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)
    
    // ========================================
    // Properties
    // ========================================
    
    property string searchTerm: ""
    property var suggestions: []
    property var movieResults: []
    property var seriesResults: []
    property var seerrResults: []
    readonly property bool isSearching: waitingForLibrarySearch || waitingForSeerrSearch
    property bool hasSearched: false
    property bool waitingForLibrarySearch: false
    property bool waitingForSeerrSearch: false
    property string pendingLibraryConnectionId: ""
    property string pendingLibrarySearchTerm: ""
    // Merged spaces search every enabled source at once.
    readonly property bool mergedMode: typeof MergedCatalog !== "undefined" && MergedCatalog.active

    // Details for a merged result open on the connection that owns it.
    function openSeries(item) {
        sourceRouter.run(item.connectionId || "", function() { root.navigateToSeries(item.itemId) })
    }

    function openMovie(item) {
        sourceRouter.run(item.connectionId || "", function() { root.navigateToMovie(item) })
    }

    function applyLibrarySearchResults(term, movies, series, failed) {
        waitingForLibrarySearch = false
        pendingLibraryConnectionId = ""
        pendingLibrarySearchTerm = ""
        if (term !== searchTerm.trim())
            return
        hasSearched = true
        movieResults = failed ? [] : (movies || [])
        seriesResults = failed ? [] : (series || [])
    }

    function startLibrarySearch(term) {
        pendingLibraryConnectionId = mergedMode ? "" : LibraryService.getActiveConnectionId()
        pendingLibrarySearchTerm = term
        waitingForLibrarySearch = true
        hasSearched = false
        if (mergedMode)
            MergedCatalog.search(term)
        else
            LibraryService.search(term, 50, typeof Workspaces !== "undefined"
                                  ? Workspaces.searchHiddenLibraryIds(pendingLibraryConnectionId) : [])
    }

    SourceRouter {
        id: sourceRouter
    }

    /**
     * Scrolls contentFlickable so that @p item is fully within the visible viewport.
     *
     * Maps the item's top-left into contentColumn coordinates and adjusts
     * contentFlickable.contentY as needed.  A small spacingMedium margin is added
     * so the item is not flush against the viewport edge.
     *
     * @param item  The QML Item that should be brought into view.
     */
    function ensureItemVisibleInResults(item) {
        if (!item || !contentFlickable || !contentColumn) {
            return
        }

        var pointInContent = item.mapToItem(contentColumn, 0, 0)
        var itemTop = pointInContent.y
        var itemBottom = itemTop + item.height
        var viewTop = contentFlickable.contentY
        var viewBottom = viewTop + contentFlickable.height

        if (itemTop < viewTop) {
            contentFlickable.contentY = Math.max(0, itemTop - Theme.spacingMedium)
        } else if (itemBottom > viewBottom) {
            var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(maxY, itemBottom - contentFlickable.height + Theme.spacingMedium)
        }
    }
    
    // Debounce timer for search input
    Timer {
        id: searchDebounce
        interval: 300
        repeat: false
        onTriggered: {
            const normalizedSearchTerm = searchTerm.trim()
            if (normalizedSearchTerm.length > 0) {
                root.startLibrarySearch(normalizedSearchTerm)

                if (SeerrService.isConfigured()) {
                    waitingForSeerrSearch = true
                    seerrResults = []
                    SeerrService.search(normalizedSearchTerm)
                } else {
                    waitingForSeerrSearch = false
                    seerrResults = []
                }
            } else {
                hasSearched = false
                waitingForLibrarySearch = false
                waitingForSeerrSearch = false
                pendingLibraryConnectionId = ""
                pendingLibrarySearchTerm = ""
                movieResults = []
                seriesResults = []
                seerrResults = []
            }
        }
    }
    
    // ========================================
    // Initialization
    // ========================================
    
    Component.onCompleted: {
        // Load random suggestions
        LibraryService.getRandomItems(20)
        // Focus search input
        searchInput.forceActiveFocus()
    }
    
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            searchInput.forceActiveFocus()
        }
    }
    
    // ========================================
    // API Connections
    // ========================================
    
    Connections {
        target: LibraryService

        function onCanonicalRandomItemsLoaded(connectionId, items) {
            if (connectionId !== LibraryService.getActiveConnectionId()) {
                return
            }
            suggestions = items || []
        }

        function onCanonicalRandomItemsFailed(connectionId, error) {
            if (connectionId === LibraryService.getActiveConnectionId()) {
                suggestions = []
            }
        }

        function onCanonicalSearchResultsLoaded(connectionId, term, movies, series) {
            if (root.mergedMode || connectionId !== pendingLibraryConnectionId
                    || term !== pendingLibrarySearchTerm) {
                return
            }
            waitingForLibrarySearch = false
            pendingLibraryConnectionId = ""
            pendingLibrarySearchTerm = ""
            if (connectionId !== LibraryService.getActiveConnectionId()
                    || term !== searchTerm.trim()) {
                return
            }
            hasSearched = true
            movieResults = movies || []
            seriesResults = series || []
        }

        function onCanonicalSearchResultsFailed(connectionId, term, error) {
            if (root.mergedMode || connectionId !== pendingLibraryConnectionId
                    || term !== pendingLibrarySearchTerm) {
                return
            }
            waitingForLibrarySearch = false
            pendingLibraryConnectionId = ""
            pendingLibrarySearchTerm = ""
            if (connectionId !== LibraryService.getActiveConnectionId()
                    || term !== searchTerm.trim()) {
                return
            }
            hasSearched = true
            movieResults = []
            seriesResults = []
        }
    }

    Connections {
        target: typeof MergedCatalog !== "undefined" ? MergedCatalog : null
        ignoreUnknownSignals: true

        function onSearchResultsLoaded(term, movies, series) {
            if (!root.mergedMode || term !== root.pendingLibrarySearchTerm)
                return
            root.applyLibrarySearchResults(term, movies, series, false)
        }

        function onSearchResultsFailed(term, error) {
            if (!root.mergedMode || term !== root.pendingLibrarySearchTerm)
                return
            root.applyLibrarySearchResults(term, [], [], true)
        }

        // Editing the space's sources drops in-flight member requests and
        // invalidates shown results; search the new source list again.
        function onActiveChanged() {
            const term = root.searchTerm.trim()
            if (term.length === 0 || (!root.waitingForLibrarySearch && !root.hasSearched))
                return
            // Results from a removed or disabled source must not stay selectable.
            root.movieResults = []
            root.seriesResults = []
            root.startLibrarySearch(term)
        }
    }

    // A single space changed which libraries search covers; merged spaces
    // search again through MergedCatalog.activeChanged.
    Connections {
        target: typeof Workspaces !== "undefined" ? Workspaces : null
        ignoreUnknownSignals: true
        function onLibraryVisibilityChanged() {
            const term = root.searchTerm.trim()
            if (root.mergedMode || term.length === 0
                    || (!root.waitingForLibrarySearch && !root.hasSearched))
                return
            root.startLibrarySearch(term)
        }
    }

    Connections {
        target: SeerrService

        function onSearchResultsLoaded(term, results) {
            if (term === searchTerm.trim()) {
                waitingForSeerrSearch = false

                seerrResults = []
                for (var i = 0; i < results.length; i++) {
                    seerrResults.push(results[i])
                }
                seerrResultsChanged()
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint === "search") {
                waitingForSeerrSearch = false
            }
        }
    }
    
    // ========================================
    // Background
    // ========================================
    
    // Quiet theme backdrop (search has no artwork of its own), full-bleed
    // under the floating rail.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -Theme.navRailReservedWidth
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.mix(Theme.backgroundPrimary, Theme.accentPrimary, 0.08) }
            GradientStop { position: 0.5; color: Theme.backgroundPrimary }
            GradientStop { position: 1.0; color: Theme.backgroundSecondary }
        }
    }
    
    // ========================================
    // Main Content
    // ========================================
    
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageMarginTop
        spacing: Theme.spacingXLarge
        
        // Search Input Container
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge + Theme.spacingMedium
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            
            RowLayout {
                anchors.left: parent.left
                width: Math.min(parent.width, Math.round(1100 * Theme.layoutScale))
                height: parent.height
                spacing: Theme.spacingMedium
                
                // Search input field
                Rectangle {
                    id: searchFieldSurface
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.buttonHeightLarge
                    radius: Theme.radiusControl
                    color: searchInput.activeFocus ? Theme.surfaceGlassStrong : Theme.surfaceGlass
                    border.width: Theme.borderWidth
                    border.color: Theme.surfaceBorder

                    FocusRing {
                        active: searchInput.activeFocus
                        radius: searchFieldSurface.radius
                    }
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLarge
                        anchors.rightMargin: Theme.spacingLarge
                        spacing: Theme.spacingMedium

                        Text {
                            text: Icons.search
                            font.pixelSize: Theme.iconSizeNav
                            font.family: Theme.fontIcon
                            color: searchInput.activeFocus ? Theme.textPrimary : Theme.textSecondary
                            Layout.alignment: Qt.AlignVCenter
                        }
                        
                        TextField {
                            id: searchInput
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: qsTr("Search movies and shows")
                            placeholderTextColor: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMedium
                            verticalAlignment: TextInput.AlignVCenter
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            background: null
                            focus: true
                            
                            onTextChanged: {
                                searchTerm = text
                                searchDebounce.restart()
                            }
                            
                            Keys.onDownPressed: {
                                // Navigate to results or suggestions
                                if (hasSearched && seriesResults.length > 0) {
                                    seriesGrid.forceActiveFocus()
                                    seriesGrid.currentIndex = 0
                                } else if (hasSearched && movieResults.length > 0) {
                                    moviesGrid.forceActiveFocus()
                                    moviesGrid.currentIndex = 0
                                } else if (hasSearched && seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                } else if (!hasSearched && suggestions.length > 0) {
                                    suggestionsColumn.children[0].forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Content Area (scrollable)
        Flickable {
            id: contentFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.height
            clip: true
            
            ColumnLayout {
                id: contentColumn
                width: parent.width
                spacing: Theme.spacingLarge
                
                // ========================================
                // Suggestions (shown when no search)
                // ========================================
                
                Item {
                    id: suggestionsSection
                    Layout.fillWidth: true
                    Layout.preferredHeight: suggestionsInner.height
                    visible: !hasSearched && suggestions.length > 0
                    
                    ColumnLayout {
                        id: suggestionsInner
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.pageMarginX
                        spacing: Theme.sectionTitleSpacing
                        
                        // Title
                        SectionHeader {
                            text: qsTr("Try searching for")
                            level: "group"
                        }
                        
                        // Suggestions list (centered, vertical list of titles)
                        ColumnLayout {
                            id: suggestionsColumn
                            Layout.alignment: Qt.AlignLeft
                            spacing: Theme.spacingXSmall
                            
                            Repeater {
                                model: suggestions
                                
                                ItemDelegate {
                                    id: suggestionDelegate
                                    required property var modelData
                                    required property int index
                                    
                                    Layout.alignment: Qt.AlignLeft
                                    Layout.preferredWidth: suggestionText.implicitWidth + Theme.spacingLarge * 2 + Theme.fontSizeIcon
                                    implicitHeight: Theme.buttonHeightSmall
                                
                                background: Rectangle {
                                    id: suggestionBackground
                                    radius: Theme.radiusControl
                                    color: suggestionDelegate.activeFocus || suggestionDelegate.hovered
                                           ? Theme.surfaceInteractiveHover : "transparent"
                                    FocusRing {
                                        active: suggestionDelegate.activeFocus
                                        radius: suggestionBackground.radius
                                    }
                                }
                                
                                contentItem: Item {
                                    implicitWidth: suggestionText.implicitWidth
                                    implicitHeight: suggestionText.implicitHeight
                                    
                                    Text {
                                        id: suggestionText
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.spacingMedium
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: suggestionDelegate.modelData.name || ""
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                        font.weight: suggestionDelegate.activeFocus ? Theme.fontWeightStrong : Theme.fontWeightRegular
                                        color: suggestionDelegate.activeFocus ? Theme.textPrimary : Theme.textSecondary
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                                
                                onClicked: {
                                    const item = modelData
                                    if (item.mediaType === "Series") {
                                        root.openSeries(item)
                                    } else if (item.mediaType === "Movie") {
                                        root.openMovie(item)
                                    }
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
                                
                                Keys.onUpPressed: {
                                    if (index > 0) {
                                        suggestionsColumn.children[index - 1].forceActiveFocus()
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                }
                                
                                Keys.onDownPressed: {
                                    if (index < suggestions.length - 1) {
                                        suggestionsColumn.children[index + 1].forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                    }
                }
                
                // ========================================
                // Search Results
                // ========================================
                
                // Loading indicator
                StateMessage {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sectionSpacing
                    visible: isSearching
                    kind: "loading"
                    compact: true
                    message: qsTr("Searching\u2026")
                }
                
                // No results message
                StateMessage {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sectionSpacing
                    kind: "empty"
                    iconGlyph: Icons.search
                    title: qsTr("No results")
                    message: qsTr("Nothing in your libraries matches \u201c%1\u201d.").arg(searchTerm)
                    visible: hasSearched && !isSearching && seriesResults.length === 0 && movieResults.length === 0
                             && seerrResults.length === 0 && !waitingForSeerrSearch
                }

                Text {
                    text: "Searching Seerr..."
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                    visible: hasSearched
                    opacity: hasSearched ? (waitingForSeerrSearch ? 1 : 0) : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationMedium }
                    }
                }
                
                // TV Shows section
                ColumnLayout {
                    id: seriesSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pageMarginX - Theme.spacingSmall
                    Layout.rightMargin: Theme.pageMarginX - Theme.spacingSmall
                    spacing: Theme.spacingMedium
                    visible: hasSearched && seriesResults.length > 0
                    
                    Text {
                        text: "TV Shows"
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }
                    
                    GridView {
                        id: seriesGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
                        
                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        
                        interactive: false
                        
                        model: seriesResults
                        
                        delegate: FocusScope {
                            id: seriesDelegateScope
                            width: seriesGrid.cellWidth
                            height: seriesGrid.cellHeight
                            
                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(seriesDelegateScope)
                                }
                            }
                            
                            SearchResultCard {
                                id: seriesCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall
                                
                                itemData: seriesDelegateScope.modelData
                                isFocused: seriesDelegateScope.activeFocus
                                
                                onClicked: {
                                    root.openSeries(seriesDelegateScope.modelData)
                                }
                            }
                            
                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seriesCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seriesCard.clicked()
                                event.accepted = true
                            }
                            
                            Keys.onUpPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if (index < columns) {
                                    searchInput.forceActiveFocus()
                                } else {
                                    seriesGrid.currentIndex = index - columns
                                }
                            }
                            
                            Keys.onDownPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if (index + columns < seriesResults.length) {
                                    seriesGrid.currentIndex = index + columns
                                } else if (movieResults.length > 0) {
                                    moviesGrid.forceActiveFocus()
                                    moviesGrid.currentIndex = 0
                                } else if (seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                }
                            }
                            
                            Keys.onLeftPressed: {
                                if (index % Math.floor(seriesGrid.width / seriesGrid.cellWidth) > 0) {
                                    seriesGrid.currentIndex = index - 1
                                }
                            }
                            
                            Keys.onRightPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < seriesResults.length) {
                                    seriesGrid.currentIndex = index + 1
                                }
                            }
                        }
                        
                        onCurrentIndexChanged: {
                            // currentIndex also changes when results load; only move focus
                            // if the user is already navigating this grid, never out of the search field.
                            if (currentItem && seriesGrid.activeFocus) {
                                const item = currentItem
                                Qt.callLater(() => {
                                    if (item) {
                                        item.forceActiveFocus()
                                        root.ensureItemVisibleInResults(item)
                                    }
                                })
                            }
                        }
                    }
                }
                
                // Movies section
                ColumnLayout {
                    id: moviesSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pageMarginX - Theme.spacingSmall
                    Layout.rightMargin: Theme.pageMarginX - Theme.spacingSmall
                    spacing: Theme.spacingMedium
                    visible: hasSearched && movieResults.length > 0
                    
                    Text {
                        text: "Movies"
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }
                    
                    GridView {
                        id: moviesGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
                        
                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        
                        interactive: false
                        
                        model: movieResults
                        
                        delegate: FocusScope {
                            id: movieDelegateScope
                            width: moviesGrid.cellWidth
                            height: moviesGrid.cellHeight
                            
                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(movieDelegateScope)
                                }
                            }
                            
                            SearchResultCard {
                                id: movieCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall
                                
                                itemData: movieDelegateScope.modelData
                                isFocused: movieDelegateScope.activeFocus
                                
                                onClicked: {
                                    root.openMovie(movieDelegateScope.modelData)
                                }
                            }
                            
                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                movieCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                movieCard.clicked()
                                event.accepted = true
                            }
                            
                            Keys.onUpPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if (index < columns) {
                                    if (seriesResults.length > 0) {
                                        seriesGrid.forceActiveFocus()
                                        seriesGrid.currentIndex = Math.min(seriesResults.length - 1, 
                                            seriesResults.length - (seriesResults.length % columns) + index)
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                } else {
                                    moviesGrid.currentIndex = index - columns
                                }
                            }
                            
                            Keys.onDownPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if (index + columns < movieResults.length) {
                                    moviesGrid.currentIndex = index + columns
                                } else if (seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                }
                            }
                            
                            Keys.onLeftPressed: {
                                if (index % Math.floor(moviesGrid.width / moviesGrid.cellWidth) > 0) {
                                    moviesGrid.currentIndex = index - 1
                                }
                            }
                            
                            Keys.onRightPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < movieResults.length) {
                                    moviesGrid.currentIndex = index + 1
                                }
                            }
                        }
                        
                        onCurrentIndexChanged: {
                            // currentIndex also changes when results load; only move focus
                            // if the user is already navigating this grid, never out of the search field.
                            if (currentItem && moviesGrid.activeFocus) {
                                const item = currentItem
                                Qt.callLater(() => {
                                    if (item) {
                                        item.forceActiveFocus()
                                        root.ensureItemVisibleInResults(item)
                                    }
                                })
                            }
                        }
                    }
                }

                // Seerr section
                ColumnLayout {
                    id: seerrSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pageMarginX - Theme.spacingSmall
                    Layout.rightMargin: Theme.pageMarginX - Theme.spacingSmall
                    spacing: Theme.spacingMedium
                    visible: hasSearched && seerrResults.length > 0
                    opacity: visible ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationMedium }
                    }

                    Text {
                        text: "Seerr"
                        font.pixelSize: Theme.fontSizeHeader
                        font.family: Theme.fontPrimary
                        font.weight: Theme.fontWeightHeavy
                        color: Theme.textPrimary
                    }

                    GridView {
                        id: seerrGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight

                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        interactive: false

                        model: seerrResults

                        delegate: FocusScope {
                            id: seerrDelegateScope
                            width: seerrGrid.cellWidth
                            height: seerrGrid.cellHeight

                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(seerrDelegateScope)
                                }
                            }

                            SearchResultCard {
                                id: seerrCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall

                                itemData: seerrDelegateScope.modelData
                                isFocused: seerrDelegateScope.activeFocus

                                onClicked: {
                                    seerrRequestDialog.openForItem(seerrDelegateScope.modelData, seerrDelegateScope)
                                }
                            }

                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seerrCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seerrCard.clicked()
                                event.accepted = true
                            }

                            Keys.onUpPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if (index < columns) {
                                    if (movieResults.length > 0) {
                                        moviesGrid.forceActiveFocus()
                                        moviesGrid.currentIndex = Math.min(movieResults.length - 1, index)
                                    } else if (seriesResults.length > 0) {
                                        seriesGrid.forceActiveFocus()
                                        seriesGrid.currentIndex = Math.min(seriesResults.length - 1, index)
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                } else {
                                    seerrGrid.currentIndex = index - columns
                                }
                            }

                            Keys.onDownPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if (index + columns < seerrResults.length) {
                                    seerrGrid.currentIndex = index + columns
                                }
                            }

                            Keys.onLeftPressed: {
                                if (index % Math.floor(seerrGrid.width / seerrGrid.cellWidth) > 0) {
                                    seerrGrid.currentIndex = index - 1
                                }
                            }

                            Keys.onRightPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < seerrResults.length) {
                                    seerrGrid.currentIndex = index + 1
                                }
                            }
                        }

                        onCurrentIndexChanged: {
                            // currentIndex also changes when results load; only move focus
                            // if the user is already navigating this grid, never out of the search field.
                            if (currentItem && seerrGrid.activeFocus) {
                                const capturedItem = currentItem
                                Qt.callLater(() => {
                                    if (capturedItem) {
                                        capturedItem.forceActiveFocus()
                                        root.ensureItemVisibleInResults(capturedItem)
                                    }
                                })
                            }
                        }
                    }
                }
                
                // Bottom padding
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.spacingXLarge
                }
            }
        }
    }

    WheelStepScroller {
        // contentFlickable lives inside the layout, so attach directly to it.
        parent: contentFlickable
        anchors.fill: parent
        target: contentFlickable
        stepPx: Math.round(120 * Theme.layoutScale)
    }

    SeerrRequestDialog {
        id: seerrRequestDialog
        parent: Overlay.overlay
    }
}
