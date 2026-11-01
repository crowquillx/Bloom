pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * DiscoverScreen - titles to request, from Seerr or the Silo server
 * (docs/seerr.md).
 *
 * Top to bottom: the request list (the profile's own on Silo, the server's
 * recent ones on Seerr), then the discovery rows (trending, popular,
 * upcoming) followed by genres, studios and networks to browse. A Silo
 * server with an admin-defined layout orders the rows itself and places the
 * browse rows among them, or leaves them out. Only shown while
 * MediaRequests.discoverAvailable. A title already in the library opens its
 * details page; any other title opens the request dialog. Each discovery row
 * loads its next page as focus nears its end; a browse card opens
 * BrowseScreen through browseRequested().
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "discover"

    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)
    /// A genre, studio, or network card: {kind, id, name, logoUrl, movieId, tvId}.
    signal browseRequested(var category)

    // ========================================
    // Properties
    // ========================================

    /// Shelves in page order, as MediaRequests.discoverLoaded delivers them:
    /// title rows {key, title, items, page, nextPage, hasMore} and brand rows
    /// {key, title, browseKind}, which draw their browse group's cards. A
    /// Silo admin layout places the brand rows itself, or leaves them out.
    /// Browse groups arriving later fill brand rows in place, so loaded
    /// pages and focus survive.
    property var rows: []
    property bool loading: false
    property string loadError: ""
    /// Groups of {kind, title, items} to browse.
    property var browseGroups: []
    property bool browseLoading: false
    property string browseError: ""
    /// Some row has something to show: titles, or a brand row's categories.
    readonly property bool hasRows: {
        for (var i = 0; i < rows.length; ++i) {
            var kind = rows[i].browseKind || ""
            var group = kind !== "" ? browseGroupFor(kind) : null
            if (kind === "" || (group && (group.items || []).length > 0))
                return true
        }
        return false
    }
    /// Brand rows wait for the browse categories before the page is empty.
    readonly property bool waitingForBrowse: {
        if (!browseLoading)
            return false
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].browseKind)
                return true
        }
        return false
    }
    /// Why the page has nothing to show, when a read failed: Discover's, or
    /// with brand rows only, the browse categories'.
    readonly property string pageError: loadError !== "" ? loadError : browseError
    readonly property bool available: MediaRequests.discoverAvailable

    /// The shelf (or state action) that last held focus.
    property Item lastFocusTarget: null
    /// The shelf the page focused by itself, until the viewer moves: rows
    /// that arrive later above it take focus instead.
    property ShelfList autoFocusShelf: null
    property string stageSource: ""
    property string pendingStageSource: ""

    /// The navigation bar recedes once the page scrolls past its header.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.spacingLarge
    readonly property real shelfScrollOffset: Math.round(56 * Theme.layoutScale)
    /// Cards left before a row's end that start loading its next page.
    readonly property int loadAheadCards: 6

    // ========================================
    // Loading
    // ========================================

    function load() {
        if (!available) {
            rows = []
            browseGroups = []
            requestsModel.clear()
            loading = false
            loadError = ""
            browseLoading = false
            browseError = ""
            return
        }
        loading = true
        loadError = ""
        browseLoading = true
        browseError = ""
        MediaRequests.loadDiscover()
        MediaRequests.loadMyRequests()
        MediaRequests.loadBrowseCategories()
    }

    // ========================================
    // Opening titles
    // ========================================

    function openItem(item, list) {
        if (!item)
            return
        autoFocusShelf = null
        var libraryItemId = item.libraryItemId || ""
        if (libraryItemId !== "") {
            if (item.mediaType === "Series") {
                root.navigateToSeries(libraryItemId)
            } else {
                root.navigateToMovie({
                    itemId: libraryItemId,
                    connectionId: LibraryService.getActiveConnectionId(),
                    name: item.name || "",
                    type: "Movie",
                    mediaType: "Movie",
                    productionYear: item.productionYear || 0
                })
            }
            return
        }
        requestDialog.openForItem(item, list)
    }

    function browseGroupFor(kind) {
        for (var i = 0; i < browseGroups.length; ++i) {
            if (browseGroups[i].kind === kind)
                return browseGroups[i]
        }
        return null
    }

    function showBackdropFor(item) {
        pendingStageSource = item ? (item.backdropUrl || "") : ""
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
        if (requestsSection.visible && requestsList.count > 0)
            lists.push(requestsList)
        for (var i = 0; i < sectionRepeater.count; ++i) {
            var section = sectionRepeater.itemAt(i)
            if (section && section.visible && section["shelf"].count > 0)
                lists.push(section["shelf"])
        }
        return lists
    }

    function focusFirstShelf() {
        var lists = shelfLists()
        if (lists.length > 0) {
            autoFocusShelf = lists[0]
            lists[0].forceActiveFocus()
            return true
        }
        if (errorState.visible) {
            errorState.focusAction()
            return true
        }
        return false
    }

    // Moves focus to the shelf above/below. Up from the first shelf is left
    // unhandled so it reaches the navigation bar.
    function moveShelfFocus(fromList, delta) {
        autoFocusShelf = null
        var lists = shelfLists()
        var index = lists.indexOf(fromList)
        var target = index + delta
        if (index >= 0 && target >= 0 && target < lists.length) {
            lists[target].forceActiveFocus()
            return true
        }
        return delta > 0
    }

    // The first shelf keeps the header in view; later shelves scroll near
    // the top so the focused row always lands in the same place.
    function scrollToSection(section) {
        if (!section)
            return
        var lists = shelfLists()
        var y
        if (lists.length > 0 && lists[0] === section.shelf)
            y = 0
        else
            y = section.mapToItem(contentFlickable.contentItem, 0, 0).y - root.shelfScrollOffset
        var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    // Rows that arrive after the page took focus (the request list, browse
    // categories) can land above the focused shelf. Until the viewer moves,
    // focus goes to the top shelf.
    function focusTopShelfIfUnmoved() {
        if (!root.activeFocus || root.loading)
            return
        var focused = root.Window.activeFocusItem
        var shelf = autoFocusShelf
        if (focused === root || (shelf && shelf.activeFocus && shelf.currentIndex === 0))
            focusFirstShelf()
    }

    function restoreLastFocus() {
        var target = lastFocusTarget
        if (target && target.visible && target["count"] > 0) {
            target.forceActiveFocus()
            return
        }
        if (!focusFirstShelf())
            root.forceActiveFocus()
    }

    function saveFocusForNavigation() {
        // lastFocusTarget already tracks the focused shelf.
        autoFocusShelf = null
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    // ========================================
    // Initialization
    // ========================================

    Component.onCompleted: {
        // The server may have changed who can request since the last read.
        MediaRequests.refreshAvailability()
        load()
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
            Qt.callLater(root.restoreLastFocus)
        else
            root.autoFocusShelf = null
    }

    Connections {
        target: MediaRequests

        function onDiscoverLoaded(loadedRows) {
            if (!root.loading)
                return
            root.loading = false
            root.rows = loadedRows || []
            Qt.callLater(function() {
                if (root.activeFocus || root.Window.activeFocusItem === null)
                    root.focusFirstShelf()
            })
        }

        function onDiscoverFailed(error) {
            if (!root.loading)
                return
            root.loading = false
            root.loadError = error || ""
            Qt.callLater(function() {
                if (root.activeFocus)
                    root.focusFirstShelf()
            })
        }

        function onAvailabilityChanged() {
            // Seerr was configured, or the account or its request access
            // changed: nothing shown so far belongs to the new backend.
            root.lastFocusTarget = null
            root.autoFocusShelf = null
            root.rows = []
            root.browseGroups = []
            requestsModel.clear()
            root.load()
        }

        function onMyRequestsLoaded(requests) {
            var index = requestsList.currentIndex
            requestsModel.clear()
            requestsModel.appendItems(requests)
            requestsList.currentIndex = Math.min(Math.max(index, 0), requestsModel.count - 1)
            Qt.callLater(root.focusTopShelfIfUnmoved)
        }

        function onMyRequestsFailed(error) {
            console.warn("[DiscoverScreen] Requests failed to load:", error)
        }

        function onBrowseCategoriesLoaded(groups) {
            root.browseGroups = groups || []
            root.browseLoading = false
            root.browseError = ""
            Qt.callLater(root.focusTopShelfIfUnmoved)
        }

        function onBrowseCategoriesFailed(error) {
            console.warn("[DiscoverScreen] Browse categories failed to load:", error)
            root.browseLoading = false
            root.browseError = error || qsTr("The browse categories couldn't load.")
            Qt.callLater(root.focusTopShelfIfUnmoved)
        }

        // A request made or cancelled anywhere changes the request list.
        function onRequestCreated(mediaType, tmdbId, result) {
            MediaRequests.loadMyRequests()
        }

        function onRequestCancelled(requestId, mediaType, tmdbId) {
            MediaRequests.loadMyRequests()
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
    // Main Content
    // ========================================

    NumberAnimation {
        id: scrollAnimation
        target: contentFlickable
        property: "contentY"
        duration: Theme.uiAnimationsEnabled ? Theme.durationLong : 0
        easing.type: Easing.OutCubic
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
                title: qsTr("Discover")
                detail: MediaRequests.backendName !== ""
                        ? qsTr("Popular and upcoming titles to request from %1").arg(MediaRequests.backendName)
                        : qsTr("Popular and upcoming titles to request")
            }

            // ========================================
            // States
            // ========================================

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: (root.loading || root.waitingForBrowse) && !root.hasRows
                kind: "loading"
                message: qsTr("Loading…")
            }

            StateMessage {
                id: errorState
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !root.loading && !root.waitingForBrowse && root.available
                         && root.pageError !== "" && !root.hasRows
                kind: "error"
                title: qsTr("Discover couldn't load")
                message: root.pageError
                actionText: qsTr("Try again")
                onActionTriggered: root.load()
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !root.loading && !root.waitingForBrowse && root.available
                         && root.pageError === "" && !root.hasRows
                kind: "empty"
                iconGlyph: Icons.explore
                title: qsTr("Nothing to discover")
                message: qsTr("The server has no discovery rows right now.")
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !root.available
                kind: "empty"
                iconGlyph: Icons.explore
                title: qsTr("Discover isn't available")
                message: qsTr("Add a Seerr server in Settings, or use a Silo server that takes requests from this profile.")
            }

            // ========================================
            // Shelves
            // ========================================

            ColumnLayout {
                id: requestsSection
                readonly property alias shelf: requestsList
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing
                spacing: Theme.sectionTitleSpacing
                visible: root.available && requestsModel.count > 0

                DiscoveryListModel {
                    id: requestsModel
                    keyRole: "requestId"
                }

                SectionHeader {
                    // Seerr's API key sees every user's requests.
                    text: MediaRequests.backend === "seerr" ? qsTr("Recent Requests") : qsTr("My Requests")
                    detail: String(requestsModel.count)
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: requestsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    model: requestsModel

                    delegate: DiscoveryCard {
                        id: requestCard
                        required property var model
                        required property int index
                        entry: requestCard.model
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onFocusedChanged: if (focused) root.showBackdropFor(requestCard.model)
                        onClicked: {
                            requestsList.currentIndex = index
                            requestsList.forceActiveFocus()
                            root.openItem(requestsModel.get(index), requestsList)
                        }
                    }

                    Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(requestsList, -1) }
                    Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(requestsList, 1) }
                    Keys.onReturnPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < requestsModel.count)
                            root.openItem(requestsModel.get(currentIndex), requestsList)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < requestsModel.count)
                            root.openItem(requestsModel.get(currentIndex), requestsList)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.lastFocusTarget = requestsList
                            root.scrollToSection(requestsSection)
                        }
                    }
                }
            }

            Repeater {
                id: sectionRepeater
                model: root.rows

                delegate: ColumnLayout {
                    id: rowSection
                    required property var modelData
                    required property int index

                    readonly property string rowKey: modelData.key || ""
                    /// A brand row: "genre", "studio", or "network".
                    readonly property string browseKind: modelData.browseKind || ""
                    readonly property bool isBrowse: browseKind !== ""
                    readonly property var browseGroup: isBrowse ? root.browseGroupFor(browseKind) : null
                    readonly property var categories: browseGroup ? (browseGroup.items || []) : []
                    readonly property Item shelf: isBrowse ? browseList : rowList

                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sectionSpacing
                    spacing: Theme.sectionTitleSpacing
                    visible: isBrowse ? categories.length > 0 : rowItems.count > 0

                    function loadMoreIfNear(index) {
                        if (isBrowse || index < rowItems.count - root.loadAheadCards)
                            return
                        var page = rowItems.beginNextPage()
                        if (page > 0)
                            MediaRequests.loadDiscoverRow(rowSection.rowKey, page)
                    }

                    DiscoveryListModel {
                        id: rowItems
                    }

                    Component.onCompleted: {
                        if (isBrowse)
                            return
                        rowItems.appendItems(modelData.items || [])
                        rowItems.nextPage = modelData.nextPage || 0
                        rowItems.hasMore = !!modelData.hasMore
                    }

                    Connections {
                        target: MediaRequests
                        enabled: !rowSection.isBrowse

                        function onDiscoverRowLoaded(key, page, row) {
                            if (key !== rowSection.rowKey)
                                return
                            // A page of repeated titles reads on at once.
                            var next = rowItems.acceptPage(page, row)
                            if (next > 0)
                                MediaRequests.loadDiscoverRow(rowSection.rowKey, next)
                        }

                        function onDiscoverRowFailed(key, page, error) {
                            if (key === rowSection.rowKey && rowItems.failPage(page))
                                console.warn("[DiscoverScreen] Loading more of", key, "failed:", error)
                        }

                        // Show a request change on the card without reloading the row.
                        function onRequestCreated(mediaType, tmdbId, result) {
                            rowItems.setRequestStatus(mediaType, tmdbId, result.requestState || "pending")
                        }

                        function onRequestCancelled(requestId, mediaType, tmdbId) {
                            rowItems.setRequestStatus(mediaType, tmdbId, "")
                        }
                    }

                    SectionHeader {
                        // A layout's own title, else the browse group's.
                        text: rowSection.modelData.title
                              || (rowSection.browseGroup ? rowSection.browseGroup.title || "" : "")
                        Layout.leftMargin: Theme.pageMarginX
                        Layout.rightMargin: Theme.pageMarginX
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    ShelfList {
                        id: rowList
                        visible: !rowSection.isBrowse
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        model: rowItems

                        delegate: DiscoveryCard {
                            id: card
                            required property var model
                            required property int index
                            entry: card.model
                            focused: ListView.isCurrentItem && ListView.view.activeFocus
                            onFocusedChanged: if (focused) root.showBackdropFor(card.model)
                            onClicked: {
                                rowList.currentIndex = index
                                rowList.forceActiveFocus()
                                root.openItem(rowItems.get(index), rowList)
                            }
                        }

                        onCurrentIndexChanged: rowSection.loadMoreIfNear(currentIndex)
                        Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(rowList, -1) }
                        Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(rowList, 1) }
                        Keys.onReturnPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < rowItems.count)
                                root.openItem(rowItems.get(currentIndex), rowList)
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < rowItems.count)
                                root.openItem(rowItems.get(currentIndex), rowList)
                            event.accepted = true
                        }
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.lastFocusTarget = rowList
                                root.scrollToSection(rowSection)
                            }
                        }
                    }

                    ShelfList {
                        id: browseList
                        visible: rowSection.isBrowse
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        cardHeight: Math.round(Theme.landscapeCardWidth * 9 / 16)
                        model: rowSection.categories

                        delegate: MediaCard {
                            id: categoryCard
                            required property var modelData
                            required property int index
                            variant: "landscape"
                            artworkSources: []
                            logoSource: categoryCard.modelData.logoUrl || ""
                            title: categoryCard.modelData.name || ""
                            placeholderIcon: categoryCard.modelData.kind === "network" ? Icons.tvShows
                                             : categoryCard.modelData.kind === "studio" ? Icons.movie
                                             : Icons.explore
                            item: null
                            focused: ListView.isCurrentItem && ListView.view.activeFocus
                            onClicked: {
                                browseList.currentIndex = index
                                browseList.forceActiveFocus()
                                root.browseRequested(categoryCard.modelData)
                            }
                            Accessible.description: qsTr("Browse %1").arg(categoryCard.modelData.name || "")
                        }

                        Keys.onUpPressed: function(event) { event.accepted = root.moveShelfFocus(browseList, -1) }
                        Keys.onDownPressed: function(event) { event.accepted = root.moveShelfFocus(browseList, 1) }
                        Keys.onReturnPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < rowSection.categories.length)
                                root.browseRequested(rowSection.categories[currentIndex])
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < rowSection.categories.length)
                                root.browseRequested(rowSection.categories[currentIndex])
                            event.accepted = true
                        }
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.lastFocusTarget = browseList
                                root.scrollToSection(rowSection)
                            }
                        }
                    }
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
