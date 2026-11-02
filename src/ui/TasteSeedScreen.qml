pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * TasteSeedScreen - "pick a few titles you like" for a cold Silo profile
 * (docs/style-guide.md, Taste seeding).
 *
 * A browse page with no hero, opened from Home's Personalize Home prompt or
 * Settings > About & Account. TasteSeedViewModel reads the candidates and
 * owns the selection: Enter (or a click) picks or unpicks the focused
 * poster, and the header counts the picks against the minimum. Titles the
 * profile already favorited stay picked. Done sends them; Skip (Cancel from
 * Settings) leaves without sending. Up from the first row reaches Done and
 * Skip, Up again the navigation bar. Back is ignored while the picks are
 * saved. On success the page asks Main to close it with a toast
 * (`finished`).
 */
FocusScope {
    id: root
    focus: true
    /// Set by Main: opened from Settings (Cancel instead of Skip, and the
    /// bar shows Settings).
    property bool fromSettings: false
    property string navigationId: fromSettings ? "settings" : "home"
    /// Stamped by Main with the live connection; Back skips this page once a
    /// route moves the session to another source.
    property string ownerConnectionId: ""

    /// Main closes the page and shows `message` when it is not empty.
    signal finished(string message)

    // Main's Back shortcut waits while the picks are saved.
    readonly property bool handlesOwnBackNavigation: picker.submitting
    Keys.onEscapePressed: function(event) { event.accepted = picker.submitting }
    Keys.onBackPressed: function(event) { event.accepted = picker.submitting }

    readonly property bool navigationRecessed: grid.contentY > Theme.spacingLarge
    // Same request width as Home and Search posters (shared cache entries).
    readonly property int posterRequestWidth: 640

    property int lastIndex: 0

    TasteSeedViewModel {
        id: picker
        onCountChanged: Qt.callLater(root.focusGridIfIdle)
        onIsLoadingChanged: if (!picker.isLoading) Qt.callLater(root.focusGridIfIdle)
        onLoadingMoreChanged: if (!picker.loadingMore) Qt.callLater(root.focusGridIfIdle)
        onHasErrorChanged: Qt.callLater(root.focusGridIfIdle)
        onPageFailed: function(message) { toast.show(message) }
        onSubmitFailed: function(message) {
            toast.show(qsTr("Couldn't save your picks. %1").arg(message))
        }
        onSubmitted: function(added) {
            root.finished(added > 0
                          ? qsTr("Thanks! Your recommendations will update shortly.")
                          : qsTr("Your favorites already shape your recommendations."))
        }
    }

    Component.onCompleted: picker.load()

    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active)
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

    function posterSubtitle(item) {
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

    // The header's counter: how many are picked against the minimum.
    function counterText() {
        var picked = picker.selectedCount
        var minimum = picker.minimumPicks
        if (picked === 0)
            return qsTr("Pick at least %1 titles you like").arg(minimum)
        if (picked < minimum)
            return qsTr("%1 picked — pick at least %2").arg(picked).arg(minimum)
        return qsTr("%1 picked").arg(picked)
    }

    // ========================================
    // Actions
    // ========================================

    function togglePick(index) {
        if (index < 0 || index >= picker.count)
            return
        if (picker.favoriteAt(index)) {
            toast.show(qsTr("Already a favorite. It counts toward your picks."))
            return
        }
        if (!picker.toggle(index) && !picker.submitting)
            toast.show(qsTr("That's plenty: up to %1 new picks at a time.").arg(picker.maximumPicks))
    }

    function done() {
        if (!picker.canSubmit) {
            toast.show(qsTr("Pick at least %1 titles first.").arg(picker.minimumPicks))
            return
        }
        // False after a session change: the page starts over on the new one.
        if (!picker.submit())
            toast.show(qsTr("The server or profile changed. Pick again."))
    }

    function leave() {
        if (picker.submitting)
            return
        if (!root.fromSettings)
            picker.skip()
        root.finished("")
    }

    // ========================================
    // Focus
    // ========================================

    function focusGrid() {
        if (picker.count > 0) {
            grid.forceActiveFocus()
            return true
        }
        if (errorState.visible) {
            errorState.focusAction()
            return true
        }
        if (emptyState.visible && emptyState.actionText !== "") {
            emptyState.focusAction()
            return true
        }
        return false
    }

    function focusActions() {
        (doneButton.enabled ? doneButton : skipButton).forceActiveFocus()
    }

    function restoreLastFocus() {
        if (picker.count > 0) {
            grid.currentIndex = Math.max(0, Math.min(lastIndex, picker.count - 1))
            grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        }
        if (!focusGrid())
            focusActions()
    }

    // After the first page lands, focus moves from the page to the grid;
    // never away from the navigation bar or the header actions.
    function focusGridIfIdle() {
        if (root.StackView.status !== StackView.Active)
            return
        if (root.activeFocus && !grid.activeFocus && !errorState.activeFocus
                && !emptyState.activeFocus && !doneButton.activeFocus && !skipButton.activeFocus) {
            if (!focusGrid())
                focusActions()
        }
    }

    function saveFocusForNavigation() {
        lastIndex = Math.max(0, grid.currentIndex)
    }

    function restoreFocusFromNavigation() {
        restoreLastFocus()
    }

    function loadMoreIfNear(index) {
        if (picker.hasMore && index >= picker.count - grid.columns * 2)
            picker.loadMore()
    }

    // ========================================
    // Stage
    // ========================================

    // Taste-seed cards carry posters only, so the stage has no art.
    StageBackdrop {
        anchors.fill: parent
        mode: "ambient"
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
            eyebrow: qsTr("Personalize")
            title: qsTr("Pick a few you like")
            detail: picker.available ? root.counterText() : ""

            ActionButton {
                id: skipButton
                variant: "secondary"
                text: root.fromSettings ? qsTr("Cancel") : qsTr("Skip")
                enabled: !picker.submitting
                focusPolicy: Qt.StrongFocus
                Accessible.description: root.fromSettings
                                        ? qsTr("Leave without saving picks")
                                        : qsTr("Leave without saving picks and stop offering this on Home")
                KeyNavigation.right: doneButton
                Keys.onDownPressed: function(event) { event.accepted = root.focusGrid() }
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                onClicked: root.leave()
            }

            ActionButton {
                id: doneButton
                variant: "primary"
                text: picker.submitting ? qsTr("Saving…") : qsTr("Done")
                busy: picker.submitting
                enabled: picker.available && !picker.submitting
                focusPolicy: Qt.StrongFocus
                Accessible.description: qsTr("Save your picks as favorites")
                KeyNavigation.left: skipButton
                Keys.onDownPressed: function(event) { event.accepted = root.focusGrid() }
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                onClicked: root.done()
            }
        }

        StateMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: (picker.isLoading || picker.loadingMore) && picker.count === 0
            kind: "loading"
            message: qsTr("Loading…")
        }

        StateMessage {
            id: errorState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !picker.isLoading && picker.hasError && picker.count === 0
            kind: "error"
            title: qsTr("Couldn't load titles to pick from")
            message: picker.errorMessage
            actionText: qsTr("Try again")
            onActionTriggered: picker.reload()
            Keys.onUpPressed: function(event) { event.accepted = true; root.focusActions() }
        }

        StateMessage {
            id: emptyState
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            visible: !picker.isLoading && !picker.loadingMore && !picker.hasError && picker.count === 0
            kind: "empty"
            iconGlyph: Icons.favoriteBorder
            title: !picker.available ? qsTr("Not available on this server")
                 : picker.hasMore ? qsTr("Nothing to pick from here")
                 : qsTr("Nothing to pick from yet")
            message: !picker.available ? qsTr("Picking titles you like needs a Silo server.")
                   : picker.hasMore ? qsTr("The first titles were ones Bloom doesn't show. More may follow.")
                   : qsTr("Once your libraries have matched titles, popular ones show up here.")
            // Pages of titles Bloom doesn't show stop the reading; more
            // may still follow.
            actionText: picker.available && picker.hasMore ? qsTr("Look further") : ""
            onActionTriggered: picker.loadMore()
            Keys.onUpPressed: function(event) { event.accepted = true; root.focusActions() }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            // Hidden by opacity: toggling visible loops the wheel scroller's
            // enabled binding.
            opacity: picker.count > 0 ? 1 : 0
            model: picker
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Titles to pick from")

            property int columns: Math.max(Theme.gridColumns, 1)
            cellWidth: width / columns
            readonly property int cardWidth: Math.round(cellWidth - Theme.cardGap)
            readonly property int artworkHeight: Math.round(cardWidth * 3 / 2)
            cellHeight: artworkHeight + Theme.cardCaptionHeight + Theme.cardGap

            // Room for the focus lift at the edges.
            clip: false
            topMargin: Theme.shelfFocusMargin
            boundsBehavior: Flickable.StopAtBounds
            preferredHighlightBegin: height * 0.05
            preferredHighlightEnd: height * 0.95
            highlightRangeMode: InputModeManager.pointerActive ? GridView.NoHighlightRange
                                                               : GridView.ApplyRange
            cacheBuffer: cellHeight * 2

            delegate: Item {
                id: cell
                required property var itemData
                required property bool selected
                required property bool favorite
                required property int index
                width: card.width
                height: card.height
                z: card.focused ? 2 : 0

                MediaCard {
                    id: card
                    variant: "poster"
                    artworkWidth: grid.cardWidth
                    artworkSources: [root.artworkUrlFromRef(cell.itemData.primaryArtwork, root.posterRequestWidth)]
                    title: cell.itemData.name || ""
                    subtitle: cell.favorite ? qsTr("Already a favorite") : root.posterSubtitle(cell.itemData)
                    placeholderIcon: cell.itemData.mediaType === "Movie" ? Icons.movie : Icons.tvShows
                    focused: cell.GridView.isCurrentItem && grid.activeFocus
                    onClicked: {
                        grid.currentIndex = cell.index
                        grid.forceActiveFocus()
                        root.togglePick(cell.index)
                    }
                    Accessible.name: (cell.selected ? qsTr("Picked: %1") : "%1").arg(title)
                    Accessible.description: cell.favorite ? qsTr("Already a favorite")
                                          : cell.selected ? qsTr("Press Enter to unpick")
                                          : qsTr("Press Enter to pick")
                }

                // Selection, never a ring: a tinted fill and an accent
                // indicator over the art, lifted with the card.
                Item {
                    width: card.artworkWidth
                    height: card.artworkHeight
                    visible: cell.selected
                    scale: card.focused ? Theme.focusScale : 1.0
                    transformOrigin: Item.Center
                    Behavior on scale {
                        enabled: Theme.uiAnimationsEnabled
                        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEmphasized }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusArtwork
                        color: Theme.selectionBackground
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.radiusArtwork
                        anchors.rightMargin: Theme.radiusArtwork
                        height: Theme.selectionIndicatorWidth
                        radius: height / 2
                        color: Theme.selectionIndicator
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Theme.spacingSmall
                        width: Math.round(48 * Theme.layoutScale)
                        height: width
                        radius: width / 2
                        color: Theme.selectionIndicator

                        Text {
                            anchors.centerIn: parent
                            text: Icons.check
                            font.family: Theme.fontIcon
                            font.pixelSize: Theme.iconSizeMedium
                            color: Theme.actionPrimaryText
                            Accessible.ignored: true
                        }
                    }
                }
            }

            onCurrentIndexChanged: root.loadMoreIfNear(currentIndex)
            onContentYChanged: {
                // Pointer scrolling near the end loads more too.
                if (contentHeight > height && contentHeight - contentY - height < cellHeight * 2)
                    root.loadMoreIfNear(picker.count - 1)
            }

            // Up from the first row reaches Done and Skip.
            Keys.onUpPressed: function(event) {
                if (currentIndex >= columns) {
                    event.accepted = false
                    return
                }
                event.accepted = true
                root.saveFocusForNavigation()
                root.focusActions()
            }
            Keys.onDownPressed: function(event) {
                if (currentIndex + columns < picker.count) {
                    event.accepted = false
                    return
                }
                // From a row whose card below is missing, the last card.
                event.accepted = true
                var lastRow = Math.floor((picker.count - 1) / columns)
                if (Math.floor(currentIndex / columns) < lastRow)
                    currentIndex = picker.count - 1
                else
                    picker.loadMore()
            }
            Keys.onReturnPressed: function(event) {
                if (!event.isAutoRepeat)
                    root.togglePick(currentIndex)
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (!event.isAutoRepeat)
                    root.togglePick(currentIndex)
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
