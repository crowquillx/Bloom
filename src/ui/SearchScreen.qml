import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * SearchScreen - search movies and shows across the active space.
 *
 * Browse archetype (docs/style-guide.md): an ambient stage that picks up the
 * focused result's backdrop, a PageHeader, a large search field, then results
 * as poster shelves (Shows, Movies, Requests). Before a search, a shelf of random
 * suggestions from the active library.
 *
 * Keys: the field is the first focus. Down moves into the first shelf, Up and
 * Down move between shelves, Up from the first shelf returns to the field, and
 * Up from the field reaches the navigation bar. Return in the field searches
 * immediately (or enters the results once they are in).
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
    property var requestResults: []
    readonly property bool isSearching: waitingForLibrarySearch || waitingForRequestSearch
    property bool hasSearched: false
    property bool waitingForLibrarySearch: false
    property bool waitingForRequestSearch: false
    property bool librarySearchFailed: false
    property string librarySearchError: ""
    property string pendingLibraryConnectionId: ""
    property string pendingLibrarySearchTerm: ""
    // Merged spaces search every enabled source at once.
    readonly property bool mergedMode: typeof MergedCatalog !== "undefined" && MergedCatalog.active

    readonly property int libraryResultCount: movieResults.length + seriesResults.length
    readonly property bool showSuggestions: !hasSearched && !waitingForLibrarySearch
                                            && searchTerm.trim().length === 0 && suggestions.length > 0

    /// The navigation bar recedes once the page scrolls past its header.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.spacingLarge

    // Same request width as Home posters, so both pages share image-cache entries.
    readonly property int posterRequestWidth: 640
    readonly property real shelfScrollOffset: Math.round(56 * Theme.layoutScale)

    /// Last focused field or shelf; restored after the navigation bar or a
    /// details page hands focus back.
    property Item lastFocusTarget: null
    /// Backdrop of the focused result, shown softly on the stage.
    property string stageSource: ""
    property string pendingStageSource: ""

    // Details for a merged result open on the connection that owns it.
    function openSeries(item) {
        sourceRouter.run(item.connectionId || "", function() { root.navigateToSeries(item.itemId) })
    }

    function openMovie(item) {
        sourceRouter.run(item.connectionId || "", function() { root.navigateToMovie(item) })
    }

    function openResult(item, list) {
        if (!item)
            return
        if (item.source === "request") {
            requestDialog.openForItem(item, list)
        } else if (item.mediaType === "Series") {
            openSeries(item)
        } else if (item.mediaType === "Movie") {
            openMovie(item)
        }
    }

    function applyLibrarySearchResults(term, movies, series, failed, error) {
        waitingForLibrarySearch = false
        pendingLibraryConnectionId = ""
        pendingLibrarySearchTerm = ""
        if (term !== searchTerm.trim())
            return
        hasSearched = true
        librarySearchFailed = !!failed
        librarySearchError = failed ? (error || "") : ""
        movieResults = failed ? [] : (movies || [])
        seriesResults = failed ? [] : (series || [])
        Qt.callLater(repairFocus)
    }

    function startLibrarySearch(term) {
        pendingLibraryConnectionId = mergedMode ? "" : LibraryService.getActiveConnectionId()
        pendingLibrarySearchTerm = term
        waitingForLibrarySearch = true
        librarySearchFailed = false
        librarySearchError = ""
        hasSearched = false
        if (mergedMode)
            MergedCatalog.search(term)
        else
            LibraryService.search(term, 50, typeof Workspaces !== "undefined"
                                  ? Workspaces.searchHiddenLibraryIds(pendingLibraryConnectionId) : [])
    }

    function runSearchNow() {
        const normalizedSearchTerm = searchTerm.trim()
        if (normalizedSearchTerm.length > 0) {
            root.startLibrarySearch(normalizedSearchTerm)

            if (MediaRequests.available) {
                waitingForRequestSearch = true
                requestResults = []
                MediaRequests.search(normalizedSearchTerm)
            } else {
                waitingForRequestSearch = false
                requestResults = []
            }
        } else {
            hasSearched = false
            waitingForLibrarySearch = false
            waitingForRequestSearch = false
            librarySearchFailed = false
            librarySearchError = ""
            pendingLibraryConnectionId = ""
            pendingLibrarySearchTerm = ""
            movieResults = []
            seriesResults = []
            requestResults = []
            stageDebounce.stop()
            stageSource = ""
        }
    }

    SourceRouter {
        id: sourceRouter
    }

    // ========================================
    // Card text and artwork
    // ========================================

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId)
            return ""
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 0)
    }

    function extractYear(dateString) {
        if (!dateString)
            return ""
        var d = new Date(dateString)
        if (isNaN(d.getTime()))
            return ""
        return d.getFullYear()
    }

    // Style-guide subtitles: movies show the year, series their run
    // ("2019 – Present" or "2019 – 2023"). Request results add their status.
    function resultSubtitle(item) {
        if (!item)
            return ""
        var parts = []
        if (item.source === "request") {
            if (item.productionYear)
                parts.push(String(item.productionYear))
            var status = MediaRequests.statusLabel(item.requestStatus)
            if (status !== "")
                parts.push(status)
            return parts.join("  ·  ")
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

    function showBackdropFor(item) {
        // Request results carry a TMDB backdrop instead of library artwork.
        pendingStageSource = !item ? ""
                             : item.source === "request" ? (item.backdropUrl || "")
                             : artworkUrlFromRef(item.backdropArtwork, 1920)
        stageDebounce.restart()
    }

    // Moving quickly along a shelf shouldn't thrash the stage.
    Timer {
        id: stageDebounce
        interval: 280
        onTriggered: root.stageSource = root.pendingStageSource
    }

    // ========================================
    // Focus and scrolling
    // ========================================

    // Shelves in focus order (visible, non-empty ones only).
    function shelfLists() {
        var lists = []
        var candidates = [suggestionsList, seriesList, moviesList, requestList]
        for (var i = 0; i < candidates.length; i++) {
            var list = candidates[i]
            if (list.parent && list.parent.visible && list.count > 0)
                lists.push(list)
        }
        return lists
    }

    function focusSearchField() {
        searchInput.forceActiveFocus()
        scrollAnimation.to = 0
        scrollAnimation.restart()
    }

    // Down from the field: the first shelf, else the error action.
    function focusFirstResult() {
        var lists = shelfLists()
        if (lists.length > 0) {
            lists[0].forceActiveFocus()
            return true
        }
        if (errorState.visible) {
            errorState.focusAction()
            return true
        }
        return false
    }

    // Moves focus to the shelf above/below. Above the first shelf is the field.
    function moveShelfFocus(fromList, delta) {
        var lists = shelfLists()
        var index = lists.indexOf(fromList)
        var target = index + delta
        if (index >= 0 && target >= 0 && target < lists.length) {
            lists[target].forceActiveFocus()
        } else if (target < 0) {
            focusSearchField()
        }
        return true
    }

    // The first shelf keeps the header and field in view; later shelves
    // scroll near the top so the focused row always lands in the same place.
    function scrollToSection(section) {
        if (!section)
            return
        var lists = shelfLists()
        var y
        if (lists.length > 0 && lists[0].parent === section)
            y = 0
        else
            y = section.mapToItem(contentFlickable.contentItem, 0, 0).y - root.shelfScrollOffset
        var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    function isUsableTarget(target) {
        if (!target || !target.visible)
            return false
        if (target === searchInput)
            return true
        return target.parent && target.parent.visible && target.count > 0
    }

    function restoreLastFocus() {
        var target = lastFocusTarget
        if (target && target !== searchInput && isUsableTarget(target)) {
            target.forceActiveFocus()
            return
        }
        focusSearchField()
    }

    // Results replaced or hidden under a focused shelf: return to the field.
    function repairFocus() {
        var target = lastFocusTarget
        if (!target || target === searchInput || isUsableTarget(target))
            return
        var focusItem = root.Window.activeFocusItem
        if (root.activeFocus || focusItem === null)
            focusSearchField()
    }

    function saveFocusForNavigation() {
        // lastFocusTarget already tracks the field and shelves.
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    // Debounce timer for search input
    Timer {
        id: searchDebounce
        interval: 300
        repeat: false
        onTriggered: root.runSearchNow()
    }

    // ========================================
    // Initialization
    // ========================================

    Component.onCompleted: {
        // Load random suggestions
        LibraryService.getRandomItems(20)
        searchInput.forceActiveFocus()
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
            Qt.callLater(root.restoreLastFocus)
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
                Qt.callLater(root.repairFocus)
            }
        }

        function onCanonicalSearchResultsLoaded(connectionId, term, movies, series) {
            if (root.mergedMode || connectionId !== pendingLibraryConnectionId
                    || term !== pendingLibrarySearchTerm) {
                return
            }
            if (connectionId !== LibraryService.getActiveConnectionId()) {
                waitingForLibrarySearch = false
                pendingLibraryConnectionId = ""
                pendingLibrarySearchTerm = ""
                return
            }
            root.applyLibrarySearchResults(term, movies, series, false, "")
        }

        function onCanonicalSearchResultsFailed(connectionId, term, error) {
            if (root.mergedMode || connectionId !== pendingLibraryConnectionId
                    || term !== pendingLibrarySearchTerm) {
                return
            }
            if (connectionId !== LibraryService.getActiveConnectionId()) {
                waitingForLibrarySearch = false
                pendingLibraryConnectionId = ""
                pendingLibrarySearchTerm = ""
                return
            }
            root.applyLibrarySearchResults(term, [], [], true, error)
        }
    }

    Connections {
        target: typeof MergedCatalog !== "undefined" ? MergedCatalog : null
        ignoreUnknownSignals: true

        function onSearchResultsLoaded(term, movies, series) {
            if (!root.mergedMode || term !== root.pendingLibrarySearchTerm)
                return
            root.applyLibrarySearchResults(term, movies, series, false, "")
        }

        function onSearchResultsFailed(term, error) {
            if (!root.mergedMode || term !== root.pendingLibrarySearchTerm)
                return
            root.applyLibrarySearchResults(term, [], [], true, error)
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
            Qt.callLater(root.repairFocus)
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
            Qt.callLater(root.repairFocus)
        }
    }

    Connections {
        target: MediaRequests

        function onSearchResultsLoaded(term, results) {
            if (term === root.searchTerm.trim()) {
                root.waitingForRequestSearch = false

                var copy = []
                for (var i = 0; i < results.length; i++)
                    copy.push(results[i])
                root.requestResults = copy
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint === "search") {
                root.waitingForRequestSearch = false
            }
        }

        // Seerr was configured or removed, or the Silo account changed:
        // search the backend that now serves requests.
        function onAvailabilityChanged() {
            if (root.hasSearched || root.waitingForLibrarySearch)
                root.runSearchNow()
        }
    }

    // ========================================
    // Stage
    // ========================================

    StageBackdrop {
        id: stage
        anchors.fill: parent
        mode: "ambient"
        source: root.stageSource
    }

    // ========================================
    // Cards
    // ========================================

    Component {
        id: resultCardDelegate

        MediaCard {
            id: resultCard
            required property var modelData
            required property int index
            readonly property bool isRequest: modelData.source === "request"
            variant: "poster"
            artworkSources: [
                resultCard.isRequest ? (modelData.imageUrl || "")
                                   : root.artworkUrlFromRef(modelData.primaryArtwork, root.posterRequestWidth)
            ]
            title: modelData.name || ""
            subtitle: root.resultSubtitle(modelData)
            placeholderIcon: modelData.mediaType === "Series" ? Icons.tvShows : Icons.movie
            item: resultCard.isRequest ? null : modelData
            showWatchState: !resultCard.isRequest
                            && (modelData.mediaType === "Series"
                                || (modelData.mediaType === "Movie" && !!modelData.watched))
            unplayedCount: modelData.mediaType === "Series" ? (modelData.unplayedItemCount || 0) : 0
            watched: !!modelData.watched
            sourceLabel: resultCard.isRequest ? "" : (modelData.sourceLabel || "")
            focused: ListView.isCurrentItem && ListView.view.activeFocus
            onFocusedChanged: if (focused) root.showBackdropFor(modelData)
            onClicked: {
                var list = resultCard.ListView.view
                list.currentIndex = index
                list.forceActiveFocus()
                root.openResult(modelData, list)
            }
            Accessible.description: resultCard.isRequest
                                    ? qsTr("Request %1 with %2").arg(modelData.name || "").arg(MediaRequests.backendName)
                                    : qsTr("Details for %1").arg(modelData.name || "")
        }
    }

    // ========================================
    // Main Content
    // ========================================

    NumberAnimation {
        id: scrollAnimation
        target: contentFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
    }

    Flickable {
        id: contentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: 0

            // Clear the navigation bar.
            Item { Layout.preferredHeight: Theme.pageMarginTop }

            PageHeader {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                title: qsTr("Search")
                detail: {
                    if (root.hasSearched && !root.librarySearchFailed && root.libraryResultCount > 0)
                        return root.libraryResultCount === 1 ? qsTr("1 result in your libraries")
                                                               : qsTr("%1 results in your libraries").arg(root.libraryResultCount)
                    return root.mergedMode ? qsTr("Movies and shows from every source in this space")
                                           : qsTr("Movies and shows in your libraries")
                }
            }

            // Search field: a large pill, the page's first focus.
            Item {
                id: searchFieldRow
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingLarge
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.preferredHeight: Theme.buttonHeightHero

                FieldBackground {
                    id: searchFieldSurface
                    width: Math.min(parent.width, Math.round(1240 * Theme.layoutScale))
                    height: parent.height
                    radius: height / 2
                    control: searchInput

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.IBeamCursor
                        onClicked: searchInput.forceActiveFocus()
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Math.round(34 * Theme.layoutScale)
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        Text {
                            text: Icons.search
                            font.pixelSize: Theme.iconSizeNav
                            font.family: Theme.fontIcon
                            color: searchInput.activeFocus ? Theme.textPrimary : Theme.textSecondary
                            Layout.alignment: Qt.AlignVCenter
                            Accessible.ignored: true
                        }

                        TextField {
                            id: searchInput
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: qsTr("Search movies and shows")
                            placeholderTextColor: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeMedium
                            font.family: Theme.fontPrimary
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textPrimary
                            background: null
                            leftPadding: 0
                            rightPadding: 0
                            focus: true
                            Accessible.name: qsTr("Search movies and shows")

                            onTextChanged: {
                                root.searchTerm = text
                                searchDebounce.restart()
                            }

                            onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = searchInput

                            Keys.onDownPressed: function(event) {
                                event.accepted = root.focusFirstResult()
                            }

                            // Return searches now, or enters the results once they are in.
                            Keys.onReturnPressed: function(event) {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                if (searchDebounce.running) {
                                    searchDebounce.stop()
                                    root.runSearchNow()
                                } else {
                                    root.focusFirstResult()
                                }
                                event.accepted = true
                            }
                            Keys.onEnterPressed: function(event) {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                if (searchDebounce.running) {
                                    searchDebounce.stop()
                                    root.runSearchNow()
                                } else {
                                    root.focusFirstResult()
                                }
                                event.accepted = true
                            }
                        }

                        BusyIndicator {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: Math.round(40 * Theme.layoutScale)
                            Layout.preferredHeight: Layout.preferredWidth
                            visible: root.isSearching
                            running: visible
                        }

                        // Pointer convenience; keyboard users edit the text directly.
                        ActionButton {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: Theme.buttonHeightMedium
                            Layout.preferredHeight: Theme.buttonHeightMedium
                            variant: "ghost"
                            iconGlyph: Icons.close
                            accessibleLabel: qsTr("Clear search")
                            cornerRadius: height / 2
                            focusPolicy: Qt.NoFocus
                            visible: searchInput.text.length > 0
                            onClicked: {
                                searchInput.clear()
                                root.focusSearchField()
                            }
                        }
                    }
                }
            }

            // ========================================
            // States
            // ========================================

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: root.waitingForLibrarySearch
                kind: "loading"
                message: qsTr("Searching…")
            }

            StateMessage {
                id: errorState
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: root.hasSearched && !root.waitingForLibrarySearch && root.librarySearchFailed
                kind: "error"
                title: qsTr("Search failed")
                message: root.librarySearchError !== ""
                         ? root.librarySearchError
                         : qsTr("Bloom couldn't reach your server. Check the connection and try again.")
                actionText: qsTr("Try again")
                onActionTriggered: {
                    searchDebounce.stop()
                    root.runSearchNow()
                    root.focusSearchField()
                }
                Keys.onUpPressed: function(event) {
                    root.focusSearchField()
                    event.accepted = true
                }
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                kind: "empty"
                iconGlyph: Icons.search
                title: qsTr("No results")
                message: qsTr("Nothing in your libraries matches “%1”.").arg(root.searchTerm.trim())
                visible: root.hasSearched && !root.waitingForLibrarySearch && !root.librarySearchFailed
                         && root.libraryResultCount === 0
                         && root.requestResults.length === 0 && !root.waitingForRequestSearch
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                kind: "empty"
                iconGlyph: Icons.search
                title: qsTr("Find something to watch")
                message: qsTr("Type a title to search your movies and shows.")
                visible: !root.hasSearched && !root.waitingForLibrarySearch
                         && root.searchTerm.trim().length === 0 && root.suggestions.length === 0
            }

            // ========================================
            // Shelves
            // ========================================

            ColumnLayout {
                id: suggestionsSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.showSuggestions

                SectionHeader {
                    text: qsTr("Suggestions")
                    detail: qsTr("Picked at random from your library")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: suggestionsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: root.suggestions
                    delegate: resultCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(suggestionsList, -1) }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(suggestionsList, 1) }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.suggestions.length)
                            root.openResult(root.suggestions[currentIndex], suggestionsList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.suggestions.length)
                            root.openResult(root.suggestions[currentIndex], suggestionsList)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = suggestionsList
                            root.scrollToSection(suggestionsSection)
                        }
                    }
                }
            }

            ColumnLayout {
                id: seriesSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.hasSearched && root.seriesResults.length > 0

                SectionHeader {
                    text: qsTr("Shows")
                    detail: String(root.seriesResults.length)
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: seriesList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: root.seriesResults
                    delegate: resultCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(seriesList, -1) }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(seriesList, 1) }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.seriesResults.length)
                            root.openResult(root.seriesResults[currentIndex], seriesList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.seriesResults.length)
                            root.openResult(root.seriesResults[currentIndex], seriesList)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = seriesList
                            root.scrollToSection(seriesSection)
                        }
                    }
                }
            }

            ColumnLayout {
                id: moviesSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.hasSearched && root.movieResults.length > 0

                SectionHeader {
                    text: qsTr("Movies")
                    detail: String(root.movieResults.length)
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: moviesList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: root.movieResults
                    delegate: resultCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(moviesList, -1) }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(moviesList, 1) }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.movieResults.length)
                            root.openResult(root.movieResults[currentIndex], moviesList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.movieResults.length)
                            root.openResult(root.movieResults[currentIndex], moviesList)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = moviesList
                            root.scrollToSection(moviesSection)
                        }
                    }
                }
            }

            ColumnLayout {
                id: requestSection
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.hasSearched && root.requestResults.length > 0
                opacity: visible ? 1 : 0

                Behavior on opacity {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationMedium }
                }

                SectionHeader {
                    text: qsTr("Request with %1").arg(MediaRequests.backendName)
                    detail: String(root.requestResults.length)
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: requestList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: root.requestResults
                    delegate: resultCardDelegate
                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(requestList, -1) }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(requestList, 1) }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.requestResults.length)
                            root.openResult(root.requestResults[currentIndex], requestList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < root.requestResults.length)
                            root.openResult(root.requestResults[currentIndex], requestList)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = requestList
                            root.scrollToSection(requestSection)
                        }
                    }
                }
            }

            // Requests answer after the library; say so while they are pending.
            RowLayout {
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.spacingMedium
                visible: root.hasSearched && root.waitingForRequestSearch

                BusyIndicator {
                    Layout.preferredWidth: Math.round(40 * Theme.layoutScale)
                    Layout.preferredHeight: Layout.preferredWidth
                    running: parent.visible
                }

                Text {
                    text: qsTr("Searching %1…").arg(MediaRequests.backendName)
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(120 * Theme.layoutScale)
    }

    MediaRequestDialog {
        id: requestDialog
        parent: Overlay.overlay
    }
}
