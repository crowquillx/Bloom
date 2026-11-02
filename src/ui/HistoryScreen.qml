pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * HistoryScreen - what the viewer watched on the live connection, most
 * recent first, as a poster grid (docs/style-guide.md, Watch history).
 *
 * Opened from Settings > About & Account. WatchHistoryViewModel reads the
 * pages and decides which cards and actions there are; the grid reads the
 * next page as focus or scrolling nears its end, and while more remain its
 * footer offers "Look further back" (Down from the last row). Enter opens a
 * title, Back returns to the same card. On Silo the actions key (nav.menu)
 * opens a menu with Remove from history, which asks first.
 */
FocusScope {
    id: root
    focus: true
    // Reached from Settings; the bar shows it as the current page.
    property string navigationId: "settings"
    /// Stamped by Main with the live connection; Back skips this page once a
    /// route moves the session to another source.
    property string ownerConnectionId: ""

    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)
    signal navigateToEpisode(var episodeData)

    readonly property bool navigationRecessed: grid.contentY > Theme.spacingLarge
    // Same request width as Home and Search posters (shared cache entries).
    readonly property int posterRequestWidth: 640

    property string stageSource: ""
    property string pendingStageSource: ""
    // The card focus returns to, by ID and position.
    property string lastItemId: ""
    property int lastIndex: 0
    // Cards shown when the page being read started; a page that added
    // none does not chain into the next.
    property int countBeforePage: 0

    WatchHistoryViewModel {
        id: history
        onActionFailed: function(message) { toast.show(message) }
        onPageFailed: function(message) { toast.show(message) }
        onCountChanged: Qt.callLater(root.focusFirstCardIfIdle)
        // A failed or empty first read leaves no card to focus: its Try
        // again or Look further back action takes focus instead.
        onIsLoadingChanged: {
            if (history.isLoading) {
                root.countBeforePage = 0
                return
            }
            Qt.callLater(root.focusFirstCardIfIdle)
            Qt.callLater(root.pageFinished)
        }
        onHasErrorChanged: Qt.callLater(root.focusFirstCardIfIdle)
        onHasMoreChanged: Qt.callLater(root.focusFirstCardIfIdle)
        onLoadingMoreChanged: {
            if (history.loadingMore) {
                root.countBeforePage = history.count
                return
            }
            Qt.callLater(root.focusFirstCardIfIdle)
            Qt.callLater(root.pageFinished)
        }
    }

    SourceRouter {
        id: sourceRouter
        onFailed: function(message) {
            toast.show(qsTr("Couldn't switch to that title's server. %1")
                       .arg(message || qsTr("The server isn't reachable.")))
        }
    }

    Component.onCompleted: loadHistory()

    function loadHistory() {
        history.load(typeof Workspaces !== "undefined"
                     ? Workspaces.hiddenLibraryIds(LibraryService.getActiveConnectionId()) : [])
    }

    StackView.onStatusChanged: {
        if (StackView.status !== StackView.Active)
            return
        // A removal the session change left unconfirmed: read the list
        // the server has now.
        if (history.needsReload)
            loadHistory()
        Qt.callLater(root.restoreLastFocus)
    }

    // ========================================
    // Text and artwork
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
        return isNaN(d.getTime()) ? "" : d.getFullYear()
    }

    // Style-guide subtitles: movies show the year, series their run,
    // episodes "S1 E4 · Title".
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

    // The account the history belongs to: the live connection, which in a
    // merged space is the source the session is on.
    function accountLine() {
        var user = ConfigManager.username || ""
        var server = ConfigManager.serverUrl || ""
        var host = server.replace(/^[a-z]+:\/\//i, "").replace(/\/.*$/, "")
        if (user !== "" && host !== "")
            return qsTr("%1 · %2").arg(user).arg(host)
        return user || host
    }

    function showBackdropFor(item) {
        pendingStageSource = item ? artworkUrlFromRef(item.backdropArtwork, 1920) : ""
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
    function openItem(item) {
        if (!item)
            return
        rememberCard()
        sourceRouter.run(item.connectionId || "", function() {
            if (item.mediaType === "Series")
                root.navigateToSeries(item.itemId)
            else if (item.mediaType === "Episode")
                root.navigateToEpisode(item)
            else if (item.mediaType === "Movie")
                root.navigateToMovie(item)
        })
    }

    function openSeries(item) {
        if (!item || !item.seriesId)
            return
        rememberCard()
        sourceRouter.run(item.connectionId || "", function() { root.navigateToSeries(item.seriesId) })
    }

    // ========================================
    // Focus
    // ========================================

    function rememberCard() {
        var item = history.get(grid.currentIndex)
        lastItemId = item.itemId || ""
        lastIndex = Math.max(0, grid.currentIndex)
    }

    function focusGrid() {
        if (history.count > 0) {
            leaveFooter()
            grid.forceActiveFocus()
            return true
        }
        if (errorState.visible) {
            errorState.focusAction()
            return true
        }
        if (moreState.visible) {
            moreState.focusAction()
            return true
        }
        return false
    }

    function restoreLastFocus() {
        if (history.count > 0) {
            var at = history.indexOfItem(lastItemId)
            grid.currentIndex = at >= 0 ? at : Math.max(0, Math.min(lastIndex, history.count - 1))
            grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        }
        if (!focusGrid())
            root.forceActiveFocus()
    }

    // After the first page lands, focus moves from the page to the grid;
    // never away from the navigation bar or a dialog.
    function focusFirstCardIfIdle() {
        if (root.StackView.status !== StackView.Active || cardMenu.opened || confirmDialog.opened)
            return
        if (root.activeFocus && !grid.activeFocus && !errorState.activeFocus && !moreState.activeFocus)
            focusGrid()
        else if (grid.footerHasFocus && !(grid.footerItem && grid.footerItem.shown))
            focusGrid()
    }

    function saveFocusForNavigation() {
        rememberCard()
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    function loadMoreIfNear(index) {
        if (history.hasMore && index >= history.count - grid.columns * 2)
            history.loadMore()
    }

    // After a page lands: a focused footer hands focus to the first new
    // card (or the last card when nothing more remains), and focus or the
    // pointer still near the end reads the next page. A page that added no
    // card waits for the viewer, so a run of empty pages does not chain.
    function pageFinished() {
        if (history.isLoading || history.loadingMore)
            return
        var added = history.count > countBeforePage
        if (grid.footerHasFocus && (added || !history.hasMore)) {
            grid.currentIndex = added ? countBeforePage : history.count - 1
            focusGrid()
            grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        }
        if (!added)
            return
        if (InputModeManager.pointerActive)
            root.loadMoreIfNear(grid.atYEnd ? history.count - 1 : -1)
        else
            root.loadMoreIfNear(grid.currentIndex)
    }

    // The footer's "Look further back" action.
    function focusFooter() {
        if (grid.footerItem && grid.footerItem.shown)
            grid.footerItem.focusAction()
    }

    function leaveFooter() {
        if (grid.footerItem)
            grid.footerItem.releaseFocus()
    }

    // Down from the last row reads the next page and moves to the footer;
    // from a row whose card below is missing, it moves to the last card.
    function moveDownFromGrid() {
        var columns = Math.max(grid.columns, 1)
        var row = Math.floor(grid.currentIndex / columns)
        var lastRow = Math.floor((history.count - 1) / columns)
        if (row < lastRow) {
            grid.currentIndex = history.count - 1
            return
        }
        history.loadMore()
        focusFooter()
    }

    // ========================================
    // Card actions
    // ========================================

    // The actions key (Menu, Shift+F10, controller Start); InputBindingManager
    // posts Key_Menu for the controller.
    function isActionsKey(event) {
        return event.key === Qt.Key_Menu
                || InputBindingManager.actionForKeyboardEvent(event.key, event.modifiers, "navigation") === "nav.menu"
    }

    function pickAction(action, item) {
        if (!action || !item)
            return
        if (action.id === "openDetails") {
            openItem(item)
        } else if (action.id === "openSeries") {
            openSeries(item)
        } else if (action.confirmTitle) {
            confirmDialog.ask(action, item)
        } else {
            runAction(action.id, item)
        }
    }

    function runAction(actionId, item) {
        var index = grid.currentIndex
        sourceRouter.run(item.connectionId || "", function() {
            if (history.trigger(actionId, item))
                Qt.callLater(function() { root.focusAfterRemoval(index) })
        })
    }

    // The card that took the removed one's place takes focus (or the one
    // before it); an emptied grid hands focus to its state message.
    function focusAfterRemoval(index) {
        if (root.StackView.status !== StackView.Active || cardMenu.opened || confirmDialog.opened)
            return
        if (history.count > 0) {
            grid.currentIndex = Math.max(0, Math.min(index, history.count - 1))
            grid.forceActiveFocus()
        } else if (!focusGrid()) {
            root.forceActiveFocus()
        }
    }

    // Back closes the menu and focus returns to the card.
    BloomMenu {
        id: cardMenu
        property var item: null
        property var actions: []
        // The entry picked, run once the menu has closed.
        property var pendingAction: null

        function openFor(index) {
            var card = index >= 0 && index < history.count ? history.get(index) : null
            if (!card)
                return
            var entries = Array.from(history.actionsFor(card))
            if (entries.length === 0)
                return
            grid.currentIndex = index
            item = card
            actions = entries
            pendingAction = null
            var delegate = grid.itemAtIndex(index)
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
        onClosed: {
            var action = pendingAction
            var target = item
            pendingAction = null
            Qt.callLater(function() {
                if (root.StackView.status === StackView.Active)
                    root.focusGrid()
                if (action)
                    root.pickAction(action, target)
            })
        }

        Repeater {
            model: cardMenu.actions

            BloomMenuItem {
                required property var modelData
                text: modelData.text
                enabled: modelData.enabled
                // Esc closes the menu instead of reaching Main's Back.
                Keys.onShortcutOverride: function(event) {
                    event.accepted = event.key === Qt.Key_Escape
                }
                onTriggered: {
                    cardMenu.pendingAction = modelData
                    cardMenu.close()
                }
            }
        }
    }

    ModalFocusGuard {
        popup: cardMenu
    }

    // Removal also counts the title as unwatched and drops its resume
    // point, so it asks first; Cancel takes focus.
    Dialog {
        id: confirmDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.round(680 * Theme.layoutScale),
                        Math.max(0, root.width - Theme.spacingLarge * 2))
        padding: Theme.dialogPadding
        topPadding: Theme.spacingLarge

        property var action: null
        property var item: null
        property bool confirmed: false

        function ask(cardAction, card) {
            action = cardAction
            item = card
            open()
        }

        background: DialogSurface {}
        Overlay.modal: ModalScrim {}

        ModalFocusGuard {
            popup: confirmDialog
            fallbackItem: confirmCancelButton
        }

        header: Item {
            implicitHeight: confirmTitle.implicitHeight + Theme.dialogPadding + Theme.spacingSmall

            Text {
                id: confirmTitle
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.dialogPadding
                anchors.rightMargin: Theme.dialogPadding
                text: confirmDialog.action ? confirmDialog.action.confirmTitle : ""
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
        }

        contentItem: Text {
            text: confirmDialog.action ? confirmDialog.action.confirmMessage : ""
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
                        confirmDialog.close()
                    }
                }

                ActionButton {
                    id: confirmCancelButton
                    variant: "secondary"
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.right: confirmRemoveButton
                    Keys.onReturnPressed: confirmDialog.close()
                    Keys.onEnterPressed: confirmDialog.close()
                    onClicked: confirmDialog.close()
                }

                ActionButton {
                    id: confirmRemoveButton
                    variant: "destructive"
                    Layout.fillWidth: true
                    text: confirmDialog.action ? confirmDialog.action.confirmText : ""
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.left: confirmCancelButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    onClicked: {
                        confirmDialog.confirmed = true
                        confirmDialog.close()
                    }
                }
            }
        }

        onOpened: {
            confirmed = false
            Qt.callLater(function() { confirmCancelButton.forceActiveFocus() })
        }
        onClosed: {
            var cardAction = action
            var card = item
            var run = confirmed
            action = null
            item = null
            confirmed = false
            Qt.callLater(function() {
                if (root.StackView.status === StackView.Active)
                    root.focusGrid()
                if (run && cardAction)
                    root.runAction(cardAction.id, card)
            })
        }
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
            title: qsTr("Watch History")
            detail: root.accountLine()
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: history.isLoading && history.count === 0
            kind: "loading"
            message: qsTr("Loading…")
        }

        StateMessage {
            id: errorState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !history.isLoading && history.hasError && history.count === 0
            kind: "error"
            title: qsTr("Couldn't load your history")
            message: history.errorMessage
            actionText: qsTr("Try again")
            onActionTriggered: history.reload()
        }

        // Nothing shown yet, but older pages remain (Silo pages can hold
        // only titles Bloom does not show, such as audiobooks).
        StateMessage {
            id: moreState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !history.isLoading && !history.hasError && history.count === 0 && history.hasMore
            kind: "empty"
            iconGlyph: Icons.schedule
            title: qsTr("Nothing recent to show")
            message: qsTr("Your latest watches aren't movies or shows.")
            actionText: history.loadingMore ? qsTr("Loading…") : qsTr("Look further back")
            onActionTriggered: history.loadMore()
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !history.isLoading && !history.hasError && history.count === 0 && !history.hasMore
            kind: "empty"
            iconGlyph: Icons.schedule
            title: qsTr("Nothing watched yet")
            message: qsTr("Movies and episodes you watch show up here, newest first.")
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            // Hidden by opacity: toggling visible loops the wheel scroller's
            // enabled binding.
            opacity: history.count > 0 ? 1 : 0
            model: history
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Watch History")

            property int columns: Math.max(Theme.gridColumns, 1)
            // The footer's action holds focus inside the grid's scope.
            readonly property bool footerHasFocus: footerItem ? footerItem.activeFocus : false
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

            delegate: MediaCard {
                id: historyCard
                required property var itemData
                required property int index
                variant: "poster"
                artworkWidth: grid.cardWidth
                artworkSources: [
                    itemData.mediaType === "Episode" ? root.artworkUrlFromRef(itemData.seriesPrimaryArtwork, root.posterRequestWidth) : "",
                    root.artworkUrlFromRef(itemData.primaryArtwork, root.posterRequestWidth)
                ]
                title: itemData.mediaType === "Episode" ? (itemData.seriesName || itemData.name || "")
                                                        : (itemData.name || "")
                subtitle: root.posterSubtitle(itemData)
                placeholderIcon: itemData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
                item: itemData
                showWatchState: itemData.mediaType === "Series"
                                || (itemData.mediaType === "Movie" && !!itemData.watched)
                unplayedCount: itemData.mediaType === "Series" ? (itemData.unplayedItemCount || 0) : 0
                watched: !!itemData.watched
                focused: GridView.isCurrentItem && grid.activeFocus && !grid.footerHasFocus
                menuEnabled: history.canRemove
                onFocusedChanged: if (focused) root.showBackdropFor(itemData)
                onClicked: {
                    grid.currentIndex = index
                    grid.forceActiveFocus()
                    root.openItem(itemData)
                }
                onMenuRequested: {
                    grid.currentIndex = index
                    grid.forceActiveFocus()
                    cardMenu.openFor(index)
                }
                Accessible.description: qsTr("Details for %1").arg(title)
            }

            onCurrentIndexChanged: root.loadMoreIfNear(currentIndex)
            onContentYChanged: {
                // Pointer scrolling near the end loads more too.
                if (contentHeight > height && contentHeight - contentY - height < cellHeight * 2)
                    root.loadMoreIfNear(history.count - 1)
            }
            // So does the pointer reaching the bottom.
            onAtYEndChanged: {
                if (atYEnd && InputModeManager.pointerActive)
                    root.loadMoreIfNear(history.count - 1)
            }

            // While more remain: "Look further back", with a busy
            // indicator while a page loads. Up returns to the cards.
            footer: FocusScope {
                id: historyFooter
                readonly property bool shown: history.count > 0 && history.hasMore
                width: grid.width
                height: shown ? footerRow.implicitHeight + Theme.spacingLarge * 2 : 0
                visible: shown

                function focusAction() {
                    moreButton.forceActiveFocus()
                }
                function releaseFocus() {
                    if (moreButton.activeFocus)
                        moreButton.focus = false
                }

                RowLayout {
                    id: footerRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingMedium

                    ActionButton {
                        id: moreButton
                        variant: "secondary"
                        text: history.loadingMore ? qsTr("Loading…") : qsTr("Look further back")
                        focusPolicy: Qt.StrongFocus
                        Accessible.description: qsTr("Load older watch history")
                        onClicked: history.loadMore()
                        Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                        Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                        Keys.onUpPressed: function(event) {
                            event.accepted = true
                            root.focusGrid()
                            grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
                        }
                        // The grid must not move its cards under the footer.
                        Keys.onDownPressed: function(event) { event.accepted = true }
                        Keys.onLeftPressed: function(event) { event.accepted = true }
                        Keys.onRightPressed: function(event) { event.accepted = true }
                    }

                    BusyIndicator {
                        running: history.loadingMore
                        visible: running
                        Layout.preferredWidth: Math.round(32 * Theme.layoutScale)
                        Layout.preferredHeight: Layout.preferredWidth
                        palette.dark: Theme.textPrimary
                        palette.light: Theme.accentSecondary
                    }
                }
            }

            // Up from the first row is left to Main, which enters the
            // navigation bar.
            Keys.onPressed: function(event) {
                if (!footerHasFocus && history.canRemove && root.isActionsKey(event)) {
                    event.accepted = true
                    cardMenu.openFor(currentIndex)
                }
            }
            Keys.onDownPressed: function(event) {
                if (footerHasFocus || currentIndex + columns < history.count) {
                    // The GridView moves down a row.
                    event.accepted = footerHasFocus
                    return
                }
                event.accepted = true
                root.moveDownFromGrid()
            }
            Keys.onReturnPressed: function(event) {
                if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < history.count)
                    root.openItem(history.get(currentIndex))
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < history.count)
                    root.openItem(history.get(currentIndex))
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

    ToastNotification {
        id: toast
        z: 10
    }
}
