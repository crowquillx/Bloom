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
 * recent ones on Seerr), the discovery rows (trending, popular, upcoming),
 * then genres, studios and networks to browse. Only shown while
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

    /// Rows as delivered: {key, title, items, page, nextPage, hasMore}.
    property var rows: []
    property bool loading: false
    property string loadError: ""
    /// Groups of {kind, title, items} to browse.
    property var browseGroups: []
    readonly property bool available: MediaRequests.discoverAvailable

    /// The shelf (or state action) that last held focus.
    property Item lastFocusTarget: null
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
            return
        }
        loading = true
        loadError = ""
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
        for (var r = 0; r < 2; ++r) {
            var repeater = r === 0 ? rowRepeater : browseRepeater
            for (var i = 0; i < repeater.count; ++i) {
                var section = repeater.itemAt(i)
                if (section && section.visible && section["shelf"].count > 0)
                    lists.push(section["shelf"])
            }
        }
        return lists
    }

    function focusFirstShelf() {
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

    // Moves focus to the shelf above/below. Up from the first shelf is left
    // unhandled so it reaches the navigation bar.
    function moveShelfFocus(fromList, delta) {
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
            Qt.callLater(function() {
                if (root.activeFocus && !root.loading && root.Window.activeFocusItem === root)
                    root.focusFirstShelf()
            })
        }

        function onMyRequestsFailed(error) {
            console.warn("[DiscoverScreen] Requests failed to load:", error)
        }

        function onBrowseCategoriesLoaded(groups) {
            root.browseGroups = groups || []
        }

        function onBrowseCategoriesFailed(error) {
            console.warn("[DiscoverScreen] Browse categories failed to load:", error)
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
        duration: Theme.durationLong
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
                visible: root.loading && root.rows.length === 0
                kind: "loading"
                message: qsTr("Loading…")
            }

            StateMessage {
                id: errorState
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !root.loading && root.available && root.loadError !== ""
                         && root.rows.length === 0
                kind: "error"
                title: qsTr("Discover couldn't load")
                message: root.loadError
                actionText: qsTr("Try again")
                onActionTriggered: root.load()
            }

            StateMessage {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sectionSpacing * 2
                Layout.leftMargin: Theme.pageMarginX
                Layout.rightMargin: Theme.pageMarginX
                visible: !root.loading && root.available && root.loadError === ""
                         && root.rows.length === 0
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
                id: rowRepeater
                model: root.rows

                delegate: ColumnLayout {
                    id: rowSection
                    required property var modelData
                    required property int index

                    readonly property string rowKey: modelData.key || ""
                    property alias shelf: rowList
                    property int nextPage: modelData.nextPage || 0
                    property bool hasMore: !!modelData.hasMore
                    property int loadingPage: 0
                    /// Pages in a row that brought only titles already shown.
                    property int repeatedPages: 0

                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sectionSpacing
                    spacing: Theme.sectionTitleSpacing
                    visible: rowItems.count > 0

                    function loadMoreIfNear(index) {
                        if (!hasMore || loadingPage > 0 || nextPage < 1)
                            return
                        if (index >= rowItems.count - root.loadAheadCards) {
                            loadingPage = nextPage
                            MediaRequests.loadDiscoverRow(rowKey, nextPage)
                        }
                    }

                    DiscoveryListModel {
                        id: rowItems
                    }

                    Component.onCompleted: rowItems.appendItems(modelData.items || [])

                    Connections {
                        target: MediaRequests

                        function onDiscoverRowLoaded(key, page, row) {
                            if (key !== rowSection.rowKey || page !== rowSection.loadingPage)
                                return
                            rowSection.loadingPage = 0
                            // Lists shift between reads, so a page can repeat
                            // titles already shown. Keep following the
                            // server's pages past those, three at most, and
                            // never re-read the same page.
                            var added = rowItems.appendItems(row.items || [])
                            var next = row.nextPage || page + 1
                            rowSection.repeatedPages = added > 0 ? 0 : rowSection.repeatedPages + 1
                            rowSection.nextPage = next
                            rowSection.hasMore = !!row.hasMore && next > page && rowSection.repeatedPages < 3
                            if (added === 0 && rowSection.hasMore) {
                                rowSection.loadingPage = next
                                MediaRequests.loadDiscoverRow(rowSection.rowKey, next)
                            }
                        }

                        function onDiscoverRowFailed(key, page, error) {
                            if (key !== rowSection.rowKey || page !== rowSection.loadingPage)
                                return
                            console.warn("[DiscoverScreen] Loading more of", key, "failed:", error)
                            rowSection.loadingPage = 0
                            rowSection.hasMore = false
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
                        text: rowSection.modelData.title || ""
                        Layout.leftMargin: Theme.pageMarginX
                        Layout.rightMargin: Theme.pageMarginX
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    ShelfList {
                        id: rowList
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
                }
            }

            Repeater {
                id: browseRepeater
                model: root.browseGroups

                delegate: ColumnLayout {
                    id: browseSection
                    required property var modelData
                    required property int index

                    readonly property var categories: modelData.items || []
                    property alias shelf: browseList

                    Layout.fillWidth: true
                    Layout.topMargin: Theme.sectionSpacing
                    spacing: Theme.sectionTitleSpacing
                    visible: categories.length > 0

                    SectionHeader {
                        text: browseSection.modelData.title || ""
                        Layout.leftMargin: Theme.pageMarginX
                        Layout.rightMargin: Theme.pageMarginX
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    ShelfList {
                        id: browseList
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        cardHeight: Math.round(Theme.landscapeCardWidth * 9 / 16)
                        model: browseSection.categories

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
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < browseSection.categories.length)
                                root.browseRequested(browseSection.categories[currentIndex])
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < browseSection.categories.length)
                                root.browseRequested(browseSection.categories[currentIndex])
                            event.accepted = true
                        }
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                root.lastFocusTarget = browseList
                                root.scrollToSection(browseSection)
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
