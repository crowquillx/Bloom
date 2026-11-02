import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * CollectionScreen - one collection's movies and shows.
 *
 * Browse archetype (docs/style-guide.md): an ambient stage with the
 * collection's backdrop, then the focused title's; no hero. Below the
 * navigation bar: a PageHeader with the collection's name and count, a Sort
 * menu (collection order, release date, title), the description, and a
 * poster grid of its titles from the live connection.
 *
 * Collections are a browse view over a library's titles, opened from a
 * library's Collections view or a detail page's "Part of" shelf, both on
 * the connection that owns them.
 *
 * Keys: Sort is the top-most control; Up from it reaches the navigation
 * bar, Down enters the grid, Up from the grid's first row returns to it.
 * Enter opens a title on its details page; Back returns here with focus on
 * that title. The grid reads further pages as focus nears its end.
 */
FocusScope {
    id: root
    focus: true

    /// The canonical collection map (models/MediaCollections.h).
    required property var collectionData

    property string navigationId: "collection"
    /// Stamped by Main with the live connection; Back skips this page once a
    /// route moves the session to another source.
    property string ownerConnectionId: ""

    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)

    readonly property var collection: viewModel.collection || ({})
    readonly property string overview: collection.overview || ""
    readonly property bool hasTitles: viewModel.count > 0

    // Same request width as library and Search posters (shared cache entries).
    readonly property int posterRequestWidth: 640
    // Read the next page when focus is within two rows of the end.
    readonly property int loadMoreRows: 2

    /// The header collapses (and the navigation bar recedes) once the grid
    /// scrolls past its first row; it returns at the top or on Sort.
    readonly property bool headerExpanded: sortButton.activeFocus || sortMenu.visible
                                           || !titleGrid.activeFocus || titleGrid.count === 0
                                           || (InputModeManager.pointerActive ? titleGrid.atYBeginning
                                                                              : titleGrid.currentIndex < titleGrid.columns)
    readonly property bool navigationRecessed: !headerExpanded

    readonly property var sortOptions: [
        { value: "", label: qsTr("Collection order") },
        { value: "releaseDate", label: qsTr("Release date") },
        { value: "title", label: qsTr("Title") }
    ]

    /// Last focused control, restored after a details page or the
    /// navigation bar hands focus back.
    property Item lastFocusTarget: null
    property string stageSource: ""
    property string pendingStageSource: ""

    CollectionViewModel {
        id: viewModel
    }

    Component.onCompleted: {
        viewModel.load(root.collectionData)
        root.stageSource = root.collectionBackdrop()
    }

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
            Qt.callLater(root.restoreLastFocus)
    }

    // The first titles arrive after the page has focus: move into them once
    // loading settles, unless focus is already inside the page.
    Connections {
        target: viewModel
        function onItemsChanged() { Qt.callLater(root.focusInitialTarget) }
        function onIsLoadingChanged() { Qt.callLater(root.focusInitialTarget) }
        function onHasErrorChanged() { Qt.callLater(root.focusInitialTarget) }
    }

    SourceRouter {
        id: sourceRouter
    }

    // ========================================
    // Text helpers
    // ========================================

    function extractYear(dateString) {
        if (!dateString)
            return ""
        var d = new Date(dateString)
        return isNaN(d.getTime()) ? "" : d.getFullYear()
    }

    // Style-guide subtitles: movies show the year, series their run.
    function titleSubtitle(item) {
        if (!item)
            return ""
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

    function countLine() {
        var total = viewModel.totalCount
        if (total <= 0)
            return ""
        return total === 1 ? qsTr("1 title") : qsTr("%1 titles").arg(total)
    }

    function sortLabel(value) {
        for (var i = 0; i < sortOptions.length; i++) {
            if (sortOptions[i].value === value)
                return sortOptions[i].label
        }
        return sortOptions[0].label
    }

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId)
            return ""
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 0)
    }

    function collectionBackdrop() {
        return artworkUrlFromRef(root.collectionData ? root.collectionData.backdropArtwork : null, 1920)
    }

    function showBackdropFor(item) {
        var url = item ? artworkUrlFromRef(item.backdropArtwork, 1920) : ""
        pendingStageSource = url !== "" ? url : collectionBackdrop()
        stageDebounce.restart()
    }

    Timer {
        id: stageDebounce
        interval: 280
        onTriggered: root.stageSource = root.pendingStageSource
    }

    // ========================================
    // Opening titles
    // ========================================

    // Titles belong to the live connection; the router is a no-op there and
    // keeps the merged-space rule in one place.
    function openTitle(item) {
        if (!item)
            return
        if (item.mediaType === "Series")
            sourceRouter.run(item.connectionId || "", function() { root.navigateToSeries(item.itemId) })
        else if (item.mediaType === "Movie")
            sourceRouter.run(item.connectionId || "", function() { root.navigateToMovie(item) })
    }

    // ========================================
    // Focus
    // ========================================

    function hasFocusInside() {
        var item = root.Window.activeFocusItem
        while (item) {
            if (item === contentColumn)
                return true
            item = item.parent
        }
        return false
    }

    // The grid once titles arrive, else Retry, else Sort.
    function focusInitialTarget() {
        if (!root.activeFocus || hasFocusInside() || (viewModel.isLoading && !root.hasTitles))
            return
        focusContent()
    }

    function focusContent() {
        if (titleGrid.count > 0) {
            if (titleGrid.currentIndex < 0 || titleGrid.currentIndex >= titleGrid.count)
                titleGrid.currentIndex = 0
            titleGrid.forceActiveFocus()
        } else if (errorState.visible) {
            errorState.focusAction()
        } else {
            sortButton.forceActiveFocus()
        }
    }

    function restoreLastFocus() {
        var target = lastFocusTarget
        if (target === titleGrid && titleGrid.count > 0) {
            titleGrid.forceActiveFocus()
            return
        }
        if (target === sortButton) {
            sortButton.forceActiveFocus()
            return
        }
        root.forceActiveFocus()
        focusContent()
    }

    function saveFocusForNavigation() {
        // lastFocusTarget already tracks Sort and the grid.
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
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
        id: contentColumn
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: root.headerExpanded ? Theme.pageMarginTop : Theme.navBarInsetTop
        spacing: Theme.spacingMedium

        Behavior on anchors.topMargin {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.headerExpanded ? headerColumn.implicitHeight : 0
            visible: Layout.preferredHeight > 0
            opacity: root.headerExpanded ? 1.0 : 0.0
            clip: !root.headerExpanded

            Behavior on Layout.preferredHeight {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easingStandard }
            }
            Behavior on opacity {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationNormal }
            }

            ColumnLayout {
                id: headerColumn
                width: parent.width
                spacing: Theme.spacingMedium

                PageHeader {
                    Layout.fillWidth: true
                    eyebrow: qsTr("Collection")
                    title: root.collection.name || ""
                    detail: root.countLine()

                    ActionButton {
                        id: sortButton
                        variant: "secondary"
                        iconGlyph: Icons.swapVert
                        text: qsTr("Sort: %1").arg(root.sortLabel(viewModel.sortBy))
                        Layout.alignment: Qt.AlignBottom
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        accessibleLabel: qsTr("Sort titles, now by %1").arg(root.sortLabel(viewModel.sortBy))
                        onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = sortButton
                        onClicked: sortMenu.popup(sortButton, 0, sortButton.height + Theme.spacingSmall)
                        Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                        Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                        Keys.onDownPressed: (event) => {
                            if (titleGrid.count > 0 || errorState.visible)
                                root.focusContent()
                            event.accepted = true
                        }
                        // Top-most control: Up goes on to the navigation bar.
                        Keys.onLeftPressed: (event) => { event.accepted = true }
                        Keys.onRightPressed: (event) => { event.accepted = true }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.heroCopyMaxWidth
                    visible: text !== ""
                    text: root.overview
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    opacity: 0.86
                    lineHeight: 1.12
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }

        BloomMenu {
            id: sortMenu

            onOpened: {
                var current = 0
                for (var i = 0; i < root.sortOptions.length; i++) {
                    if (root.sortOptions[i].value === viewModel.sortBy)
                        current = i
                }
                currentIndex = current
                forceActiveFocus()
            }
            onClosed: Qt.callLater(function() { sortButton.forceActiveFocus() })

            Repeater {
                model: root.sortOptions

                BloomMenuItem {
                    required property var modelData
                    text: modelData.label
                    marked: modelData.value === viewModel.sortBy
                    onTriggered: {
                        viewModel.sortBy = modelData.value
                        sortMenu.close()
                    }
                }
            }
        }

        // ========================================
        // States
        // ========================================

        StateMessage {
            Layout.fillWidth: true
            Layout.topMargin: Theme.sectionSpacing
            visible: viewModel.isLoading && !root.hasTitles
            kind: "loading"
            message: qsTr("Loading collection…")
        }

        StateMessage {
            id: errorState
            Layout.fillWidth: true
            Layout.topMargin: Theme.sectionSpacing
            visible: viewModel.hasError && !root.hasTitles
            kind: "error"
            title: qsTr("Couldn't load this collection")
            message: viewModel.errorMessage
            actionText: qsTr("Try again")
            onActionTriggered: viewModel.reload()
            onActiveFocusChanged: if (activeFocus) root.lastFocusTarget = null
            Keys.onUpPressed: (event) => { sortButton.forceActiveFocus(); event.accepted = true }
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.topMargin: Theme.sectionSpacing
            visible: !viewModel.isLoading && !viewModel.hasError && !root.hasTitles
            kind: "empty"
            iconGlyph: Icons.videoLibrary
            title: qsTr("Nothing to watch here yet")
            message: qsTr("None of this collection's movies or shows are in your libraries.")
        }

        // ========================================
        // Titles
        // ========================================

        GridView {
            id: titleGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            model: viewModel
            clip: false
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            highlightRangeMode: GridView.ApplyRange
            preferredHighlightBegin: Theme.shelfFocusMargin
            preferredHighlightEnd: height - cellHeight
            highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("%1 titles").arg(root.collection.name || "")
            readonly property int columns: Math.max(Theme.gridColumns, 1)
            cellWidth: width / columns
            readonly property int cardWidth: Math.round(cellWidth - Theme.cardGap)
            cellHeight: Math.round(cardWidth * 3 / 2) + Theme.cardCaptionHeight + Theme.cardGap

            onCurrentIndexChanged: {
                if (viewModel.hasMore && currentIndex >= count - columns * root.loadMoreRows)
                    viewModel.loadMore()
            }
            onActiveFocusChanged: {
                if (activeFocus) {
                    root.lastFocusTarget = titleGrid
                    root.showBackdropFor(viewModel.get(currentIndex))
                }
            }

            delegate: Item {
                id: titleCell
                required property var itemData
                required property int index
                width: titleGrid.cellWidth
                height: titleGrid.cellHeight

                MediaCard {
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "poster"
                    artworkWidth: titleGrid.cardWidth
                    artworkSources: [root.artworkUrlFromRef(titleCell.itemData.primaryArtwork, root.posterRequestWidth)]
                    title: titleCell.itemData.name || ""
                    subtitle: root.titleSubtitle(titleCell.itemData)
                    placeholderIcon: titleCell.itemData.mediaType === "Series" ? Icons.tvShows : Icons.movie
                    item: titleCell.itemData
                    showWatchState: titleCell.itemData.mediaType === "Series"
                                    || (titleCell.itemData.mediaType === "Movie" && !!titleCell.itemData.watched)
                    unplayedCount: titleCell.itemData.mediaType === "Series" ? (titleCell.itemData.unplayedItemCount || 0) : 0
                    watched: !!titleCell.itemData.watched
                    focused: titleCell.GridView.isCurrentItem && titleGrid.activeFocus
                    onFocusedChanged: if (focused) root.showBackdropFor(titleCell.itemData)
                    onClicked: {
                        titleGrid.currentIndex = titleCell.index
                        titleGrid.forceActiveFocus()
                        root.openTitle(titleCell.itemData)
                    }
                    Accessible.description: qsTr("Details for %1").arg(titleCell.itemData.name || "")
                }
            }

            Keys.onUpPressed: (event) => {
                if (currentIndex < columns) {
                    sortButton.forceActiveFocus()
                    event.accepted = true
                } else {
                    event.accepted = false
                }
            }
            Keys.onReturnPressed: (event) => {
                if (!event.isAutoRepeat)
                    root.openTitle(viewModel.get(currentIndex))
                event.accepted = true
            }
            Keys.onEnterPressed: (event) => {
                if (!event.isAutoRepeat)
                    root.openTitle(viewModel.get(currentIndex))
                event.accepted = true
            }
        }

        // More titles are loading at the end of the grid.
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.round(40 * Theme.layoutScale)
            Layout.preferredHeight: Layout.preferredWidth
            visible: viewModel.loadingMore
            running: visible
        }
    }
}
