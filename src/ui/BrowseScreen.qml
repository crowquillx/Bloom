pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * BrowseScreen - one genre, studio, or network from Discover as a poster
 * grid of titles to request (docs/seerr.md).
 *
 * The toolbar switches between movies and shows (when the category has
 * both) and between popular, top rated, and newest. The grid loads the next
 * page as focus or scrolling nears its end. A title already in the library
 * opens its details page; any other title opens the request dialog.
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "discover"

    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)

    // ========================================
    // Properties
    // ========================================

    /// {kind, id, name, logoUrl, movieId, tvId} from Discover.
    property var category: ({})
    readonly property bool hasMovies: (category.movieId || "") !== ""
    readonly property bool hasShows: (category.tvId || "") !== ""
    property string mediaType: hasMovies ? "movie" : "tv"
    property string sort: "popularity"

    readonly property string listKey: MediaRequests.browseKey(category.kind || "", currentId,
                                                             mediaType, sort)
    readonly property string currentId: mediaType === "movie" ? (category.movieId || "")
                                                               : (category.tvId || "")
    property int nextPage: 1
    property bool hasMore: false
    property int loadingPage: 0
    /// Pages in a row that brought only titles already shown.
    property int repeatedPages: 0
    /// Pages of nothing new to read past before giving up.
    readonly property int maxRepeatedPages: 3
    property string loadError: ""

    property Item lastFocusTarget: null
    property string stageSource: ""
    property string pendingStageSource: ""

    readonly property bool navigationRecessed: grid.contentY > Theme.spacingLarge

    // ========================================
    // Loading
    // ========================================

    function reload() {
        titles.clear()
        loadError = ""
        hasMore = false
        loadingPage = 0
        repeatedPages = 0
        loadPage(1)
    }

    function loadPage(page) {
        if (loadingPage > 0 || currentId === "")
            return
        loadingPage = page
        MediaRequests.browse(category.kind || "", currentId, mediaType, sort, page)
    }

    function loadMoreIfNear(index) {
        if (hasMore && loadingPage === 0 && index >= titles.count - grid.columns * 2)
            loadPage(nextPage)
    }

    function setMediaType(type) {
        if (type === mediaType)
            return
        mediaType = type
        reload()
    }

    function setSort(order) {
        if (order === sort)
            return
        sort = order
        reload()
    }

    // ========================================
    // Opening titles
    // ========================================

    function openItem(item, focusTarget) {
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
        requestDialog.openForItem(item, focusTarget)
    }

    function showBackdropFor(item) {
        pendingStageSource = item ? (item.backdropUrl || "") : ""
        stageDebounce.restart()
    }

    Timer {
        id: stageDebounce
        interval: 280
        onTriggered: root.stageSource = root.pendingStageSource
    }

    // ========================================
    // Focus
    // ========================================

    /// Toolbar choices: the media types when there are two, then the orders.
    readonly property var toolbarOptions: {
        var options = []
        if (hasMovies && hasShows) {
            options.push({ group: "type", value: "movie", label: qsTr("Movies") })
            options.push({ group: "type", value: "tv", label: qsTr("Shows") })
        }
        options.push({ group: "sort", value: "popularity", label: qsTr("Popular"), gapBefore: options.length > 0 })
        options.push({ group: "sort", value: "vote_average", label: qsTr("Top Rated") })
        options.push({ group: "sort", value: "release_date", label: qsTr("Newest") })
        return options
    }

    function focusToolbarAt(index) {
        var button = toolbarRepeater.itemAt(index)
        if (button)
            button.forceActiveFocus()
    }

    // The selected order (the last choice made) takes focus.
    function focusToolbar() {
        for (var i = 0; i < toolbarRepeater.count; ++i) {
            var button = toolbarRepeater.itemAt(i)
            if (button && button["selected"] && root.toolbarOptions[i].group === "sort") {
                button.forceActiveFocus()
                return
            }
        }
        focusToolbarAt(0)
    }

    function focusGrid() {
        if (titles.count > 0) {
            grid.forceActiveFocus()
            return true
        }
        if (errorState.visible) {
            errorState.focusAction()
            return true
        }
        return false
    }

    function restoreLastFocus() {
        var target = lastFocusTarget
        if (target === grid && titles.count > 0) {
            grid.forceActiveFocus()
            return
        }
        focusToolbar()
    }

    function saveFocusForNavigation() {
        // lastFocusTarget already tracks the toolbar and grid.
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    Component.onCompleted: {
        reload()
        Qt.callLater(root.focusToolbar)
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
            Qt.callLater(root.restoreLastFocus)
    }

    Connections {
        target: MediaRequests

        function onBrowseLoaded(key, page, row) {
            if (key !== root.listKey || page !== root.loadingPage)
                return
            root.loadingPage = 0
            var added = titles.appendItems(row.items || [])
            var next = row.nextPage || page + 1
            // Lists shift between reads, so a page can repeat titles already
            // shown. Keep following the server's pages past those, a few at
            // most, and never re-read the same page.
            root.repeatedPages = added > 0 ? 0 : root.repeatedPages + 1
            root.nextPage = next
            root.hasMore = !!row.hasMore && next > page && root.repeatedPages < root.maxRepeatedPages
            if (added === 0 && root.hasMore)
                root.loadPage(next)
        }

        function onBrowseFailed(key, page, error) {
            if (key !== root.listKey || page !== root.loadingPage)
                return
            root.loadingPage = 0
            root.hasMore = false
            if (titles.count === 0)
                root.loadError = error || ""
        }

        function onRequestCreated(mediaType, tmdbId, result) {
            titles.setRequestStatus(mediaType, tmdbId, result.requestState || "pending")
        }

        function onRequestCancelled(requestId, mediaType, tmdbId) {
            titles.setRequestStatus(mediaType, tmdbId, "")
        }

        // The backend changed under the page; its ids no longer apply.
        function onAvailabilityChanged() {
            titles.clear()
            root.hasMore = false
            root.loadError = MediaRequests.available ? "" : qsTr("Media requests are no longer available.")
        }
    }

    DiscoveryListModel {
        id: titles
    }

    // ========================================
    // Stage
    // ========================================

    StageBackdrop {
        anchors.fill: parent
        mode: "ambient"
        source: root.stageSource
    }

    // ========================================
    // Content
    // ========================================

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageMarginTop
        spacing: Theme.spacingLarge

        PageHeader {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            title: root.category.name || ""
            detail: root.category.kind === "studio" ? qsTr("Studio")
                    : root.category.kind === "network" ? qsTr("Network")
                    : qsTr("Genre")
        }

        // Toolbar: media type (when the category has both), then order.
        RowLayout {
            id: toolbar
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            spacing: Theme.controlSpacing

            Repeater {
                id: toolbarRepeater
                model: root.toolbarOptions

                delegate: ActionButton {
                    id: toolbarButton
                    required property var modelData
                    required property int index
                    Layout.leftMargin: toolbarButton.modelData.gapBefore ? Theme.spacingLarge : 0
                    variant: "secondary"
                    text: toolbarButton.modelData.label
                    selected: toolbarButton.modelData.group === "type"
                              ? root.mediaType === toolbarButton.modelData.value
                              : root.sort === toolbarButton.modelData.value
                    focusPolicy: Qt.StrongFocus
                    onClicked: {
                        if (toolbarButton.modelData.group === "type")
                            root.setMediaType(toolbarButton.modelData.value)
                        else
                            root.setSort(toolbarButton.modelData.value)
                    }
                    onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = toolbarButton
                    Keys.onLeftPressed: function(event) { root.focusToolbarAt(toolbarButton.index - 1); event.accepted = true }
                    Keys.onRightPressed: function(event) { root.focusToolbarAt(toolbarButton.index + 1); event.accepted = true }
                    Keys.onDownPressed: function(event) { event.accepted = root.focusGrid() }
                    Keys.onReturnPressed: function(event) { toolbarButton.clicked(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { toolbarButton.clicked(); event.accepted = true }
                }
            }
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: root.loadingPage === 1 && titles.count === 0
            kind: "loading"
            message: qsTr("Loading…")
        }

        StateMessage {
            id: errorState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: root.loadingPage === 0 && titles.count === 0 && root.loadError !== ""
            kind: "error"
            title: qsTr("Couldn't load titles")
            message: root.loadError
            actionText: qsTr("Try again")
            onActionTriggered: root.reload()
            Keys.onUpPressed: function(event) { root.focusToolbar(); event.accepted = true }
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: root.loadingPage === 0 && titles.count === 0 && root.loadError === ""
            kind: "empty"
            iconGlyph: Icons.explore
            title: qsTr("Nothing here yet")
            message: qsTr("There are no titles to show for this choice.")
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            // Hidden by opacity: toggling visible loops the wheel scroller's
            // enabled binding.
            opacity: titles.count > 0 ? 1 : 0
            model: titles
            Accessible.role: Accessible.List
            Accessible.name: root.category.name || ""

            property int columns: Math.max(Theme.gridColumns, 1)
            cellWidth: width / columns
            readonly property int cardWidth: Math.round(cellWidth - Theme.cardGap)
            cellHeight: Math.round(cardWidth * 3 / 2) + Theme.cardCaptionHeight + Theme.cardGap

            // Room for the focus lift at the edges.
            clip: false
            topMargin: Theme.shelfFocusMargin
            boundsBehavior: Flickable.StopAtBounds
            preferredHighlightBegin: height * 0.05
            preferredHighlightEnd: height * 0.95
            highlightRangeMode: InputModeManager.pointerActive ? GridView.NoHighlightRange
                                                               : GridView.ApplyRange
            cacheBuffer: cellHeight * 2

            delegate: DiscoveryCard {
                id: gridCard
                required property var model
                required property int index
                entry: gridCard.model
                artworkWidth: grid.cardWidth
                focused: GridView.isCurrentItem && grid.activeFocus
                onFocusedChanged: if (focused) root.showBackdropFor(gridCard.model)
                onClicked: {
                    grid.currentIndex = index
                    grid.forceActiveFocus()
                    root.openItem(titles.get(index), grid)
                }
            }

            onCurrentIndexChanged: root.loadMoreIfNear(currentIndex)
            onContentYChanged: {
                // Pointer scrolling near the end loads more too.
                if (contentHeight > height && contentHeight - contentY - height < cellHeight * 2)
                    root.loadMoreIfNear(titles.count - 1)
            }
            onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = grid

            Keys.onUpPressed: function(event) {
                if (currentIndex < columns) {
                    root.focusToolbar()
                    event.accepted = true
                } else {
                    event.accepted = false
                }
            }
            Keys.onReturnPressed: function(event) {
                if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < titles.count)
                    root.openItem(titles.get(currentIndex), grid)
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < titles.count)
                    root.openItem(titles.get(currentIndex), grid)
                event.accepted = true
            }

            WheelStepScroller {
                anchors.fill: parent
                target: grid
                stepPx: grid.cellHeight * 0.45
                z: 2
            }
        }
    }

    MediaRequestDialog {
        id: requestDialog
        parent: Overlay.overlay
    }
}
